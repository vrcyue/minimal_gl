/* Copyright (C) 2024 */

#include "common.h"
#include "package.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

static const char kPackageMagic[] = "MGLPKG1";
static const size_t kPackageMagicSize = sizeof(kPackageMagic) - 1; /* exclude NUL */

static void AppendUint32(std::vector<uint8_t> &buffer, uint32_t value){
	for (int i = 0; i < 4; ++i) {
		buffer.push_back((uint8_t)((value >> (i * 8)) & 0xff));
	}
}

static void AppendUint64(std::vector<uint8_t> &buffer, uint64_t value){
	for (int i = 0; i < 8; ++i) {
		buffer.push_back((uint8_t)((value >> (i * 8)) & 0xff));
	}
}

static bool EnsureDirectoryForFile(const char *filePath, std::string &errorMessage){
	char directory[MAX_PATH] = {0};
	SplitDirectoryPathFromFilePath(directory, sizeof(directory), filePath);
	if (directory[0] == '\0') return true;

	int ret = SHCreateDirectoryExA(NULL, directory, NULL);
	if (ret == ERROR_SUCCESS || ret == ERROR_ALREADY_EXISTS) {
		return true;
	}

	errorMessage = "Failed to create directory: ";
	errorMessage += directory;
	return false;
}

static bool ContainsParentTraversal(const std::string &path){
	if (path.find("..\\") != std::string::npos) return true;
	if (path.find("../") != std::string::npos) return true;
	if (path.compare(0, 2, "..") == 0) return true;
	return false;
}

bool PackageBuildPayload(
	const std::vector<PackageEntry> &entries,
	std::vector<uint8_t> &payload,
	std::string &errorMessage
){
	payload.clear();
	errorMessage.clear();

	if (entries.size() > UINT32_MAX) {
		errorMessage = "Too many entries to package.";
		return false;
	}

	AppendUint32(payload, (uint32_t)entries.size());
	for (const PackageEntry &entry : entries) {
		if (entry.path.empty()) {
			errorMessage = "Entry path is empty.";
			return false;
		}
		if (entry.path.size() > UINT32_MAX) {
			errorMessage = "Entry path is too long.";
			return false;
		}
		if (ContainsParentTraversal(entry.path)) {
			errorMessage = "Entry path contains '..' and is not safe to embed.";
			return false;
		}
		AppendUint32(payload, (uint32_t)entry.path.size());
		payload.insert(payload.end(), entry.path.begin(), entry.path.end());
		AppendUint64(payload, (uint64_t)entry.data.size());
		payload.insert(payload.end(), entry.data.begin(), entry.data.end());
	}

	return true;
}

bool PackageAppendToFile(
	const char *filePath,
	const std::vector<PackageEntry> &entries,
	std::string &errorMessage
){
	std::vector<uint8_t> payload;
	if (PackageBuildPayload(entries, payload, errorMessage) == false) {
		return false;
	}

	FILE *file = fopen(filePath, "ab");
	if (file == NULL) {
		errorMessage = "Failed to open output file.";
		return false;
	}

	bool success = true;
	do {
		if (!payload.empty()) {
			if (fwrite(payload.data(), 1, payload.size(), file) != payload.size()) {
				errorMessage = "Failed to append payload.";
				success = false;
				break;
			}
		}

		uint64_t payloadSize = (uint64_t)payload.size();
		if (fwrite(&payloadSize, sizeof(payloadSize), 1, file) != 1) {
			errorMessage = "Failed to append payload size.";
			success = false;
			break;
		}

		if (fwrite(kPackageMagic, 1, kPackageMagicSize, file) != kPackageMagicSize) {
			errorMessage = "Failed to append magic.";
			success = false;
			break;
		}
	} while (false);

	fclose(file);
	return success;
}

bool PackageFind(
	const char *filePath,
	uint64_t &payloadOffset,
	uint64_t &payloadSize
){
	payloadOffset = 0;
	payloadSize = 0;

	struct _stat64 fileStat;
	if (_stat64(filePath, &fileStat) != 0) {
		return false;
	}
	if (fileStat.st_size < (int64_t)(kPackageMagicSize + sizeof(uint64_t))) {
		return false;
	}

	FILE *file = fopen(filePath, "rb");
	if (file == NULL) return false;

	bool found = false;
	do {
		if (_fseeki64(file, -((int64_t)kPackageMagicSize), SEEK_END) != 0) break;

		char magicBuffer[kPackageMagicSize] = {0};
		if (fread(magicBuffer, 1, kPackageMagicSize, file) != kPackageMagicSize) break;
		if (memcmp(magicBuffer, kPackageMagic, kPackageMagicSize) != 0) break;

		if (_fseeki64(file, -((int64_t)kPackageMagicSize + (int64_t)sizeof(uint64_t)), SEEK_END) != 0) break;
		uint64_t size = 0;
		if (fread(&size, sizeof(size), 1, file) != 1) break;

		int64_t offset = fileStat.st_size - (int64_t)kPackageMagicSize - (int64_t)sizeof(uint64_t) - (int64_t)size;
		if (offset < 0) break;

		payloadOffset = (uint64_t)offset;
		payloadSize = size;
		found = true;
	} while (false);

	fclose(file);
	return found;
}

bool PackageExtractToDirectory(
	const char *filePath,
	const char *dstDirectory,
	std::string &errorMessage
){
	errorMessage.clear();

	uint64_t payloadOffset = 0;
	uint64_t payloadSize = 0;
	if (PackageFind(filePath, payloadOffset, payloadSize) == false) {
		errorMessage = "Package not found.";
		return false;
	}

	if (payloadSize == 0) {
		errorMessage = "Package is empty.";
		return false;
	}

	std::vector<uint8_t> payload;
	payload.resize((size_t)payloadSize);

	FILE *file = fopen(filePath, "rb");
	if (file == NULL) {
		errorMessage = "Failed to open file for reading.";
		return false;
	}

	bool success = true;
	do {
		if (_fseeki64(file, (int64_t)payloadOffset, SEEK_SET) != 0) {
			errorMessage = "Failed to seek to payload.";
			success = false;
			break;
		}
		if (fread(payload.data(), 1, payload.size(), file) != payload.size()) {
			errorMessage = "Failed to read payload.";
			success = false;
			break;
		}
	} while (false);

	fclose(file);
	if (success == false) return false;

	size_t cursor = 0;
	if (payload.size() < sizeof(uint32_t)) {
		errorMessage = "Corrupted package header.";
		return false;
	}

	auto readUint32 = [&payload, &cursor, &errorMessage](uint32_t &value)->bool{
		if (cursor + sizeof(uint32_t) > payload.size()) {
			errorMessage = "Unexpected end of package.";
			return false;
		}
		value = 0;
		for (int i = 0; i < 4; ++i) {
			value |= ((uint32_t)payload[cursor + i]) << (i * 8);
		}
		cursor += sizeof(uint32_t);
		return true;
	};
	auto readUint64 = [&payload, &cursor, &errorMessage](uint64_t &value)->bool{
		if (cursor + sizeof(uint64_t) > payload.size()) {
			errorMessage = "Unexpected end of package.";
			return false;
		}
		value = 0;
		for (int i = 0; i < 8; ++i) {
			value |= ((uint64_t)payload[cursor + i]) << (i * 8);
		}
		cursor += sizeof(uint64_t);
		return true;
	};

	uint32_t fileCount = 0;
	if (readUint32(fileCount) == false) {
		return false;
	}

	for (uint32_t i = 0; i < fileCount; ++i) {
		uint32_t pathLen = 0;
		if (readUint32(pathLen) == false) return false;
		if (pathLen == 0 || cursor + pathLen > payload.size()) {
			errorMessage = "Corrupted package entry path.";
			return false;
		}

		std::string path((const char *)&payload[cursor], pathLen);
		cursor += pathLen;
		if (ContainsParentTraversal(path)) {
			errorMessage = "Unsafe path inside package.";
			return false;
		}

		uint64_t dataLen = 0;
		if (readUint64(dataLen) == false) return false;
		if (cursor + dataLen > payload.size()) {
			errorMessage = "Corrupted package entry size.";
			return false;
		}

		char dstPath[MAX_PATH] = {0};
		GenerateCombinedPath(dstPath, sizeof(dstPath), dstDirectory, path.c_str());
		if (EnsureDirectoryForFile(dstPath, errorMessage) == false) {
			return false;
		}

		FILE *out = fopen(dstPath, "wb");
		if (out == NULL) {
			errorMessage = std::string("Failed to write: ") + dstPath;
			return false;
		}
		if (dataLen > 0) {
			if (fwrite(&payload[cursor], 1, (size_t)dataLen, out) != dataLen) {
				errorMessage = std::string("Failed to write: ") + dstPath;
				fclose(out);
				return false;
			}
		}
		fclose(out);
		cursor += (size_t)dataLen;
	}

	return true;
}

bool PackageExists(const char *filePath){
	uint64_t offset = 0;
	uint64_t size = 0;
	return PackageFind(filePath, offset, size);
}

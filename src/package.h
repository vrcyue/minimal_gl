/* Simple appendable package writer/reader for embedding assets at the end of
 * the executable.
 *
 * Layout (little endian):
 *   payload:
 *     uint32  file_count
 *     repeated file_count times:
 *       uint32 path_length
 *       path_length bytes of path (UTF-8)
 *       uint64 data_length
 *       data_length bytes of file data
 *   [payload][payload_size(uint64)][magic("MGLPKG1")]
 */

#ifndef _PACKAGE_H_
#define _PACKAGE_H_

#include <stdint.h>
#include <string>
#include <vector>

struct PackageEntry {
	std::string path;
	std::vector<uint8_t> data;
};

bool PackageBuildPayload(
	const std::vector<PackageEntry> &entries,
	std::vector<uint8_t> &payload,
	std::string &errorMessage
);

bool PackageAppendToFile(
	const char *filePath,
	const std::vector<PackageEntry> &entries,
	std::string &errorMessage
);

bool PackageFind(
	const char *filePath,
	uint64_t &payloadOffset,
	uint64_t &payloadSize
);

bool PackageExtractToDirectory(
	const char *filePath,
	const char *dstDirectory,
	std::string &errorMessage
);

bool PackageExists(const char *filePath);

#endif /* _PACKAGE_H_ */

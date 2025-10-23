/* Copyright (C) 2018 Yosshin(@yosshin4004) */

#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#define WINDOWS_IGNORE_PACKING_MISMATCH
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "config.h"

#ifndef ENABLE_PIPELINE_DEBUG_LOG
#define ENABLE_PIPELINE_DEBUG_LOG 1
#endif


/* fopen 等のセキュアでないレガシー API に対する警告の抑制 */
#pragma warning(disable:4996)


/* 決め打ちのリソースハンドル */
#define ASSUMED_SOUND_SSBO													1

#define COMPUTE_TEXTURE_START_INDEX				(4)

#define PIPELINE_MAX_RESOURCES				(32)
#define PIPELINE_MAX_PASSES				(16)
#define PIPELINE_MAX_BINDINGS_PER_PASS		(16)
#define PIPELINE_MAX_HISTORY_LENGTH		(4)
#define PIPELINE_MAX_RESOURCE_ID_LENGTH	(64)
#define PIPELINE_MAX_PASS_NAME_LENGTH		(64)
#define PIPELINE_MAX_SHADER_PATH_LENGTH	(MAX_PATH)

typedef enum {
	PixelFormatUnorm8Rgba,
	PixelFormatFp16Rgba,
	PixelFormatFp32Rgba,
} PixelFormat;

typedef enum {
	TextureFilterNearest,
	TextureFilterLinear,
} TextureFilter;

typedef enum {
	TextureWrapRepeat,
	TextureWrapClampToEdge,
	TextureWrapMirroredRepeat,
} TextureWrap;

typedef enum {
	PipelinePassTypeFragment,
	PipelinePassTypeCompute,
	PipelinePassTypePresent,
} PipelinePassType;

typedef enum {
	PipelineResourceAccessSampled,
	PipelineResourceAccessImageRead,
	PipelineResourceAccessImageWrite,
	PipelineResourceAccessHistoryRead,
	PipelineResourceAccessColorAttachment,
} PipelineResourceAccess;

typedef enum {
	PipelineResolutionModeFramebuffer,
	PipelineResolutionModeFixed,
} PipelineResolutionMode;

typedef struct {
	PipelineResolutionMode mode;
	int width;
	int height;
} PipelineResourceResolution;

typedef struct {
	char id[PIPELINE_MAX_RESOURCE_ID_LENGTH];
	PixelFormat pixelFormat;
	PipelineResourceResolution resolution;
	int historyLength;
	TextureFilter textureFilter;
	TextureWrap textureWrap;
	GLuint glTextureIds[PIPELINE_MAX_HISTORY_LENGTH];
} PipelineResource;

typedef struct {
	int resourceIndex;
	PipelineResourceAccess access;
	int historyOffset;
} PipelineResourceBinding;

typedef struct {
	bool enableColorClear;
	float clearColor[4];
	bool enableDepthClear;
	float clearDepth;
} PipelinePassClear;

typedef struct {
	char name[PIPELINE_MAX_PASS_NAME_LENGTH];
	PipelinePassType type;
	char shaderPath[PIPELINE_MAX_SHADER_PATH_LENGTH];
	GLuint programId;
	PipelineResourceBinding inputs[PIPELINE_MAX_BINDINGS_PER_PASS];
	int numInputs;
	PipelineResourceBinding outputs[PIPELINE_MAX_BINDINGS_PER_PASS];
	int numOutputs;
	PipelinePassClear clear;
	bool overrideWorkGroupSize;
	GLuint workGroupSize[3];
} PipelinePass;

typedef struct PipelineDescription {
	PipelineResource resources[PIPELINE_MAX_RESOURCES];
	int numResources;
	PipelinePass passes[PIPELINE_MAX_PASSES];
	int numPasses;
} PipelineDescription;

#include "pipeline_description.inl"

typedef struct {
	GLuint textureIds[PIPELINE_MAX_HISTORY_LENGTH];
	int width;
	int height;
	PixelFormat pixelFormat;
	int historyLength;
	bool initialized;
} PipelineRuntimeResourceState;

static PipelineDescription s_pipelineDescription = {0};
static PipelineRuntimeResourceState s_pipelineRuntimeResources[PIPELINE_MAX_RESOURCES] = {{0}};
static int s_activePipelinePassIndex = -1;
static bool s_fragmentPipelinePassUniformAvailable = false;
static bool s_computePipelinePassUniformAvailable = false;
static bool s_loggedPipelineExecutionFailure = false;

/*=============================================================================
▼	各種リソースの取り込み
-----------------------------------------------------------------------------*/
#include "resource.cpp"


/*=============================================================================
▼	OpenGL 関数テーブル
-----------------------------------------------------------------------------*/
static void *s_glExtFunctions[NUM_GLEXT_FUNCTIONS] = {0};


/*=============================================================================
▼	各種構造体
-----------------------------------------------------------------------------*/
static GLint s_graphicsComputeWorkGroupSize[3] = {1, 1, 1};
static GLuint s_graphicsComputeProgramId = 0;

/*=============================================================================
▼	ローカル関数（CRT 非依存）
-----------------------------------------------------------------------------*/
static size_t PipelineStrLen(const char *str){
	size_t len = 0;
	if (str == NULL) return 0;
	while (str[len] != '\0') {
		++len;
	}
	return len;
}

static int PipelineStrncmp(const char *lhs, const char *rhs, size_t count){
	if (lhs == NULL || rhs == NULL) return (lhs == rhs)? 0: (lhs? 1: -1);
	for (size_t i = 0; i < count; ++i) {
		unsigned char a = (unsigned char)lhs[i];
		unsigned char b = (unsigned char)rhs[i];
		if (a != b || a == '\0' || b == '\0') {
			return (int)a - (int)b;
		}
	}
	return 0;
}

static int PipelineAtoi(const char *str){
	if (str == NULL) return 0;
	int sign = 1;
	int value = 0;
	if (*str == '+') {
		++str;
	} else if (*str == '-') {
		sign = -1;
		++str;
	}
	while (*str >= '0' && *str <= '9') {
		value = value * 10 + (*str - '0');
		++str;
	}
	return sign * value;
}

static void *PipelineMemcpy(void *dst, const void *src, size_t size){
	if (dst == NULL || src == NULL) return dst;
	unsigned char *d = (unsigned char*)dst;
	const unsigned char *s = (const unsigned char*)src;
	for (size_t i = 0; i < size; ++i) {
		d[i] = s[i];
	}
	return dst;
}

static void *PipelineMemset(void *dst, int value, size_t size){
	if (dst == NULL) return dst;
	unsigned char *d = (unsigned char*)dst;
	unsigned char v = (unsigned char)value;
	for (size_t i = 0; i < size; ++i) {
		d[i] = v;
	}
	return dst;
}

extern "C" void *__cdecl memset(void *dst, int value, size_t size){
	return PipelineMemset(dst, value, size);
}

#if ENABLE_PIPELINE_DEBUG_LOG
static HANDLE PipelineDebugGetFileHandle(){
	static HANDLE s_handle = INVALID_HANDLE_VALUE;
	if (s_handle == INVALID_HANDLE_VALUE) {
		s_handle = CreateFileA(
			"pipeline_debug_log.txt",
			FILE_APPEND_DATA,
			FILE_SHARE_READ,
			NULL,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);
		if (s_handle != INVALID_HANDLE_VALUE) {
			SetFilePointer(s_handle, 0, NULL, FILE_END);
		}
	}
	return s_handle;
}

static void PipelineDebugPrint(const char *format, ...){
	HANDLE fileHandle = PipelineDebugGetFileHandle();
	if (fileHandle == INVALID_HANDLE_VALUE) {
		return;
}

	char buffer[512];
	va_list args;
	va_start(args, format);
	int length = wvsprintfA(buffer, format, args);
	va_end(args);
	if (length <= 0) {
		return;
	}

	DWORD bytesWritten = 0;
	WriteFile(
		fileHandle,
		buffer,
		(DWORD)length,
		&bytesWritten,
		NULL
	);
}
static int s_debugLogRemainingFrames = 32;
static int s_debugLogLastFrame = -1;
static bool PipelineDebugShouldLog(int frameCount){
	if (s_debugLogRemainingFrames <= 0) {
		return false;
	}
	if (frameCount != s_debugLogLastFrame) {
		s_debugLogLastFrame = frameCount;
		--s_debugLogRemainingFrames;
	}
	return true;
}

static const char *PipelineDebugGetGlErrorString(GLenum error){
	switch (error) {
		case GL_NO_ERROR: return "GL_NO_ERROR";
		case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
		case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
		case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
		case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
		case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
		default: return "GL_UNKNOWN_ERROR";
	}
}
#if ENABLE_PIPELINE_DEBUG_LOG
static void PipelineDebugSampleTexture(GLuint textureId, int width, int height, const char *label){
	if (textureId == 0 || label == NULL) return;
	if (width <= 0 || height <= 0) return;
	size_t pixelCount = (size_t)width * (size_t)height;
	size_t componentCount = pixelCount * 4;
	if (componentCount == 0) return;
	static float *s_buffer = NULL;
	static size_t s_bufferCapacity = 0;
	if (componentCount > s_bufferCapacity) {
		if (s_buffer) {
			HeapFree(GetProcessHeap(), 0, s_buffer);
			s_buffer = NULL;
			s_bufferCapacity = 0;
		}
		size_t byteCount = componentCount * sizeof(float);
		s_buffer = (float*)HeapAlloc(
			GetProcessHeap(),
			0,
			byteCount
		);
		if (s_buffer == NULL) {
			return;
		}
		s_bufferCapacity = componentCount;
	}
	glBindTexture(GL_TEXTURE_2D, textureId);
	glGetTexImage(
		GL_TEXTURE_2D,
		0,
		GL_RGBA,
		GL_FLOAT,
		s_buffer
	);
	glBindTexture(GL_TEXTURE_2D, 0);
	float pixel[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	pixel[0] = s_buffer[0];
	pixel[1] = s_buffer[1];
	pixel[2] = s_buffer[2];
	pixel[3] = s_buffer[3];
	float pixelCenter[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	int centerX = width / 2;
	int centerY = height / 2;
	size_t centerIndex = ((size_t)centerY * (size_t)width + (size_t)centerX) * 4;
	if (centerIndex + 3 < componentCount) {
		pixelCenter[0] = s_buffer[centerIndex + 0];
		pixelCenter[1] = s_buffer[centerIndex + 1];
		pixelCenter[2] = s_buffer[centerIndex + 2];
		pixelCenter[3] = s_buffer[centerIndex + 3];
	}
	uint32_t pixelBits[4] = {0, 0, 0, 0};
	uint32_t pixelCenterBits[4] = {0, 0, 0, 0};
	for (int i = 0; i < 4; ++i) {
		pixelBits[i] = *((uint32_t*)(&pixel[i]));
		pixelCenterBits[i] = *((uint32_t*)(&pixelCenter[i]));
	}
	PipelineDebugPrint("[Pipeline Debug]    sample %s texId=%u rgba0_hex=(%08X,%08X,%08X,%08X) rgbaCenter_hex=(%08X,%08X,%08X,%08X)\n",
		label,
		textureId,
		pixelBits[0],
		pixelBits[1],
		pixelBits[2],
		pixelBits[3],
		pixelCenterBits[0],
		pixelCenterBits[1],
		pixelCenterBits[2],
		pixelCenterBits[3]
	);
}
#endif
#else
static void PipelineDebugPrint(const char *format, ...){
	(void)format;
}
static bool PipelineDebugShouldLog(int frameCount){
	(void)frameCount;
	return false;
}
#endif

static bool PipelineProgramHasUniform(
	GLuint programId,
	GLint location,
	GLenum typeEnum
){
	if (programId == 0) {
		return false;
	}
	GLint numActiveUniforms = 0;
	glExtGetProgramInterfaceiv(
		/* GLuint program */			programId,
		/* GLenum programInterface */	GL_UNIFORM,
		/* GLenum pname */				GL_ACTIVE_RESOURCES,
		/* GLint *params */				&numActiveUniforms
	);
	GLenum properties[2] = {GL_TYPE, GL_LOCATION};
	GLint values[2] = {0, 0};
	for (int index = 0; index < numActiveUniforms; ++index) {
		glExtGetProgramResourceiv(
			/* GLuint program */			programId,
			/* GLenum programInterface */	GL_UNIFORM,
			/* GLuint index */				(GLuint)index,
			/* GLsizei propCount */			2,
			/* const GLenum *props */		properties,
			/* GLsizei bufSize */			2,
			/* GLsizei *length */			NULL,
			/* GLint *params */				values
		);
		if (values[0] == (GLint)typeEnum && values[1] == location) {
			return true;
		}
	}
	return false;
}

static void PipelineDeleteRuntimeResource(PipelineRuntimeResourceState *state){
	if (state == NULL) return;
	if (state->initialized == false) return;
	for (int historyIndex = 0; historyIndex < PIPELINE_MAX_HISTORY_LENGTH; ++historyIndex) {
		if (state->textureIds[historyIndex] != 0) {
			glDeleteTextures(1, &state->textureIds[historyIndex]);
			state->textureIds[historyIndex] = 0;
		}
	}
	state->initialized = false;
	state->width = 0;
	state->height = 0;
	state->pixelFormat = PixelFormatUnorm8Rgba;
	state->historyLength = 0;
}

static void PipelineResetRuntimeResources(void){
	for (int resourceIndex = 0; resourceIndex < PIPELINE_MAX_RESOURCES; ++resourceIndex) {
		PipelineDeleteRuntimeResource(&s_pipelineRuntimeResources[resourceIndex]);
	}
}

static void PipelineGetPixelFormatInfo(PixelFormat pixelFormat, GLenum *internalformat, GLenum *format, GLenum *type){
	GLenum internal = GL_RGBA8;
	GLenum fmt = GL_RGBA;
	GLenum tp = GL_UNSIGNED_BYTE;
	switch (pixelFormat) {
		default:
		case PixelFormatUnorm8Rgba: {
			internal = GL_RGBA8;
			fmt = GL_RGBA;
			tp = GL_UNSIGNED_BYTE;
		} break;
		case PixelFormatFp16Rgba: {
			internal = GL_RGBA16F;
			fmt = GL_RGBA;
			tp = GL_HALF_FLOAT;
		} break;
		case PixelFormatFp32Rgba: {
			internal = GL_RGBA32F;
			fmt = GL_RGBA;
			tp = GL_FLOAT;
		} break;
	}
	if (internalformat) *internalformat = internal;
	if (format) *format = fmt;
	if (type) *type = tp;
}

static void PipelineSetTextureSampler(GLenum target, TextureFilter filter, TextureWrap wrap, bool enableMipmap){
	GLint minFilter = GL_LINEAR;
	GLint magFilter = GL_LINEAR;
	switch (filter) {
		case TextureFilterNearest: {
			minFilter = enableMipmap? GL_NEAREST_MIPMAP_NEAREST: GL_NEAREST;
			magFilter = GL_NEAREST;
		} break;
		case TextureFilterLinear:
		default: {
			minFilter = enableMipmap? GL_LINEAR_MIPMAP_LINEAR: GL_LINEAR;
			magFilter = GL_LINEAR;
		} break;
	}
	GLint wrapParam = GL_REPEAT;
	switch (wrap) {
		case TextureWrapRepeat: {
			wrapParam = GL_REPEAT;
		} break;
		case TextureWrapClampToEdge: {
			wrapParam = GL_CLAMP_TO_EDGE;
		} break;
		case TextureWrapMirroredRepeat: {
			wrapParam = GL_MIRRORED_REPEAT;
		} break;
		default: {
			wrapParam = GL_REPEAT;
		} break;
	}
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, magFilter);
	glTexParameteri(target, GL_TEXTURE_WRAP_S, wrapParam);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, wrapParam);
	glTexParameteri(target, GL_TEXTURE_WRAP_R, wrapParam);
}

static void PipelineCreateOrResizeResource(
	PipelineRuntimeResourceState *state,
	const PipelineResource *resource
){
	if (state == NULL || resource == NULL) return;

	int historyLength = resource->historyLength;
	if (historyLength <= 0) historyLength = 1;
	if (historyLength > PIPELINE_MAX_HISTORY_LENGTH) {
		historyLength = PIPELINE_MAX_HISTORY_LENGTH;
	}

	int width = (resource->resolution.mode == PipelineResolutionModeFixed)? resource->resolution.width: SCREEN_WIDTH;
	int height = (resource->resolution.mode == PipelineResolutionModeFixed)? resource->resolution.height: SCREEN_HEIGHT;
	if (width <= 0) width = SCREEN_WIDTH;
	if (height <= 0) height = SCREEN_HEIGHT;

	bool needsRecreate =
		state->initialized == false
	||	state->width != width
	||	state->height != height
	||	state->pixelFormat != resource->pixelFormat
	||	state->historyLength != historyLength;

	if (needsRecreate) {
		PipelineDeleteRuntimeResource(state);

		GLenum internalformat = GL_RGBA8;
		GLenum format = GL_RGBA;
		GLenum type = GL_UNSIGNED_BYTE;
		PipelineGetPixelFormatInfo(resource->pixelFormat, &internalformat, &format, &type);

		glGenTextures(historyLength, state->textureIds);
		for (int historyIndex = 0; historyIndex < historyLength; ++historyIndex) {
			glBindTexture(GL_TEXTURE_2D, state->textureIds[historyIndex]);
			glTexImage2D(GL_TEXTURE_2D, 0, internalformat, width, height, 0, format, type, NULL);
			PipelineSetTextureSampler(GL_TEXTURE_2D, resource->textureFilter, resource->textureWrap, false);
		}
		glBindTexture(GL_TEXTURE_2D, 0);

		state->initialized = true;
		state->width = width;
		state->height = height;
		state->pixelFormat = resource->pixelFormat;
		state->historyLength = historyLength;
	} else {
		for (int historyIndex = 0; historyIndex < state->historyLength; ++historyIndex) {
			glBindTexture(GL_TEXTURE_2D, state->textureIds[historyIndex]);
			PipelineSetTextureSampler(GL_TEXTURE_2D, resource->textureFilter, resource->textureWrap, false);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

static void PipelineEnsureResources(const PipelineDescription *pipeline){
	if (pipeline == NULL) return;
	for (int resourceIndex = 0; resourceIndex < pipeline->numResources; ++resourceIndex) {
		PipelineCreateOrResizeResource(&s_pipelineRuntimeResources[resourceIndex], &pipeline->resources[resourceIndex]);
	}
}

static int PipelineResolveHistoryIndex(
	const PipelineRuntimeResourceState *state,
	int frameCount,
	int historyOffset
){
	if (state == NULL || state->initialized == false || state->historyLength <= 0) {
		return -1;
	}

	int historyLength = state->historyLength;
	int baseIndex = 0;
	if (historyLength > 0) {
		baseIndex = frameCount % historyLength;
		if (baseIndex < 0) {
			baseIndex += historyLength;
		}
	}

	int resolvedIndex = baseIndex + historyOffset;
	while (historyLength > 0 && resolvedIndex < 0) {
		resolvedIndex += historyLength;
	}
	if (historyLength > 0) {
		resolvedIndex %= historyLength;
		if (resolvedIndex < 0) {
			resolvedIndex += historyLength;
		}
	}
	if (resolvedIndex < 0 || resolvedIndex >= historyLength) {
		resolvedIndex = baseIndex;
	}
	return resolvedIndex;
}

static GLuint PipelineAcquireResourceTexture(
	int resourceIndex,
	int frameCount,
	int historyOffset
){
	if (resourceIndex < 0 || resourceIndex >= PIPELINE_MAX_RESOURCES) {
		return 0;
	}
	PipelineRuntimeResourceState *state = &s_pipelineRuntimeResources[resourceIndex];
	if (state->initialized == false || state->historyLength <= 0) {
		return 0;
	}

	int resolvedIndex = PipelineResolveHistoryIndex(state, frameCount, historyOffset);
	if (resolvedIndex < 0) {
		return 0;
	}
	return state->textureIds[resolvedIndex];
}

static bool PipelineParseLegacyResourceIndex(
	const PipelineResource *resource,
	const char *prefix,
	int *outIndex
){
	if (resource == NULL || prefix == NULL) return false;
	size_t prefixLength = PipelineStrLen(prefix);
	if (PipelineStrncmp(resource->id, prefix, prefixLength) != 0) {
		return false;
	}
	int index = 0;
	const char *suffix = resource->id + prefixLength;
	if (*suffix != '\0') {
		index = PipelineAtoi(suffix);
	}
	if (outIndex) {
		*outIndex = index;
	}
	return true;
}

static bool PipelineExecuteComputePass(
	const PipelineDescription *pipeline,
	const PipelinePass *pass,
	int frameCount,
	int waveOutPos,
	float timeInSeconds
){
	if (pipeline == NULL || pass == NULL) return false;
	if (s_graphicsComputeProgramId == 0) return false;

	GLenum defaultInternalformat = GL_RGBA8;
	PipelineGetPixelFormatInfo(PixelFormatUnorm8Rgba, &defaultInternalformat, NULL, NULL);

	GLuint samplerUnitBase = COMPUTE_TEXTURE_START_INDEX;
	GLuint samplerUnitCount = 0;
	GLuint boundSamplerUnits[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	int numBoundSamplerUnits = 0;
	GLenum imageFormats[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	GLuint boundImageUnits[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	GLenum boundImageAccess[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	int numBoundImageUnits = 0;
#if ENABLE_PIPELINE_DEBUG_LOG
	bool debugLog = PipelineDebugShouldLog(frameCount);
	GLuint debugPrevTextureId = 0;
	GLuint debugOutputTextureId = 0;
	int debugPrevWidth = 0;
	int debugPrevHeight = 0;
	int debugOutputWidth = 0;
	int debugOutputHeight = 0;
#else
	bool debugLog = false;
#endif

	glExtUseProgram(s_graphicsComputeProgramId);
	if (s_computePipelinePassUniformAvailable) {
		glExtUniform1i(UNIFORM_LOCATION_PIPELINE_PASS_INDEX, s_activePipelinePassIndex);
	}

	for (int inputIndex = 0; inputIndex < pass->numInputs; ++inputIndex) {
		const PipelineResourceBinding *binding = &pass->inputs[inputIndex];
		GLuint textureId = PipelineAcquireResourceTexture(binding->resourceIndex, frameCount, binding->historyOffset);
		const PipelineResource *resource = &pipeline->resources[binding->resourceIndex];
		const PipelineRuntimeResourceState *inputState = &s_pipelineRuntimeResources[binding->resourceIndex];
		if (textureId == 0 || resource == NULL) {
#if ENABLE_PIPELINE_DEBUG_LOG
			if (debugLog) {
				PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" failed: input %d resourceIndex=%d textureId=%u resource=%p\n",
					frameCount, pass->name, inputIndex, binding->resourceIndex, textureId, (const void*)resource);
			}
#endif
			return false;
		}
		GLenum internalformat = defaultInternalformat;
		PipelineGetPixelFormatInfo(resource->pixelFormat, &internalformat, NULL, NULL);
		switch (binding->access) {
			case PipelineResourceAccessSampled:
			case PipelineResourceAccessHistoryRead: {
				GLuint samplerUnit = samplerUnitBase + samplerUnitCount;
				glExtActiveTexture(GL_TEXTURE0 + samplerUnit);
				glBindTexture(GL_TEXTURE_2D, textureId);
				PipelineSetTextureSampler(GL_TEXTURE_2D, resource->textureFilter, resource->textureWrap, false);
				boundSamplerUnits[numBoundSamplerUnits++] = samplerUnit;
				++samplerUnitCount;
#if ENABLE_PIPELINE_DEBUG_LOG
				if (debugLog) {
					int resolvedIndex = PipelineResolveHistoryIndex(inputState, frameCount, binding->historyOffset);
					PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" input %d \"%s\" sampler unit %u historyOffset=%d resolvedIndex=%d textureId=%u\n",
						frameCount, pass->name, inputIndex, resource->id, samplerUnit, binding->historyOffset, resolvedIndex, textureId);
					if (binding->access == PipelineResourceAccessHistoryRead) {
						debugPrevTextureId = textureId;
						if (inputState && inputState->initialized) {
							debugPrevWidth = inputState->width;
							debugPrevHeight = inputState->height;
						}
						PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" input history \"%s\" resolvedIndex=%d size=%dx%d\n",
							frameCount, pass->name, resource->id, resolvedIndex,
							inputState && inputState->initialized ? inputState->width : 0,
							inputState && inputState->initialized ? inputState->height : 0);
					}
				}
#endif
			} break;
			case PipelineResourceAccessImageRead: {
				GLenum format = internalformat;
				glExtBindImageTexture(numBoundImageUnits, textureId, 0, GL_FALSE, 0, GL_READ_ONLY, format);
				boundImageUnits[numBoundImageUnits] = numBoundImageUnits;
				boundImageAccess[numBoundImageUnits] = GL_READ_ONLY;
				imageFormats[numBoundImageUnits] = format;
				++numBoundImageUnits;
#if ENABLE_PIPELINE_DEBUG_LOG
				if (debugLog) {
					int resolvedIndex = PipelineResolveHistoryIndex(inputState, frameCount, binding->historyOffset);
					PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" input %d \"%s\" image unit %u READ historyOffset=%d resolvedIndex=%d textureId=%u\n",
						frameCount, pass->name, inputIndex, resource->id, numBoundImageUnits - 1, binding->historyOffset, resolvedIndex, textureId);
				}
#endif
			} break;
			default: {
#if ENABLE_PIPELINE_DEBUG_LOG
				if (debugLog) {
					PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" unsupported input access %d\n",
						frameCount, pass->name, (int)binding->access);
				}
#endif
				return false;
			} break;
		}
	}

	bool hasWritableOutput = false;
	for (int outputIndex = 0; outputIndex < pass->numOutputs; ++outputIndex) {
		const PipelineResourceBinding *binding = &pass->outputs[outputIndex];
		GLuint textureId = PipelineAcquireResourceTexture(binding->resourceIndex, frameCount, binding->historyOffset);
		const PipelineResource *resource = &pipeline->resources[binding->resourceIndex];
		const PipelineRuntimeResourceState *outputState = &s_pipelineRuntimeResources[binding->resourceIndex];
		if (textureId == 0 || resource == NULL) {
#if ENABLE_PIPELINE_DEBUG_LOG
			if (debugLog) {
				PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" failed: output %d resourceIndex=%d textureId=%u resource=%p\n",
					frameCount, pass->name, outputIndex, binding->resourceIndex, textureId, (const void*)resource);
			}
#endif
			return false;
		}
		GLenum internalformat = defaultInternalformat;
		PipelineGetPixelFormatInfo(resource->pixelFormat, &internalformat, NULL, NULL);
		switch (binding->access) {
			case PipelineResourceAccessImageWrite: {
				bool hasMatchingInput = false;
				for (int inputIndex = 0; inputIndex < pass->numInputs; ++inputIndex) {
					const PipelineResourceBinding *inputBinding = &pass->inputs[inputIndex];
					if (inputBinding->resourceIndex == binding->resourceIndex) {
						hasMatchingInput = true;
						break;
					}
				}
				GLenum imageAccess = GL_READ_WRITE;
				int imageUnit = numBoundImageUnits;
				glExtBindImageTexture(imageUnit, textureId, 0, GL_FALSE, 0, imageAccess, internalformat);
				boundImageUnits[numBoundImageUnits] = imageUnit;
				boundImageAccess[numBoundImageUnits] = imageAccess;
				imageFormats[numBoundImageUnits] = internalformat;
				++numBoundImageUnits;
				hasWritableOutput = true;
#if ENABLE_PIPELINE_DEBUG_LOG
				if (debugLog) {
					int resolvedIndex = PipelineResolveHistoryIndex(outputState, frameCount, binding->historyOffset);
					PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" output %d \"%s\" image unit %d access=%s historyOffset=%d resolvedIndex=%d textureId=%u matchingInput=%d\n",
						frameCount, pass->name, outputIndex, resource->id, imageUnit,
						(imageAccess == GL_READ_WRITE) ? "READ_WRITE" : "WRITE_ONLY",
						binding->historyOffset, resolvedIndex, textureId, hasMatchingInput ? 1 : 0);
					debugOutputTextureId = textureId;
					if (outputState && outputState->initialized) {
						debugOutputWidth = outputState->width;
						debugOutputHeight = outputState->height;
					}
					PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" output \"%s\" resolvedIndex=%d size=%dx%d access=%s\n",
						frameCount, pass->name, resource->id, resolvedIndex,
						outputState && outputState->initialized ? outputState->width : 0,
						outputState && outputState->initialized ? outputState->height : 0,
						(imageAccess == GL_READ_WRITE) ? "READ_WRITE" : "WRITE_ONLY");
					GLenum errorCode = glGetError();
					if (errorCode != GL_NO_ERROR) {
						PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" glBindImageTexture error resource=\"%s\" resolvedIndex=%d textureId=%u error=%s(0x%04x)\n",
							frameCount, pass->name, resource->id, resolvedIndex, textureId, PipelineDebugGetGlErrorString(errorCode), errorCode);
						while ((errorCode = glGetError()) != GL_NO_ERROR) {
							PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" additional GL error=%s(0x%04x)\n",
								frameCount, pass->name, PipelineDebugGetGlErrorString(errorCode), errorCode);
						}
					}
				}
#endif
			} break;
			default: {
#if ENABLE_PIPELINE_DEBUG_LOG
				if (debugLog) {
					PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" unsupported output access %d\n",
						frameCount, pass->name, (int)binding->access);
				}
#endif
				return false;
			} break;
		}
	}

	if (hasWritableOutput == false) {
		for (int i = 0; i < numBoundImageUnits; ++i) {
			glExtBindImageTexture(boundImageUnits[i], 0, 0, GL_FALSE, 0, boundImageAccess[i], imageFormats[i]);
		}
		for (int i = 0; i < numBoundSamplerUnits; ++i) {
			glExtActiveTexture(GL_TEXTURE0 + boundSamplerUnits[i]);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		glExtActiveTexture(GL_TEXTURE0);
#if ENABLE_PIPELINE_DEBUG_LOG
		if (debugLog) {
			PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" aborted: no writable outputs\n",
				frameCount, pass->name);
		}
#endif
		return false;
	}

	glExtUniform1i(UNIFORM_LOCATION_WAVE_OUT_POS, waveOutPos);
	glExtUniform1i(UNIFORM_LOCATION_FRAME_COUNT, frameCount);
	glExtUniform1f(UNIFORM_LOCATION_TIME, timeInSeconds);
	glExtUniform2f(UNIFORM_LOCATION_RESO, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT);
	glExtUniform3i(UNIFORM_LOCATION_MOUSE_BUTTONS, 0, 0, 0);

	GLuint workGroupSizeX = s_graphicsComputeWorkGroupSize[0] > 0? (GLuint)s_graphicsComputeWorkGroupSize[0]: 1;
	GLuint workGroupSizeY = s_graphicsComputeWorkGroupSize[1] > 0? (GLuint)s_graphicsComputeWorkGroupSize[1]: 1;
	GLuint workGroupSizeZ = s_graphicsComputeWorkGroupSize[2] > 0? (GLuint)s_graphicsComputeWorkGroupSize[2]: 1;
	if (pass->overrideWorkGroupSize) {
		if (pass->workGroupSize[0] > 0) workGroupSizeX = pass->workGroupSize[0];
		if (pass->workGroupSize[1] > 0) workGroupSizeY = pass->workGroupSize[1];
		if (pass->workGroupSize[2] > 0) workGroupSizeZ = pass->workGroupSize[2];
	}

	GLuint targetWidth = SCREEN_WIDTH;
	GLuint targetHeight = SCREEN_HEIGHT;
	if (pass->numOutputs > 0) {
		const PipelineRuntimeResourceState *state = &s_pipelineRuntimeResources[pass->outputs[0].resourceIndex];
		if (state->initialized) {
			targetWidth = (GLuint)state->width;
			targetHeight = (GLuint)state->height;
		}
	}

	GLuint numGroupsX = (targetWidth + workGroupSizeX - 1) / workGroupSizeX;
	GLuint numGroupsY = (targetHeight + workGroupSizeY - 1) / workGroupSizeY;
	GLuint numGroupsZ = (1 + workGroupSizeZ - 1) / workGroupSizeZ;
	if (numGroupsX == 0) numGroupsX = 1;
	if (numGroupsY == 0) numGroupsY = 1;
	if (numGroupsZ == 0) numGroupsZ = 1;
#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" dispatch (%u,%u,%u) target=%ux%u\n",
			frameCount, pass->name, numGroupsX, numGroupsY, numGroupsZ, targetWidth, targetHeight);
	}
#endif

	glExtDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
	glExtMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);

#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		if (debugPrevTextureId != 0) {
			PipelineDebugSampleTexture(debugPrevTextureId, debugPrevWidth, debugPrevHeight, "compute_prev_before");
		}
		if (debugOutputTextureId != 0) {
			PipelineDebugSampleTexture(debugOutputTextureId, debugOutputWidth, debugOutputHeight, "compute_output_after");
		}
	}
#endif

	for (int i = 0; i < numBoundImageUnits; ++i) {
		glExtBindImageTexture(boundImageUnits[i], 0, 0, GL_FALSE, 0, boundImageAccess[i], imageFormats[i]);
	}
	for (int i = 0; i < numBoundSamplerUnits; ++i) {
		glExtActiveTexture(GL_TEXTURE0 + boundSamplerUnits[i]);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glExtActiveTexture(GL_TEXTURE0);
	glExtUseProgram(0);

	return true;
}

static bool PipelineExecuteFragmentPass(
	const PipelineDescription *pipeline,
	const PipelinePass *pass,
	int frameCount,
	int waveOutPos,
	float timeInSeconds,
	GLuint fragmentProgramId,
	bool enableFrameCountUniform
){
	if (pipeline == NULL || pass == NULL) return false;
	if (fragmentProgramId == 0) return false;

	glExtUseProgram(fragmentProgramId);
	if (s_fragmentPipelinePassUniformAvailable) {
		glExtUniform1i(UNIFORM_LOCATION_PIPELINE_PASS_INDEX, s_activePipelinePassIndex);
	}

	GLuint framebuffer = 0;
	glExtGenFramebuffers(1, &framebuffer);
	glExtBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	GLenum drawBuffers[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	int numDrawBuffers = 0;
	GLuint targetWidth = SCREEN_WIDTH;
	GLuint targetHeight = SCREEN_HEIGHT;
#if ENABLE_PIPELINE_DEBUG_LOG
	bool debugLog = PipelineDebugShouldLog(frameCount);
	GLuint debugFragmentInputTextureId = 0;
	GLuint debugFragmentOutputTextureId = 0;
	int debugFragmentInputWidth = 0;
	int debugFragmentInputHeight = 0;
	int debugFragmentOutputWidth = 0;
	int debugFragmentOutputHeight = 0;
#else
	bool debugLog = false;
#endif

	for (int outputIndex = 0; outputIndex < pass->numOutputs; ++outputIndex) {
		const PipelineResourceBinding *binding = &pass->outputs[outputIndex];
		if (binding->access != PipelineResourceAccessColorAttachment) {
			continue;
		}
		GLuint textureId = PipelineAcquireResourceTexture(binding->resourceIndex, frameCount, binding->historyOffset);
		PipelineRuntimeResourceState *state = &s_pipelineRuntimeResources[binding->resourceIndex];
		if (textureId == 0 || state->initialized == false) {
			glExtBindFramebuffer(GL_FRAMEBUFFER, 0);
			glExtDeleteFramebuffers(1, &framebuffer);
#if ENABLE_PIPELINE_DEBUG_LOG
			if (debugLog) {
				PipelineDebugPrint("[Pipeline Debug] frame %d fragment pass \"%s\" failed: output %d resourceIndex=%d textureId=%u initialized=%d\n",
					frameCount, pass->name, outputIndex, binding->resourceIndex, textureId, state ? state->initialized : 0);
			}
#endif
			return false;
		}
		if (numDrawBuffers == 0) {
			targetWidth = (GLuint)state->width;
			targetHeight = (GLuint)state->height;
		}
		GLenum attachment = GL_COLOR_ATTACHMENT0 + numDrawBuffers;
		glExtFramebufferTexture(GL_FRAMEBUFFER, attachment, textureId, 0);
		drawBuffers[numDrawBuffers] = attachment;
		++numDrawBuffers;
#if ENABLE_PIPELINE_DEBUG_LOG
		if (debugLog) {
			const PipelineResource *resource = &pipeline->resources[binding->resourceIndex];
			int resolvedIndex = PipelineResolveHistoryIndex(state, frameCount, binding->historyOffset);
			PipelineDebugPrint("[Pipeline Debug] frame %d fragment pass \"%s\" output %d \"%s\" attachment=%d textureId=%u historyOffset=%d resolvedIndex=%d\n",
				frameCount, pass->name, outputIndex, resource->id, attachment, textureId, binding->historyOffset, resolvedIndex);
			if (outputIndex == 0) {
				debugFragmentOutputTextureId = textureId;
				if (state) {
					debugFragmentOutputWidth = state->width;
					debugFragmentOutputHeight = state->height;
				}
				PipelineDebugPrint("[Pipeline Debug] frame %d fragment pass \"%s\" output \"%s\" resolvedIndex=%d size=%dx%d\n",
					frameCount, pass->name, resource->id, resolvedIndex,
					state ? state->width : 0,
					state ? state->height : 0);
			}
		}
#endif
	}

	if (numDrawBuffers == 0) {
		glExtBindFramebuffer(GL_FRAMEBUFFER, 0);
		glExtDeleteFramebuffers(1, &framebuffer);
#if ENABLE_PIPELINE_DEBUG_LOG
		if (debugLog) {
			PipelineDebugPrint("[Pipeline Debug] frame %d fragment pass \"%s\" aborted: no draw buffers\n",
				frameCount, pass->name);
		}
#endif
		return false;
	}

	glExtDrawBuffers(numDrawBuffers, drawBuffers);
	if (glExtCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		glExtBindFramebuffer(GL_FRAMEBUFFER, 0);
		glExtDeleteFramebuffers(1, &framebuffer);
#if ENABLE_PIPELINE_DEBUG_LOG
		if (debugLog) {
			PipelineDebugPrint("[Pipeline Debug] frame %d fragment pass \"%s\" aborted: framebuffer incomplete\n",
				frameCount, pass->name);
		}
#endif
		return false;
	}

	glViewport(0, 0, targetWidth, targetHeight);

	if (pass->clear.enableColorClear) {
		glClearColor(
			pass->clear.clearColor[0],
			pass->clear.clearColor[1],
			pass->clear.clearColor[2],
			pass->clear.clearColor[3]
		);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	GLuint samplerUnits[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	int numBoundSamplerUnits = 0;
	for (int inputIndex = 0; inputIndex < pass->numInputs; ++inputIndex) {
		const PipelineResourceBinding *binding = &pass->inputs[inputIndex];
		GLuint textureId = PipelineAcquireResourceTexture(binding->resourceIndex, frameCount, binding->historyOffset);
		const PipelineResource *resource = &pipeline->resources[binding->resourceIndex];
		if (textureId == 0 || resource == NULL) {
			glExtBindFramebuffer(GL_FRAMEBUFFER, 0);
			glExtDeleteFramebuffers(1, &framebuffer);
#if ENABLE_PIPELINE_DEBUG_LOG
			if (debugLog) {
				PipelineDebugPrint("[Pipeline Debug] frame %d fragment pass \"%s\" failed: input %d resourceIndex=%d textureId=%u resource=%p\n",
					frameCount, pass->name, inputIndex, binding->resourceIndex, textureId, (const void*)resource);
			}
#endif
			return false;
		}
		if (binding->access != PipelineResourceAccessSampled && binding->access != PipelineResourceAccessHistoryRead) {
			continue;
		}
		GLuint samplerUnit = numBoundSamplerUnits;
		int legacyIndex = 0;
		if (PipelineParseLegacyResourceIndex(resource, "legacy_mrt", &legacyIndex)) {
			samplerUnit = (GLuint)legacyIndex;
		}
		else if (PipelineParseLegacyResourceIndex(resource, "legacy_compute", &legacyIndex)) {
			samplerUnit = COMPUTE_TEXTURE_START_INDEX + (GLuint)legacyIndex;
		}
		else {
			samplerUnit = (GLuint)(numBoundSamplerUnits);
		}
		if (samplerUnit >= PIPELINE_MAX_BINDINGS_PER_PASS) {
			samplerUnit = (GLuint)(PIPELINE_MAX_BINDINGS_PER_PASS - 1);
		}
		glExtActiveTexture(GL_TEXTURE0 + samplerUnit);
		glBindTexture(GL_TEXTURE_2D, textureId);
		PipelineSetTextureSampler(GL_TEXTURE_2D, resource->textureFilter, resource->textureWrap, false);
		if (samplerUnit >= numBoundSamplerUnits) {
			samplerUnits[numBoundSamplerUnits++] = samplerUnit;
		}
#if ENABLE_PIPELINE_DEBUG_LOG
		if (debugLog) {
			const PipelineRuntimeResourceState *inputState = &s_pipelineRuntimeResources[binding->resourceIndex];
			int resolvedIndex = PipelineResolveHistoryIndex(inputState, frameCount, binding->historyOffset);
			PipelineDebugPrint("[Pipeline Debug] frame %d fragment pass \"%s\" input %d \"%s\" sampler unit %u historyOffset=%d resolvedIndex=%d textureId=%u\n",
				frameCount, pass->name, inputIndex, resource->id, samplerUnit, binding->historyOffset, resolvedIndex, textureId);
			if (inputIndex == 0) {
				debugFragmentInputTextureId = textureId;
				if (inputState && inputState->initialized) {
					debugFragmentInputWidth = inputState->width;
					debugFragmentInputHeight = inputState->height;
				}
				PipelineDebugPrint("[Pipeline Debug] frame %d fragment pass \"%s\" input \"%s\" resolvedIndex=%d size=%dx%d\n",
					frameCount, pass->name, resource->id, resolvedIndex,
					(inputState && inputState->initialized) ? inputState->width : 0,
					(inputState && inputState->initialized) ? inputState->height : 0);
			}
		}
#endif
	}
	glExtActiveTexture(GL_TEXTURE0);

	glExtUniform1i(UNIFORM_LOCATION_WAVE_OUT_POS, waveOutPos);
	glExtUniform1i(UNIFORM_LOCATION_FRAME_COUNT, frameCount);
	glExtUniform1f(UNIFORM_LOCATION_TIME, timeInSeconds);
	glExtUniform2f(UNIFORM_LOCATION_RESO, (float)targetWidth, (float)targetHeight);
	glExtUniform3i(UNIFORM_LOCATION_MOUSE_BUTTONS, 0, 0, 0);

	GLfloat vertices[] = {
		-1.0f, -1.0f,
		 1.0f, -1.0f,
		-1.0f,  1.0f,
		 1.0f,  1.0f
	};
#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		PipelineDebugPrint("[Pipeline Debug] frame %d fragment pass \"%s\" drawing quad target=%ux%u numInputs=%d numDrawBuffers=%d\n",
			frameCount, pass->name, targetWidth, targetHeight, pass->numInputs, numDrawBuffers);
	}
#endif
	glExtVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), vertices);
	glExtEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glExtDisableVertexAttribArray(0);

#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		if (debugFragmentInputTextureId != 0) {
			PipelineDebugSampleTexture(debugFragmentInputTextureId, debugFragmentInputWidth, debugFragmentInputHeight, "fragment_input_after");
		}
		if (debugFragmentOutputTextureId != 0) {
			PipelineDebugSampleTexture(debugFragmentOutputTextureId, debugFragmentOutputWidth, debugFragmentOutputHeight, "fragment_output_after");
		}
	}
#endif

	glExtBindFramebuffer(GL_FRAMEBUFFER, 0);
	glExtDeleteFramebuffers(1, &framebuffer);
	glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

	for (int i = 0; i < numBoundSamplerUnits; ++i) {
		glExtActiveTexture(GL_TEXTURE0 + samplerUnits[i]);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glExtActiveTexture(GL_TEXTURE0);

	if (enableFrameCountUniform) {
		glExtUniform1i(1, frameCount);
	}

	return true;
}

static bool PipelineExecutePresentPass(
	const PipelineDescription *pipeline,
	const PipelinePass *pass,
	int frameCount
){
	if (pipeline == NULL || pass == NULL) return false;
	if (pass->numInputs <= 0) return false;
#if ENABLE_PIPELINE_DEBUG_LOG
	bool debugLog = PipelineDebugShouldLog(frameCount);
#else
	bool debugLog = false;
#endif
	GLuint textureId = PipelineAcquireResourceTexture(pass->inputs[0].resourceIndex, frameCount, pass->inputs[0].historyOffset);
	PipelineRuntimeResourceState *state = &s_pipelineRuntimeResources[pass->inputs[0].resourceIndex];
	if (textureId == 0 || state->initialized == false) {
#if ENABLE_PIPELINE_DEBUG_LOG
		if (debugLog) {
			PipelineDebugPrint("[Pipeline Debug] frame %d present pass \"%s\" failed: input resourceIndex=%d textureId=%u initialized=%d\n",
				frameCount, pass->name, pass->inputs[0].resourceIndex, textureId, state ? state->initialized : 0);
		}
#endif
		return false;
	}
	GLuint readFramebuffer = 0;
	glExtGenFramebuffers(1, &readFramebuffer);
	glExtBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
	glExtFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textureId, 0);
	if (glExtCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		glExtBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glExtDeleteFramebuffers(1, &readFramebuffer);
#if ENABLE_PIPELINE_DEBUG_LOG
		if (debugLog) {
			PipelineDebugPrint("[Pipeline Debug] frame %d present pass \"%s\" aborted: source framebuffer incomplete textureId=%u\n",
				frameCount, pass->name, textureId);
		}
#endif
		return false;
	}
#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		PipelineDebugPrint("[Pipeline Debug] frame %d present pass \"%s\" blitting from textureId=%u size=%dx%d\n",
			frameCount, pass->name, textureId, state->width, state->height);
	}
#endif
	glExtBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glExtBlitNamedFramebuffer(
		readFramebuffer,
		0,
		0, 0, state->width, state->height,
		0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
		GL_COLOR_BUFFER_BIT,
		GL_NEAREST
	);
	glExtBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glExtDeleteFramebuffers(1, &readFramebuffer);
#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		PipelineDebugPrint("[Pipeline Debug] frame %d present pass \"%s\" completed blit to back buffer\n",
			frameCount, pass->name);
	}
#endif
	return true;
}

/* サウンドレンダリング先バッファの番号 */
#define BUFFER_INDEX_FOR_SOUND_OUTPUT			0


/* サウンドレンダリングの分割数 */
#define NUM_SOUND_BUFFER_SAMPLES_PER_DISPATCH	0x8000
#define NUM_SOUND_BUFFER_BYTES					NUM_SOUND_BUFFER_SAMPLES * NUM_SOUND_CHANNELS * sizeof(SOUND_SAMPLE_TYPE)


#pragma data_seg(".s_waveFormat")
static /* const */ WAVEFORMATEX s_waveFormat = {
	/* WORD  wFormatTag */		WAVE_FORMAT_IEEE_FLOAT,
	/* WORD  nChannels */		NUM_SOUND_CHANNELS,
	/* DWORD nSamplesPerSec */	NUM_SOUND_SAMPLES_PER_SEC,
	/* DWORD nAvgBytesPerSec */	NUM_SOUND_SAMPLES_PER_SEC * sizeof(SOUND_SAMPLE_TYPE) * NUM_SOUND_CHANNELS,
	/* WORD  nBlockAlign */		sizeof(SOUND_SAMPLE_TYPE) * NUM_SOUND_CHANNELS,
	/* WORD  wBitsPerSample */	sizeof(SOUND_SAMPLE_TYPE) * 8,
	/* WORD  cbSize */			0	/* extension not needed */
};

#pragma data_seg(".s_waveHeader")
static WAVEHDR s_waveHeader = {
	/* LPSTR lpData */					NULL,
	/* DWORD dwBufferLength */			NUM_SOUND_BUFFER_SAMPLES * sizeof(SOUND_SAMPLE_TYPE) * NUM_SOUND_CHANNELS,
	/* DWORD dwBytesRecorded */			0,
	/* DWORD dwUser */					0,
	/* DWORD dwFlags */					WHDR_PREPARED,
	/* DWORD dwLoops */					0,
	/* struct wavehdr_tag* lpNext */	NULL,
	/* DWORD reserved */				0
};

#pragma data_seg(".s_mmTime")
static MMTIME s_mmTime = {
	/* DWORD wType */	TIME_SAMPLES,
	/* DWORD sample */	0
};

#pragma data_seg(".s_pixelFormatDescriptor")
static /* const */ uint32_t s_pixelFormatDescriptor[2] = {
	0, PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER
};

#pragma data_seg(".s_screenSettings")
static DEVMODE s_screenSettings = {
	/* char dmDeviceName[32] */		{0},
	/* int16_t dmSpecVersion */		0,
	/* int16_t dmDriverVersion */	0,
	/* int16_t dmSize */			156,
	/* int16_t dmDriverExtra */		0,
	/* int32_t dmFields */			DM_PELSHEIGHT | DM_PELSWIDTH,
	/*
		union {
			struct {
				int16_t dmOrientation
				int16_t dmPaperSize
				int16_t dmPaperLength
				int16_t dmPaperWidth
				int16_t dmScale
				int16_t dmCopies
				int16_t dmDefaultSource
				int16_t dmPrintQuality
			};
			struct {
				int32_t dmPositionX
				int32_t dmPositionX
				int32_t dmDisplayOrientation
				int32_t dmDisplayFixedOutput
			};
		};
	*/								{0},			/* unused */
	/* int16_t dmColor */			0,				/* unused */
	/* int16_t dmDuplex */			0,				/* unused */
	/* int16_t dmYResolution */		0,				/* unused */
	/* int16_t dmTTOption */		0,				/* unused */
	/* int16_t dmCollate */			0,				/* unused */
	/* char dmFormName[32] */		{0},			/* unused */
	/* int16_t dmLogPixels */		0,				/* unused */
	/* int32_t dmBitsPerPel */		0,				/* unused */
	/* int32_t dmPelsWidth */		SCREEN_WIDTH,
	/* int32_t dmPelsHeight */		SCREEN_HEIGHT,
	/*
		union {
			int32_t dmDisplayFlags;
			int32_t dmNup;
		} DUMMYUNIONNAME2;
	*/								{0},			/* unused */
	/* int32_t dmDisplayFrequency */0,				/* unused */
	/* int32_t dmICMMethod */		0,				/* unused */
	/* int32_t dmICMIntent */		0,				/* unused */
	/* int32_t dmMediaType */		0,				/* unused */
	/* int32_t dmDitherType */		0,				/* unused */
	/* int32_t dmReserved1 */		0,				/* unused */
	/* int32_t dmReserved2 */		0,				/* unused */
	/* int32_t dmPanningWidth */	0,				/* unused */
	/* int32_t dmPanningHeight */	0				/* unused */
};


/*=============================================================================
▼	エントリポイント
-----------------------------------------------------------------------------*/
#pragma code_seg(".entrypoint")
void
entrypoint(
	void
){
#if !ARG_USE_WINDOW_MODE
	/* フルスクリーン化 */
	ChangeDisplaySettings(
		/* DEVMODEA *lpDevMode */	&s_screenSettings,
		/* DWORD    dwFlags */		CDS_FULLSCREEN
	);
#endif

	/*
		ウィンドウ作成
		CreateWindowEx ではなく CreateWindow を使うことで、c++ コード上では
		引数を一つ減らせるが、CreateWindow は CreateWindowEx のエイリアスに
		過ぎず、アセンブリレベルでは差がない。
		ここでは CreateWindowEx を使う。
	*/
	HWND hWnd = CreateWindowEx(
		/* DWORD dwExStyle */		0,
		/* LPCTSTR lpClassName */	(LPCSTR)0xC018	/* "edit" を意味する ATOM */,
		/* LPCTSTR lpWindowName */	NULL,
#if ARG_USE_WINDOW_MODE
		/* DWORD dwStyle */			WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		/* int x */					CW_USEDEFAULT,
		/* int y */					CW_USEDEFAULT,
		/* int nWidth */			SCREEN_WIDTH,
		/* int nHeight */			SCREEN_HEIGHT,
#else
		/* DWORD dwStyle */			WS_POPUP | WS_VISIBLE | WS_MAXIMIZE,
		/* int x */					0,
		/* int y */					0,
		/* int nWidth */			0,
		/* int nHeight */			0,
#endif
		/* HWND hWndParent */		(HWND)NULL,
		/* HMENU hMenu */			(HMENU)NULL,
		/* HINSTANCE hInstance */	(HINSTANCE)NULL,
		/* LPVOID lpParam */		NULL
	);

	/* ディスプレイデバイスコンテキストのハンドルを取得 */
	HDC hDC = GetDC(
		/* HWND hWnd */	hWnd
	);

	/* ピクセルフォーマットの選択 */
	int pixelFormat = ChoosePixelFormat(
		/* HDC                         hdc */	hDC,
		/* const PIXELFORMATDESCRIPTOR *ppfd */	(PIXELFORMATDESCRIPTOR*)s_pixelFormatDescriptor
	);

	/* ピクセルフォーマットの設定 */
	SetPixelFormat(
		/* HDC hdc */							hDC,
		/* int format */						pixelFormat,
		/* const PIXELFORMATDESCRIPTOR *ppfd */	(PIXELFORMATDESCRIPTOR*)s_pixelFormatDescriptor
	);

	/* OopenGL のコンテキストを作成 */
	HGLRC hRC = wglCreateContext(
		/* HDC Arg1 */	hDC
	);

	/* OopenGL のカレントコンテキストを設定する */
	wglMakeCurrent(
		/* HDC   Arg1 */	hDC,
		/* HGLRC Arg2 */	hRC
	);

	/* カーソルを消す */
	ShowCursor(0);

	const char *p = g_concatenatedString_align0;

	/*
		GL 拡張関数アドレスの取得
		（OpenGL の初期化が終わってからでないと実行できないので注意）
	*/
	{
		int i = 0;
		char byte;
		do {
			PROC procAddress = wglGetProcAddress(
				/* LPCSTR Arg1 */	p
			);
			s_glExtFunctions[i] = procAddress;
			i++;
			do {
				byte = *p;
				p++;
				--byte;
			} while (byte >= 0);
			++byte;
		} while (byte == 0);
	}

	/* フラグメントシェーダのポインタ配列 */
	const char *graphicsFragmentShaderCode = p;
	const char *graphicsFragmentShaderCodes[] = {graphicsFragmentShaderCode};

	/* コンピュートシェーダコードの開始位置を検索 */
	while (*p != '\0') { p++; }
	p++;

	/* グラフィクス用コンピュートシェーダのポインタ配列 */
	const char *graphicsComputeShaderCode = p;
	const char *graphicsComputeShaderCodes[] = {graphicsComputeShaderCode};

	/* サウンド用コンピュートシェーダコードの開始位置を検索 */
	while (*p != '\0') { p++; }
	p++;

	/* サウンド用コンピュートシェーダのポインタ配列 */
	const char *soundShaderCode = p;
	const char *soundShaderCodes[] = {soundShaderCode};

	/* サウンド出力バッファの作成 */
	/*
		glExtGenBuffers の実行は省略している。
	*/
	glExtBindBufferBase(
		/* GLenum target */			GL_SHADER_STORAGE_BUFFER,
		/* GLuint index */			BUFFER_INDEX_FOR_SOUND_OUTPUT,
		/* GLuint buffer */			ASSUMED_SOUND_SSBO
	);
	glExtBufferStorage(
		/* GLenum target */			GL_SHADER_STORAGE_BUFFER,
		/* GLsizeiptr size */		NUM_SOUND_BUFFER_BYTES,
		/* const void * data */		NULL,
		/* GLbitfield flags */			GL_MAP_READ_BIT			/* 0x0001 */
									|	GL_MAP_WRITE_BIT		/* 0x0002 */
									|	GL_MAP_PERSISTENT_BIT	/* 0x0040 */
	);

	/* サウンドバッファのポインタを waveHeader に設定 */
	s_waveHeader.lpData = (LPSTR)glExtMapBuffer(
		/* GLenum target */			GL_SHADER_STORAGE_BUFFER,
		/* GLenum access */			GL_READ_WRITE
	);

	/* コンピュートシェーダの作成 */
	int soundCsProgramId = glExtCreateShaderProgramv(
		/* (GLenum type */					GL_COMPUTE_SHADER,
		/* GLsizei count */					1,
		/* const GLchar* const *strings */	soundShaderCodes
	);

	/* コンピュートシェーダのバインド */
	glExtUseProgram(
		/* GLuint program */	soundCsProgramId
	);

	/* サウンド生成 */
	for (
		int i = NUM_SOUND_BUFFER_SAMPLES - NUM_SOUND_BUFFER_SAMPLES_PER_DISPATCH;
		i >= 0;
		i -= NUM_SOUND_BUFFER_SAMPLES_PER_DISPATCH
	) {
		glExtUniform1i(
			/* GLint location */	0,
			/* GLfloat v0 */		i
		);
		glExtDispatchCompute(
			/* GLuint num_groups_x */	NUM_SOUND_BUFFER_SAMPLES_PER_DISPATCH,
			/* GLuint num_groups_y */	1,
			/* GLuint num_groups_z */	1
		);

#if ENABLE_SOUND_DISPATCH_WAIT
		/*
			サウンド生成の dispatch は重い処理になりがちである。
			GPU タイムアウト対策のため dispatch 毎に glFinish() を
			実行している。
			この glFinish() は dispatch 完了待ちも兼ねる。
		*/
		glFinish();
#endif
	}

	/* グラフィクス用コンピュートシェーダの作成 */
	s_graphicsComputeProgramId = glExtCreateShaderProgramv(
		/* GLenum type */		GL_COMPUTE_SHADER,
		/* GLsizei count */		1,
		/* const GLchar* const *strings */	graphicsComputeShaderCodes
	);
	glExtGetProgramiv(
		/* GLuint program */		s_graphicsComputeProgramId,
		/* GLenum pname */		GL_COMPUTE_WORK_GROUP_SIZE,
		/* GLint *params */		s_graphicsComputeWorkGroupSize
	);
	for (int i = 0; i < 3; ++i) {
		if (s_graphicsComputeWorkGroupSize[i] <= 0) {
			s_graphicsComputeWorkGroupSize[i] = 1;
		}
	}

	
	/* グラフィクス用コンピュートシェーダの作成 */
	s_graphicsComputeProgramId = glExtCreateShaderProgramv(
		/* GLenum type */		GL_COMPUTE_SHADER,
		/* GLsizei count */		1,
		/* const GLchar* const *strings */	graphicsComputeShaderCodes
	);
	glExtGetProgramiv(
		/* GLuint program */		s_graphicsComputeProgramId,
		/* GLenum pname */		GL_COMPUTE_WORK_GROUP_SIZE,
		/* GLint *params */		s_graphicsComputeWorkGroupSize
	);
	for (int i = 0; i < 3; ++i) {
		if (s_graphicsComputeWorkGroupSize[i] <= 0) {
			s_graphicsComputeWorkGroupSize[i] = 1;
		}
	}
	if (s_graphicsComputeProgramId != 0) {
		s_computePipelinePassUniformAvailable = PipelineProgramHasUniform(
			s_graphicsComputeProgramId,
			UNIFORM_LOCATION_PIPELINE_PASS_INDEX,
			GL_INT
		);
	} else {
		s_computePipelinePassUniformAvailable = false;
	}

	/* フラグメントシェーダの作成 */
	int graphicsFsProgramId = glExtCreateShaderProgramv(
		/* GLenum type */				GL_FRAGMENT_SHADER,
		/* GLsizei count */			1,
		/* const GLchar* const *strings */	graphicsFragmentShaderCodes
	);

	/* フラグメントシェーダのバインド */
	glExtUseProgram(
		/* GLuint program */	graphicsFsProgramId
	);
	if (graphicsFsProgramId != 0) {
		s_fragmentPipelinePassUniformAvailable = PipelineProgramHasUniform(
			graphicsFsProgramId,
			UNIFORM_LOCATION_PIPELINE_PASS_INDEX,
			GL_INT
		);
	} else {
		s_fragmentPipelinePassUniformAvailable = false;
	}

	PipelineMemcpy(&s_pipelineDescription, &g_exportedPipelineDescription, sizeof(PipelineDescription));
	PipelineResetRuntimeResources();
	PipelineEnsureResources(&s_pipelineDescription);

/* サウンド再生 */
	HWAVEOUT hWaveOut;
	waveOutOpen(
		/* LPHWAVEOUT      phwo */			&hWaveOut,
		/* UINT            uDeviceID */		WAVE_MAPPER,
		/* LPCWAVEFORMATEX pwfx */			&s_waveFormat,
		/* DWORD_PTR       dwCallback */	NULL,
		/* DWORD_PTR       dwInstance */	0,
		/* DWORD           fdwOpen */		CALLBACK_NULL
	);
	waveOutWrite(
		/* HWAVEOUT  hwo */		hWaveOut,
		/* LPWAVEHDR pwh */		&s_waveHeader,
		/* UINT      cbwh */	sizeof(s_waveHeader)
	);

	
	/* メインループ */
	int frameCount = 0;
	bool enableFrameCountUniform = (ENABLE_FRAME_COUNT_UNIFORM != 0);
	do {
		waveOutGetPosition(
			/* HWAVEOUT hwo */	hWaveOut,
			/* LPMMTIME pmmt */	&s_mmTime,
			/* UINT cbmmt */	sizeof(MMTIME)
		);

		int32_t waveOutPos = s_mmTime.u.sample;
		float timeInSeconds = (float)waveOutPos / (float)NUM_SOUND_SAMPLES_PER_SEC;

		PipelineEnsureResources(&s_pipelineDescription);

		for (int passIndex = 0; passIndex < s_pipelineDescription.numPasses; ++passIndex) {
			const PipelinePass *pass = &s_pipelineDescription.passes[passIndex];
			bool executed = false;
			s_activePipelinePassIndex = passIndex;
			switch (pass->type) {
				case PipelinePassTypeCompute: {
					executed = PipelineExecuteComputePass(&s_pipelineDescription, pass, frameCount, waveOutPos, timeInSeconds);
				} break;
				case PipelinePassTypeFragment: {
					executed = PipelineExecuteFragmentPass(&s_pipelineDescription, pass, frameCount, waveOutPos, timeInSeconds, graphicsFsProgramId, enableFrameCountUniform);
				} break;
				case PipelinePassTypePresent: {
					executed = PipelineExecutePresentPass(&s_pipelineDescription, pass, frameCount);
				} break;
				default: {
					executed = false;
				} break;
			}
				if (!executed && s_loggedPipelineExecutionFailure == false) {
					char debugMessage[256];
					wsprintfA(
						debugMessage,
						"[Pipeline Warning] Pass \"%s\" (type %d) failed to execute.\n",
						pass->name,
						(int)pass->type
					);
					OutputDebugStringA(debugMessage);
					s_loggedPipelineExecutionFailure = true;
				}
				s_activePipelinePassIndex = -1;
			}
		s_activePipelinePassIndex = -1;

		SwapBuffers(
			/* HDC  Arg1 */	hDC
		);

		PeekMessage(
			/* LPMSG lpMsg */		NULL,
			/* HWND  hWnd */		0,
			/* UINT  wMsgFilterMin */	0,
			/* UINT  wMsgFilterMax */	0,
			/* UINT  wRemoveMsg */		PM_REMOVE
		);

		uint16_t escapeKeyState = GetAsyncKeyState(VK_ESCAPE);
		if (escapeKeyState != 0) break;

		++frameCount;
	} while (s_mmTime.u.sample < NUM_SOUND_BUFFER_AVAILABLE_SAMPLES);

	if (s_graphicsComputeProgramId != 0) {
		glExtDeleteProgram(s_graphicsComputeProgramId);
		s_graphicsComputeProgramId = 0;
	}
	PipelineResetRuntimeResources();

	/* デモを終了する */
	ExitProcess(
		/* UINT uExitCode */	0
	);
}

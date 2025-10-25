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
#include <ctype.h>

#include "config.h"

#ifndef ENABLE_PIPELINE_DEBUG_LOG
#define ENABLE_PIPELINE_DEBUG_LOG 1
#endif

#ifndef ARG_SCREEN_WIDTH
#define ARG_SCREEN_WIDTH 1280
#endif
#ifndef ARG_SCREEN_HEIGHT
#define ARG_SCREEN_HEIGHT 720
#endif

#if ENABLE_PIPELINE_DEBUG_LOG
static void PipelineDebugPrint(const char *format, ...);
#endif
static size_t PipelineStrLen(const char *str);
static int PipelineStrncmp(const char *lhs, const char *rhs, size_t count);
static void *PipelineMemcpy(void *dst, const void *src, size_t size);
static const char *PipelineFindSubstring(const char *haystack, const char *needle);
static const char *PipelineFindChar(const char *str, char ch);

#ifndef GL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS
#define GL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS GL_MAX_TEXTURE_IMAGE_UNITS
#endif
#ifndef GL_IMAGE_BINDING
#define GL_IMAGE_BINDING 0x8F3F
#endif


/* fopen 等のセキュアでないレガシー API に対する警告の抑制 */
#pragma warning(disable:4996)


/* gl3w ライブラリを単一 TU で再現するための最小実装 */
#define GL3W_OK 0
#define GL3W_ERROR_INIT -1
#define GL3W_ERROR_LIBRARY_OPEN -2

typedef void (*GL3WglProc)(void);

static HMODULE s_gl3wLibrary = NULL;
static PROC (__stdcall *s_gl3wWglGetProcAddress)(LPCSTR) = NULL;

static int gl3wInit(void){
	if (s_gl3wLibrary != NULL) {
		return GL3W_OK;
	}
	s_gl3wLibrary = LoadLibraryA("opengl32.dll");
	if (s_gl3wLibrary == NULL) {
		return GL3W_ERROR_LIBRARY_OPEN;
	}
	s_gl3wWglGetProcAddress = (PROC (__stdcall *)(LPCSTR))GetProcAddress(s_gl3wLibrary, "wglGetProcAddress");
	if (s_gl3wWglGetProcAddress == NULL) {
		return GL3W_ERROR_INIT;
	}
	return GL3W_OK;
}

static GL3WglProc gl3wGetProcAddress(const char *proc){
	if (proc == NULL || *proc == '\0') {
		return NULL;
	}
	GL3WglProc result = NULL;
	if (s_gl3wWglGetProcAddress != NULL) {
		result = (GL3WglProc)s_gl3wWglGetProcAddress(proc);
	}
	if (result == NULL && s_gl3wLibrary != NULL) {
		result = (GL3WglProc)GetProcAddress(s_gl3wLibrary, proc);
	}
	return result;
}

/* gl3w ラッパー */
static bool CallGl3wInit(void){
	return gl3wInit() == GL3W_OK;
}

static void *CallGl3wGetProcAddress(const char *procName){
	if (procName == NULL) {
		return NULL;
	}
	return (void*)gl3wGetProcAddress(procName);
}

static PFNGLBINDIMAGETEXTUREPROC CallGl3wGetBindImageTexture(void){
	static PFNGLBINDIMAGETEXTUREPROC cached = NULL;
	if (cached == NULL) {
		cached = (PFNGLBINDIMAGETEXTUREPROC)CallGl3wGetProcAddress("glBindImageTexture");
	}
	return cached;
}

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
static bool s_computeFrameCountUniformAvailable = false;
static bool s_computeWaveOutUniformAvailable = false;
static bool s_loggedPipelineExecutionFailure = false;
static GLint s_graphicsComputeWorkGroupSize[3] = {1, 1, 1};
static GLuint s_graphicsComputeProgramId = 0;
static GLint s_graphicsComputeUniformLocationUResult = -1;
static GLint s_graphicsComputeUniformLocationUPrevResult = -1;
static const char *s_graphicsComputeShaderSource = NULL;
#if ENABLE_PIPELINE_DEBUG_LOG
static int s_debugLastObservedUResultUnit = -9999;
static int s_debugLastObservedUPrevResultUnit = -9999;
static void PipelineDebugResetObservedUniformUnits(void);
static void PipelineDebugObserveComputeUniformUnits(const char *context, int frameCount, const char *passName);
static void PipelineDebugTrackComputeUniformWrite(int frameCount, const char *passName, const char *context, GLuint programId, GLint location, GLint value);
#else
static void PipelineDebugResetObservedUniformUnits(void);
static void PipelineDebugObserveComputeUniformUnits(const char *context, int frameCount, const char *passName);
static void PipelineDebugTrackComputeUniformWrite(int frameCount, const char *passName, const char *context, GLuint programId, GLint location, GLint value);
#endif
static bool PipelineIsSamplerType(GLenum type);
static bool PipelineIsImageType(GLenum type);

/*=============================================================================
▼	各種リソースの取り込み
-----------------------------------------------------------------------------*/
#include "resource.cpp"


/*=============================================================================
▼	OpenGL 関数テーブル
-----------------------------------------------------------------------------*/
static void *s_glExtFunctions[NUM_GLEXT_FUNCTIONS] = {0};
static PFNGLGETINTEGERI_VPROC s_glGetIntegeri_v = NULL;
static PFNGLGETUNIFORMIVPROC s_glGetUniformiv = NULL;
static GLint s_computeSamplerBindingUnits[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
static int s_computeSamplerBindingCount = 0;
static GLint s_computeImageBindingUnits[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
static int s_computeImageBindingCount = 0;
static bool s_computeBindingUnitsCached = false;
typedef struct {
	char name[PIPELINE_MAX_RESOURCE_ID_LENGTH];
	int binding;
	bool isImage;
	bool isSampler;
} PipelineDeclaredBinding;
static PipelineDeclaredBinding s_computeDeclaredBindings[PIPELINE_MAX_BINDINGS_PER_PASS] = {{0}};
static int s_computeDeclaredBindingCount = 0;
static bool s_computeDeclaredBindingParsed = false;

static void PipelineComputeParseDeclaredBindings(void){
	if (s_computeDeclaredBindingParsed) {
		return;
	}
	s_computeDeclaredBindingParsed = true;
	s_computeDeclaredBindingCount = 0;
	const char *src = s_graphicsComputeShaderSource;
	if (src == NULL) {
		return;
	}
	while (*src != '\0') {
		const char *layoutPos = PipelineFindSubstring(src, "layout(");
		if (layoutPos == NULL) {
			break;
		}
		src = layoutPos + 6; /* skip "layout" */
		const char *parenOpen = PipelineFindChar(layoutPos, '(');
		if (parenOpen == NULL) {
			break;
		}
		const char *parenClose = PipelineFindChar(parenOpen, ')');
		if (parenClose == NULL) {
			break;
		}
		const char *bindingPos = PipelineFindSubstring(parenOpen, "binding");
		if (bindingPos == NULL || bindingPos > parenClose) {
			src = parenClose + 1;
			continue;
		}
		const char *equalsPos = PipelineFindChar(bindingPos, '=');
		if (equalsPos == NULL || equalsPos > parenClose) {
			src = parenClose + 1;
			continue;
		}
		const char *numberPos = equalsPos + 1;
		while (*numberPos == ' ' || *numberPos == '\t') {
			++numberPos;
		}
		int bindingValue = 0;
		bool hasDigits = false;
		while (*numberPos >= '0' && *numberPos <= '9') {
			hasDigits = true;
			bindingValue = bindingValue * 10 + (*numberPos - '0');
			++numberPos;
		}
		if (!hasDigits) {
			src = parenClose + 1;
			continue;
		}
		const char *afterLayout = parenClose + 1;
		while (*afterLayout == ' ' || *afterLayout == '\t' || *afterLayout == '\n' || *afterLayout == '\r') {
			++afterLayout;
		}
		if (PipelineStrncmp(afterLayout, "uniform", 7) != 0) {
			src = afterLayout;
			continue;
		}
		const char *typePos = afterLayout + 7;
		while (*typePos == ' ' || *typePos == '\t') {
			++typePos;
		}
		char typeToken[32] = {0};
		size_t typeLen = 0;
		while (*typePos != '\0' && typeLen < sizeof(typeToken) - 1) {
			char c = *typePos;
			if (!( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' )) {
				break;
			}
			typeToken[typeLen++] = c;
			++typePos;
		}
		typeToken[typeLen] = '\0';
		while (*typePos == ' ' || *typePos == '\t') {
			++typePos;
		}
		char nameToken[PIPELINE_MAX_RESOURCE_ID_LENGTH] = {0};
		size_t nameLen = 0;
		while (*typePos != '\0' && nameLen < sizeof(nameToken) - 1) {
			char c = *typePos;
			if (!( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' )) {
				break;
			}
			nameToken[nameLen++] = c;
			++typePos;
		}
		nameToken[nameLen] = '\0';
		bool isImage = false;
		bool isSampler = false;
		if (typeLen > 0) {
			if (PipelineFindSubstring(typeToken, "image") != NULL) {
				isImage = true;
			} else if (PipelineFindSubstring(typeToken, "sampler") != NULL) {
				isSampler = true;
			}
		}
		if ((isImage || isSampler) && nameLen > 0) {
			if (s_computeDeclaredBindingCount < PIPELINE_MAX_BINDINGS_PER_PASS) {
				PipelineDeclaredBinding *entry = &s_computeDeclaredBindings[s_computeDeclaredBindingCount++];
				size_t copyLen = (nameLen >= sizeof(entry->name)) ? (sizeof(entry->name) - 1) : nameLen;
				PipelineMemcpy(entry->name, nameToken, copyLen);
				entry->name[copyLen] = '\0';
				entry->binding = bindingValue;
				entry->isImage = isImage;
				entry->isSampler = isSampler;
#if ENABLE_PIPELINE_DEBUG_LOG
				PipelineDebugPrint("[Pipeline Debug] declared binding name=\"%s\" binding=%d isImage=%d isSampler=%d\n",
					entry->name,
					entry->binding,
					entry->isImage ? 1 : 0,
					entry->isSampler ? 1 : 0
				);
#endif
			}
		}
		src = typePos;
	}
}

static int PipelineComputeLookupDeclaredBinding(const char *name, bool isImage){
	if (name == NULL || *name == '\0') {
		return -1;
	}
	for (int i = 0; i < s_computeDeclaredBindingCount; ++i) {
		const PipelineDeclaredBinding *entry = &s_computeDeclaredBindings[i];
		if ((isImage && entry->isImage) || (!isImage && entry->isSampler)) {
			if (PipelineStrncmp(entry->name, name, PIPELINE_MAX_RESOURCE_ID_LENGTH - 1) == 0) {
				return entry->binding;
			}
		}
	}
	return -1;
}

static void PipelineComputeInvalidateBindingCache(void){
	s_computeBindingUnitsCached = false;
	s_computeSamplerBindingCount = 0;
	s_computeImageBindingCount = 0;
#if ENABLE_PIPELINE_DEBUG_LOG
	PipelineDebugResetObservedUniformUnits();
#endif
}

static PFNGLGETINTEGERI_VPROC PipelineResolveGetIntegeriV(void){
	if (s_glGetIntegeri_v == NULL) {
		s_glGetIntegeri_v = (PFNGLGETINTEGERI_VPROC)CallGl3wGetProcAddress("glGetIntegeri_v");
		if (s_glGetIntegeri_v == NULL && s_gl3wWglGetProcAddress) {
			s_glGetIntegeri_v = (PFNGLGETINTEGERI_VPROC)s_gl3wWglGetProcAddress("glGetIntegeri_v");
		}
		if (s_glGetIntegeri_v == NULL && s_gl3wLibrary != NULL) {
			s_glGetIntegeri_v = (PFNGLGETINTEGERI_VPROC)GetProcAddress(s_gl3wLibrary, "glGetIntegeri_v");
		}
	}
	return s_glGetIntegeri_v;
}

static PFNGLGETUNIFORMIVPROC PipelineResolveGetUniformiv(void){
	if (s_glGetUniformiv == NULL) {
		s_glGetUniformiv = (PFNGLGETUNIFORMIVPROC)CallGl3wGetProcAddress("glGetUniformiv");
		if (s_glGetUniformiv == NULL && s_gl3wWglGetProcAddress) {
			s_glGetUniformiv = (PFNGLGETUNIFORMIVPROC)s_gl3wWglGetProcAddress("glGetUniformiv");
		}
		if (s_glGetUniformiv == NULL && s_gl3wLibrary != NULL) {
			s_glGetUniformiv = (PFNGLGETUNIFORMIVPROC)GetProcAddress(s_gl3wLibrary, "glGetUniformiv");
		}
	}
	return s_glGetUniformiv;
}

static void PipelineComputeEnsureBindingCache(void){
	if (s_computeBindingUnitsCached) {
		return;
	}
	PipelineComputeParseDeclaredBindings();
	s_computeSamplerBindingCount = 0;
	s_computeImageBindingCount = 0;
	if (s_graphicsComputeProgramId == 0) {
		return;
	}
	if (glExtGetProgramInterfaceiv == NULL || glExtGetProgramResourceiv == NULL) {
		return;
	}
	PFNGLGETUNIFORMIVPROC getUniformiv = PipelineResolveGetUniformiv();
	if (getUniformiv == NULL) {
		return;
	}
	GLint numUniforms = 0;
	glExtGetProgramInterfaceiv(
		/* GLuint program */			s_graphicsComputeProgramId,
		/* GLenum programInterface */	GL_UNIFORM,
		/* GLenum pname */				GL_ACTIVE_RESOURCES,
		/* GLint *params */				&numUniforms
	);
	GLenum properties[4] = {
		GL_TYPE,
		GL_LOCATION,
		GL_ARRAY_SIZE,
		GL_REFERENCED_BY_COMPUTE_SHADER
	};
	for (int uniformIndex = 0; uniformIndex < numUniforms; ++uniformIndex) {
		GLint values[4] = {0, 0, 0, 0};
		glExtGetProgramResourceiv(
			/* GLuint program */			s_graphicsComputeProgramId,
			/* GLenum programInterface */	GL_UNIFORM,
			/* GLuint index */				(GLuint)uniformIndex,
			/* GLsizei propCount */			4,
			/* const GLenum *props */		properties,
			/* GLsizei bufSize */			4,
			/* GLsizei *length */			NULL,
			/* GLint *params */				values
		);
		GLenum type = (GLenum)values[0];
		GLint location = values[1];
		GLint arraySize = values[2];
		bool referencedByCompute = (values[3] != 0);
		char uniformName[PIPELINE_MAX_RESOURCE_ID_LENGTH] = {0};
		if (glExtGetProgramResourceName != NULL) {
			GLsizei uniformNameLength = 0;
			GLsizei maxLength = (GLsizei)(sizeof(uniformName) - 1);
			glExtGetProgramResourceName(
				s_graphicsComputeProgramId,
				GL_UNIFORM,
				(GLuint)uniformIndex,
				maxLength,
				&uniformNameLength,
				uniformName
			);
			uniformName[(uniformNameLength < maxLength) ? uniformNameLength : maxLength] = '\0';
		}
#if ENABLE_PIPELINE_DEBUG_LOG
		PipelineDebugPrint("[Pipeline Debug] binding cache uniform index=%d name=\"%s\" type=0x%04X location=%d arraySize=%d referencedByCS=%d\n",
			uniformIndex,
			(*uniformName != '\0') ? uniformName : "(unknown)",
			type,
			location,
			arraySize,
			referencedByCompute ? 1 : 0
		);
#endif
		if (!referencedByCompute || location < 0) {
#if ENABLE_PIPELINE_DEBUG_LOG
			if (!referencedByCompute) {
				PipelineDebugPrint("[Pipeline Debug] binding cache uniform index=%d skipped (not referenced by compute)\n", uniformIndex);
			} else {
				PipelineDebugPrint("[Pipeline Debug] binding cache uniform index=%d skipped (location < 0)\n", uniformIndex);
			}
#endif
			continue;
		}
		if (arraySize <= 0) {
			arraySize = 1;
		}
		for (int element = 0; element < arraySize; ++element) {
			GLint elementLocation = location + element;
			GLint uniformUnitValue = -1;
			getUniformiv(s_graphicsComputeProgramId, elementLocation, &uniformUnitValue);
			GLint chosenUnitValue = uniformUnitValue;
			const char *uniformNamePtr = (*uniformName != '\0') ? uniformName : NULL;
#if ENABLE_PIPELINE_DEBUG_LOG
			PipelineDebugPrint("[Pipeline Debug] binding cache uniform index=%d element=%d type=0x%04X elementLocation=%d uniformUnitValue=%d\n",
				uniformIndex,
				element,
				type,
				elementLocation,
				uniformUnitValue
			);
#endif
			if (PipelineIsSamplerType(type)) {
				int declaredBinding = PipelineComputeLookupDeclaredBinding(uniformNamePtr, false);
				if (declaredBinding >= 0) {
					chosenUnitValue = declaredBinding;
				}
#if ENABLE_PIPELINE_DEBUG_LOG
				PipelineDebugPrint("[Pipeline Debug] binding cache sampler \"%s\" declaredBinding=%d chosenUnit=%d\n",
					(uniformNamePtr != NULL) ? uniformNamePtr : "(unknown)",
					declaredBinding,
					chosenUnitValue
				);
#endif
				if (s_computeSamplerBindingCount < PIPELINE_MAX_BINDINGS_PER_PASS) {
					s_computeSamplerBindingUnits[s_computeSamplerBindingCount++] = chosenUnitValue;
				}
			} else if (PipelineIsImageType(type)) {
				int declaredBinding = PipelineComputeLookupDeclaredBinding(uniformNamePtr, true);
				if (declaredBinding >= 0) {
					chosenUnitValue = declaredBinding;
				}
#if ENABLE_PIPELINE_DEBUG_LOG
				PipelineDebugPrint("[Pipeline Debug] binding cache image \"%s\" declaredBinding=%d chosenUnit=%d\n",
					(uniformNamePtr != NULL) ? uniformNamePtr : "(unknown)",
					declaredBinding,
					chosenUnitValue
				);
#endif
				if (s_computeImageBindingCount < PIPELINE_MAX_BINDINGS_PER_PASS) {
					s_computeImageBindingUnits[s_computeImageBindingCount++] = chosenUnitValue;
				}
			}
		}
	}
	s_computeBindingUnitsCached = true;
}

static PFNGLBINDIMAGETEXTUREPROC PipelineResolveBindImageTextureProc(void){
	static PFNGLBINDIMAGETEXTUREPROC proc = NULL;
	if (proc != NULL) {
		return proc;
	}
	proc = CallGl3wGetBindImageTexture();
	if (proc == NULL) {
		proc = (PFNGLBINDIMAGETEXTUREPROC)CallGl3wGetProcAddress("glBindImageTexture");
	}
	if (proc == NULL) {
		proc = (PFNGLBINDIMAGETEXTUREPROC)wglGetProcAddress("glBindImageTexture");
	}
	if (proc == NULL) {
		HMODULE module = GetModuleHandleA("opengl32.dll");
		if (module == NULL) {
			module = LoadLibraryA("opengl32.dll");
		}
		if (module != NULL) {
			proc = (PFNGLBINDIMAGETEXTUREPROC)GetProcAddress(module, "glBindImageTexture");
		}
	}
	if (proc != NULL) {
		s_glExtFunctions[GlExtBindImageTexture] = (void*)proc;
	}
	return proc;
}

static void PipelineBindImageTexture(
	GLuint unit,
	GLuint texture,
	GLint level,
	GLboolean layered,
	GLint layer,
	GLenum access,
	GLenum format
){
	PFNGLBINDIMAGETEXTUREPROC proc = PipelineResolveBindImageTextureProc();
	if (proc != NULL) {
		proc(unit, texture, level, layered, layer, access, format);
	}
}

static bool PipelineIsSamplerType(GLenum type){
	switch (type) {
		case GL_SAMPLER_1D:
		case GL_SAMPLER_2D:
		case GL_SAMPLER_3D:
		case GL_SAMPLER_CUBE:
		case GL_SAMPLER_1D_SHADOW:
		case GL_SAMPLER_2D_SHADOW:
		case GL_SAMPLER_1D_ARRAY:
		case GL_SAMPLER_2D_ARRAY:
		case GL_SAMPLER_1D_ARRAY_SHADOW:
		case GL_SAMPLER_2D_ARRAY_SHADOW:
		case GL_SAMPLER_2D_MULTISAMPLE:
		case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
		case GL_SAMPLER_CUBE_SHADOW:
		case GL_INT_SAMPLER_1D:
		case GL_INT_SAMPLER_2D:
		case GL_INT_SAMPLER_3D:
		case GL_INT_SAMPLER_CUBE:
		case GL_INT_SAMPLER_1D_ARRAY:
		case GL_INT_SAMPLER_2D_ARRAY:
		case GL_UNSIGNED_INT_SAMPLER_1D:
		case GL_UNSIGNED_INT_SAMPLER_2D:
		case GL_UNSIGNED_INT_SAMPLER_3D:
		case GL_UNSIGNED_INT_SAMPLER_CUBE:
		case GL_UNSIGNED_INT_SAMPLER_1D_ARRAY:
		case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
		case GL_SAMPLER_2D_RECT:
		case GL_SAMPLER_2D_RECT_SHADOW:
		case GL_INT_SAMPLER_2D_RECT:
		case GL_UNSIGNED_INT_SAMPLER_2D_RECT:
		case GL_SAMPLER_BUFFER:
		case GL_INT_SAMPLER_BUFFER:
		case GL_UNSIGNED_INT_SAMPLER_BUFFER:
		case GL_SAMPLER_CUBE_MAP_ARRAY:
		case GL_SAMPLER_CUBE_MAP_ARRAY_SHADOW:
		case GL_INT_SAMPLER_CUBE_MAP_ARRAY:
		case GL_UNSIGNED_INT_SAMPLER_CUBE_MAP_ARRAY:
			return true;
		default:
			return false;
	}
}

static bool PipelineIsImageType(GLenum type){
	switch (type) {
		case GL_IMAGE_1D:
		case GL_IMAGE_2D:
		case GL_IMAGE_3D:
		case GL_IMAGE_2D_RECT:
		case GL_IMAGE_CUBE:
		case GL_IMAGE_BUFFER:
		case GL_IMAGE_1D_ARRAY:
		case GL_IMAGE_2D_ARRAY:
		case GL_IMAGE_CUBE_MAP_ARRAY:
		case GL_IMAGE_2D_MULTISAMPLE:
		case GL_IMAGE_2D_MULTISAMPLE_ARRAY:
		case GL_INT_IMAGE_1D:
		case GL_INT_IMAGE_2D:
		case GL_INT_IMAGE_3D:
		case GL_INT_IMAGE_2D_RECT:
		case GL_INT_IMAGE_CUBE:
		case GL_INT_IMAGE_BUFFER:
		case GL_INT_IMAGE_1D_ARRAY:
		case GL_INT_IMAGE_2D_ARRAY:
		case GL_INT_IMAGE_CUBE_MAP_ARRAY:
		case GL_INT_IMAGE_2D_MULTISAMPLE:
		case GL_INT_IMAGE_2D_MULTISAMPLE_ARRAY:
		case GL_UNSIGNED_INT_IMAGE_1D:
		case GL_UNSIGNED_INT_IMAGE_2D:
		case GL_UNSIGNED_INT_IMAGE_3D:
		case GL_UNSIGNED_INT_IMAGE_2D_RECT:
		case GL_UNSIGNED_INT_IMAGE_CUBE:
		case GL_UNSIGNED_INT_IMAGE_BUFFER:
		case GL_UNSIGNED_INT_IMAGE_1D_ARRAY:
		case GL_UNSIGNED_INT_IMAGE_2D_ARRAY:
		case GL_UNSIGNED_INT_IMAGE_CUBE_MAP_ARRAY:
		case GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE:
		case GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE_ARRAY:
			return true;
		default:
			return false;
	}
}


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

static const char *PipelineFindChar(const char *str, char ch){
	if (str == NULL) return NULL;
	while (*str != '\0') {
		if (*str == ch) {
			return str;
		}
		++str;
	}
	return NULL;
}

static const char *PipelineFindSubstring(const char *haystack, const char *needle){
	if (haystack == NULL || needle == NULL) return NULL;
	if (*needle == '\0') {
		return haystack;
	}
	size_t needleLen = PipelineStrLen(needle);
	if (needleLen == 0) {
		return haystack;
	}
	const char first = needle[0];
	const char *pos = haystack;
	while (*pos != '\0') {
		if (*pos == first) {
			size_t matchCount = 1;
			while (matchCount < needleLen && pos[matchCount] != '\0' && pos[matchCount] == needle[matchCount]) {
				++matchCount;
			}
			if (matchCount == needleLen) {
				return pos;
			}
		}
		++pos;
	}
	return NULL;
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

static int PipelineDebugDrainGlErrors(const char *label){
	int drainedCount = 0;
	GLenum error = GL_NO_ERROR;
	while ((error = glGetError()) != GL_NO_ERROR) {
		++drainedCount;
		if (label != NULL) {
			PipelineDebugPrint("[Pipeline Debug] %s pre-existing GL error=%s(0x%04X)\n",
				label,
				PipelineDebugGetGlErrorString(error),
				error
			);
		}
	}
	return drainedCount;
}
#if ENABLE_PIPELINE_DEBUG_LOG
static void PipelineDebugResetObservedUniformUnits(void){
	s_debugLastObservedUResultUnit = -9999;
	s_debugLastObservedUPrevResultUnit = -9999;
}

static bool PipelineDebugFrameShouldLog(int frameCount){
	if (frameCount < 0) {
		return true;
	}
	return PipelineDebugShouldLog(frameCount);
}

static void PipelineDebugObserveComputeUniformUnits(const char *context, int frameCount, const char *passName){
	if (!PipelineDebugFrameShouldLog(frameCount)) {
		return;
	}
	if (s_graphicsComputeProgramId == 0) {
		return;
	}
	PFNGLGETUNIFORMIVPROC getUniformiv = PipelineResolveGetUniformiv();
	if (getUniformiv == NULL) {
		return;
	}
	GLint uResultValue = -1;
	GLint uPrevResultValue = -1;
	bool hasResult = false;
	bool hasPrevResult = false;
	if (s_graphicsComputeUniformLocationUResult >= 0) {
		getUniformiv(s_graphicsComputeProgramId, s_graphicsComputeUniformLocationUResult, &uResultValue);
		hasResult = true;
	}
	if (s_graphicsComputeUniformLocationUPrevResult >= 0) {
		getUniformiv(s_graphicsComputeProgramId, s_graphicsComputeUniformLocationUPrevResult, &uPrevResultValue);
		hasPrevResult = true;
	}
	if (!hasResult && !hasPrevResult) {
		return;
	}
	bool resultChanged = hasResult && (uResultValue != s_debugLastObservedUResultUnit);
	bool prevChanged = hasPrevResult && (uPrevResultValue != s_debugLastObservedUPrevResultUnit);
	const char *contextLabel = (context != NULL) ? context : "(unknown)";
	const char *passLabel = (passName != NULL) ? passName : "(none)";
	const char *resultNote = resultChanged ? " updated" : "";
	const char *prevNote = prevChanged ? " updated" : "";
	PipelineDebugPrint("[Pipeline Debug] frame %d compute uniform slots context=\"%s\" pass=\"%s\" u_result=%d%s u_prevResult=%d%s\n",
		frameCount,
		contextLabel,
		passLabel,
		hasResult ? uResultValue : -1,
		resultNote,
		hasPrevResult ? uPrevResultValue : -1,
		prevNote
	);
	if (hasResult) {
		s_debugLastObservedUResultUnit = uResultValue;
	}
	if (hasPrevResult) {
		s_debugLastObservedUPrevResultUnit = uPrevResultValue;
	}
}

static void PipelineDebugTrackComputeUniformWrite(
	int frameCount,
	const char *passName,
	const char *context,
	GLuint programId,
	GLint location,
	GLint value
){
	if (programId == 0 || programId != s_graphicsComputeProgramId) {
		return;
	}
	bool matchesResult = (s_graphicsComputeUniformLocationUResult >= 0 && location == s_graphicsComputeUniformLocationUResult);
	bool matchesPrev = (s_graphicsComputeUniformLocationUPrevResult >= 0 && location == s_graphicsComputeUniformLocationUPrevResult);
	if (!matchesResult && !matchesPrev) {
		return;
	}
	if (!PipelineDebugFrameShouldLog(frameCount)) {
		return;
	}
	const char *target = matchesResult ? "u_result" : "u_prevResult";
	const char *contextLabel = (context != NULL) ? context : "(unknown)";
	const char *passLabel = (passName != NULL) ? passName : "(none)";
	PipelineDebugPrint("[Pipeline Debug] frame %d compute uniform write context=\"%s\" pass=\"%s\" target=%s location=%d value=%d program=%u\n",
		frameCount,
		contextLabel,
		passLabel,
		target,
		location,
		value,
		(unsigned int)programId
	);
	if (matchesResult) {
		s_debugLastObservedUResultUnit = value;
	}
	if (matchesPrev) {
		s_debugLastObservedUPrevResultUnit = value;
	}
}
#else
static void PipelineDebugResetObservedUniformUnits(void){}
static void PipelineDebugObserveComputeUniformUnits(const char *, int, const char *){}
static void PipelineDebugTrackComputeUniformWrite(int, const char *, const char *, GLuint, GLint, GLint){}
#endif

#if ENABLE_PIPELINE_DEBUG_LOG
static void *PipelineDebugAllocZero(size_t size){
	if (size == 0) {
		return NULL;
	}
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static void PipelineDebugFree(void *ptr){
	if (ptr) {
		HeapFree(GetProcessHeap(), 0, ptr);
	}
}

typedef struct {
	GLboolean *samplerUsed;
	int samplerCapacity;
	int samplerCount;
	GLboolean *imageUsed;
	int imageCapacity;
	int imageCount;
	bool samplerOverflowLogged;
	bool imageOverflowLogged;
} PipelineDebugStageUsage;

static void PipelineDebugStageUsageInit(
	PipelineDebugStageUsage *usage,
	int samplerCapacity,
	int imageCapacity
){
	if (usage == NULL) return;
	usage->samplerCapacity = (samplerCapacity > 0) ? samplerCapacity : 0;
	usage->samplerCount = 0;
	usage->samplerOverflowLogged = false;
	usage->samplerUsed = NULL;
	if (usage->samplerCapacity > 0) {
		usage->samplerUsed = (GLboolean*)PipelineDebugAllocZero((size_t)usage->samplerCapacity * sizeof(GLboolean));
	}
	usage->imageCapacity = (imageCapacity > 0) ? imageCapacity : 0;
	usage->imageCount = 0;
	usage->imageOverflowLogged = false;
	usage->imageUsed = NULL;
	if (usage->imageCapacity > 0) {
		usage->imageUsed = (GLboolean*)PipelineDebugAllocZero((size_t)usage->imageCapacity * sizeof(GLboolean));
	}
}

static void PipelineDebugStageUsageDispose(PipelineDebugStageUsage *usage){
	if (usage == NULL) return;
	if (usage->samplerUsed) {
		PipelineDebugFree(usage->samplerUsed);
		usage->samplerUsed = NULL;
	}
	if (usage->imageUsed) {
		PipelineDebugFree(usage->imageUsed);
		usage->imageUsed = NULL;
	}
	usage->samplerCapacity = 0;
	usage->samplerCount = 0;
	usage->imageCapacity = 0;
	usage->imageCount = 0;
	usage->samplerOverflowLogged = false;
	usage->imageOverflowLogged = false;
}

static void PipelineDebugStageUsageMarkUnit(
	PipelineDebugStageUsage *usage,
	bool isSampler,
	int unit,
	const char *stageLabel
){
	if (usage == NULL) return;
	if (unit < 0) {
		return;
	}
	if (isSampler) {
		if (usage->samplerCapacity <= 0 || usage->samplerUsed == NULL) {
			if (usage->samplerOverflowLogged == false) {
				PipelineDebugPrint("[Pipeline Debug] %s sampler unit %d reported but sampler capacity is zero\n",
					stageLabel, unit);
				usage->samplerOverflowLogged = true;
			}
			return;
		}
		if (unit >= usage->samplerCapacity) {
			if (usage->samplerOverflowLogged == false) {
				PipelineDebugPrint("[Pipeline Debug] %s sampler unit %d exceeds limit %d\n",
					stageLabel, unit, usage->samplerCapacity);
				usage->samplerOverflowLogged = true;
			}
			return;
		}
		if (usage->samplerUsed[unit] == GL_FALSE) {
			usage->samplerUsed[unit] = GL_TRUE;
			++usage->samplerCount;
		}
	} else {
		if (usage->imageCapacity <= 0 || usage->imageUsed == NULL) {
			if (usage->imageOverflowLogged == false) {
				PipelineDebugPrint("[Pipeline Debug] %s image unit %d reported but image capacity is zero\n",
					stageLabel, unit);
				usage->imageOverflowLogged = true;
			}
			return;
		}
		if (unit >= usage->imageCapacity) {
			if (usage->imageOverflowLogged == false) {
				PipelineDebugPrint("[Pipeline Debug] %s image unit %d exceeds limit %d\n",
					stageLabel, unit, usage->imageCapacity);
				usage->imageOverflowLogged = true;
			}
			return;
		}
		if (usage->imageUsed[unit] == GL_FALSE) {
			usage->imageUsed[unit] = GL_TRUE;
			++usage->imageCount;
		}
	}
}

static void PipelineDebugPrintUnitFlags(
	const char *label,
	const GLboolean *flags,
	int capacity
){
	if (label == NULL || flags == NULL || capacity <= 0) {
		return;
	}
	int count = 0;
	for (int i = 0; i < capacity; ++i) {
		if (flags[i] != GL_FALSE) {
			++count;
		}
	}
	PipelineDebugPrint("[Pipeline Debug]    %s count=%d\n", label, count);
	if (count <= 0) {
		return;
	}
	char line[512];
	int offset = wsprintfA(line, "%s units:", label);
	if (offset < 0) {
		return;
	}
	for (int i = 0; i < capacity; ++i) {
		if (flags[i] != GL_FALSE) {
			if (offset > (int)(sizeof(line) - 16)) {
				break;
			}
			offset += wsprintfA(line + offset, " %d", i);
		}
	}
	PipelineDebugPrint("[Pipeline Debug]    %s\n", line);
}

static void PipelineDebugCheckLimit(const char *label, int used, int limit){
	if (label == NULL) return;
	if (limit <= 0) return;
	if (used > limit) {
		PipelineDebugPrint("[Pipeline Debug]    EXCEEDED %s used=%d limit=%d\n",
			label, used, limit);
	}
}

static void PipelineDebugReportProgramUnitUsage(
	GLuint programId,
	const char *passName,
	int frameCount
){
	if (programId == 0) return;
	GLint prevProgram = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
	if ((GLuint)prevProgram != programId) {
		glExtUseProgram(programId);
	}

	GLint maxCombinedTex = 0;
	GLint maxImageUnits = 0;
	GLint maxTexVS = 0, maxTexFS = 0, maxTexCS = 0;
	GLint maxImgVS = 0, maxImgFS = 0, maxImgCS = 0;
	glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedTex);
	glGetIntegerv(GL_MAX_IMAGE_UNITS, &maxImageUnits);
	glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &maxTexVS);
	glGetIntegerv(GL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS, &maxTexFS);
	glGetIntegerv(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS, &maxTexCS);
	glGetIntegerv(GL_MAX_VERTEX_IMAGE_UNIFORMS, &maxImgVS);
	glGetIntegerv(GL_MAX_FRAGMENT_IMAGE_UNIFORMS, &maxImgFS);
	glGetIntegerv(GL_MAX_COMPUTE_IMAGE_UNIFORMS, &maxImgCS);

	PipelineDebugStageUsage vsUsage;
	PipelineDebugStageUsage fsUsage;
	PipelineDebugStageUsage csUsage;
	PipelineDebugStageUsageInit(&vsUsage, maxTexVS, maxImgVS);
	PipelineDebugStageUsageInit(&fsUsage, maxTexFS, maxImgFS);
	PipelineDebugStageUsageInit(&csUsage, maxTexCS, maxImgCS);

	GLboolean *combinedSamplerUsed = NULL;
	int combinedSamplerCapacity = (maxCombinedTex > 0) ? maxCombinedTex : 0;
	int combinedSamplerCount = 0;
	bool combinedOverflowLogged = false;
	if (combinedSamplerCapacity > 0) {
		combinedSamplerUsed = (GLboolean*)PipelineDebugAllocZero((size_t)combinedSamplerCapacity * sizeof(GLboolean));
	}

	GLint numUniforms = 0;
	glExtGetProgramInterfaceiv(
		/* GLuint program */			programId,
		/* GLenum programInterface */	GL_UNIFORM,
		/* GLenum pname */				GL_ACTIVE_RESOURCES,
		/* GLint *params */				&numUniforms
	);
	GLenum properties[6] = {
		GL_TYPE,
		GL_LOCATION,
		GL_ARRAY_SIZE,
		GL_REFERENCED_BY_VERTEX_SHADER,
		GL_REFERENCED_BY_FRAGMENT_SHADER,
		GL_REFERENCED_BY_COMPUTE_SHADER
	};
	GLint values[6] = {0, 0, 0, 0, 0, 0};
	for (int uniformIndex = 0; uniformIndex < numUniforms; ++uniformIndex) {
		char uniformName[PIPELINE_MAX_RESOURCE_ID_LENGTH] = {0};
		if (glExtGetProgramResourceName != NULL) {
			GLsizei written = 0;
			GLsizei capacity = (GLsizei)(PIPELINE_MAX_RESOURCE_ID_LENGTH - 1);
			glExtGetProgramResourceName(
				programId,
				GL_UNIFORM,
				(GLuint)uniformIndex,
				capacity,
				&written,
				uniformName
			);
			if (written >= capacity) {
				uniformName[PIPELINE_MAX_RESOURCE_ID_LENGTH - 1] = '\0';
			}
		}
		glExtGetProgramResourceiv(
			/* GLuint program */			programId,
			/* GLenum programInterface */	GL_UNIFORM,
			/* GLuint index */				(GLuint)uniformIndex,
			/* GLsizei propCount */			6,
			/* const GLenum *props */		properties,
			/* GLsizei bufSize */			6,
			/* GLsizei *length */			NULL,
			/* GLint *params */				values
		);
		GLenum type = (GLenum)values[0];
		GLint location = values[1];
		GLint arraySize = values[2];
		bool refVS = (values[3] != 0);
		bool refFS = (values[4] != 0);
		bool refCS = (values[5] != 0);
#if ENABLE_PIPELINE_DEBUG_LOG
		PipelineDebugPrint("[Pipeline Debug] program unit usage uniform index=%d name=\"%s\" type=0x%04X location=%d arraySize=%d refVS=%d refFS=%d refCS=%d\n",
			uniformIndex,
			(*uniformName != '\0') ? uniformName : "(unknown)",
			(GLuint)type,
			location,
			arraySize,
			refVS ? 1 : 0,
			refFS ? 1 : 0,
			refCS ? 1 : 0
		);
#endif
		if (location < 0) {
			continue;
		}
		if (arraySize <= 0) {
			arraySize = 1;
		}
		bool isSampler = PipelineIsSamplerType(type);
		bool isImage = PipelineIsImageType(type);
		if (!isSampler && !isImage) {
			continue;
		}
		for (int element = 0; element < arraySize; ++element) {
			GLint elementLocation = location + element;
			GLint unit = -1;
			PFNGLGETUNIFORMIVPROC getUniformiv = PipelineResolveGetUniformiv();
			if (getUniformiv != NULL) {
				getUniformiv(programId, elementLocation, &unit);
			}
#if ENABLE_PIPELINE_DEBUG_LOG
			PipelineDebugPrint("[Pipeline Debug] program unit usage uniform \"%s\" element=%d location=%d rawUnit=%d isSampler=%d isImage=%d\n",
				(*uniformName != '\0') ? uniformName : "(unknown)",
				element,
				elementLocation,
				unit,
				isSampler ? 1 : 0,
				isImage ? 1 : 0
			);
#endif
			if (unit < 0) {
				continue;
			}
			if (isSampler) {
				if (combinedSamplerUsed != NULL) {
					if (unit < combinedSamplerCapacity) {
						if (combinedSamplerUsed[unit] == GL_FALSE) {
							combinedSamplerUsed[unit] = GL_TRUE;
							++combinedSamplerCount;
						}
					} else if (combinedOverflowLogged == false) {
						PipelineDebugPrint("[Pipeline Debug] combined sampler unit %d exceeds GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS=%d\n",
							unit, maxCombinedTex);
						combinedOverflowLogged = true;
					}
				}
				if (refVS) {
					PipelineDebugStageUsageMarkUnit(&vsUsage, true, unit, "VS");
				}
				if (refFS) {
					PipelineDebugStageUsageMarkUnit(&fsUsage, true, unit, "FS");
				}
				if (refCS) {
					PipelineDebugStageUsageMarkUnit(&csUsage, true, unit, "CS");
				}
			} else if (isImage) {
				if (refVS) {
					PipelineDebugStageUsageMarkUnit(&vsUsage, false, unit, "VS");
				}
				if (refFS) {
					PipelineDebugStageUsageMarkUnit(&fsUsage, false, unit, "FS");
				}
				if (refCS) {
					PipelineDebugStageUsageMarkUnit(&csUsage, false, unit, "CS");
				}
			}
		}
	}

	PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" program unit usage\n",
		frameCount, (passName != NULL) ? passName : "(null)");
	PipelineDebugPrint("[Pipeline Debug]    Limits: MAX_COMBINED_TEXTURE_IMAGE_UNITS=%d MAX_IMAGE_UNITS=%d\n",
		maxCombinedTex, maxImageUnits);
	PipelineDebugPrint("[Pipeline Debug]    Per-stage sampler limits VS=%d FS=%d CS=%d\n",
		maxTexVS, maxTexFS, maxTexCS);
	PipelineDebugPrint("[Pipeline Debug]    Per-stage image   limits VS=%d FS=%d CS=%d\n",
		maxImgVS, maxImgFS, maxImgCS);
	if (vsUsage.samplerUsed) {
		PipelineDebugPrintUnitFlags("VS sampler", vsUsage.samplerUsed, vsUsage.samplerCapacity);
	}
	if (fsUsage.samplerUsed) {
		PipelineDebugPrintUnitFlags("FS sampler", fsUsage.samplerUsed, fsUsage.samplerCapacity);
	}
	if (csUsage.samplerUsed) {
		PipelineDebugPrintUnitFlags("CS sampler", csUsage.samplerUsed, csUsage.samplerCapacity);
	}
	if (vsUsage.imageUsed) {
		PipelineDebugPrintUnitFlags("VS image", vsUsage.imageUsed, vsUsage.imageCapacity);
	}
	if (fsUsage.imageUsed) {
		PipelineDebugPrintUnitFlags("FS image", fsUsage.imageUsed, fsUsage.imageCapacity);
	}
	if (csUsage.imageUsed) {
		PipelineDebugPrintUnitFlags("CS image", csUsage.imageUsed, csUsage.imageCapacity);
	}
	if (combinedSamplerUsed) {
		PipelineDebugPrintUnitFlags("Combined sampler", combinedSamplerUsed, combinedSamplerCapacity);
	}

	PipelineDebugCheckLimit("VS sampler units", vsUsage.samplerCount, maxTexVS);
	PipelineDebugCheckLimit("FS sampler units", fsUsage.samplerCount, maxTexFS);
	PipelineDebugCheckLimit("CS sampler units", csUsage.samplerCount, maxTexCS);
	PipelineDebugCheckLimit("VS image units", vsUsage.imageCount, maxImgVS);
	PipelineDebugCheckLimit("FS image units", fsUsage.imageCount, maxImgFS);
	PipelineDebugCheckLimit("CS image units", csUsage.imageCount, maxImgCS);
	PipelineDebugCheckLimit("Combined sampler units", combinedSamplerCount, maxCombinedTex);

	if (maxImageUnits > 0) {
		for (GLint unit = 0; unit < maxImageUnits; ++unit) {
			GLint name = 0;
			GLint level = 0;
			GLint layered = 0;
			GLint layer = 0;
			GLint access = 0;
			GLint format = 0;
			PFNGLGETINTEGERI_VPROC getIntegeri_v = PipelineResolveGetIntegeriV();
			if (getIntegeri_v == NULL) {
				break;
			}
			getIntegeri_v(GL_IMAGE_BINDING_NAME, unit, &name);
			if (name == 0) {
				continue;
			}
			getIntegeri_v(GL_IMAGE_BINDING_LEVEL, unit, &level);
			getIntegeri_v(GL_IMAGE_BINDING_LAYERED, unit, &layered);
			getIntegeri_v(GL_IMAGE_BINDING_LAYER, unit, &layer);
			getIntegeri_v(GL_IMAGE_BINDING_ACCESS, unit, &access);
			getIntegeri_v(GL_IMAGE_BINDING_FORMAT, unit, &format);
			PipelineDebugPrint("[Pipeline Debug]    IMAGE[%d] tex=%d level=%d layered=%d layer=%d access=0x%X format=0x%X\n",
				unit, name, level, layered, layer, access, format);
		}
	}

	if (combinedSamplerUsed) {
		PipelineDebugFree(combinedSamplerUsed);
	}
	PipelineDebugStageUsageDispose(&vsUsage);
	PipelineDebugStageUsageDispose(&fsUsage);
	PipelineDebugStageUsageDispose(&csUsage);

	if ((GLuint)prevProgram != programId) {
		glExtUseProgram((GLuint)prevProgram);
	}
}

static int s_debugLastProgramUsageReportFrame = -1;
static void PipelineDebugReportProgramUnitUsageOncePerFrame(
	int frameCount,
	const char *passName
){
	if (frameCount == s_debugLastProgramUsageReportFrame) {
		return;
	}
	s_debugLastProgramUsageReportFrame = frameCount;
	PipelineDebugReportProgramUnitUsage(s_graphicsComputeProgramId, passName, frameCount);
}

static const char *PipelineDebugGetGlDebugSourceString(GLenum source){
	switch (source) {
		case GL_DEBUG_SOURCE_API: return "API";
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "WINDOW_SYSTEM";
		case GL_DEBUG_SOURCE_SHADER_COMPILER: return "SHADER_COMPILER";
		case GL_DEBUG_SOURCE_THIRD_PARTY: return "THIRD_PARTY";
		case GL_DEBUG_SOURCE_APPLICATION: return "APPLICATION";
		case GL_DEBUG_SOURCE_OTHER: return "OTHER";
		default: return "UNKNOWN_SOURCE";
	}
}

static const char *PipelineDebugGetGlDebugTypeString(GLenum type){
	switch (type) {
		case GL_DEBUG_TYPE_ERROR: return "ERROR";
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "DEPRECATED_BEHAVIOR";
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "UNDEFINED_BEHAVIOR";
		case GL_DEBUG_TYPE_PORTABILITY: return "PORTABILITY";
		case GL_DEBUG_TYPE_PERFORMANCE: return "PERFORMANCE";
		case GL_DEBUG_TYPE_OTHER: return "OTHER";
		case GL_DEBUG_TYPE_MARKER: return "MARKER";
		case GL_DEBUG_TYPE_PUSH_GROUP: return "PUSH_GROUP";
		case GL_DEBUG_TYPE_POP_GROUP: return "POP_GROUP";
		default: return "UNKNOWN_TYPE";
	}
}

static const char *PipelineDebugGetGlDebugSeverityString(GLenum severity){
	switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH: return "HIGH";
		case GL_DEBUG_SEVERITY_MEDIUM: return "MEDIUM";
		case GL_DEBUG_SEVERITY_LOW: return "LOW";
		case GL_DEBUG_SEVERITY_NOTIFICATION: return "NOTIFICATION";
		default: return "UNKNOWN_SEVERITY";
	}
}

static void APIENTRY PipelineGlDebugMessageCallback(
	GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar *message,
	const void *userParam
){
	(void)length;
	(void)userParam;
	if (message == NULL) {
		message = "";
	}
	PipelineDebugPrint("[GL Debug] source=%s(0x%04X) type=%s(0x%04X) id=%u severity=%s(0x%04X) message=%s\n",
		PipelineDebugGetGlDebugSourceString(source),
		source,
		PipelineDebugGetGlDebugTypeString(type),
		type,
		id,
		PipelineDebugGetGlDebugSeverityString(severity),
		severity,
		message
	);
}

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
#if ENABLE_PIPELINE_DEBUG_LOG
			PipelineDebugPrint("[Pipeline Debug] create resource \"%s\" historyIndex=%d textureId=%u size=%dx%d internalformat=0x%04X format=0x%04X type=0x%04X\n",
				resource->id, historyIndex, state->textureIds[historyIndex], width, height, internalformat, format, type);
			GLenum textureError = glGetError();
			if (textureError != GL_NO_ERROR) {
				PipelineDebugPrint("[Pipeline Debug] create resource \"%s\" historyIndex=%d glTexImage2D error=%s(0x%04X)\n",
					resource->id, historyIndex, PipelineDebugGetGlErrorString(textureError), textureError);
				while ((textureError = glGetError()) != GL_NO_ERROR) {
					PipelineDebugPrint("[Pipeline Debug] create resource \"%s\" historyIndex=%d additional GL error=%s(0x%04X)\n",
						resource->id, historyIndex, PipelineDebugGetGlErrorString(textureError), textureError);
				}
			}
#endif
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
	struct PipelineDebugImageBindingEntry {
		GLuint unit;
		GLenum access;
		GLuint textureId;
		const char *resourceId;
		int historyOffset;
		int resolvedIndex;
		GLint mipLevel;
		bool isOutput;
	};
	PipelineDebugImageBindingEntry debugImageBindings[PIPELINE_MAX_BINDINGS_PER_PASS] = {};
	int debugImageBindingCount = 0;
#else
	bool debugLog = false;
#endif

	glExtUseProgram(s_graphicsComputeProgramId);
	PipelineComputeEnsureBindingCache();
#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" binding cache samplerCount=%d imageCount=%d\n",
			frameCount,
			pass->name,
			s_computeSamplerBindingCount,
			s_computeImageBindingCount
		);
		for (int samplerIndex = 0; samplerIndex < s_computeSamplerBindingCount; ++samplerIndex) {
			PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" binding cache sampler[%d]=%d\n",
				frameCount,
				pass->name,
				samplerIndex,
				s_computeSamplerBindingUnits[samplerIndex]
			);
		}
		for (int imageIndex = 0; imageIndex < s_computeImageBindingCount; ++imageIndex) {
			PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" binding cache image[%d]=%d\n",
				frameCount,
				pass->name,
				imageIndex,
				s_computeImageBindingUnits[imageIndex]
			);
		}
		PipelineDebugObserveComputeUniformUnits("postEnsureBindingCache", frameCount, pass->name);
	}
#endif
	if (s_computePipelinePassUniformAvailable) {
		glExtUniform1i(UNIFORM_LOCATION_PIPELINE_PASS_INDEX, s_activePipelinePassIndex);
#if ENABLE_PIPELINE_DEBUG_LOG
		PipelineDebugTrackComputeUniformWrite(
			frameCount,
			pass->name,
			"compute pass pipeline index",
			s_graphicsComputeProgramId,
			UNIFORM_LOCATION_PIPELINE_PASS_INDEX,
			s_activePipelinePassIndex
		);
#endif
	}
#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		char entryLabel[128];
		wsprintfA(entryLabel, "frame %d compute pass \"%s\" ENTRY after glUseProgram", frameCount, pass->name);
		int entryErrors = PipelineDebugDrainGlErrors(entryLabel);
		if (entryErrors > 0) {
			PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" ENTRY: found %d pre-existing GL errors\n",
				frameCount, pass->name, entryErrors);
		}
	}
#endif

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
					GLint cachedUnit = -1;
					if (samplerUnitCount < s_computeSamplerBindingCount) {
						cachedUnit = s_computeSamplerBindingUnits[samplerUnitCount];
					}
					GLuint samplerUnit = (cachedUnit >= 0) ? (GLuint)cachedUnit : (samplerUnitBase + samplerUnitCount);
					glExtActiveTexture(GL_TEXTURE0 + samplerUnit);
					glBindTexture(GL_TEXTURE_2D, textureId);
					PipelineSetTextureSampler(GL_TEXTURE_2D, resource->textureFilter, resource->textureWrap, false);
				boundSamplerUnits[numBoundSamplerUnits++] = samplerUnit;
				++samplerUnitCount;
#if ENABLE_PIPELINE_DEBUG_LOG
				if (debugLog) {
					const char *unitSource = (cachedUnit >= 0) ? "cached" : "fallback";
					int resolvedIndex = PipelineResolveHistoryIndex(inputState, frameCount, binding->historyOffset);
					PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" input %d \"%s\" sampler unit %u (%s=%d) historyOffset=%d resolvedIndex=%d textureId=%u\n",
						frameCount, pass->name, inputIndex, resource->id, samplerUnit, unitSource, cachedUnit, binding->historyOffset, resolvedIndex, textureId);
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
					const GLint mipLevel = 0;
					GLint cachedUnit = -1;
					if (numBoundImageUnits < s_computeImageBindingCount) {
						cachedUnit = s_computeImageBindingUnits[numBoundImageUnits];
					}
					GLuint imageUnit = (GLuint)((cachedUnit >= 0) ? cachedUnit : numBoundImageUnits);
#if ENABLE_PIPELINE_DEBUG_LOG
					PipelineDebugPrint("[Pipeline Debug] image bind request frame %d pass \"%s\" resource=\"%s\" access=READ historyOffset=%d cachedUnit=%d resolvedUnit=%u textureId=%u\n",
						frameCount,
						pass->name,
						resource->id,
						binding->historyOffset,
						cachedUnit,
						imageUnit,
						textureId
					);
#endif
					PipelineBindImageTexture(imageUnit, textureId, mipLevel, GL_FALSE, 0, GL_READ_ONLY, format);
					GLenum imageBindError = glGetError();
#if ENABLE_PIPELINE_DEBUG_LOG
					if (imageBindError != GL_NO_ERROR) {
						PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" glBindImageTexture(unit=%d, tex=%u, READ_ONLY) immediate error=%s(0x%04X)\n",
							frameCount,
							pass->name,
							imageUnit,
							textureId,
							PipelineDebugGetGlErrorString(imageBindError),
							imageBindError
						);
						PipelineDebugReportProgramUnitUsageOncePerFrame(frameCount, pass->name);
					}
#endif
#if !ENABLE_PIPELINE_DEBUG_LOG
				(void)imageBindError;
#endif
				boundImageUnits[numBoundImageUnits] = imageUnit;
				boundImageAccess[numBoundImageUnits] = GL_READ_ONLY;
				imageFormats[numBoundImageUnits] = format;
				++numBoundImageUnits;
#if ENABLE_PIPELINE_DEBUG_LOG
				if (debugLog) {
					const char *unitSource = (cachedUnit >= 0) ? "cached" : "fallback";
					int resolvedIndex = PipelineResolveHistoryIndex(inputState, frameCount, binding->historyOffset);
					PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" input %d \"%s\" image unit %u (%s=%d) READ level=%d historyOffset=%d resolvedIndex=%d textureId=%u internalformat=0x%04X\n",
						frameCount, pass->name, inputIndex, resource->id, imageUnit, unitSource, cachedUnit,
						mipLevel, binding->historyOffset, resolvedIndex, textureId, format);
					if (debugImageBindingCount < PIPELINE_MAX_BINDINGS_PER_PASS) {
						debugImageBindings[debugImageBindingCount].unit = (GLuint)imageUnit;
						debugImageBindings[debugImageBindingCount].access = GL_READ_ONLY;
						debugImageBindings[debugImageBindingCount].textureId = textureId;
						debugImageBindings[debugImageBindingCount].resourceId = resource->id;
						debugImageBindings[debugImageBindingCount].historyOffset = binding->historyOffset;
						debugImageBindings[debugImageBindingCount].resolvedIndex = resolvedIndex;
						debugImageBindings[debugImageBindingCount].mipLevel = mipLevel;
						debugImageBindings[debugImageBindingCount].isOutput = false;
						++debugImageBindingCount;
					}
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
					// Check if the same texture ID is bound, not just the same resource index
					GLuint inputTextureId = PipelineAcquireResourceTexture(inputBinding->resourceIndex, frameCount, inputBinding->historyOffset);
					if (inputTextureId == textureId) {
						hasMatchingInput = true;
						break;
					}
				}
				// CRITICAL FIX: Always use GL_WRITE_ONLY to avoid GL_INVALID_OPERATION
				// when reading from a different history slot of the same resource via sampler
				GLenum imageAccess = GL_WRITE_ONLY;
				const GLint mipLevel = 0;
				GLint cachedUnit = -1;
				if (numBoundImageUnits < s_computeImageBindingCount) {
					cachedUnit = s_computeImageBindingUnits[numBoundImageUnits];
				}
				GLuint imageUnit = (GLuint)((cachedUnit >= 0) ? cachedUnit : numBoundImageUnits);
#if ENABLE_PIPELINE_DEBUG_LOG
				PipelineDebugPrint("[Pipeline Debug] image bind request frame %d pass \"%s\" resource=\"%s\" access=%s historyOffset=%d cachedUnit=%d resolvedUnit=%u textureId=%u\n",
					frameCount,
					pass->name,
					resource->id,
					(imageAccess == GL_READ_WRITE) ? "READ_WRITE" : "WRITE_ONLY",
					binding->historyOffset,
					cachedUnit,
					imageUnit,
					textureId
				);
				if (debugLog) {
					char drainLabel[128];
					wsprintfA(
						drainLabel,
						"frame %d compute pass \"%s\" texId=%u preBind drain",
						frameCount,
						pass->name,
						textureId
					);
					int clearedErrors = PipelineDebugDrainGlErrors(drainLabel);
					if (clearedErrors > 0) {
						PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" texId=%u cleared %d pre-existing GL errors before glGetTexLevelParameter\n",
							frameCount, pass->name, textureId, clearedErrors);
					}

					GLboolean isValidTexture = glIsTexture(textureId);
					GLint levelInternalFormat = 0;
					GLint levelWidth = 0;
					GLint levelHeight = 0;
					glBindTexture(GL_TEXTURE_2D, textureId);
					glGetTexLevelParameteriv(GL_TEXTURE_2D, mipLevel, GL_TEXTURE_INTERNAL_FORMAT, &levelInternalFormat);
					glGetTexLevelParameteriv(GL_TEXTURE_2D, mipLevel, GL_TEXTURE_WIDTH, &levelWidth);
					glGetTexLevelParameteriv(GL_TEXTURE_2D, mipLevel, GL_TEXTURE_HEIGHT, &levelHeight);
					glBindTexture(GL_TEXTURE_2D, 0);

					GLenum texLevelError = glGetError();
					PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" preBind texId=%u isTexture=%d level=%d internalformat=0x%04X size=%dx%d\n",
						frameCount, pass->name, textureId, isValidTexture ? 1 : 0, mipLevel, levelInternalFormat, levelWidth, levelHeight);
					if (texLevelError != GL_NO_ERROR) {
						PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" preBind texId=%u glGetTexLevelParameter error=%s(0x%04X)\n",
							frameCount, pass->name, textureId, PipelineDebugGetGlErrorString(texLevelError), texLevelError);
						int extraErrorCount = PipelineDebugDrainGlErrors("compute pass glGetTexLevel follow-up");
						if (extraErrorCount > 0) {
							PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" texId=%u drained %d additional GL errors after glGetTexLevelParameter failure\n",
								frameCount, pass->name, textureId, extraErrorCount);
						}
					}
				}
#endif
					// REMOVED: Unnecessary glBindTexture calls that interfere with image binding
					// glBindTexture(GL_TEXTURE_2D, textureId);
					// glBindTexture(GL_TEXTURE_2D, 0);
#if ENABLE_PIPELINE_DEBUG_LOG
					if (debugLog) {
						char uniformContext[128];
						wsprintfA(
							uniformContext,
							"preImageBind output=%d unit=%u",
							outputIndex,
							imageUnit
						);
						PipelineDebugObserveComputeUniformUnits(uniformContext, frameCount, pass->name);
					}
#endif
					PipelineBindImageTexture(imageUnit, textureId, mipLevel, GL_FALSE, 0, imageAccess, internalformat);
					GLenum imageBindError = glGetError();
#if ENABLE_PIPELINE_DEBUG_LOG
						if (imageBindError != GL_NO_ERROR) {
							PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" glBindImageTexture(unit=%d, tex=%u, %s) immediate error=%s(0x%04X)\n",
								frameCount,
								pass->name,
								imageUnit,
								textureId,
								(imageAccess == GL_READ_WRITE) ? "READ_WRITE" : "WRITE_ONLY",
								PipelineDebugGetGlErrorString(imageBindError),
								imageBindError
							);
							PipelineDebugReportProgramUnitUsageOncePerFrame(frameCount, pass->name);
						}
#endif
#if ENABLE_PIPELINE_DEBUG_LOG
					if (debugLog) {
						char uniformContextAfter[128];
						wsprintfA(
							uniformContextAfter,
							"postImageBind output=%d unit=%u",
							outputIndex,
							imageUnit
						);
						PipelineDebugObserveComputeUniformUnits(uniformContextAfter, frameCount, pass->name);
					}
#endif
#if !ENABLE_PIPELINE_DEBUG_LOG
					(void)imageBindError;
#endif
					boundImageUnits[numBoundImageUnits] = imageUnit;
					boundImageAccess[numBoundImageUnits] = imageAccess;
					imageFormats[numBoundImageUnits] = internalformat;
				++numBoundImageUnits;
				hasWritableOutput = true;
#if ENABLE_PIPELINE_DEBUG_LOG
				if (debugLog) {
					const char *unitSource = (cachedUnit >= 0) ? "cached" : "fallback";
					int resolvedIndex = PipelineResolveHistoryIndex(outputState, frameCount, binding->historyOffset);
					PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" output %d \"%s\" image unit %d (%s=%d) level=%d access=%s historyOffset=%d resolvedIndex=%d textureId=%u matchingInput=%d internalformat=0x%04X\n",
						frameCount, pass->name, outputIndex, resource->id, imageUnit, unitSource, cachedUnit, mipLevel,
						(imageAccess == GL_READ_WRITE) ? "READ_WRITE" : "WRITE_ONLY",
						binding->historyOffset, resolvedIndex, textureId, hasMatchingInput ? 1 : 0, internalformat);
					if (debugImageBindingCount < PIPELINE_MAX_BINDINGS_PER_PASS) {
						debugImageBindings[debugImageBindingCount].unit = (GLuint)imageUnit;
						debugImageBindings[debugImageBindingCount].access = imageAccess;
						debugImageBindings[debugImageBindingCount].textureId = textureId;
						debugImageBindings[debugImageBindingCount].resourceId = resource->id;
						debugImageBindings[debugImageBindingCount].historyOffset = binding->historyOffset;
						debugImageBindings[debugImageBindingCount].resolvedIndex = resolvedIndex;
						debugImageBindings[debugImageBindingCount].mipLevel = mipLevel;
						debugImageBindings[debugImageBindingCount].isOutput = true;
						++debugImageBindingCount;
					}
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
						PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" glBindImageTexture error resource=\"%s\" resolvedIndex=%d textureId=%u level=%d error=%s(0x%04x)\n",
							frameCount, pass->name, resource->id, resolvedIndex, textureId, mipLevel,
							PipelineDebugGetGlErrorString(errorCode), errorCode);
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

#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog && debugImageBindingCount > 0) {
		PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" image binding summary count=%d nextImageUnit=%d\n",
			frameCount, pass->name, debugImageBindingCount, numBoundImageUnits);
		for (int bindingIndex = 0; bindingIndex < debugImageBindingCount; ++bindingIndex) {
			const PipelineDebugImageBindingEntry *entry = &debugImageBindings[bindingIndex];
			const char *accessStr = "UNKNOWN";
			switch (entry->access) {
				case GL_READ_ONLY: accessStr = "READ"; break;
				case GL_WRITE_ONLY: accessStr = "WRITE"; break;
				case GL_READ_WRITE: accessStr = "READ_WRITE"; break;
			}
			const char *direction = entry->isOutput ? "OUT" : "IN";
			PipelineDebugPrint("[Pipeline Debug]    %s unit=%u level=%d access=%s resource=\"%s\" historyOffset=%d resolvedIndex=%d textureId=%u\n",
				direction,
				entry->unit,
				entry->mipLevel,
				accessStr,
				entry->resourceId ? entry->resourceId : "(null)",
				entry->historyOffset,
				entry->resolvedIndex,
				entry->textureId
			);
		}
	}
#endif

		if (hasWritableOutput == false) {
			for (int i = 0; i < numBoundImageUnits; ++i) {
				GLuint unit = boundImageUnits[i];
				GLenum access = boundImageAccess[i];
				GLenum unbindFormat = (defaultInternalformat != 0) ? defaultInternalformat : GL_RGBA8;
				PipelineBindImageTexture(unit, 0, 0, GL_FALSE, 0, access, unbindFormat);
#if ENABLE_PIPELINE_DEBUG_LOG
					GLenum unbindError = glGetError();
					if (unbindError != GL_NO_ERROR) {
						PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" glBindImageTexture(unit=%d, UNBIND) immediate error=%s(0x%04X)\n",
							frameCount,
							pass->name,
							unit,
							PipelineDebugGetGlErrorString(unbindError),
							unbindError
						);
						PipelineDebugReportProgramUnitUsageOncePerFrame(frameCount, pass->name);
					}
#endif
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

	if (s_computeWaveOutUniformAvailable) {
		glExtUniform1i(UNIFORM_LOCATION_WAVE_OUT_POS, waveOutPos);
#if ENABLE_PIPELINE_DEBUG_LOG
		PipelineDebugTrackComputeUniformWrite(
			frameCount,
			pass->name,
			"compute pass waveOutPos",
			s_graphicsComputeProgramId,
			UNIFORM_LOCATION_WAVE_OUT_POS,
			waveOutPos
		);
#endif
	}
	if (s_computeFrameCountUniformAvailable) {
		glExtUniform1i(UNIFORM_LOCATION_FRAME_COUNT, frameCount);
#if ENABLE_PIPELINE_DEBUG_LOG
		PipelineDebugTrackComputeUniformWrite(
			frameCount,
			pass->name,
			"compute pass frameCount",
			s_graphicsComputeProgramId,
			UNIFORM_LOCATION_FRAME_COUNT,
			frameCount
		);
#endif
	}
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
#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		GLint currentProgram = 0;
		glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
		PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" currentProgram=%u expectedProgram=%u\n",
			frameCount,
			pass->name,
			(unsigned int)currentProgram,
			(unsigned int)s_graphicsComputeProgramId
		);
	}
#endif

	glExtDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		GLenum dispatchError = glGetError();
		if (dispatchError != GL_NO_ERROR) {
			PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" glDispatchCompute error=%s(0x%04X)\n",
				frameCount, pass->name, PipelineDebugGetGlErrorString(dispatchError), dispatchError);
		}
	}
#endif
	glExtMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		GLenum barrierError = glGetError();
		if (barrierError != GL_NO_ERROR) {
			PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" glMemoryBarrier error=%s(0x%04X)\n",
				frameCount, pass->name, PipelineDebugGetGlErrorString(barrierError), barrierError);
		}
	}
#endif

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
		GLuint unit = boundImageUnits[i];
		GLenum access = boundImageAccess[i];
		GLenum unbindFormat = (defaultInternalformat != 0) ? defaultInternalformat : GL_RGBA8;
		PipelineBindImageTexture(unit, 0, 0, GL_FALSE, 0, access, unbindFormat);
#if ENABLE_PIPELINE_DEBUG_LOG
			GLenum unbindError = glGetError();
			if (unbindError != GL_NO_ERROR) {
				PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" glBindImageTexture(unit=%d, UNBIND) immediate error=%s(0x%04X)\n",
					frameCount,
					pass->name,
					unit,
					PipelineDebugGetGlErrorString(unbindError),
					unbindError
				);
				PipelineDebugReportProgramUnitUsageOncePerFrame(frameCount, pass->name);
			}
#endif
		}
	for (int i = 0; i < numBoundSamplerUnits; ++i) {
		glExtActiveTexture(GL_TEXTURE0 + boundSamplerUnits[i]);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glExtActiveTexture(GL_TEXTURE0);
	glExtUseProgram(0);
#if ENABLE_PIPELINE_DEBUG_LOG
	if (debugLog) {
		char exitLabel[128];
		wsprintfA(exitLabel, "frame %d compute pass \"%s\" EXIT after cleanup", frameCount, pass->name);
		int exitErrors = PipelineDebugDrainGlErrors(exitLabel);
		if (exitErrors > 0) {
			PipelineDebugPrint("[Pipeline Debug] frame %d compute pass \"%s\" EXIT: found %d GL errors after cleanup\n",
				frameCount, pass->name, exitErrors);
		}
	}
#endif

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
#if ENABLE_PIPELINE_DEBUG_LOG
		PipelineDebugTrackComputeUniformWrite(
			frameCount,
			pass->name,
			"fragment pass pipeline index",
			fragmentProgramId,
			UNIFORM_LOCATION_PIPELINE_PASS_INDEX,
			s_activePipelinePassIndex
		);
#endif
	}
#if ENABLE_PIPELINE_DEBUG_LOG
	bool debugLog = PipelineDebugShouldLog(frameCount);
	if (debugLog) {
		char entryLabel[128];
		wsprintfA(entryLabel, "frame %d fragment pass \"%s\" ENTRY after glUseProgram", frameCount, pass->name);
		int entryErrors = PipelineDebugDrainGlErrors(entryLabel);
		if (entryErrors > 0) {
			PipelineDebugPrint("[Pipeline Debug] frame %d fragment pass \"%s\" ENTRY: found %d pre-existing GL errors\n",
				frameCount, pass->name, entryErrors);
		}
	}
#endif

	GLuint framebuffer = 0;
	glExtGenFramebuffers(1, &framebuffer);
	glExtBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	GLenum drawBuffers[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	int numDrawBuffers = 0;
	GLuint targetWidth = SCREEN_WIDTH;
	GLuint targetHeight = SCREEN_HEIGHT;
#if ENABLE_PIPELINE_DEBUG_LOG
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
#if ENABLE_PIPELINE_DEBUG_LOG
	PipelineDebugTrackComputeUniformWrite(
		frameCount,
		pass->name,
		"fragment pass waveOutPos",
		fragmentProgramId,
		UNIFORM_LOCATION_WAVE_OUT_POS,
		waveOutPos
	);
#endif
	glExtUniform1i(UNIFORM_LOCATION_FRAME_COUNT, frameCount);
#if ENABLE_PIPELINE_DEBUG_LOG
	PipelineDebugTrackComputeUniformWrite(
		frameCount,
		pass->name,
		"fragment pass frameCount",
		fragmentProgramId,
		UNIFORM_LOCATION_FRAME_COUNT,
		frameCount
	);
#endif
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
#if ENABLE_PIPELINE_DEBUG_LOG
		PipelineDebugTrackComputeUniformWrite(
			frameCount,
			pass->name,
			"fragment pass optional frameCount",
			fragmentProgramId,
			1,
			frameCount
		);
#endif
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
	if (debugLog) {
		char entryLabel[128];
		wsprintfA(entryLabel, "frame %d present pass \"%s\" ENTRY", frameCount, pass->name);
		int entryErrors = PipelineDebugDrainGlErrors(entryLabel);
		if (entryErrors > 0) {
			PipelineDebugPrint("[Pipeline Debug] frame %d present pass \"%s\" ENTRY: found %d pre-existing GL errors\n",
				frameCount, pass->name, entryErrors);
		}
	}
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

	/* gl3w を初期化（既存コードと共通のローダを利用）*/
	if (!CallGl3wInit()) {
#if ENABLE_PIPELINE_DEBUG_LOG
		PipelineDebugPrint("[Pipeline Debug] gl3w initialization failed; falling back to wglGetProcAddress only\n");
#endif
	}

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
			PROC procAddress = (PROC)CallGl3wGetProcAddress(p);
			if (procAddress == NULL) {
				procAddress = wglGetProcAddress(
					/* LPCSTR Arg1 */	p
				);
			}
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

	/* glBindImageTexture だけは gl3w の解決結果を優先して記録する */
	PFNGLBINDIMAGETEXTUREPROC bindImageTexture = CallGl3wGetBindImageTexture();
	if (bindImageTexture != NULL) {
		s_glExtFunctions[GlExtBindImageTexture] = (void *)bindImageTexture;
	}

	s_glGetIntegeri_v = (PFNGLGETINTEGERI_VPROC)CallGl3wGetProcAddress("glGetIntegeri_v");
	if (s_glGetIntegeri_v == NULL) {
		s_glGetIntegeri_v = (PFNGLGETINTEGERI_VPROC)wglGetProcAddress("glGetIntegeri_v");
	}
	s_glGetUniformiv = (PFNGLGETUNIFORMIVPROC)CallGl3wGetProcAddress("glGetUniformiv");
	if (s_glGetUniformiv == NULL) {
		s_glGetUniformiv = (PFNGLGETUNIFORMIVPROC)wglGetProcAddress("glGetUniformiv");
	}
#if ENABLE_PIPELINE_DEBUG_LOG
	if (glExtDebugMessageCallback != NULL) {
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		if (glExtDebugMessageControl != NULL) {
			glExtDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
		}
		glExtDebugMessageCallback(PipelineGlDebugMessageCallback, NULL);
		PipelineDebugPrint("[Pipeline Debug] GL debug message callback registered\n");
	} else {
		PipelineDebugPrint("[Pipeline Debug] glDebugMessageCallback unavailable; GL debug output disabled\n");
	}
#endif

	/* フラグメントシェーダのポインタ配列 */
	const char *graphicsFragmentShaderCode = p;
	const char *graphicsFragmentShaderCodes[] = {graphicsFragmentShaderCode};

	/* コンピュートシェーダコードの開始位置を検索 */
	while (*p != '\0') { p++; }
	p++;

	/* グラフィクス用コンピュートシェーダのポインタ配列 */
	const char *graphicsComputeShaderCode = p;
	const char *graphicsComputeShaderCodes[] = {graphicsComputeShaderCode};
	s_graphicsComputeShaderSource = graphicsComputeShaderCode;

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
#if ENABLE_PIPELINE_DEBUG_LOG
		PipelineDebugTrackComputeUniformWrite(
			-1,
			NULL,
			"sound dispatch offset",
			soundCsProgramId,
			0,
			i
		);
#endif
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
		PipelineComputeInvalidateBindingCache();

	
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
	PipelineComputeInvalidateBindingCache();
#if ENABLE_PIPELINE_DEBUG_LOG
	{
		glExtUseProgram(s_graphicsComputeProgramId);
		PipelineComputeParseDeclaredBindings();
		PFNGLGETUNIFORMIVPROC getUniformiv = PipelineResolveGetUniformiv();
		GLint uResultLocation = -1;
		GLint uPrevResultLocation = -1;
		if (glExtGetUniformLocation != NULL) {
			uResultLocation = glExtGetUniformLocation(s_graphicsComputeProgramId, "u_result");
			uPrevResultLocation = glExtGetUniformLocation(s_graphicsComputeProgramId, "u_prevResult");
		}
		s_graphicsComputeUniformLocationUResult = uResultLocation;
		s_graphicsComputeUniformLocationUPrevResult = uPrevResultLocation;
		PipelineDebugResetObservedUniformUnits();
		if (getUniformiv != NULL) {
			GLint uResultInitial = -1;
			GLint uPrevResultInitial = -1;
			if (uResultLocation >= 0) {
				getUniformiv(s_graphicsComputeProgramId, uResultLocation, &uResultInitial);
			}
			if (uPrevResultLocation >= 0) {
				getUniformiv(s_graphicsComputeProgramId, uPrevResultLocation, &uPrevResultInitial);
			}
			PipelineDebugPrint("[Pipeline Debug] compute program init uniform \"u_result\" location=%d initialUnit=%d\n",
				uResultLocation,
				uResultInitial
			);
			PipelineDebugPrint("[Pipeline Debug] compute program init uniform \"u_prevResult\" location=%d initialUnit=%d\n",
				uPrevResultLocation,
				uPrevResultInitial
			);
			if (uResultLocation >= 0) {
				int declaredBinding = PipelineComputeLookupDeclaredBinding("u_result", true);
				if (declaredBinding >= 0) {
					glExtUniform1i(uResultLocation, declaredBinding);
					PipelineDebugTrackComputeUniformWrite(
						-1,
						NULL,
						"compute program init force u_result",
						s_graphicsComputeProgramId,
						uResultLocation,
						declaredBinding
					);
					GLint uResultForced = -1;
					getUniformiv(s_graphicsComputeProgramId, uResultLocation, &uResultForced);
					PipelineDebugPrint("[Pipeline Debug] compute program forced uniform \"u_result\" to declared binding=%d verifiedUnit=%d\n",
						declaredBinding,
						uResultForced
					);
				} else {
					PipelineDebugPrint("[Pipeline Debug] compute program could not find declared binding for \"u_result\"\n");
				}
			}
		} else {
			PipelineDebugPrint("[Pipeline Debug] compute program init uniform values unavailable (glGetUniformiv missing)\n");
		}
		PipelineDebugObserveComputeUniformUnits("program init", -1, NULL);
	}
#endif
#if ENABLE_PIPELINE_DEBUG_LOG
	{
		if (s_glGetIntegeri_v != NULL) {
			GLint maxWorkGroupCount[3] = {0, 0, 0};
			s_glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxWorkGroupCount[0]);
			s_glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &maxWorkGroupCount[1]);
			s_glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &maxWorkGroupCount[2]);
			GLint maxInvocations = 0;
			glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxInvocations);
			GLint maxCombinedTextureImageUnits = 0;
			glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedTextureImageUnits);
			GLint maxComputeImageUniforms = 0;
			glGetIntegerv(GL_MAX_COMPUTE_IMAGE_UNIFORMS, &maxComputeImageUniforms);
			GLint maxFragmentImageUniforms = 0;
			glGetIntegerv(GL_MAX_FRAGMENT_IMAGE_UNIFORMS, &maxFragmentImageUniforms);
			GLint maxCombinedImageUniforms = 0;
			glGetIntegerv(GL_MAX_COMBINED_IMAGE_UNIFORMS, &maxCombinedImageUniforms);
			GLint maxImageUnits = 0;
			glGetIntegerv(GL_MAX_IMAGE_UNITS, &maxImageUnits);
			PipelineDebugPrint("[Pipeline Debug] compute caps maxWorkGroupCount=(%d,%d,%d) maxInvocations=%d\n",
				maxWorkGroupCount[0],
				maxWorkGroupCount[1],
				maxWorkGroupCount[2],
				maxInvocations
			);
			PipelineDebugPrint("[Pipeline Debug] image/sampler caps maxCombinedTextureImageUnits=%d maxImageUnits=%d maxComputeImageUniforms=%d maxFragmentImageUniforms=%d maxCombinedImageUniforms=%d\n",
				maxCombinedTextureImageUnits,
				maxImageUnits,
				maxComputeImageUniforms,
				maxFragmentImageUniforms,
				maxCombinedImageUniforms
			);
		} else {
			PipelineDebugPrint("[Pipeline Debug] compute caps glGetIntegeri_v unavailable; max work group count query skipped\n");
		}
	}
#endif
	if (s_graphicsComputeProgramId != 0) {
		s_computePipelinePassUniformAvailable = PipelineProgramHasUniform(
			s_graphicsComputeProgramId,
			UNIFORM_LOCATION_PIPELINE_PASS_INDEX,
			GL_INT
		);
		s_computeFrameCountUniformAvailable = PipelineProgramHasUniform(
			s_graphicsComputeProgramId,
			UNIFORM_LOCATION_FRAME_COUNT,
			GL_INT
		);
		s_computeWaveOutUniformAvailable = PipelineProgramHasUniform(
			s_graphicsComputeProgramId,
			UNIFORM_LOCATION_WAVE_OUT_POS,
			GL_INT
		);
	} else {
		s_computePipelinePassUniformAvailable = false;
		s_computeFrameCountUniformAvailable = false;
		s_computeWaveOutUniformAvailable = false;
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
		PipelineComputeInvalidateBindingCache();
		s_graphicsComputeUniformLocationUResult = -1;
		s_graphicsComputeUniformLocationUPrevResult = -1;
		s_computePipelinePassUniformAvailable = false;
		s_computeFrameCountUniformAvailable = false;
		s_computeWaveOutUniformAvailable = false;
#if ENABLE_PIPELINE_DEBUG_LOG
		PipelineDebugResetObservedUniformUnits();
#endif
	}
	PipelineResetRuntimeResources();

	/* デモを終了する */
	ExitProcess(
		/* UINT uExitCode */	0
	);
}

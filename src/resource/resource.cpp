﻿/* Copyright (C) 2018 Yosshin(@yosshin4004) */

#include "stdint.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
	Shader Minifier v1.3.5以降、このプリプロセッサが必要
	See: https://github.com/laurentlb/Shader_Minifier/pull/304
*/
#define SHADER_MINIFIER_IMPL

/*=============================================================================
▼	各種リソース
-----------------------------------------------------------------------------*/

/* 結合された文字列 */
/*
	crinkler に /OVERRIDEALIGNMENTS オプションを指定すると、末尾に _align? が
	付いたシンボルは、? で指定したアライメントで配置される。
	文字列情報はアライメントを問わないので _align0（= 1 byte アライン）を指定
	すると良い。
*/
#pragma data_seg("s_concatenatedString")
char g_concatenatedString_align0[] =
	/* GL 拡張関数名テーブル（なるべく似た関数名が連続するように配置）*/
#if ENABLE_SWAP_INTERVAL_CONTROL
	"wglSwapIntervalEXT\0"			/* 頻出ワードを持たないので先に配置 */
#endif
	"glDispatchCompute\0"			/* 頻出ワードを持たないので先に配置 */
	"glUniform1i\0"					/* 頻出ワードを持たないので先に配置 */
	"glBindImageTexture\0"			/* 頻出ワードを持たないので先に配置 */
	"glMemoryBarrier\0"				/* 頻出ワードを持たないので先に配置 */
#if ENABLE_MIPMAP_GENERATION
	"glGenerateMipmap\0"			/* 既出ワードを持たないので先に配置 */
#endif
	"glCreateShaderProgramv\0"		/* Generate の ate が 既出ワード */
	"glUseProgram\0"				/* Program が既出ワード */
#if ENABLE_BACK_BUFFER && ((NUM_RENDER_TARGETS > 1) || (PIXEL_FORMAT != PIXEL_FORMAT_UNORM8_RGBA))
	#if PREFER_GL_TEX_STORAGE_2D
	"glTexStorage2D\0"
	#endif
#endif
	"glBufferStorage\0"				/* Storage が既出ワード */
	"glMapBuffer\0"					/* Buffer が既出ワード */
#if ENABLE_BACK_BUFFER && ((NUM_RENDER_TARGETS > 1) || (PIXEL_FORMAT != PIXEL_FORMAT_UNORM8_RGBA))
	"glDrawBuffers\0"				/* Buffer が既出ワード */
#endif
	"glBindBufferBase\0"			/* Buffer が既出ワード */
#if ENABLE_BACK_BUFFER && ((NUM_RENDER_TARGETS > 1) || (PIXEL_FORMAT != PIXEL_FORMAT_UNORM8_RGBA))
	"glBindFramebuffer\0"			/* Bind と Buffer の uffer が既出ワード */
	"glGenFramebuffers\0"			/* Framebuffer が既出ワード */
	"glBlitNamedFramebuffer\0"		/* Framebuffer が既出ワード */
	"glFramebufferTexture\0"		/* Framebuffer Tex が既出ワード */
	#if (NUM_RENDER_TARGETS > 1)
	"glActiveTexture\0"				/* Texture が既出ワード */
	#endif
#endif
#ifdef _DEBUG
	"glUniform2f\0"
	"glGetUniformLocation\0"
	"glGetProgramiv\0"
	"glGetProgramInfoLog\0"
#endif
	"\xFF"			/* end mark */

	/* 描画用シェーダ */
	#include "graphics_fragment_shader.inl"
	"\0"			/* end mark */

	/* コンピュート用シェーダ */
	#include "graphics_compute_shader.inl"
	"\0"			/* end mark */

	/* サウンド用シェーダ */
	#include "sound_compute_shader.inl"
;

/* GL 拡張関数テーブルのインデクス */
typedef enum {
#if ENABLE_SWAP_INTERVAL_CONTROL
	kWglSwapIntervalEXT,
#endif
	GlExtDispatchCompute,
	GlExtUniform1i,
	GlExtBindImageTexture,
	GlExtMemoryBarrier,
#if ENABLE_MIPMAP_GENERATION
	GlExtGenerateMipmap,
#endif
	GlExtCreateShaderProgramv,
	GlExtUseProgram,
#if ENABLE_BACK_BUFFER && ((NUM_RENDER_TARGETS > 1) || (PIXEL_FORMAT != PIXEL_FORMAT_UNORM8_RGBA))
	#if PREFER_GL_TEX_STORAGE_2D
	GlExtTexStorage2D,
	#endif
#endif
	GlExtBufferStorage,
	GlExtMapBuffer,
#if ENABLE_BACK_BUFFER && ((NUM_RENDER_TARGETS > 1) || (PIXEL_FORMAT != PIXEL_FORMAT_UNORM8_RGBA))
	GlExtDrawBuffers,
#endif
	GlExtBindBufferBase,
#if ENABLE_BACK_BUFFER && ((NUM_RENDER_TARGETS > 1) || (PIXEL_FORMAT != PIXEL_FORMAT_UNORM8_RGBA))
	GlExtBindFramebuffer,
	GlExtGenFramebuffers,
	GlExtBlitNamedFramebuffer,
	GlExtFramebufferTexture,
	#if (NUM_RENDER_TARGETS > 1)
	GlExtActiveTexture,
	#endif
#endif
#ifdef _DEBUG
	GlExtUniform2f,
	GlExtGetUniformLocation,
	GlExtGetProgramiv,
	GlExtGetProgramInfoLog,
#endif
	NUM_GLEXT_FUNCTIONS
} GlExt;

/* GL 拡張関数 */
#define wglSwapIntervalEXT			((BOOL(WINAPI*)(int))           s_glExtFunctions[kWglSwapIntervalEXT])
#define glExtDispatchCompute		((PFNGLDISPATCHCOMPUTEPROC)     s_glExtFunctions[GlExtDispatchCompute])
#define glExtUniform1i				((PFNGLUNIFORM1IPROC)           s_glExtFunctions[GlExtUniform1i])
#define glExtBindImageTexture		((PFNGLBINDIMAGETEXTUREPROC)   s_glExtFunctions[GlExtBindImageTexture])
#define glExtMemoryBarrier		((PFNGLMEMORYBARRIERPROC)        s_glExtFunctions[GlExtMemoryBarrier])
#define glExtGenerateMipmap			((PFNGLGENERATEMIPMAPPROC)      s_glExtFunctions[GlExtGenerateMipmap])
#define glExtCreateShaderProgramv	((PFNGLCREATESHADERPROGRAMVPROC)s_glExtFunctions[GlExtCreateShaderProgramv])
#define glExtUseProgram				((PFNGLUSEPROGRAMPROC)          s_glExtFunctions[GlExtUseProgram])
#define glExtTexStorage2D			((PFNGLTEXSTORAGE2DPROC)        s_glExtFunctions[GlExtTexStorage2D])
#define glExtBufferStorage			((PFNGLBUFFERSTORAGEPROC)       s_glExtFunctions[GlExtBufferStorage])
#define glExtMapBuffer				((PFNGLMAPBUFFERPROC)	        s_glExtFunctions[GlExtMapBuffer])
#define glExtDrawBuffers			((PFNGLDRAWBUFFERSPROC)         s_glExtFunctions[GlExtDrawBuffers])
#define glExtBindBufferBase			((PFNGLBINDBUFFERBASEPROC)      s_glExtFunctions[GlExtBindBufferBase])
#define glExtBindFramebuffer		((PFNGLBINDFRAMEBUFFERPROC)     s_glExtFunctions[GlExtBindFramebuffer])
#define glExtGenFramebuffers		((PFNGLGENFRAMEBUFFERSPROC)     s_glExtFunctions[GlExtGenFramebuffers])
#define glExtBlitNamedFramebuffer	((PFNGLBLITNAMEDFRAMEBUFFERPROC)s_glExtFunctions[GlExtBlitNamedFramebuffer])
#define glExtFramebufferTexture		((PFNGLFRAMEBUFFERTEXTUREPROC)  s_glExtFunctions[GlExtFramebufferTexture])
#define glExtActiveTexture			((PFNGLACTIVETEXTUREPROC)       s_glExtFunctions[GlExtActiveTexture])
#define glExtUniform2f				((PFNGLUNIFORM2FPROC)           s_glExtFunctions[GlExtUniform2f])
#define glExtGetUniformLocation		((PFNGLGETUNIFORMLOCATIONPROC)  s_glExtFunctions[GlExtGetUniformLocation])
#define glExtGetProgramiv			((PFNGLGETPROGRAMIVPROC)        s_glExtFunctions[GlExtGetProgramiv])
#define glExtGetProgramInfoLog		((PFNGLGETPROGRAMINFOLOGPROC)   s_glExtFunctions[GlExtGetProgramInfoLog])



#ifdef __cplusplus
}
#endif

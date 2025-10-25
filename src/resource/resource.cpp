/* Copyright (C) 2018 Yosshin(@yosshin4004) */

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
	"glUniform1f\0"					/* 頻出ワードを持たないので先に配置 */
	"glUniform2f\0"					/* 頻出ワードを持たないので先に配置 */
	"glUniform3i\0"					/* 頻出ワードを持たないので先に配置 */
	"glBindImageTexture\0"			/* 頻出ワードを持たないので先に配置 */
	"glMemoryBarrier\0"				/* 頻出ワードを持たないので先に配置 */
	"glDebugMessageCallback\0"		/* 頻出ワードを持たないので先に配置 */
	"glDebugMessageControl\0"		/* 頻出ワードを持たないので先に配置 */
#if ENABLE_MIPMAP_GENERATION
	"glGenerateMipmap\0"			/* 既出ワードを持たないので先に配置 */
#endif
	"glCreateShaderProgramv\0"		/* Generate の ate が 既出ワード */
	"glUseProgram\0"				/* Program が既出ワード */
	"glGetUniformLocation\0"		/* Program が既出ワード */
	"glGetProgramInterfaceiv\0"		/* Program が既出ワード */
	"glGetProgramResourceiv\0"		/* Program が既出ワード */
	"glGetProgramiv\0"				/* Program が既出ワード */
	"glDeleteProgram\0"				/* Program が既出ワード */
#if ENABLE_BACK_BUFFER && ((NUM_RENDER_TARGETS > 1) || (PIXEL_FORMAT != PIXEL_FORMAT_UNORM8_RGBA))
	#if PREFER_GL_TEX_STORAGE_2D
	"glTexStorage2D\0"
	#endif
#endif
	"glBufferStorage\0"				/* Storage が既出ワード */
	"glMapBuffer\0"					/* Buffer が既出ワード */
	"glDrawBuffers\0"				/* Buffer が既出ワード */
	"glBindBufferBase\0"			/* Buffer が既出ワード */
	"glBindFramebuffer\0"			/* Bind と Buffer の uffer が既出ワード */
	"glGenFramebuffers\0"			/* Framebuffer が既出ワード */
	"glDeleteFramebuffers\0"		/* Framebuffer が既出ワード */
	"glBlitNamedFramebuffer\0"		/* Framebuffer が既出ワード */
	"glFramebufferTexture\0"		/* Framebuffer Tex が既出ワード */
	"glActiveTexture\0"				/* Texture が既出ワード */
	"glCheckFramebufferStatus\0"	/* Framebuffer が既出ワード */
	"glVertexAttribPointer\0"		/* Vertex が既出ワード */
	"glEnableVertexAttribArray\0"	/* Vertex が既出ワード */
	"glDisableVertexAttribArray\0"	/* Vertex が既出ワード */
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
	GlExtUniform1f,
	GlExtUniform2f,
	GlExtUniform3i,
	GlExtBindImageTexture,
	GlExtMemoryBarrier,
	GlExtDebugMessageCallback,
	GlExtDebugMessageControl,
#if ENABLE_MIPMAP_GENERATION
	GlExtGenerateMipmap,
#endif
	GlExtCreateShaderProgramv,
	GlExtUseProgram,
	GlExtGetUniformLocation,
	GlExtGetProgramInterfaceiv,
	GlExtGetProgramResourceiv,
	GlExtGetProgramiv,
	GlExtDeleteProgram,
#if ENABLE_BACK_BUFFER && ((NUM_RENDER_TARGETS > 1) || (PIXEL_FORMAT != PIXEL_FORMAT_UNORM8_RGBA))
	#if PREFER_GL_TEX_STORAGE_2D
	GlExtTexStorage2D,
	#endif
#endif
	GlExtBufferStorage,
	GlExtMapBuffer,
	GlExtDrawBuffers,
	GlExtBindBufferBase,
	GlExtBindFramebuffer,
	GlExtGenFramebuffers,
	GlExtDeleteFramebuffers,
	GlExtBlitNamedFramebuffer,
	GlExtFramebufferTexture,
	GlExtActiveTexture,
	GlExtCheckFramebufferStatus,
	GlExtVertexAttribPointer,
	GlExtEnableVertexAttribArray,
	GlExtDisableVertexAttribArray,
	NUM_GLEXT_FUNCTIONS
} GlExt;

/* GL 拡張関数 */
#if ENABLE_SWAP_INTERVAL_CONTROL
#define wglSwapIntervalEXT			((BOOL(WINAPI*)(int))           s_glExtFunctions[kWglSwapIntervalEXT])
#endif
#define glExtDispatchCompute		((PFNGLDISPATCHCOMPUTEPROC)     s_glExtFunctions[GlExtDispatchCompute])
#define glExtUniform1i				((PFNGLUNIFORM1IPROC)           s_glExtFunctions[GlExtUniform1i])
#define glExtUniform1f				((PFNGLUNIFORM1FPROC)           s_glExtFunctions[GlExtUniform1f])
#define glExtUniform2f				((PFNGLUNIFORM2FPROC)           s_glExtFunctions[GlExtUniform2f])
#define glExtUniform3i				((PFNGLUNIFORM3IPROC)           s_glExtFunctions[GlExtUniform3i])
#define glExtBindImageTexture		((PFNGLBINDIMAGETEXTUREPROC)   s_glExtFunctions[GlExtBindImageTexture])
#define glExtMemoryBarrier			((PFNGLMEMORYBARRIERPROC)       s_glExtFunctions[GlExtMemoryBarrier])
#define glExtDebugMessageCallback	((PFNGLDEBUGMESSAGECALLBACKPROC)s_glExtFunctions[GlExtDebugMessageCallback])
#define glExtDebugMessageControl	((PFNGLDEBUGMESSAGECONTROLPROC) s_glExtFunctions[GlExtDebugMessageControl])
#if ENABLE_MIPMAP_GENERATION
#define glExtGenerateMipmap			((PFNGLGENERATEMIPMAPPROC)      s_glExtFunctions[GlExtGenerateMipmap])
#endif
#define glExtCreateShaderProgramv	((PFNGLCREATESHADERPROGRAMVPROC)s_glExtFunctions[GlExtCreateShaderProgramv])
#define glExtUseProgram				((PFNGLUSEPROGRAMPROC)          s_glExtFunctions[GlExtUseProgram])
#define glExtGetUniformLocation		((PFNGLGETUNIFORMLOCATIONPROC)  s_glExtFunctions[GlExtGetUniformLocation])
#define glExtGetProgramInterfaceiv	((PFNGLGETPROGRAMINTERFACEIVPROC)s_glExtFunctions[GlExtGetProgramInterfaceiv])
#define glExtGetProgramResourceiv	((PFNGLGETPROGRAMRESOURCEIVPROC)s_glExtFunctions[GlExtGetProgramResourceiv])
#define glExtGetProgramiv			((PFNGLGETPROGRAMIVPROC)        s_glExtFunctions[GlExtGetProgramiv])
#define glExtDeleteProgram			((PFNGLDELETEPROGRAMPROC)       s_glExtFunctions[GlExtDeleteProgram])
#if ENABLE_BACK_BUFFER && ((NUM_RENDER_TARGETS > 1) || (PIXEL_FORMAT != PIXEL_FORMAT_UNORM8_RGBA))
	#if PREFER_GL_TEX_STORAGE_2D
#define glExtTexStorage2D			((PFNGLTEXSTORAGE2DPROC)        s_glExtFunctions[GlExtTexStorage2D])
	#endif
#endif
#define glExtBufferStorage			((PFNGLBUFFERSTORAGEPROC)       s_glExtFunctions[GlExtBufferStorage])
#define glExtMapBuffer				((PFNGLMAPBUFFERPROC)	        s_glExtFunctions[GlExtMapBuffer])
#define glExtDrawBuffers			((PFNGLDRAWBUFFERSPROC)         s_glExtFunctions[GlExtDrawBuffers])
#define glExtBindBufferBase			((PFNGLBINDBUFFERBASEPROC)      s_glExtFunctions[GlExtBindBufferBase])
#define glExtBindFramebuffer		((PFNGLBINDFRAMEBUFFERPROC)     s_glExtFunctions[GlExtBindFramebuffer])
#define glExtGenFramebuffers		((PFNGLGENFRAMEBUFFERSPROC)     s_glExtFunctions[GlExtGenFramebuffers])
#define glExtDeleteFramebuffers		((PFNGLDELETEFRAMEBUFFERSPROC)  s_glExtFunctions[GlExtDeleteFramebuffers])
#define glExtBlitNamedFramebuffer	((PFNGLBLITNAMEDFRAMEBUFFERPROC)s_glExtFunctions[GlExtBlitNamedFramebuffer])
#define glExtFramebufferTexture		((PFNGLFRAMEBUFFERTEXTUREPROC)  s_glExtFunctions[GlExtFramebufferTexture])
#define glExtActiveTexture			((PFNGLACTIVETEXTUREPROC)       s_glExtFunctions[GlExtActiveTexture])
#define glExtCheckFramebufferStatus	((PFNGLCHECKFRAMEBUFFERSTATUSPROC)s_glExtFunctions[GlExtCheckFramebufferStatus])
#define glExtVertexAttribPointer	((PFNGLVERTEXATTRIBPOINTERPROC) s_glExtFunctions[GlExtVertexAttribPointer])
#define glExtEnableVertexAttribArray ((PFNGLENABLEVERTEXATTRIBARRAYPROC)s_glExtFunctions[GlExtEnableVertexAttribArray])
#define glExtDisableVertexAttribArray ((PFNGLDISABLEVERTEXATTRIBARRAYPROC)s_glExtFunctions[GlExtDisableVertexAttribArray])



#ifdef __cplusplus
}
#endif

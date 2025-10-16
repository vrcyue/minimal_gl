/* Copyright (C) 2018 Yosshin(@yosshin4004) */

#include <math.h>
#include <string.h>
#include "common.h"
#include "app.h"
#include "graphics.h"
#include "config.h"
#include "dds_util.h"
#include "png_util.h"
#include "sound.h"
#include "tiny_vmath.h"
#include "dds_parser.h"
#include "pipeline_description.h"


#define USER_TEXTURE_START_INDEX				(8)
#define COMPUTE_TEXTURE_START_INDEX				(4)
#define BUFFER_INDEX_FOR_SOUND_VISUALIZER_INPUT	(0)

static GLuint s_mrtTextures[2 /* 裏表 */][NUM_RENDER_TARGETS] = {{0}};
static GLuint s_mrtFrameBuffer = 0;
static struct {
	GLenum target;
	GLuint id;
} s_userTextures[NUM_USER_TEXTURES] = {{0}};
static GLuint s_shaderPipelineId = 0;
static GLuint s_vertexShaderId = 0;
static GLuint s_fragmentShaderId = 0;
static GLuint s_computeTextures[2 /* 裏表 */][NUM_RENDER_TARGETS] = {{0}};
static GLuint s_computeShaderId = 0;
static GLint s_computeWorkGroupSize[3] = {1, 1, 1};
static RenderSettings s_currentRenderSettings = {(PixelFormat)0};
static int s_xReso = DEFAULT_SCREEN_XRESO;
static int s_yReso = DEFAULT_SCREEN_YRESO;
static PipelineDescription s_pipelineDescription = {{0}};
static bool s_pipelineHasCustomDescription = false;
static int s_activePipelinePassIndex = -1;

typedef struct {
	GLuint textureIds[PIPELINE_MAX_HISTORY_LENGTH];
	int width;
	int height;
	PixelFormat pixelFormat;
	int historyLength;
	bool initialized;
} PipelineRuntimeResourceState;

static PipelineRuntimeResourceState s_pipelineRuntimeResources[PIPELINE_MAX_RESOURCES] = {{0}};

static void GraphicsDispatchCompute(
	const CurrentFrameParams *params,
	const RenderSettings *settings
);
static void GraphicsBuildLegacyPipelineDescription(
	PipelineDescription *pipeline
);
static const PipelineDescription *GraphicsResolvePipelineDescription();
static void GraphicsExecutePipeline(
	const PipelineDescription *pipeline,
	const CurrentFrameParams *params,
	const RenderSettings *settings
);
static void GraphicsResetPipelineRuntimeResources();
static void GraphicsEnsurePipelineResources(
	const PipelineDescription *pipeline,
	const CurrentFrameParams *params
);
static GLuint GraphicsAcquirePipelineResourceTexture(
	int resourceIndex,
	int frameCount,
	int historyOffset
);
static const PipelineResource *GraphicsGetPipelineResource(
	const PipelineDescription *pipeline,
	int resourceIndex
);
static PipelineRuntimeResourceState *GraphicsGetPipelineRuntimeResource(
	int resourceIndex
);
static bool GraphicsParseLegacyResourceIndex(
	const PipelineResource *resource,
	const char *prefix,
	int *outIndex
);
static void GraphicsSetTextureSampler(
	GLenum target,
	TextureFilter filter,
	TextureWrap wrap,
	bool enableMipmap
);
static void GraphicsDrawFullScreenQuad(
	GLuint outputFrameBuffer,
	const CurrentFrameParams *params,
	const RenderSettings *settings
);
static void GraphicsCreateFrameBuffer(
	int xReso,
	int yReso,
	const RenderSettings *settings
);
static void GraphicsDeleteFrameBuffer(void);
static void GraphicsCreateComputeTextures(
	int xReso,
	int yReso,
	const RenderSettings *settings
);
static void GraphicsDeleteComputeTextures(void);
static bool GraphicsExecuteComputePassPipeline(
	const PipelineDescription *pipeline,
	const PipelinePass *pass,
	const CurrentFrameParams *params,
	const RenderSettings *settings
);
static bool GraphicsExecuteFragmentPassPipeline(
	const PipelineDescription *pipeline,
	const PipelinePass *pass,
	const CurrentFrameParams *params,
	const RenderSettings *settings
){
	if (pipeline == NULL || pass == NULL || params == NULL || settings == NULL) {
		return false;
	}
	if (s_fragmentShaderId == 0 || s_shaderPipelineId == 0) {
		return false;
	}

	GLuint framebuffer = 0;
	GLenum drawBuffers[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	int numColorAttachments = 0;
	int targetWidth = params->xReso;
	int targetHeight = params->yReso;

	/* Collect outputs */
	for (int outputIndex = 0; outputIndex < pass->numOutputs; ++outputIndex) {
		const PipelineResourceBinding *binding = &pass->outputs[outputIndex];
		if (binding->access != PipelineResourceAccessColorAttachment) {
			continue;
		}
		const PipelineResource *resource = GraphicsGetPipelineResource(pipeline, binding->resourceIndex);
		PipelineRuntimeResourceState *runtimeState = GraphicsGetPipelineRuntimeResource(binding->resourceIndex);
		if (resource == NULL || runtimeState == NULL || runtimeState->initialized == false) {
			return false;
		}
		GLuint textureId = GraphicsAcquirePipelineResourceTexture(
			binding->resourceIndex,
			params->frameCount,
			binding->historyOffset
		);
		if (textureId == 0) {
			return false;
		}
		if (numColorAttachments == 0) {
			targetWidth = runtimeState->width;
			targetHeight = runtimeState->height;
		}
		if (framebuffer == 0) {
			glGenFramebuffers(1, &framebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		}
		GLenum attachment = GL_COLOR_ATTACHMENT0 + numColorAttachments;
		glFramebufferTexture2D(
			GL_FRAMEBUFFER,
			attachment,
			GL_TEXTURE_2D,
			textureId,
			0
		);
		drawBuffers[numColorAttachments] = attachment;
		++numColorAttachments;
	}

	if (numColorAttachments == 0) {
		/* No color targets -> fall back */
		if (framebuffer != 0) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glDeleteFramebuffers(1, &framebuffer);
		}
		return false;
	}

	glDrawBuffers(numColorAttachments, drawBuffers);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &framebuffer);
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

	/* Bind shader pipeline */
	glBindProgramPipeline(s_shaderPipelineId);

	/* Bind sampled inputs */
	GLuint samplerBaseUnit = 0;
	GLuint boundSamplerUnits[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	int numBoundSamplerUnits = 0;
	for (int inputIndex = 0; inputIndex < pass->numInputs; ++inputIndex) {
		const PipelineResourceBinding *binding = &pass->inputs[inputIndex];
		const PipelineResource *resource = GraphicsGetPipelineResource(pipeline, binding->resourceIndex);
		PipelineRuntimeResourceState *runtimeState = GraphicsGetPipelineRuntimeResource(binding->resourceIndex);
		if (resource == NULL || runtimeState == NULL || runtimeState->initialized == false) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glDeleteFramebuffers(1, &framebuffer);
			glBindProgramPipeline(0);
			return false;
		}
		GLuint textureId = GraphicsAcquirePipelineResourceTexture(
			binding->resourceIndex,
			params->frameCount,
			binding->historyOffset
		);
		if (textureId == 0) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glDeleteFramebuffers(1, &framebuffer);
			glBindProgramPipeline(0);
			return false;
		}

		switch (binding->access) {
			case PipelineResourceAccessSampled:
			case PipelineResourceAccessHistoryRead: {
				int legacyIndex = 0;
				GLuint samplerUnit = samplerBaseUnit + numBoundSamplerUnits;
				bool enableMipmap = settings->enableMipmapGeneration;
				if (GraphicsParseLegacyResourceIndex(resource, "legacy_mrt", &legacyIndex)) {
					samplerUnit = (GLuint)legacyIndex;
					enableMipmap = settings->enableMipmapGeneration;
				} else if (GraphicsParseLegacyResourceIndex(resource, "legacy_compute", &legacyIndex)) {
					samplerUnit = COMPUTE_TEXTURE_START_INDEX + (GLuint)legacyIndex;
					enableMipmap = false;
				}
				glActiveTexture(GL_TEXTURE0 + samplerUnit);
				glBindTexture(GL_TEXTURE_2D, textureId);
				GraphicsSetTextureSampler(
					GL_TEXTURE_2D,
					resource->textureFilter,
					resource->textureWrap,
					enableMipmap
				);
				if (enableMipmap) {
					glGenerateMipmap(GL_TEXTURE_2D);
				}
				boundSamplerUnits[numBoundSamplerUnits++] = samplerUnit;
			} break;
			default: {
				/* Unsupported binding type for fragment pass */
			} break;
		}
	}

	/* Bind user textures (legacy support) */
	for (int userTextureIndex = 0; userTextureIndex < NUM_USER_TEXTURES; userTextureIndex++) {
		if (s_userTextures[userTextureIndex].id) {
			GLuint unit = USER_TEXTURE_START_INDEX + userTextureIndex;
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(
				s_userTextures[userTextureIndex].target,
				s_userTextures[userTextureIndex].id
			);
			GraphicsSetTextureSampler(
				s_userTextures[userTextureIndex].target,
				settings->textureFilter,
				settings->textureWrap,
				true
			);
		}
	}

	/* Bind sound SSBO */
	glBindBufferBase(
		GL_SHADER_STORAGE_BUFFER,
		BUFFER_INDEX_FOR_SOUND_VISUALIZER_INPUT,
		SoundGetOutputSsbo()
	);

	/* Upload uniforms */
	glUseProgram(s_fragmentShaderId);
	if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_PIPELINE_PASS_INDEX, GL_INT)) {
		glUniform1i(UNIFORM_LOCATION_PIPELINE_PASS_INDEX, s_activePipelinePassIndex);
	}
	if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_WAVE_OUT_POS, GL_INT)) {
		glUniform1i(UNIFORM_LOCATION_WAVE_OUT_POS, params->waveOutPos);
	}
	if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_FRAME_COUNT, GL_INT)) {
		glUniform1i(UNIFORM_LOCATION_FRAME_COUNT, params->frameCount);
	}
	if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_TIME, GL_FLOAT)) {
		glUniform1f(UNIFORM_LOCATION_TIME, params->time);
	}
	if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_RESO, GL_FLOAT_VEC2)) {
		glUniform2f(
			UNIFORM_LOCATION_RESO,
			(GLfloat)targetWidth,
			(GLfloat)targetHeight
		);
	}
	if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_MOUSE_POS, GL_FLOAT_VEC2)) {
		glUniform2f(
			UNIFORM_LOCATION_MOUSE_POS,
			(GLfloat)params->xMouse / (GLfloat)targetWidth,
			1.0f - (GLfloat)params->yMouse / (GLfloat)targetHeight
		);
	}
	if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_MOUSE_BUTTONS, GL_INT_VEC3)) {
		glUniform3i(
			UNIFORM_LOCATION_MOUSE_BUTTONS,
			params->mouseLButtonPressed,
			params->mouseMButtonPressed,
			params->mouseRButtonPressed
		);
	}
	if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_TAN_FOVY, GL_FLOAT)) {
		glUniform1f(UNIFORM_LOCATION_TAN_FOVY, tanf(params->fovYInRadians));
	}
	if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_CAMERA_COORD, GL_FLOAT_MAT4)) {
		glUniformMatrix4fv(
			UNIFORM_LOCATION_CAMERA_COORD,
			1,
			GL_FALSE,
			&params->mat4x4CameraInWorld[0][0]
		);
	}
	if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_PREV_CAMERA_COORD, GL_FLOAT_MAT4)) {
		glUniformMatrix4fv(
			UNIFORM_LOCATION_PREV_CAMERA_COORD,
			1,
			GL_FALSE,
			&params->mat4x4PrevCameraInWorld[0][0]
		);
	}

	/* Draw fullscreen quad */
	GLfloat vertices[] = {
		-1.0f, -1.0f,
		 1.0f, -1.0f,
		-1.0f,  1.0f,
		 1.0f,  1.0f
	};
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), vertices);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glMemoryBarrier(
			GL_TEXTURE_FETCH_BARRIER_BIT
		|	GL_FRAMEBUFFER_BARRIER_BIT
	);

	/* Cleanup */
	glDisableVertexAttribArray(0);
	glBindBufferBase(
		GL_SHADER_STORAGE_BUFFER,
		BUFFER_INDEX_FOR_SOUND_VISUALIZER_INPUT,
		0
	);

	for (int userTextureIndex = 0; userTextureIndex < NUM_USER_TEXTURES; userTextureIndex++) {
		if (s_userTextures[userTextureIndex].id) {
			GLuint unit = USER_TEXTURE_START_INDEX + userTextureIndex;
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(s_userTextures[userTextureIndex].target, 0);
		}
	}

	for (int samplerIndex = 0; samplerIndex < numBoundSamplerUnits; ++samplerIndex) {
		GLuint unit = boundSamplerUnits[samplerIndex];
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glActiveTexture(GL_TEXTURE0);

	glBindProgramPipeline(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &framebuffer);
	glViewport(0, 0, params->xReso, params->yReso);

	return true;
}

static bool GraphicsExecutePresentPassPipeline(
	const PipelineDescription *pipeline,
	const PipelinePass *pass,
	const CurrentFrameParams *params,
	const RenderSettings *settings
){
	(void)settings;

	if (pipeline == NULL || pass == NULL || params == NULL) {
		return false;
	}

	if (pass->numInputs <= 0) {
		return false;
	}

	const PipelineResourceBinding *binding = &pass->inputs[0];
	const PipelineResource *resource = GraphicsGetPipelineResource(pipeline, binding->resourceIndex);
	PipelineRuntimeResourceState *runtimeState = GraphicsGetPipelineRuntimeResource(binding->resourceIndex);
	if (resource == NULL || runtimeState == NULL || runtimeState->initialized == false) {
		return false;
	}
	GLuint textureId = GraphicsAcquirePipelineResourceTexture(
		binding->resourceIndex,
		params->frameCount,
		binding->historyOffset
	);
	if (textureId == 0) {
		return false;
	}

	int width = runtimeState->width > 0 ? runtimeState->width : params->xReso;
	int height = runtimeState->height > 0 ? runtimeState->height : params->yReso;
	if (width <= 0) width = params->xReso;
	if (height <= 0) height = params->yReso;

	GLuint readFbo = 0;
	glGenFramebuffers(1, &readFbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
	glFramebufferTexture2D(
		GL_READ_FRAMEBUFFER,
		GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D,
		textureId,
		0
	);

	if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &readFbo);
		return false;
	}

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBlitFramebuffer(
		0, 0, width, height,
		0, 0, params->xReso, params->yReso,
		GL_COLOR_BUFFER_BIT,
		GL_NEAREST
	);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &readFbo);
	glViewport(0, 0, params->xReso, params->yReso);

	return true;
}

static void GraphicsBuildLegacyPipelineDescription(
	PipelineDescription *pipeline
){
	if (pipeline == NULL) {
		return;
	}

	PipelineDescriptionInit(pipeline);

	PixelFormat pixelFormat = s_currentRenderSettings.pixelFormat;
	if (pixelFormat != PixelFormatUnorm8Rgba
	&&	pixelFormat != PixelFormatFp16Rgba
	&&	pixelFormat != PixelFormatFp32Rgba
	) {
		pixelFormat = DEFAULT_PIXEL_FORMAT;
	}

	int numRenderTargets = s_currentRenderSettings.enableMultipleRenderTargets
		? s_currentRenderSettings.numEnabledRenderTargets
		: 1;
	if (numRenderTargets <= 0) numRenderTargets = 1;
	if (numRenderTargets > NUM_RENDER_TARGETS) {
		numRenderTargets = NUM_RENDER_TARGETS;
	}

	int mrtResourceIndices[NUM_RENDER_TARGETS] = {-1, -1, -1, -1};
	for (int rtIndex = 0; rtIndex < numRenderTargets; ++rtIndex) {
		if (pipeline->numResources >= PIPELINE_MAX_RESOURCES) {
			break;
		}
		PipelineResource *mrtResource = &pipeline->resources[pipeline->numResources];
		memset(mrtResource, 0, sizeof(*mrtResource));
		char idBuffer[PIPELINE_MAX_RESOURCE_ID_LENGTH];
		_snprintf_s(idBuffer, sizeof(idBuffer), _TRUNCATE, "legacy_mrt%d", rtIndex);
		strlcpy(mrtResource->id, idBuffer, sizeof(mrtResource->id));
		mrtResource->pixelFormat = pixelFormat;
		mrtResource->resolution.mode = PipelineResolutionModeFramebuffer;
		mrtResource->historyLength = 2;
		mrtResource->textureFilter = s_currentRenderSettings.textureFilter;
		mrtResource->textureWrap = s_currentRenderSettings.textureWrap;
		mrtResourceIndices[rtIndex] = pipeline->numResources;
		pipeline->numResources++;
	}

	int computeResourceIndices[NUM_RENDER_TARGETS] = {-1, -1, -1, -1};
	if (s_computeShaderId != 0) {
		for (int rtIndex = 0; rtIndex < NUM_RENDER_TARGETS; ++rtIndex) {
			if (pipeline->numResources >= PIPELINE_MAX_RESOURCES) {
				break;
			}
			PipelineResource *computeResource = &pipeline->resources[pipeline->numResources];
			memset(computeResource, 0, sizeof(*computeResource));
			char idBuffer[PIPELINE_MAX_RESOURCE_ID_LENGTH];
			_snprintf_s(idBuffer, sizeof(idBuffer), _TRUNCATE, "legacy_compute%d", rtIndex);
			strlcpy(computeResource->id, idBuffer, sizeof(computeResource->id));
			computeResource->pixelFormat = pixelFormat;
			computeResource->resolution.mode = PipelineResolutionModeFramebuffer;
			computeResource->historyLength = 2;
			computeResource->textureFilter = TextureFilterNearest;
			computeResource->textureWrap = TextureWrapClampToEdge;
			computeResourceIndices[rtIndex] = pipeline->numResources;
			pipeline->numResources++;
		}
	}

	bool hasComputeResource = false;
	for (int rtIndex = 0; rtIndex < NUM_RENDER_TARGETS; ++rtIndex) {
		if (computeResourceIndices[rtIndex] >= 0) {
			hasComputeResource = true;
			break;
		}
	}

	if (hasComputeResource && pipeline->numPasses < PIPELINE_MAX_PASSES) {
		PipelinePass *computePass = &pipeline->passes[pipeline->numPasses++];
		memset(computePass, 0, sizeof(*computePass));
		strlcpy(computePass->name, "legacy_compute", sizeof(computePass->name));
		computePass->type = PipelinePassTypeCompute;
		for (int rtIndex = 0; rtIndex < NUM_RENDER_TARGETS; ++rtIndex) {
			int resourceIndex = computeResourceIndices[rtIndex];
			if (resourceIndex < 0) {
				continue;
			}
			if (computePass->numInputs < PIPELINE_MAX_BINDINGS_PER_PASS) {
				computePass->inputs[computePass->numInputs].resourceIndex = resourceIndex;
				computePass->inputs[computePass->numInputs].access = PipelineResourceAccessImageRead;
				computePass->inputs[computePass->numInputs].historyOffset = -1;
				computePass->numInputs++;
			}
			if (computePass->numOutputs < PIPELINE_MAX_BINDINGS_PER_PASS) {
				computePass->outputs[computePass->numOutputs].resourceIndex = resourceIndex;
				computePass->outputs[computePass->numOutputs].access = PipelineResourceAccessImageWrite;
				computePass->outputs[computePass->numOutputs].historyOffset = 0;
				computePass->numOutputs++;
			}
		}
	}

	if (pipeline->numPasses < PIPELINE_MAX_PASSES) {
		PipelinePass *fragmentPass = &pipeline->passes[pipeline->numPasses++];
		memset(fragmentPass, 0, sizeof(*fragmentPass));
		strlcpy(fragmentPass->name, "legacy_fragment", sizeof(fragmentPass->name));
		fragmentPass->type = PipelinePassTypeFragment;
		for (int rtIndex = 0; rtIndex < numRenderTargets; ++rtIndex) {
			int resourceIndex = mrtResourceIndices[rtIndex];
			if (resourceIndex < 0 || fragmentPass->numOutputs >= PIPELINE_MAX_BINDINGS_PER_PASS) {
				continue;
			}
			fragmentPass->outputs[fragmentPass->numOutputs].resourceIndex = resourceIndex;
			fragmentPass->outputs[fragmentPass->numOutputs].access = PipelineResourceAccessColorAttachment;
			fragmentPass->outputs[fragmentPass->numOutputs].historyOffset = 0;
			fragmentPass->numOutputs++;
		}
		if (s_currentRenderSettings.enableBackBuffer) {
			for (int rtIndex = 0; rtIndex < numRenderTargets; ++rtIndex) {
				int resourceIndex = mrtResourceIndices[rtIndex];
				if (resourceIndex < 0 || fragmentPass->numInputs >= PIPELINE_MAX_BINDINGS_PER_PASS) {
					continue;
				}
				fragmentPass->inputs[fragmentPass->numInputs].resourceIndex = resourceIndex;
				fragmentPass->inputs[fragmentPass->numInputs].access = PipelineResourceAccessSampled;
				fragmentPass->inputs[fragmentPass->numInputs].historyOffset = -1;
				fragmentPass->numInputs++;
			}
		}
	for (int rtIndex = 0; rtIndex < NUM_RENDER_TARGETS; ++rtIndex) {
		int resourceIndex = computeResourceIndices[rtIndex];
		if (resourceIndex < 0 || fragmentPass->numInputs >= PIPELINE_MAX_BINDINGS_PER_PASS) {
			continue;
		}
		fragmentPass->inputs[fragmentPass->numInputs].resourceIndex = resourceIndex;
		fragmentPass->inputs[fragmentPass->numInputs].access = PipelineResourceAccessSampled;
		fragmentPass->inputs[fragmentPass->numInputs].historyOffset = -1;
		fragmentPass->numInputs++;
	}
	}

	if (pipeline->numPasses < PIPELINE_MAX_PASSES) {
		PipelinePass *presentPass = &pipeline->passes[pipeline->numPasses++];
		memset(presentPass, 0, sizeof(*presentPass));
		strlcpy(presentPass->name, "legacy_present", sizeof(presentPass->name));
		presentPass->type = PipelinePassTypePresent;
		if (mrtResourceIndices[0] >= 0) {
			presentPass->inputs[presentPass->numInputs].resourceIndex = mrtResourceIndices[0];
			presentPass->inputs[presentPass->numInputs].access = PipelineResourceAccessSampled;
			presentPass->inputs[presentPass->numInputs].historyOffset = 0;
			presentPass->numInputs++;
		}
	}
}

static const PipelineDescription *GraphicsResolvePipelineDescription(){
	if (s_pipelineHasCustomDescription) {
		if (s_pipelineDescription.numPasses == 0) {
			s_pipelineHasCustomDescription = false;
			GraphicsBuildLegacyPipelineDescription(&s_pipelineDescription);
		}
		return &s_pipelineDescription;
	}

	GraphicsBuildLegacyPipelineDescription(&s_pipelineDescription);
	return &s_pipelineDescription;
}

static void GraphicsExecutePipeline(
	const PipelineDescription *pipeline,
	const CurrentFrameParams *params,
	const RenderSettings *settings
){
	if (pipeline == NULL) {
		return;
	}

	GraphicsEnsurePipelineResources(pipeline, params);

	for (int passIndex = 0; passIndex < pipeline->numPasses; ++passIndex) {
		const PipelinePass *pass = &pipeline->passes[passIndex];
		s_activePipelinePassIndex = passIndex;
		switch (pass->type) {
			case PipelinePassTypeCompute: {
				if (!GraphicsExecuteComputePassPipeline(pipeline, pass, params, settings)) {
					GraphicsDispatchCompute(params, settings);
				}
			} break;
			case PipelinePassTypeFragment: {
				if (!GraphicsExecuteFragmentPassPipeline(pipeline, pass, params, settings)) {
					GraphicsDrawFullScreenQuad(
						0,
						params,
						settings
					);
				}
			} break;
			case PipelinePassTypePresent: {
				if (!GraphicsExecutePresentPassPipeline(pipeline, pass, params, settings)) {
					/* Present fallback: draw fullscreen quad to default framebuffer */
					GraphicsDrawFullScreenQuad(
						0,
						params,
						settings
					);
				}
			} break;
			default: {
				/* 未対応のパスはスキップ */
			} break;
		}
		s_activePipelinePassIndex = -1;
	}
	s_activePipelinePassIndex = -1;
}

static void GraphicsDeletePipelineRuntimeResource(
	PipelineRuntimeResourceState *state
){
	if (state == NULL) return;
	if (state->initialized == false) return;

	for (int historyIndex = 0; historyIndex < state->historyLength; ++historyIndex) {
		GLuint textureId = state->textureIds[historyIndex];
		if (textureId != 0) {
			glDeleteTextures(
				/* GLsizei n */				1,
				/* const GLuint *textures */	&state->textureIds[historyIndex]
			);
			state->textureIds[historyIndex] = 0;
		}
	}
	state->initialized = false;
	state->width = 0;
	state->height = 0;
	state->pixelFormat = (PixelFormat)0;
	state->historyLength = 0;
}

static void GraphicsResetPipelineRuntimeResources(){
	for (int resourceIndex = 0; resourceIndex < PIPELINE_MAX_RESOURCES; ++resourceIndex) {
		GraphicsDeletePipelineRuntimeResource(&s_pipelineRuntimeResources[resourceIndex]);
	}
}

static void GraphicsResolveResourceDimensions(
	const PipelineResource *resource,
	const CurrentFrameParams *params,
	int *outWidth,
	int *outHeight
){
	int width = params->xReso;
	int height = params->yReso;

	if (resource != NULL) {
		switch (resource->resolution.mode) {
			case PipelineResolutionModeFixed: {
				if (resource->resolution.width > 0) {
					width = resource->resolution.width;
				}
				if (resource->resolution.height > 0) {
					height = resource->resolution.height;
				}
			} break;
			case PipelineResolutionModeFramebuffer:
			default: {
				width = params->xReso;
				height = params->yReso;
			} break;
		}
	}

	if (width <= 0) width = params->xReso;
	if (height <= 0) height = params->yReso;
	if (width <= 0) width = 1;
	if (height <= 0) height = 1;

	if (outWidth)  *outWidth = width;
	if (outHeight) *outHeight = height;
}

static void GraphicsCreateOrResizePipelineResource(
	PipelineRuntimeResourceState *state,
	const PipelineResource *resource,
	int width,
	int height
){
	if (state == NULL || resource == NULL) {
		return;
	}

	bool needsRecreate = false;

	if (state->initialized == false) {
		needsRecreate = true;
	} else if (state->width != width
	|| state->height != height
	|| state->pixelFormat != resource->pixelFormat
	|| state->historyLength != resource->historyLength
	) {
		needsRecreate = true;
	}

	if (needsRecreate) {
		GraphicsDeletePipelineRuntimeResource(state);
	}

	if (state->initialized == false) {
		int historyLength = resource->historyLength;
		if (historyLength <= 0) {
			historyLength = 1;
		}
		if (historyLength > PIPELINE_MAX_HISTORY_LENGTH) {
			historyLength = PIPELINE_MAX_HISTORY_LENGTH;
		}

		GlPixelFormatInfo pixelFormatInfo = PixelFormatToGlPixelFormatInfo(resource->pixelFormat);
		memset(state->textureIds, 0, sizeof(state->textureIds));
		glGenTextures(
			/* GLsizei n */				historyLength,
			/* GLuint *textures */		state->textureIds
		);

		for (int historyIndex = 0; historyIndex < historyLength; ++historyIndex) {
			GLuint textureId = state->textureIds[historyIndex];
			glBindTexture(
				/* GLenum target */		GL_TEXTURE_2D,
				/* GLuint texture */	textureId
			);
			glTexImage2D(
				/* GLenum target */			GL_TEXTURE_2D,
				/* GLint level */			0,
				/* GLint internalformat */	pixelFormatInfo.internalformat,
				/* GLsizei width */			width,
				/* GLsizei height */		height,
				/* GLint border */			0,
				/* GLenum format */			pixelFormatInfo.format,
				/* GLenum type */			pixelFormatInfo.type,
				/* const void * data */		NULL
			);
			GraphicsSetTextureSampler(
				GL_TEXTURE_2D,
				resource->textureFilter,
				resource->textureWrap,
				false
			);
		}
		glBindTexture(
			/* GLenum target */		GL_TEXTURE_2D,
			/* GLuint texture */	0
		);

		state->initialized = true;
		state->width = width;
		state->height = height;
		state->pixelFormat = resource->pixelFormat;
		state->historyLength = historyLength;
		for (int historyIndex = historyLength; historyIndex < PIPELINE_MAX_HISTORY_LENGTH; ++historyIndex) {
			state->textureIds[historyIndex] = 0;
		}
	} else {
		for (int historyIndex = 0; historyIndex < state->historyLength; ++historyIndex) {
			GLuint textureId = state->textureIds[historyIndex];
			glBindTexture(GL_TEXTURE_2D, textureId);
			GraphicsSetTextureSampler(
				GL_TEXTURE_2D,
				resource->textureFilter,
				resource->textureWrap,
				false
			);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

static void GraphicsEnsurePipelineResources(
	const PipelineDescription *pipeline,
	const CurrentFrameParams *params
){
	if (pipeline == NULL) return;
	if (params == NULL) return;

	for (int resourceIndex = 0; resourceIndex < pipeline->numResources; ++resourceIndex) {
		const PipelineResource *resource = &pipeline->resources[resourceIndex];
		PipelineRuntimeResourceState *state = &s_pipelineRuntimeResources[resourceIndex];
		int width = 0;
		int height = 0;
		GraphicsResolveResourceDimensions(
			resource,
			params,
			&width,
			&height
		);
		GraphicsCreateOrResizePipelineResource(
			state,
			resource,
			width,
			height
		);
	}
}

static GLuint GraphicsAcquirePipelineResourceTexture(
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

	int historyLength = state->historyLength;
	int baseIndex = historyLength > 0 ? (frameCount % historyLength) : 0;
	if (baseIndex < 0) {
		baseIndex += historyLength;
	}
	int resolvedIndex = baseIndex + historyOffset;
	while (resolvedIndex < 0) {
		resolvedIndex += historyLength;
	}
	if (historyLength > 0) {
		resolvedIndex %= historyLength;
	}
	if (resolvedIndex < 0 || resolvedIndex >= historyLength) {
		resolvedIndex = baseIndex;
	}

	return state->textureIds[resolvedIndex];
}

static const PipelineResource *GraphicsGetPipelineResource(
	const PipelineDescription *pipeline,
	int resourceIndex
){
	if (pipeline == NULL) return NULL;
	if (resourceIndex < 0 || resourceIndex >= pipeline->numResources) {
		return NULL;
	}
	return &pipeline->resources[resourceIndex];
}

static PipelineRuntimeResourceState *GraphicsGetPipelineRuntimeResource(
	int resourceIndex
){
	if (resourceIndex < 0 || resourceIndex >= PIPELINE_MAX_RESOURCES) {
		return NULL;
	}
	return &s_pipelineRuntimeResources[resourceIndex];
}

static bool GraphicsParseLegacyResourceIndex(
	const PipelineResource *resource,
	const char *prefix,
	int *outIndex
){
	if (resource == NULL || prefix == NULL) return false;
	size_t prefixLength = strlen(prefix);
	if (strncmp(resource->id, prefix, prefixLength) != 0) {
		return false;
	}
	int index = 0;
	const char *suffix = resource->id + prefixLength;
	if (*suffix != '\0') {
		index = atoi(suffix);
	}
	if (outIndex) {
		*outIndex = index;
	}
	return true;
}

static void GraphicsSynchronizeRenderSettings(
	const CurrentFrameParams *params,
	const RenderSettings *settings
){
	bool resolutionChanged = (s_xReso != params->xReso) || (s_yReso != params->yReso);
	bool renderSettingsChanged = (memcmp(&s_currentRenderSettings, settings, sizeof(RenderSettings)) != 0);

	if (resolutionChanged || renderSettingsChanged) {
		s_xReso = params->xReso;
		s_yReso = params->yReso;
		s_currentRenderSettings = *settings;

		GraphicsResetPipelineRuntimeResources();

		GraphicsDeleteFrameBuffer();
		GraphicsDeleteComputeTextures();
		GraphicsCreateFrameBuffer(params->xReso, params->yReso, settings);
		GraphicsCreateComputeTextures(params->xReso, params->yReso, settings);
	}
}

static bool GraphicsExecuteComputePassPipeline(
	const PipelineDescription *pipeline,
	const PipelinePass *pass,
	const CurrentFrameParams *params,
	const RenderSettings *settings
){
	if (pipeline == NULL || pass == NULL || params == NULL || settings == NULL) {
		return false;
	}
	if (s_computeShaderId == 0) {
		return false;
	}

	GlPixelFormatInfo defaultPixelFormatInfo = PixelFormatToGlPixelFormatInfo(settings->pixelFormat);

	GLuint samplerUnitBase = COMPUTE_TEXTURE_START_INDEX;
	GLuint samplerUnitCount = 0;
	GLint imageReadUnit = 0;
	GLint imageWriteUnit = NUM_RENDER_TARGETS;
	GLuint boundSamplerUnits[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	int numBoundSamplerUnits = 0;
	GLuint boundImageUnits[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};
	int numBoundImageUnits = 0;
	GLenum boundImageAccess[PIPELINE_MAX_BINDINGS_PER_PASS] = {0};

	/* Bind inputs */
	for (int inputIndex = 0; inputIndex < pass->numInputs; ++inputIndex) {
		const PipelineResourceBinding *binding = &pass->inputs[inputIndex];
		const PipelineResource *resource = GraphicsGetPipelineResource(pipeline, binding->resourceIndex);
		PipelineRuntimeResourceState *runtimeState = GraphicsGetPipelineRuntimeResource(binding->resourceIndex);
		if (resource == NULL || runtimeState == NULL || runtimeState->initialized == false) {
			return false;
		}
		GLuint textureId = GraphicsAcquirePipelineResourceTexture(
			binding->resourceIndex,
			params->frameCount,
			binding->historyOffset
		);
		if (textureId == 0) {
			return false;
		}

		GlPixelFormatInfo pixelFormatInfo = PixelFormatToGlPixelFormatInfo(resource->pixelFormat);

		switch (binding->access) {
			case PipelineResourceAccessSampled:
			case PipelineResourceAccessHistoryRead: {
				GLuint samplerUnit = samplerUnitBase + samplerUnitCount;
				glActiveTexture(GL_TEXTURE0 + samplerUnit);
				glBindTexture(GL_TEXTURE_2D, textureId);
				GraphicsSetTextureSampler(
					GL_TEXTURE_2D,
					resource->textureFilter,
					resource->textureWrap,
					false
				);
				boundSamplerUnits[numBoundSamplerUnits++] = samplerUnit;
				++samplerUnitCount;
			} break;
			case PipelineResourceAccessImageRead: {
				GLenum internalformat = pixelFormatInfo.internalformat != 0 ? pixelFormatInfo.internalformat : defaultPixelFormatInfo.internalformat;
				glBindImageTexture(
					imageReadUnit,
					textureId,
					0,
					GL_FALSE,
					0,
					GL_READ_ONLY,
					internalformat
				);
				boundImageUnits[numBoundImageUnits] = imageReadUnit;
				boundImageAccess[numBoundImageUnits] = GL_READ_ONLY;
				++numBoundImageUnits;
				++imageReadUnit;
			} break;
			default: {
				/* Unsupported binding type for compute pass */
				return false;
			} break;
		}
	}

	/* Bind outputs */
	bool hasWritableOutput = false;
	for (int outputIndex = 0; outputIndex < pass->numOutputs; ++outputIndex) {
		const PipelineResourceBinding *binding = &pass->outputs[outputIndex];
		const PipelineResource *resource = GraphicsGetPipelineResource(pipeline, binding->resourceIndex);
		PipelineRuntimeResourceState *runtimeState = GraphicsGetPipelineRuntimeResource(binding->resourceIndex);
		if (resource == NULL || runtimeState == NULL || runtimeState->initialized == false) {
			return false;
		}
		GLuint textureId = GraphicsAcquirePipelineResourceTexture(
			binding->resourceIndex,
			params->frameCount,
			binding->historyOffset
		);
		if (textureId == 0) {
			return false;
		}
		GlPixelFormatInfo pixelFormatInfo = PixelFormatToGlPixelFormatInfo(resource->pixelFormat);
		GLenum internalformat = pixelFormatInfo.internalformat != 0 ? pixelFormatInfo.internalformat : defaultPixelFormatInfo.internalformat;

		switch (binding->access) {
			case PipelineResourceAccessImageWrite: {
				glBindImageTexture(
					imageWriteUnit,
					textureId,
					0,
					GL_FALSE,
					0,
					GL_WRITE_ONLY,
					internalformat
				);
				boundImageUnits[numBoundImageUnits] = imageWriteUnit;
				boundImageAccess[numBoundImageUnits] = GL_WRITE_ONLY;
				++numBoundImageUnits;
				++imageWriteUnit;
				hasWritableOutput = true;
			} break;
			default: {
				/* Unsupported output access for compute */
				return false;
			} break;
		}
	}

	if (hasWritableOutput == false) {
		/* Nothing useful to write; fall back */
		return false;
	}

	glUseProgram(s_computeShaderId);
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_PIPELINE_PASS_INDEX, GL_INT)) {
		glUniform1i(UNIFORM_LOCATION_PIPELINE_PASS_INDEX, s_activePipelinePassIndex);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_WAVE_OUT_POS, GL_INT)) {
		glUniform1i(UNIFORM_LOCATION_WAVE_OUT_POS, params->waveOutPos);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_FRAME_COUNT, GL_INT)) {
		glUniform1i(UNIFORM_LOCATION_FRAME_COUNT, params->frameCount);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_TIME, GL_FLOAT)) {
		glUniform1f(UNIFORM_LOCATION_TIME, params->time);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_RESO, GL_FLOAT_VEC2)) {
		glUniform2f(
			UNIFORM_LOCATION_RESO,
			(GLfloat)params->xReso,
			(GLfloat)params->yReso
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_MOUSE_POS, GL_FLOAT_VEC2)) {
		glUniform2f(
			UNIFORM_LOCATION_MOUSE_POS,
			(GLfloat)params->xMouse / (GLfloat)params->xReso,
			1.0f - (GLfloat)params->yMouse / (GLfloat)params->yReso
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_MOUSE_BUTTONS, GL_INT_VEC3)) {
		glUniform3i(
			UNIFORM_LOCATION_MOUSE_BUTTONS,
			params->mouseLButtonPressed,
			params->mouseMButtonPressed,
			params->mouseRButtonPressed
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_TAN_FOVY, GL_FLOAT)) {
		glUniform1f(UNIFORM_LOCATION_TAN_FOVY, tanf(params->fovYInRadians));
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_CAMERA_COORD, GL_FLOAT_MAT4)) {
		glUniformMatrix4fv(
			UNIFORM_LOCATION_CAMERA_COORD,
			1,
			GL_FALSE,
			&params->mat4x4CameraInWorld[0][0]
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_PREV_CAMERA_COORD, GL_FLOAT_MAT4)) {
		glUniformMatrix4fv(
			UNIFORM_LOCATION_PREV_CAMERA_COORD,
			1,
			GL_FALSE,
			&params->mat4x4PrevCameraInWorld[0][0]
		);
	}

	GLuint workGroupSizeX = (GLuint)(s_computeWorkGroupSize[0] > 0? s_computeWorkGroupSize[0]: 1);
	GLuint workGroupSizeY = (GLuint)(s_computeWorkGroupSize[1] > 0? s_computeWorkGroupSize[1]: 1);
	GLuint workGroupSizeZ = (GLuint)(s_computeWorkGroupSize[2] > 0? s_computeWorkGroupSize[2]: 1);
	if (pass->overrideWorkGroupSize) {
		if (pass->workGroupSize[0] > 0) workGroupSizeX = pass->workGroupSize[0];
		if (pass->workGroupSize[1] > 0) workGroupSizeY = pass->workGroupSize[1];
		if (pass->workGroupSize[2] > 0) workGroupSizeZ = pass->workGroupSize[2];
	}

	GLuint numGroupsX = (GLuint)((params->xReso + workGroupSizeX - 1) / workGroupSizeX);
	GLuint numGroupsY = (GLuint)((params->yReso + workGroupSizeY - 1) / workGroupSizeY);
	GLuint numGroupsZ = (GLuint)((1 + workGroupSizeZ - 1) / workGroupSizeZ);
	if (numGroupsX == 0) numGroupsX = 1;
	if (numGroupsY == 0) numGroupsY = 1;
	if (numGroupsZ == 0) numGroupsZ = 1;

	glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);

	glMemoryBarrier(
			GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
		|	GL_TEXTURE_FETCH_BARRIER_BIT
		|	GL_TEXTURE_UPDATE_BARRIER_BIT
	);

	/* Unbind image units */
	for (int index = 0; index < numBoundImageUnits; ++index) {
		GLuint unit = boundImageUnits[index];
		GLenum access = boundImageAccess[index];
		glBindImageTexture(
			unit,
			0,
			0,
			GL_FALSE,
			0,
			access,
			defaultPixelFormatInfo.internalformat != 0 ? defaultPixelFormatInfo.internalformat : GL_RGBA8
		);
	}

	/* Unbind textures */
	for (int index = 0; index < numBoundSamplerUnits; ++index) {
		GLuint samplerUnit = boundSamplerUnits[index];
		glActiveTexture(GL_TEXTURE0 + samplerUnit);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glActiveTexture(GL_TEXTURE0);

	return true;
}
void GraphicsResetPipelineDescriptionToDefault(){
	GraphicsResetPipelineRuntimeResources();
	PipelineDescriptionInit(&s_pipelineDescription);
	s_pipelineHasCustomDescription = false;
}

bool GraphicsApplyPipelineDescription(const PipelineDescription *pipeline){
	if (pipeline == NULL) {
		GraphicsResetPipelineDescriptionToDefault();
		return true;
	}
	GraphicsResetPipelineRuntimeResources();
	memcpy(&s_pipelineDescription, pipeline, sizeof(PipelineDescription));
	s_pipelineHasCustomDescription = true;
	return true;
}

bool GraphicsHasCustomPipelineDescription(){
	return s_pipelineHasCustomDescription;
}

const PipelineDescription *GraphicsGetActivePipelineDescription(){
	return GraphicsResolvePipelineDescription();
}


static void GraphicsCreateFrameBuffer(
	int xReso,
	int yReso,
	const RenderSettings *settings
){
	GlPixelFormatInfo pixelFormatInfo = PixelFormatToGlPixelFormatInfo(settings->pixelFormat);

	/* MRT フレームバッファ作成 */
	glGenFramebuffers(
		/* GLsizei n */		1,
	 	/* GLuint *ids */	&s_mrtFrameBuffer
	);

	for (int doubleBufferIndex = 0; doubleBufferIndex < 2; doubleBufferIndex++) {
		/* テクスチャ作成 */
		glGenTextures(
			/* GLsizei n */				NUM_RENDER_TARGETS,
			/* GLuint * textures */		s_mrtTextures[doubleBufferIndex]
		);

		/* レンダーターゲットの巡回 */
		for (int renderTargetIndex = 0; renderTargetIndex < NUM_RENDER_TARGETS; renderTargetIndex++) {
			glBindTexture(
				/* GLenum target */		GL_TEXTURE_2D,
				/* GLuint texture */	s_mrtTextures[doubleBufferIndex][renderTargetIndex]
			);

			/* テクスチャリソースを生成 */
			glTexImage2D(
				/* GLenum target */			GL_TEXTURE_2D,
				/* GLint level */			0,
				/* GLint internalformat */	pixelFormatInfo.internalformat,
				/* GLsizei width */			xReso,
				/* GLsizei height */		yReso,
				/* GLint border */			0,
				/* GLenum format */			pixelFormatInfo.format,
				/* GLenum type */			pixelFormatInfo.type,
				/* const void * data */		NULL
			);
		}
	}
}

static void GraphicsDeleteComputeTextures(
){
	for (int doubleBufferIndex = 0; doubleBufferIndex < 2; ++doubleBufferIndex) {
		for (int renderTargetIndex = 0; renderTargetIndex < NUM_RENDER_TARGETS; ++renderTargetIndex) {
			GLuint textureId = s_computeTextures[doubleBufferIndex][renderTargetIndex];
			if (textureId != 0) {
				glActiveTexture(GL_TEXTURE0 + COMPUTE_TEXTURE_START_INDEX + renderTargetIndex);
				glBindTexture(
					/* GLenum target */		GL_TEXTURE_2D,
					/* GLuint texture */	0	/* unbind */
				);
				glDeleteTextures(
					/* GLsizei n */					1,
					/* const GLuint * textures */	&s_computeTextures[doubleBufferIndex][renderTargetIndex]
				);
				s_computeTextures[doubleBufferIndex][renderTargetIndex] = 0;
			}
		}
	}
	GlPixelFormatInfo pixelFormatInfo = PixelFormatToGlPixelFormatInfo(s_currentRenderSettings.pixelFormat);
	for (int imageUnit = 0; imageUnit < NUM_RENDER_TARGETS * 2; ++imageUnit) {
		glBindImageTexture(
			/* GLuint unit */			imageUnit,
			/* GLuint texture */		0,
			/* GLint level */			0,
			/* GLboolean layered */		GL_FALSE,
			/* GLint layer */			0,
			/* GLenum access */			GL_READ_ONLY,
			/* GLenum format */			pixelFormatInfo.internalformat
		);
	}
	glActiveTexture(GL_TEXTURE0);
}

static void GraphicsCreateComputeTextures(
	int xReso,
	int yReso,
	const RenderSettings *settings
){
	GraphicsDeleteComputeTextures();

	GlPixelFormatInfo pixelFormatInfo = PixelFormatToGlPixelFormatInfo(settings->pixelFormat);

	for (int doubleBufferIndex = 0; doubleBufferIndex < 2; ++doubleBufferIndex) {
		glGenTextures(
			/* GLsizei n */				NUM_RENDER_TARGETS,
			/* GLuint * textures */		s_computeTextures[doubleBufferIndex]
		);
		for (int renderTargetIndex = 0; renderTargetIndex < NUM_RENDER_TARGETS; ++renderTargetIndex) {
			glBindTexture(
				/* GLenum target */		GL_TEXTURE_2D,
				/* GLuint texture */	s_computeTextures[doubleBufferIndex][renderTargetIndex]
			);
			glTexImage2D(
				/* GLenum target */			GL_TEXTURE_2D,
				/* GLint level */			0,
				/* GLint internalformat */	pixelFormatInfo.internalformat,
				/* GLsizei width */			xReso,
				/* GLsizei height */		yReso,
				/* GLint border */			0,
				/* GLenum format */			pixelFormatInfo.format,
				/* GLenum type */			pixelFormatInfo.type,
				/* const void * data */		NULL
			);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
	}
	glBindTexture(
		/* GLenum target */		GL_TEXTURE_2D,
		/* GLuint texture */	0	/* unbind */
	);
}

static void GraphicsDeleteFrameBuffer(
){
	/* フレームバッファアンバインド */
	glBindFramebuffer(
		/* GLenum target */			GL_FRAMEBUFFER,
		/* GLuint framebuffer */	0	/* unbind */
	);

	/* MRT フレームバッファ削除 */
	glDeleteFramebuffers(
		/* GLsizei n */				1,
	 	/* GLuint *ids */			&s_mrtFrameBuffer
	);

	for (int doubleBufferIndex = 0; doubleBufferIndex < 2; doubleBufferIndex++) {
		/* MRT テクスチャ削除 */
		for (int renderTargetIndex = 0; renderTargetIndex < NUM_RENDER_TARGETS; renderTargetIndex++) {
			/* テクスチャアンバインド */
			glActiveTexture(GL_TEXTURE0 + renderTargetIndex);
			glBindTexture(
				/* GLenum target */		GL_TEXTURE_2D,
				/* GLuint texture */	0	/* unbind */
			);
		}

		/* テクスチャ削除 */
		glDeleteTextures(
			/* GLsizei n */			NUM_RENDER_TARGETS,
			/* GLuint * textures */	s_mrtTextures[doubleBufferIndex]
		);
	}
}

void GraphicsClearAllRenderTargets(){
	GraphicsDeleteFrameBuffer();
	GraphicsDeleteComputeTextures();
	GraphicsCreateFrameBuffer(s_xReso, s_yReso, &s_currentRenderSettings);
	GraphicsCreateComputeTextures(s_xReso, s_yReso, &s_currentRenderSettings);
	GraphicsResetPipelineRuntimeResources();
}

bool GraphicsShaderRequiresFrameCountUniform(){
	if (s_fragmentShaderId != 0) {
		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_FRAME_COUNT, GL_INT)) {
			return true;
		}
	}
	if (s_computeShaderId != 0) {
		if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_FRAME_COUNT, GL_INT)) {
			return true;
		}
	}
	return false;
}

bool GraphicsShaderRequiresCameraControlUniforms(){
	if (s_fragmentShaderId != 0) {
		if (
			ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_TAN_FOVY, GL_FLOAT)
		||	ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_CAMERA_COORD, GL_FLOAT_MAT4)
		) {
			return true;
		}
	}
	if (s_computeShaderId != 0) {
		if (
			ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_TAN_FOVY, GL_FLOAT)
		||	ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_CAMERA_COORD, GL_FLOAT_MAT4)
		) {
			return true;
		}
	}
	return false;
}


static bool GraphicsLoadUserTextureSubAsPng(
	const char *fileName,
	int userTextureIndex
){
	/* png ファイルの読み込み */
	void *data = NULL;
	int numComponents = 0;
	int width = 0;
	int height = 0;
	bool ret = ReadImageFileAsPng(
		/* const char *fileName */	fileName,
		/* void **dataRet */		&data,
		/* int *numComponentsRet */	&numComponents,
		/* int *widthRet */			&width,
		/* int *heightRet */		&height,
		/* bool verticalFlip */		false
	);
	if (ret == false) return false;

	/* target を決定 */
	s_userTextures[userTextureIndex].target = GL_TEXTURE_2D;

	/* テクスチャのバインド */
	glBindTexture(
		/* GLenum target */			GL_TEXTURE_2D,
		/* GLuint texture */		s_userTextures[userTextureIndex].id
	);

	/* テクスチャの設定 */
	GLint internalformat = 0;
	switch (numComponents) {
		case 1: {
			internalformat = GL_RED;
		} break;
		case 2: {
			internalformat = GL_RG;
		} break;
		case 3: {
			internalformat = GL_RGB;
		} break;
		case 4: {
			internalformat = GL_RGBA;
		} break;
	}
	if (internalformat != 0) {
		glTexImage2D(
			/* GLenum target */			GL_TEXTURE_2D,
			/* GLint level */			0,
			/* GLint internalformat */	internalformat,
			/* GLsizei width */			width,
			/* GLsizei height */		height,
			/* GLint border */			0,
			/* GLenum format */			internalformat,
			/* GLenum type */			GL_UNSIGNED_BYTE,
			/* const void * data */		data
		);

		/* 常にミップマップ生成 */
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	/* テクスチャのアンバインド */
	glBindTexture(
		/* GLenum target */			GL_TEXTURE_2D,
		/* GLuint texture */		0
	);

	/* 画像データの破棄 */
	free(data);

	return true;
}


static bool GraphicsLoadUserTextureSubAsDds(
	const char *fileName,
	int userTextureIndex
){
	/* dds ファイルの読み込み */
	size_t ddsFileSizeInBytes;
	void *ddsFileImage = MallocReadFile(fileName, &ddsFileSizeInBytes);

	/* dds ファイルのパース */
	DdsParser parser;
	if (DdsParser_Initialize(&parser, ddsFileImage, (int)ddsFileSizeInBytes) == false) {
		free(ddsFileImage);
		return false;
	}

	/* DxgiFormat から OpenGL のピクセルフォーマット情報に変換 */
	GlPixelFormatInfo glPixelFormatInfo = DxgiFormatToGlPixelFormatInfo(parser.info.dxgiFormat);
	if (glPixelFormatInfo.internalformat == 0) {
		free(ddsFileImage);
		return false;
	}

	/* パース結果の確認 */
	printf(
		"\n"
		"DdsParser\n"
		"	dxgiFormat      %d\n"
		"	numBitsPerPixel %d\n"
		"	width           %d\n"
		"	height          %d\n"
		"	depth           %d\n"
		"	arraySize       %d\n"
		"	hasCubemap      %d\n"
		"	numMips         %d\n"
		"	blockCompressed %d\n",
		parser.info.dxgiFormat,
		parser.info.numBitsPerPixel,
		parser.info.width,
		parser.info.height,
		parser.info.depth,
		parser.info.arraySize,
		parser.info.hasCubemap,
		parser.info.numMips,
		parser.info.blockCompressed
	);

	/* 対応していない形式ならエラー */
	if (
		parser.info.arraySize != 1
	) {
		free(ddsFileImage);
		return false;
	}

	/* GL_TEXTURE の種類を決定 */
	int glTextureType = (parser.info.depth == 1)? GL_TEXTURE_2D: GL_TEXTURE_3D;

	/* face 数を決定（デフォルトで 1、キューブマップで 6）*/
	int numFace = 1;
	GLenum targetFace = glTextureType;
	s_userTextures[userTextureIndex].target = glTextureType;
	if (parser.info.hasCubemap) {
		numFace = 6;
		targetFace = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
		s_userTextures[userTextureIndex].target = GL_TEXTURE_CUBE_MAP;
	}

	/* テクスチャのバインド */
	glBindTexture(
		/* GLenum target */		s_userTextures[userTextureIndex].target,
		/* GLuint texture */	s_userTextures[userTextureIndex].id
	);

	/* ミップレベルの巡回 */
	for (int faceIndex = 0; faceIndex < numFace; faceIndex++) {
		for (int mipLevel = 0; mipLevel < parser.info.numMips; mipLevel++) {
			DdsSubData subData;
			DdsParser_GetSubData(&parser, 0, faceIndex, mipLevel, &subData);

			if (parser.info.blockCompressed) {
				if (parser.info.depth == 1) {
					CheckGlError("pre glCompressedTexImage2D");
					glCompressedTexImage2D(
						/* GLenum target */			targetFace + faceIndex,
						/* GLint level */			mipLevel,
						/* GLenum internalformat */	glPixelFormatInfo.internalformat,
						/* GLsizei width */			subData.width,
						/* GLsizei height */		subData.height,
						/* GLint border */			false,
						/* GLsizei imageSize */		(GLsizei)subData.sizeInBytes,
						/* const void * data */		subData.buff
					);
					CheckGlError("post glCompressedTexImage2D");
				} else {
					CheckGlError("pre glCompressedTexImage3D");
					glCompressedTexImage3D(
						/* GLenum target */			targetFace + faceIndex,
						/* GLint level */			mipLevel,
						/* GLenum internalformat */	glPixelFormatInfo.internalformat,
						/* GLsizei width */			subData.width,
						/* GLsizei height */		subData.height,
						/* GLsizei depth */			subData.depth,
						/* GLint border */			false,
						/* GLsizei imageSize */		(GLsizei)subData.sizeInBytes,
						/* const void *data */		subData.buff
					);
					CheckGlError("post glCompressedTexImage3D");
				}
			} else {
				if (parser.info.depth == 1) {
					CheckGlError("pre glTexImage2D");
					glTexImage2D(
						/* GLenum target */			targetFace + faceIndex,
						/* GLint level */			mipLevel,
						/* GLint internalformat */	glPixelFormatInfo.internalformat,
						/* GLsizei width */			subData.width,
						/* GLsizei height */		subData.height,
						/* GLint border */			false,
						/* GLenum format */			glPixelFormatInfo.format,
						/* GLenum type */			glPixelFormatInfo.type,
						/* const void * data */		subData.buff
					);
					CheckGlError("post glTexImage2D");
				} else {
					CheckGlError("pre glTexImage3D");
					glTexImage3D(
						/* GLenum target */			targetFace + faceIndex,
						/* GLint level */			mipLevel,
						/* GLint internalformat */	glPixelFormatInfo.internalformat,
						/* GLsizei width */			subData.width,
						/* GLsizei height */		subData.height,
						/* GLsizei depth */			subData.depth,
						/* GLint border */			false,
						/* GLenum format */			glPixelFormatInfo.format,
						/* GLenum type */			glPixelFormatInfo.type,
						/* const void * data */		subData.buff
					);
					CheckGlError("post glTexImage3D");
				}
			}
		}
	}

	/* ミップ数上限の設定（これによりミップマップが有効化される）*/
	glTexParameteri(
		/* GLenum target */	s_userTextures[userTextureIndex].target,
		/* GLenum pname */	GL_TEXTURE_MAX_LEVEL,
		/* GLint param */	parser.info.numMips - 1
	);

	/*
		DDS ファイルの場合ミップマップ情報はファイルに含まれている。
		自動生成してはいけない。
	*/

	/* テクスチャのアンバインド */
	glBindTexture(
		/* GLenum target */		s_userTextures[userTextureIndex].target,
		/* GLuint texture */	0
	);

	/* dds ファイルイメージの破棄 */
	free(ddsFileImage);

	return true;
}


bool GraphicsLoadUserTexture(
	const char *fileName,
	int userTextureIndex
){
	/* エラーチェック */
	if (userTextureIndex < 0 || NUM_USER_TEXTURES <= userTextureIndex) return false;

	/* 既存のテクスチャがあるなら破棄 */
	if (s_userTextures[userTextureIndex].id != 0) {
		glDeleteTextures(
			/* GLsizei n */					1,
			/* const GLuint * textures */	&s_userTextures[userTextureIndex].id
		);
		s_userTextures[userTextureIndex].id = 0;
	}

	/* テクスチャ作成 */
	glGenTextures(
		/* GLsizei n */				1,
		/* GLuint * textures */		&s_userTextures[userTextureIndex].id
	);

	/* 画像ファイルの読み込み */
	bool succeeded = false;
	if (GraphicsLoadUserTextureSubAsPng(fileName, userTextureIndex)) {
		succeeded = true;
	} else
	if (GraphicsLoadUserTextureSubAsDds(fileName, userTextureIndex)) {
		succeeded = true;
	}

	return succeeded;
}

bool GraphicsDeleteUserTexture(
	int userTextureIndex
){
	/* エラーチェック */
	if (userTextureIndex < 0 || NUM_USER_TEXTURES <= userTextureIndex) return false;

	/* 既存のテクスチャがあるなら破棄 */
	if (s_userTextures[userTextureIndex].id != 0) {
		glDeleteTextures(
			/* GLsizei n */					1,
			/* const GLuint * textures */	&s_userTextures[userTextureIndex].id
		);
		s_userTextures[userTextureIndex].id = 0;
	}

	return true;
}

bool GraphicsCreateVertexShader(
	const char *shaderCode
){
	printf("setup the vertex shader ...\n");
	const GLchar *(strings[]) = {
		SkipBomConst(shaderCode)
	};
	assert(s_vertexShaderId == 0);
	s_vertexShaderId = CreateShader(GL_VERTEX_SHADER, SIZE_OF_ARRAY(strings), strings);
	if (s_vertexShaderId == 0) {
		printf("setup the vertex shader ... fialed.\n");
		return false;
	}
	DumpShaderInterfaces(s_vertexShaderId);
	printf("setup the vertex shader ... done.\n");

	return true;
}

bool GraphicsDeleteVertexShader(
){
	if (s_vertexShaderId == 0) return false;
	glFinish();
	glDeleteProgram(s_vertexShaderId);
	s_vertexShaderId = 0;
	return true;
}

bool GraphicsCreateFragmentShader(
	const char *shaderCode
){
	printf("setup the fragment shader ...\n");
	const GLchar *(strings[]) = {
		SkipBomConst(shaderCode)
	};
	assert(s_fragmentShaderId == 0);
	s_fragmentShaderId = CreateShader(GL_FRAGMENT_SHADER, SIZE_OF_ARRAY(strings), strings);
	if (s_fragmentShaderId == 0) {
		printf("setup the fragment shader ... fialed.\n");
		return false;
	}
	DumpShaderInterfaces(s_fragmentShaderId);
	printf("setup the fragment shader ... done.\n");

	return true;
}

bool GraphicsDeleteFragmentShader(
){
	if (s_fragmentShaderId == 0) return false;
	glFinish();
	glDeleteProgram(s_fragmentShaderId);
	s_fragmentShaderId = 0;
	return true;
}

bool GraphicsCreateComputeShader(
	const char *shaderCode
){
	printf("setup the compute shader ...\n");
	const GLchar *(strings[]) = {
		SkipBomConst(shaderCode)
	};
	assert(s_computeShaderId == 0);
	s_computeShaderId = CreateShader(GL_COMPUTE_SHADER, SIZE_OF_ARRAY(strings), strings);
	if (s_computeShaderId == 0) {
		printf("setup the compute shader ... failed.\n");
		return false;
	}
	glGetProgramiv(
		/* GLuint program */	s_computeShaderId,
		/* GLenum pname */		GL_COMPUTE_WORK_GROUP_SIZE,
		/* GLint *params */		s_computeWorkGroupSize
	);
	for (int i = 0; i < 3; ++i) {
		if (s_computeWorkGroupSize[i] <= 0) {
			s_computeWorkGroupSize[i] = 1;
		}
	}
	DumpShaderInterfaces(s_computeShaderId);
	printf("setup the compute shader ... done.\n");

	return true;
}

bool GraphicsDeleteComputeShader(
){
	if (s_computeShaderId == 0) return false;
	glFinish();
	glDeleteProgram(s_computeShaderId);
	s_computeShaderId = 0;
	s_computeWorkGroupSize[0] = 1;
	s_computeWorkGroupSize[1] = 1;
	s_computeWorkGroupSize[2] = 1;
	return true;
}

bool GraphicsCreateShaderPipeline(
){
	assert(s_shaderPipelineId == 0);
	glGenProgramPipelines(
		/* GLsizei n */			1,
		/* GLuint *pipelines */	&s_shaderPipelineId
	);
	glUseProgramStages(s_shaderPipelineId, GL_VERTEX_SHADER_BIT, s_vertexShaderId);
	glUseProgramStages(s_shaderPipelineId, GL_FRAGMENT_SHADER_BIT, s_fragmentShaderId);
	return true;
}

bool GraphicsDeleteShaderPipeline(
){
	if (s_shaderPipelineId == 0) return false;
	glFinish();
	glDeleteProgramPipelines(
		/* GLsizei n */					1,
		/* const GLuint *pipelines */	&s_shaderPipelineId
	);
	s_shaderPipelineId = 0;
	return true;
}

static void GraphicsSetTextureSampler(
	GLenum target,
	TextureFilter textureFilter,
	TextureWrap textureWrap,
	bool useMipmap
){
	{
		GLint minFilter = 0;
		GLint magFilter = 0;
		switch (textureFilter) {
			case TextureFilterNearest: {
				if (useMipmap) {
					minFilter = GL_NEAREST_MIPMAP_NEAREST;
				} else {
					minFilter = GL_NEAREST;
				}
				magFilter = GL_NEAREST;
			} break;
			case TextureFilterLinear: {
				if (useMipmap) {
					minFilter = GL_LINEAR_MIPMAP_LINEAR;
				} else {
					minFilter = GL_LINEAR;
				}
				magFilter = GL_LINEAR;
			} break;
			default: {
				assert(false);
			} break;
		}
		glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
		glTexParameteri(target, GL_TEXTURE_MAG_FILTER, magFilter);
	}

	{
		GLint param = GL_REPEAT;
		switch (textureWrap) {
			case TextureWrapRepeat: {
				param = GL_REPEAT;
			} break;
			case TextureWrapClampToEdge: {
				param = GL_CLAMP_TO_EDGE;
			} break;
			case TextureWrapMirroredRepeat: {
				param = GL_MIRRORED_REPEAT;
			} break;
			default: {
				assert(false);
			} break;
		}
		if (target == GL_TEXTURE_CUBE_MAP) {
			param = GL_CLAMP_TO_EDGE;
		}
		glTexParameteri(target, GL_TEXTURE_WRAP_S, param);
		glTexParameteri(target, GL_TEXTURE_WRAP_T, param);
		glTexParameteri(target, GL_TEXTURE_WRAP_R, param);
	}
}

static void GraphicsDrawFullScreenQuad(
	GLuint outputFrameBuffer,
	const CurrentFrameParams *params,
	const RenderSettings *settings
){
	/* RenderSettings 更新を認識 */
	if (s_xReso != params->xReso
	||	s_yReso != params->yReso
	||	memcmp(&s_currentRenderSettings, settings, sizeof(RenderSettings)) != 0
	) {
		s_xReso = params->xReso;
		s_yReso = params->yReso;
		s_currentRenderSettings = *settings;
		GraphicsDeleteFrameBuffer();
		GraphicsDeleteComputeTextures();
		GraphicsCreateFrameBuffer(params->xReso, params->yReso, settings);
		GraphicsCreateComputeTextures(params->xReso, params->yReso, settings);
	}

	/* シェーダパイプラインのバインド */
	glBindProgramPipeline(
		/* GLuint program */	s_shaderPipelineId
	);

	/* MRT テクスチャの設定 */
	if (settings->enableBackBuffer) {
		for (int renderTargetIndex = 0; renderTargetIndex < NUM_RENDER_TARGETS; renderTargetIndex++) {
			/* 裏テクスチャのバインド */
			glActiveTexture(GL_TEXTURE0 + renderTargetIndex);
			glBindTexture(
				/* GLenum target */		GL_TEXTURE_2D,
				/* GLuint texture */	s_mrtTextures[(params->frameCount & 1) ^ 1] [renderTargetIndex]
			);

			/* サンプラの設定 */
			GraphicsSetTextureSampler(GL_TEXTURE_2D, settings->textureFilter, settings->textureWrap, settings->enableMipmapGeneration);

			/* ミップマップ生成 */
			if (settings->enableMipmapGeneration) {
				glGenerateMipmap(GL_TEXTURE_2D);
			}
		}
	}

	/* コンピュートテクスチャの設定 */
	if (s_computeShaderId != 0) {
		int computeTextureIndex = (params->frameCount & 1) ^ 1;
		for (int renderTargetIndex = 0; renderTargetIndex < NUM_RENDER_TARGETS; renderTargetIndex++) {
			if (s_computeTextures[computeTextureIndex][renderTargetIndex] != 0) {
				glActiveTexture(GL_TEXTURE0 + COMPUTE_TEXTURE_START_INDEX + renderTargetIndex);
				glBindTexture(
					/* GLenum target */		GL_TEXTURE_2D,
					/* GLuint texture */	s_computeTextures[computeTextureIndex][renderTargetIndex]
				);

				/* サンプラの設定 */
				GraphicsSetTextureSampler(GL_TEXTURE_2D, settings->textureFilter, settings->textureWrap, false);
			}
		}
	}

	/* ユーザーテクスチャの設定 */
	for (int userTextureIndex = 0; userTextureIndex < NUM_USER_TEXTURES; userTextureIndex++) {
		if (s_userTextures[userTextureIndex].id) {
			/* テクスチャのバインド */
			glActiveTexture(GL_TEXTURE0 + USER_TEXTURE_START_INDEX + userTextureIndex);
			glBindTexture(
				/* GLenum target */		s_userTextures[userTextureIndex].target,
				/* GLuint texture */	s_userTextures[userTextureIndex].id
			);

			/* サンプラの設定 */
			GraphicsSetTextureSampler(s_userTextures[userTextureIndex].target, settings->textureFilter, settings->textureWrap, true);
		}
	}

	/* サウンドバッファのバインド */
	glBindBufferBase(
		/* GLenum target */		GL_SHADER_STORAGE_BUFFER,
		/* GLuint index */		BUFFER_INDEX_FOR_SOUND_VISUALIZER_INPUT,
		/* GLuint buffer */		SoundGetOutputSsbo()
	);

	/* ユニフォームパラメータ設定 */
	{
		glUseProgram(s_fragmentShaderId);

		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_PIPELINE_PASS_INDEX, GL_INT)) {
			glUniform1i(
				/* GLint location */	UNIFORM_LOCATION_PIPELINE_PASS_INDEX,
				/* GLint v0 */			s_activePipelinePassIndex
			);
		}
		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_WAVE_OUT_POS, GL_INT)) {
			glUniform1i(
				/* GLint location */	UNIFORM_LOCATION_WAVE_OUT_POS,
				/* GLint v0 */			params->waveOutPos
			);
		}
		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_FRAME_COUNT, GL_INT)) {
			glUniform1i(
				/* GLint location */	UNIFORM_LOCATION_FRAME_COUNT,
				/* GLint v0 */			params->frameCount
			);
		}
		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_TIME, GL_FLOAT)) {
			glUniform1f(
				/* GLint location */	UNIFORM_LOCATION_TIME,
				/* GLfloat v0 */		params->time
			);
		}
		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_RESO, GL_FLOAT_VEC2)) {
			glUniform2f(
				/* GLint location */	UNIFORM_LOCATION_RESO,
				/* GLfloat v0 */		(GLfloat)params->xReso,
				/* GLfloat v1 */		(GLfloat)params->yReso
			);
		}
		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_MOUSE_POS, GL_FLOAT_VEC2)) {
			glUniform2f(
				/* GLint location */	UNIFORM_LOCATION_MOUSE_POS,
				/* GLfloat v0 */		(GLfloat)params->xMouse / (GLfloat)params->xReso,
				/* GLfloat v1 */		1.0f - (GLfloat)params->yMouse / (GLfloat)params->yReso
			);
		}
		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_MOUSE_BUTTONS, GL_INT_VEC3)) {
			glUniform3i(
				/* GLint location */	UNIFORM_LOCATION_MOUSE_BUTTONS,
				/* GLint v0 */			params->mouseLButtonPressed,
				/* GLint v1 */			params->mouseMButtonPressed,
				/* GLint v2 */			params->mouseRButtonPressed
			);
		}
		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_TAN_FOVY, GL_FLOAT)) {
			glUniform1f(
				/* GLint location */	UNIFORM_LOCATION_TAN_FOVY,
				/* GLfloat v0 */		tanf(params->fovYInRadians)
			);
		}
		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_CAMERA_COORD, GL_FLOAT_MAT4)) {
			glUniformMatrix4fv(
				/* GLint location */		UNIFORM_LOCATION_CAMERA_COORD,
				/* GLsizei count */			1,
				/* GLboolean transpose */	false,
				/* const GLfloat *value */	&params->mat4x4CameraInWorld[0][0]
			);
		}
		if (ExistsShaderUniform(s_fragmentShaderId, UNIFORM_LOCATION_PREV_CAMERA_COORD, GL_FLOAT_MAT4)) {
			glUniformMatrix4fv(
				/* GLint location */		UNIFORM_LOCATION_PREV_CAMERA_COORD,
				/* GLsizei count */			1,
				/* GLboolean transpose */	false,
				/* const GLfloat *value */	&params->mat4x4PrevCameraInWorld[0][0]
			);
		}
	}

	/* MRT フレームバッファのバインド */
	glBindFramebuffer(
		/* GLenum target */			GL_FRAMEBUFFER,
		/* GLuint framebuffer */	s_mrtFrameBuffer
	);

	/* ビューポートの設定 */
	glViewport(0, 0, params->xReso, params->yReso);

	/* MRT の設定 */
	{
		GLuint bufs[NUM_RENDER_TARGETS] = {0};
		assert(settings->numEnabledRenderTargets <= NUM_RENDER_TARGETS);
		int numRenderTargets = settings->enableMultipleRenderTargets? settings->numEnabledRenderTargets: 1;
		for (int renderTargetIndex = 0; renderTargetIndex < numRenderTargets; renderTargetIndex++) {
			/* 表テクスチャを MRT として登録 */
			glFramebufferTexture(
				/* GLenum target */			GL_FRAMEBUFFER,
				/* GLenum attachment */		GL_COLOR_ATTACHMENT0 + renderTargetIndex,
				/* GLuint texture */		s_mrtTextures[params->frameCount & 1] [renderTargetIndex],
				/* GLint level */			0
			);
			bufs[renderTargetIndex] = GL_COLOR_ATTACHMENT0 + renderTargetIndex;
		}
		glDrawBuffers(
			/* GLsizei n */				numRenderTargets,
			/* const GLenum *bufs */	bufs
		);
	}

	/* 描画 */
	{
		/* 矩形の頂点座標 */
		GLfloat vertices[] = {
			-1.0f, -1.0f,
			 1.0f, -1.0f,
			-1.0f,  1.0f,
			 1.0f,  1.0f
		};

		/* 頂点アトリビュートのポインタを設定 */
		glVertexAttribPointer(
			/* GLuint index */			0,
			/* GLint size */			2,
			/* GLenum type */			GL_FLOAT,
			/* GLboolean normalized */	GL_FALSE,
			/* GLsizei stride */		2 * sizeof(GLfloat),
			/* const void * pointer */	vertices
		);

		/* 頂点アトリビュートの有効化 */
		glEnableVertexAttribArray(
			/* GLuint index */			0
		);

		/* 頂点列の描画 */
		glDrawArrays(
			/* GLenum mode */	GL_TRIANGLE_STRIP,
			/* GLint first */	0,
			/* GLsizei count */	4
		);
	}

	/* 描画結果をデフォルトフレームバッファにコピー */
	glBlitNamedFramebuffer(
		/* GLuint readFramebuffer */	s_mrtFrameBuffer,
		/* GLuint drawFramebuffer */	outputFrameBuffer,
		/* GLint srcX0 */				0,
		/* GLint srcY0 */				0,
		/* GLint srcX1 */				params->xReso,
		/* GLint srcY1 */				params->yReso,
		/* GLint dstX0 */				0,
		/* GLint dstY0 */				0,
		/* GLint dstX1 */				params->xReso,
		/* GLint dstY1 */				params->yReso,
		/* GLbitfield mask */			GL_COLOR_BUFFER_BIT,
		/* GLenum filter */				GL_NEAREST
	);

	/* MRT フレームバッファのアンバインド */
	glBindFramebuffer(
		/* GLenum target */			GL_FRAMEBUFFER,
		/* GLuint framebuffer */	0	/* unbind */
	);

	/* サウンドバッファのアンバインド */
	glBindBufferBase(
		/* GLenum target */			GL_SHADER_STORAGE_BUFFER,
		/* GLuint index */			BUFFER_INDEX_FOR_SOUND_VISUALIZER_INPUT,
		/* GLuint buffer */			0	/* unbind */
	);

	/* ユーザーテクスチャのアンバインド */
	for (int userTextureIndex = 0; userTextureIndex < NUM_USER_TEXTURES; userTextureIndex++) {
		if (s_userTextures[userTextureIndex].id) {
			glActiveTexture(GL_TEXTURE0 + USER_TEXTURE_START_INDEX + userTextureIndex);
			glBindTexture(
				/* GLenum target */		s_userTextures[userTextureIndex].target,
				/* GLuint texture */	0	/* unbind */
			);
		}
	}

	/* コンピュートテクスチャのアンバインド */
	if (s_computeShaderId != 0) {
		for (int renderTargetIndex = 0; renderTargetIndex < NUM_RENDER_TARGETS; renderTargetIndex++) {
			glActiveTexture(GL_TEXTURE0 + COMPUTE_TEXTURE_START_INDEX + renderTargetIndex);
			glBindTexture(
				/* GLenum target */		GL_TEXTURE_2D,
				/* GLuint texture */	0	/* unbind */
			);
		}
	}

	/* MRT テクスチャのアンバインド */
	for (int renderTargetIndex = 0; renderTargetIndex < NUM_RENDER_TARGETS; renderTargetIndex++) {
		glActiveTexture(GL_TEXTURE0 + renderTargetIndex);
		glBindTexture(
			/* GLenum target */		GL_TEXTURE_2D,
			/* GLuint texture */	0	/* unbind */
		);
	}

	/* シェーダパイプラインのアンバインド */
	glBindProgramPipeline(NULL);
}

bool GraphicsCaptureScreenShotOnMemory(
	void *buffer,
	size_t bufferSizeInBytes,
	const CurrentFrameParams *params,
	const RenderSettings *renderSettings,
	const CaptureScreenShotSettings *captureSettings
){
	/* OpenGL のピクセルフォーマット情報 */
	GlPixelFormatInfo glPixelFormatInfo = PixelFormatToGlPixelFormatInfo(renderSettings->pixelFormat);

	/* バッファ容量が不足しているならエラー */
	if (bufferSizeInBytes < (size_t)(params->xReso * params->yReso * glPixelFormatInfo.numBitsPerPixel / 8)) return false;

	/* FBO 作成 */
	GLuint offscreenRenderTargetFbo = 0;
	GLuint offscreenRenderTargetTexture = 0;
	glGenFramebuffers(
		/* GLsizei n */				1,
	 	/* GLuint *ids */			&offscreenRenderTargetFbo
	);
	glBindFramebuffer(
		/* GLenum target */			GL_FRAMEBUFFER,
		/* GLuint framebuffer */	offscreenRenderTargetFbo
	);

	/* レンダーターゲットとなるテクスチャ作成 */
	glGenTextures(
		/* GLsizei n */				1,
		/* GLuint * textures */		&offscreenRenderTargetTexture
	);
	glBindTexture(
		/* GLenum target */			GL_TEXTURE_2D,
		/* GLuint texture */		offscreenRenderTargetTexture
	);
	glTexStorage2D(
		/* GLenum target */			GL_TEXTURE_2D,
		/* GLsizei levels */		1,
		/* GLenum internalformat */	glPixelFormatInfo.internalformat,
		/* GLsizei width */			params->xReso,
		/* GLsizei height */		params->yReso
	);

	/* レンダーターゲットのバインド */
	glFramebufferTexture(
		/* GLenum target */			GL_FRAMEBUFFER,
		/* GLenum attachment */		GL_COLOR_ATTACHMENT0,
		/* GLuint texture */		offscreenRenderTargetTexture,
		/* GLint level */			0
	);

	/* FBO 設定、ビューポート設定 */
	glBindFramebuffer(
		/* GLenum target */			GL_FRAMEBUFFER,
		/* GLuint framebuffer */	offscreenRenderTargetFbo
	);

	/* 画面全体に四角形を描画 */
	GraphicsDispatchCompute(params, renderSettings);
	GraphicsDrawFullScreenQuad(offscreenRenderTargetFbo, params, renderSettings);

	/* 描画結果の取得 */
	glFinish();		/* 不要と信じたいが念のため */
	glBindFramebuffer(
		/* GLenum target */			GL_FRAMEBUFFER,
		/* GLuint framebuffer */	offscreenRenderTargetFbo
	);
	glReadPixels(
		/* GLint x */				0,
		/* GLint y */				0,
		/* GLsizei width */			params->xReso,
		/* GLsizei height */		params->yReso,
		/* GLenum format */			glPixelFormatInfo.format,
		/* GLenum type */			glPixelFormatInfo.type,
		/* GLvoid * data */			buffer
	);

	/* αチャンネルの強制 1.0 置換 */
	if (captureSettings->replaceAlphaByOne) {
		for (int y = 0; y < params->yReso; y++) {
			for (int x = 0; x < params->xReso; x++) {
				switch (renderSettings->pixelFormat) {
					case PixelFormatUnorm8Rgba: {
						((uint8_t *)buffer)[(y * params->xReso + x) * 4 + 3] = 255;
					} break;
					case PixelFormatFp16Rgba: {
						((uint16_t *)buffer)[(y * params->xReso + x) * 4 + 3] = 0x3c00;
					} break;
					case PixelFormatFp32Rgba: {
						((float *)buffer)[(y * params->xReso + x) * 4 + 3] = 1.0f;
					} break;
				}
			}
		}
	}

	/* オフスクリーンレンダーターゲット、FBO 破棄 */
	glDeleteTextures(
		/* GLsizei n */						1,
		/* const GLuint * textures */		&offscreenRenderTargetTexture
	);
	glDeleteFramebuffers(
		/* GLsizei n */						1,
		/* const GLuint * framebuffers */	&offscreenRenderTargetFbo
	);

	return true;
}

bool GraphicsCaptureScreenShotAsPngTexture2d(
	const CurrentFrameParams *params,
	const RenderSettings *renderSettings,
	const CaptureScreenShotSettings *captureSettings
){
	RenderSettings renderSettingsForceUnorm8 = *renderSettings;
	renderSettingsForceUnorm8.pixelFormat = PixelFormatUnorm8Rgba;
	GlPixelFormatInfo glPixelFormatInfo = PixelFormatToGlPixelFormatInfo(renderSettingsForceUnorm8.pixelFormat);
	size_t bufferSizeInBytes = (size_t)(params->xReso * params->yReso) * glPixelFormatInfo.numBitsPerPixel / 8;
	void *buffer = malloc(bufferSizeInBytes);
	if (
		GraphicsCaptureScreenShotOnMemory(
			buffer, bufferSizeInBytes,
			params, &renderSettingsForceUnorm8, captureSettings
		) == false
	) {
		free(buffer);
		return false;
	}
	if (
		SerializeAsPng(
			/* const char *fileName */	captureSettings->fileName,
			/* const void *data */		buffer,
			/* int numChannels */		4,
			/* int width */				params->xReso,
			/* int height */			params->yReso,
			/* bool verticalFlip */		true
		) == false
	) {
		free(buffer);
		return false;
	};
	free(buffer);
	return true;
}

bool GraphicsCaptureScreenShotAsDdsTexture2d(
	const CurrentFrameParams *params,
	const RenderSettings *renderSettings,
	const CaptureScreenShotSettings *captureSettings
){
	GlPixelFormatInfo glPixelFormatInfo = PixelFormatToGlPixelFormatInfo(renderSettings->pixelFormat);
	size_t bufferSizeInBytes = (size_t)(params->xReso * params->yReso) * glPixelFormatInfo.numBitsPerPixel / 8;
	void *buffer = malloc(bufferSizeInBytes);
	if (
		GraphicsCaptureScreenShotOnMemory(
			buffer, bufferSizeInBytes,
			params, renderSettings, captureSettings
		) == false
	) {
		free(buffer);
		return false;
	}
	if (
		SerializeAsDdsTexture2d(
			/* const char *fileName */	captureSettings->fileName,
			/* DxgiFormat dxgiFormat */	PixelFormatToDxgiFormat(renderSettings->pixelFormat),
			/* const void *data */		buffer,
			/* int width */				params->xReso,
			/* int height */			params->yReso,
			/* bool verticalFlip */		true
		) == false
	) {
		free(buffer);
		return false;
	};
	free(buffer);
	return true;
}

bool GraphicsCaptureAsDdsCubemap(
	const CurrentFrameParams *params,
	const RenderSettings *renderSettings,
	const CaptureCubemapSettings *captureSettings
){
	/* OpenGL のピクセルフォーマット情報 */
	GlPixelFormatInfo glPixelFormatInfo = PixelFormatToGlPixelFormatInfo(renderSettings->pixelFormat);

	/* 先だって全レンダーターゲットのクリア */
	GraphicsClearAllRenderTargets();

	/* FBO 作成 */
	GLuint offscreenRenderTargetFbo = 0;
	GLuint offscreenRenderTargetTexture = 0;
	glGenFramebuffers(
		/* GLsizei n */				1,
	 	/* GLuint *ids */			&offscreenRenderTargetFbo
	);
	glBindFramebuffer(
		/* GLenum target */			GL_FRAMEBUFFER,
		/* GLuint framebuffer */	offscreenRenderTargetFbo
	);

	/* レンダーターゲットとなるテクスチャ作成 */
	glGenTextures(
		/* GLsizei n */				1,
		/* GLuint * textures */		&offscreenRenderTargetTexture
	);
	glBindTexture(
		/* GLenum target */			GL_TEXTURE_2D,
		/* GLuint texture */		offscreenRenderTargetTexture
	);
	glTexStorage2D(
		/* GLenum target */			GL_TEXTURE_2D,
		/* GLsizei levels */		1,
		/* GLenum internalformat */	glPixelFormatInfo.internalformat,
		/* GLsizei width */			params->xReso,
		/* GLsizei height */		params->yReso
	);

	/* レンダーターゲットのバインド */
	glFramebufferTexture(
		/* GLenum target */			GL_FRAMEBUFFER,
		/* GLenum attachment */		GL_COLOR_ATTACHMENT0,
		/* GLuint texture */		offscreenRenderTargetTexture,
		/* GLint level */			0
	);

	/* キューブマップ各面の描画と結果の取得 */
	GraphicsDispatchCompute(params, renderSettings);
	void *(data[6]);
	for (int iFace = 0; iFace < 6; iFace++) {
		/*
			dds の cubemap face の配置

			                                 [5]
			                           +-------------+
			                          /             /|
			                         /     [2]     / |
			       [+y]             /             /  |  face0:+x:right
			        | /[  ]        +-------------+   |  face1:-x:left
			        |/             |             |   |  face2:+y:top
			[  ]----+----[+x]   [1]|             |[0]|  face3:-y:bottom
			       /|              |             |   |  face4:+z:front
			  [+z]/ |              |     [4]     |   +  face5:-z:back
			       [  ]            |             |  /
			                       |             | /
			                       |             |/
			                       +-------------+
			                             [3]

			                  +-----------------+
			                  |       [-z]      |
			                  |        | /[  ]  |
			                  |        |/       |
			                  |[  ]----+----[+x]|
			                  |       /|        |
			                  |  [+y]/ |        |
			                  |       [  ]      |
			                  |                 |
			                  | face2:+y:top    |
			+-----------------+-----------------+-----------------+-----------------+
			|       [+y]      |       [+y]      |       [+y]      |       [+y]      |
			|        | /[  ]  |        | /[  ]  |        | /[  ]  |        | /[  ]  |
			|        |/       |        |/       |        |/       |        |/       |
			|[  ]----+----[+z]|[  ]----+----[+x]|[  ]----+----[-z]|[  ]----+----[-x]|
			|       /|        |       /|        |       /|        |       /|        |
			|  [-x]/ |        |  [+z]/ |        |  [+x]/ |        |  [-z]/ |        |
			|       [  ]      |       [  ]      |       [  ]      |       [  ]      |
			|                 |                 |                 |                 |
			| face1:-x:left   | face4:+z:front  | face0:+x:right  | face5:-z:back   |
			+-----------------+-----------------+-----------------+-----------------+
			                  |       [+z]      |
			                  |        | /[  ]  |
			                  |        |/       |
			                  |[  ]----+----[+x]|
			                  |       /|        |
			                  |  [-y]/ |        |
			                  |       [  ]      |
			                  |                 |
			                  | face3:-y:bottom |
			                  +-----------------+
		*/
		const float mat4x4FaceInWorldTbl[6][4][4] = {
			/*
				+-----------------+
				|       [+y]      |
				|        | /[  ]  |
				|        |/       |
				|[  ]----+----[-z]|
				|       /|        |
				|  [+x]/ |        |
				|       [  ]      |
				|                 |
				| face0:+x:right  |
				+-----------------+
			*/
			{
				{ 0, 0,-1, 0},
				{ 0, 1, 0, 0},
				{-1, 0, 0, 0},		/* 裏表反転のため Z 軸の符号を反転 */
				{ 0, 0, 0, 1}
			},
			/*
				+-----------------+
				|       [+y]      |
				|        | /[  ]  |
				|        |/       |
				|[  ]----+----[+z]|
				|       /|        |
				|  [-x]/ |        |
				|       [  ]      |
				|                 |
				| face1:-x:left   |
				+-----------------+
			*/
			{
				{ 0, 0, 1, 0},
				{ 0, 1, 0, 0},
				{ 1, 0, 0, 0},		/* 裏表反転のため Z 軸の符号を反転 */
				{ 0, 0, 0, 1}
			},
			/*
				+-----------------+
				|       [-z]      |
				|        | /[  ]  |
				|        |/       |
				|[  ]----+----[+x]|
				|       /|        |
				|  [+y]/ |        |
				|       [  ]      |
				|                 |
				| face2:+y:top    |
				+-----------------+
			*/
			{
				{ 1, 0, 0, 0},
				{ 0, 0,-1, 0},
				{ 0,-1, 0, 0},		/* 裏表反転のため Z 軸の符号を反転 */
				{ 0, 0, 0, 1}
			},
			/*
				+-----------------+
				|       [+z]      |
				|        | /[  ]  |
				|        |/       |
				|[  ]----+----[+x]|
				|       /|        |
				|  [-y]/ |        |
				|       [  ]      |
				|                 |
				| face3:-y:bottom |
				+-----------------+
			*/
			{
				{ 1, 0, 0, 0},
				{ 0, 0, 1, 0},
				{ 0, 1, 0, 0},		/* 裏表反転のため Z 軸の符号を反転 */
				{ 0, 0, 0, 1}
			},
			/*
				+-----------------+
				|       [+y]      |
				|        | /[  ]  |
				|        |/       |
				|[  ]----+----[+x]|
				|       /|        |
				|  [+z]/ |        |
				|       [  ]      |
				|                 |
				| face4:+z:front  |
				+-----------------+
			*/
			{
				{ 1, 0, 0, 0},
				{ 0, 1, 0, 0},
				{ 0, 0,-1, 0},		/* 裏表反転のため Z 軸の符号を反転 */
				{ 0, 0, 0, 1}
			},
			/*
				+-----------------+
				|       [+y]      |
				|        | /[  ]  |
				|        |/       |
				|[  ]----+----[-x]|
				|       /|        |
				|  [-z]/ |        |
				|       [  ]      |
				|                 |
				| face5:-z:back   |
				+-----------------+
			*/
			{
				{-1, 0, 0, 0},
				{ 0, 1, 0, 0},
				{ 0, 0, 1, 0},		/* 裏表反転のため Z 軸の符号を反転 */
				{ 0, 0, 0, 1}
			}
		};

		/* キューブマップ各面のパラメータ */
		CurrentFrameParams faceParams = *params;
		{
			faceParams.fovYInRadians = PI / 4;	/* 垂直方向画角90度 */

			/* キューブマップの指定の面の方向を向き、カメラ位置を原点とする座標系 */
			Mat4x4Copy(faceParams.mat4x4CameraInWorld, mat4x4FaceInWorldTbl[iFace]);
			Vec4Copy(faceParams.mat4x4CameraInWorld[3], params->mat4x4CameraInWorld[3]);

			/* キャプチャ時は前回フレームのカメラ＝最新フレームのカメラ */
			Mat4x4Copy(faceParams.mat4x4PrevCameraInWorld, faceParams.mat4x4CameraInWorld);
		}

		/* 画面全体に四角形を描画 */
		GraphicsDrawFullScreenQuad(offscreenRenderTargetFbo, &faceParams, renderSettings);

		/* 描画結果の取得 */
		data[iFace] = malloc(sizeof(float) * 4 * params->xReso * params->yReso);
		glFinish();		/* 不要と信じたいが念のため */
		glBindFramebuffer(
			/* GLenum target */			GL_FRAMEBUFFER,
			/* GLuint framebuffer */	offscreenRenderTargetFbo
		);
		glReadPixels(
			/* GLint x */				0,
			/* GLint y */				0,
			/* GLsizei width */			params->xReso,
			/* GLsizei height */		params->yReso,
			/* GLenum format */			glPixelFormatInfo.format,
			/* GLenum type */			glPixelFormatInfo.type,
			/* GLvoid * data */			data[iFace]
		);
	}

	/* ファイルに書き出し */
	bool ret;
	{
		int cubemapReso = params->xReso;
		assert(params->xReso == params->yReso);
		const void *(constData[6]) = {data[0], data[1], data[2], data[3], data[4], data[5]};
		ret = SerializeAsDdsCubemap(
			/* const char *fileName */		captureSettings->fileName,
			/* DxgiFormat dxgiFormat */		PixelFormatToDxgiFormat(renderSettings->pixelFormat),
			/* const void *(data[6]) */		constData,
			/* int reso */					cubemapReso,
			/* bool verticalFlip */			true
		);
	}

	/* メモリ破棄 */
	for (int iFace = 0; iFace < 6; iFace++) {
		free(data[iFace]);
	}

	/* オフスクリーンレンダーターゲット、FBO 破棄 */
	glDeleteTextures(
		/* GLsizei n */						1,
		/* const GLuint * textures */		&offscreenRenderTargetTexture
	);
	glDeleteFramebuffers(
		/* GLsizei n */						1,
		/* const GLuint * framebuffers */	&offscreenRenderTargetFbo
	);

	return ret;
}

static void GraphicsDispatchCompute(
	const CurrentFrameParams *params,
	const RenderSettings *settings
){
	if (s_computeShaderId == 0) return;

	int readIndex = (params->frameCount & 1) ^ 1;
	int writeIndex = (params->frameCount & 1);

	bool texturesReady = true;
	for (int renderTargetIndex = 0; renderTargetIndex < NUM_RENDER_TARGETS; ++renderTargetIndex) {
		if (s_computeTextures[readIndex][renderTargetIndex] == 0
		||	s_computeTextures[writeIndex][renderTargetIndex] == 0
		) {
			texturesReady = false;
			break;
		}
	}
	if (texturesReady == false) return;

	GlPixelFormatInfo pixelFormatInfo = PixelFormatToGlPixelFormatInfo(settings->pixelFormat);

	for (int renderTargetIndex = 0; renderTargetIndex < NUM_RENDER_TARGETS; ++renderTargetIndex) {
		glBindImageTexture(
			/* GLuint unit */			renderTargetIndex,
			/* GLuint texture */		s_computeTextures[readIndex][renderTargetIndex],
			/* GLint level */			0,
			/* GLboolean layered */		GL_FALSE,
			/* GLint layer */			0,
			/* GLenum access */			GL_READ_ONLY,
			/* GLenum format */			pixelFormatInfo.internalformat
		);
		glBindImageTexture(
			/* GLuint unit */			NUM_RENDER_TARGETS + renderTargetIndex,
			/* GLuint texture */		s_computeTextures[writeIndex][renderTargetIndex],
			/* GLint level */			0,
			/* GLboolean layered */		GL_FALSE,
			/* GLint layer */			0,
			/* GLenum access */			GL_WRITE_ONLY,
			/* GLenum format */			pixelFormatInfo.internalformat
		);
	}

	glUseProgram(s_computeShaderId);
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_PIPELINE_PASS_INDEX, GL_INT)) {
		glUniform1i(
			/* GLint location */	UNIFORM_LOCATION_PIPELINE_PASS_INDEX,
			/* GLint v0 */			s_activePipelinePassIndex
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_WAVE_OUT_POS, GL_INT)) {
		glUniform1i(
			/* GLint location */	UNIFORM_LOCATION_WAVE_OUT_POS,
			/* GLint v0 */			params->waveOutPos
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_FRAME_COUNT, GL_INT)) {
		glUniform1i(
			/* GLint location */	UNIFORM_LOCATION_FRAME_COUNT,
			/* GLint v0 */			params->frameCount
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_TIME, GL_FLOAT)) {
		glUniform1f(
			/* GLint location */	UNIFORM_LOCATION_TIME,
			/* GLfloat v0 */		params->time
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_RESO, GL_FLOAT_VEC2)) {
		glUniform2f(
			/* GLint location */	UNIFORM_LOCATION_RESO,
			/* GLfloat v0 */		(GLfloat)params->xReso,
			/* GLfloat v1 */		(GLfloat)params->yReso
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_MOUSE_POS, GL_FLOAT_VEC2)) {
		glUniform2f(
			/* GLint location */	UNIFORM_LOCATION_MOUSE_POS,
			/* GLfloat v0 */		(GLfloat)params->xMouse / (GLfloat)params->xReso,
			/* GLfloat v1 */		1.0f - (GLfloat)params->yMouse / (GLfloat)params->yReso
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_MOUSE_BUTTONS, GL_INT_VEC3)) {
		glUniform3i(
			/* GLint location */	UNIFORM_LOCATION_MOUSE_BUTTONS,
			/* GLint v0 */			params->mouseLButtonPressed,
			/* GLint v1 */			params->mouseMButtonPressed,
			/* GLint v2 */			params->mouseRButtonPressed
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_TAN_FOVY, GL_FLOAT)) {
		glUniform1f(
			/* GLint location */	UNIFORM_LOCATION_TAN_FOVY,
			/* GLfloat v0 */		tanf(params->fovYInRadians)
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_CAMERA_COORD, GL_FLOAT_MAT4)) {
		glUniformMatrix4fv(
			/* GLint location */		UNIFORM_LOCATION_CAMERA_COORD,
			/* GLsizei count */			1,
			/* GLboolean transpose */	false,
			/* const GLfloat *value */	&params->mat4x4CameraInWorld[0][0]
		);
	}
	if (ExistsShaderUniform(s_computeShaderId, UNIFORM_LOCATION_PREV_CAMERA_COORD, GL_FLOAT_MAT4)) {
		glUniformMatrix4fv(
			/* GLint location */		UNIFORM_LOCATION_PREV_CAMERA_COORD,
			/* GLsizei count */			1,
			/* GLboolean transpose */	false,
			/* const GLfloat *value */	&params->mat4x4PrevCameraInWorld[0][0]
		);
	}

	GLuint workGroupSizeX = (GLuint)(s_computeWorkGroupSize[0] > 0? s_computeWorkGroupSize[0]: 1);
	GLuint workGroupSizeY = (GLuint)(s_computeWorkGroupSize[1] > 0? s_computeWorkGroupSize[1]: 1);
	GLuint workGroupSizeZ = (GLuint)(s_computeWorkGroupSize[2] > 0? s_computeWorkGroupSize[2]: 1);

	GLuint numGroupsX = (GLuint)((params->xReso + workGroupSizeX - 1) / workGroupSizeX);
	GLuint numGroupsY = (GLuint)((params->yReso + workGroupSizeY - 1) / workGroupSizeY);
	GLuint numGroupsZ = (GLuint)((1 + workGroupSizeZ - 1) / workGroupSizeZ);
	if (numGroupsX == 0) numGroupsX = 1;
	if (numGroupsY == 0) numGroupsY = 1;
	if (numGroupsZ == 0) numGroupsZ = 1;

	glDispatchCompute(
		/* GLuint num_groups_x */	numGroupsX,
		/* GLuint num_groups_y */	numGroupsY,
		/* GLuint num_groups_z */	numGroupsZ
	);

	glMemoryBarrier(
			GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
		|	GL_TEXTURE_FETCH_BARRIER_BIT
		|	GL_TEXTURE_UPDATE_BARRIER_BIT
	);

	for (int imageUnit = 0; imageUnit < NUM_RENDER_TARGETS * 2; ++imageUnit) {
		glBindImageTexture(
			/* GLuint unit */			imageUnit,
			/* GLuint texture */		0,
			/* GLint level */			0,
			/* GLboolean layered */		GL_FALSE,
			/* GLint layer */			0,
			/* GLenum access */			GL_READ_ONLY,
			/* GLenum format */			pixelFormatInfo.internalformat
		);
	}
}

void GraphicsUpdate(
	const CurrentFrameParams *params,
	const RenderSettings *settings
){
	GraphicsSynchronizeRenderSettings(params, settings);
	const PipelineDescription *pipeline = GraphicsResolvePipelineDescription();
	GraphicsExecutePipeline(
		pipeline,
		params,
		settings
	);

	/* スワップ設定 */
	if (settings->enableSwapIntervalControl) {
		typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC)(int interval);
		PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
		assert(wglSwapIntervalEXT != NULL);
		switch (settings->swapInterval) {
			case SwapIntervalAllowTearing: {
				wglSwapIntervalEXT(-1);
			} break;
			case SwapIntervalHsync: {
				wglSwapIntervalEXT(0);
			} break;
			case SwapIntervalVsync: {
				wglSwapIntervalEXT(1);
			} break;
			default: {
				assert(false);
			} break;
		}
	}
}

bool GraphicsInitialize(
){
	GraphicsCreateFrameBuffer(s_xReso, s_yReso, &s_currentRenderSettings);
	GraphicsCreateComputeTextures(s_xReso, s_yReso, &s_currentRenderSettings);
	GraphicsResetPipelineDescriptionToDefault();

	/* glRects() 相当の動作を模倣する簡単な頂点シェーダを作成 */
	{
	    const char *shaderCode =
	    	"#version 330 core\n"
	        "layout(location = 0) in vec2 position;\n"
	        "void main() {\n"
	        "    gl_Position = vec4(position, 0.0, 1.0);\n"
	        "}\0"
		;
 		GraphicsCreateVertexShader(shaderCode);
	}

	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	return true;
}

bool GraphicsTerminate(
){
	GraphicsDeleteComputeShader();	/* false が得られてもエラー扱いとしない */
	GraphicsDeleteFragmentShader();	/* false が得られてもエラー扱いとしない */
	GraphicsDeleteVertexShader();	/* false が得られてもエラー扱いとしない */
	GraphicsDeleteComputeTextures();
	GraphicsDeleteFrameBuffer();
	GraphicsResetPipelineDescriptionToDefault();
	return true;
}

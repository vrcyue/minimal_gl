/* PipelineDescription
 *
 * Experimental frame-graph style pipeline description shared by the editor
 * and exported runtime.  The implementation focuses on data modelling and
 * JSON serialization so that the existing hard-coded MRT/compute flow can be
 * migrated gradually.
 */

#ifndef _PIPELINE_DESCRIPTION_H_
#define _PIPELINE_DESCRIPTION_H_

#include "common.h"
#include "pixel_format.h"
#include "graphics.h"

typedef struct cJSON cJSON;

#ifdef __cplusplus
extern "C" {
#endif

#define PIPELINE_MAX_RESOURCES                 (32)
#define PIPELINE_MAX_PASSES                    (16)
#define PIPELINE_MAX_BINDINGS_PER_PASS         (16)
#define PIPELINE_MAX_HISTORY_LENGTH            (4)
#define PIPELINE_MAX_RESOURCE_ID_LENGTH        (64)
#define PIPELINE_MAX_PASS_NAME_LENGTH          (64)
#define PIPELINE_MAX_SHADER_PATH_LENGTH        (MAX_PATH)

typedef enum {
    PipelinePassTypeFragment,
    PipelinePassTypeCompute,
    PipelinePassTypePresent,
    PipelinePassTypeCount
} PipelinePassType;

typedef enum {
    PipelineResourceAccessSampled,
    PipelineResourceAccessImageRead,
    PipelineResourceAccessImageWrite,
    PipelineResourceAccessHistoryRead,
    PipelineResourceAccessColorAttachment,
    PipelineResourceAccessCount
} PipelineResourceAccess;

typedef enum {
    PipelineResolutionModeFramebuffer,
    PipelineResolutionModeFixed,
    PipelineResolutionModeCount
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

void PipelineDescriptionInit(PipelineDescription *description);

/* Serialization helpers */
bool PipelineDescriptionDeserializeFromJson(
    PipelineDescription *description,
    cJSON *jsonRoot,
    char *errorMessage,
    size_t errorMessageSizeInBytes
);

cJSON *PipelineDescriptionSerializeToJson(
    const PipelineDescription *description
);

const char *PipelinePassTypeToString(PipelinePassType type);
bool PipelinePassTypeFromString(const char *value, PipelinePassType *type);

const char *PipelineResourceAccessToString(PipelineResourceAccess access);
bool PipelineResourceAccessFromString(const char *value, PipelineResourceAccess *access);

const char *PipelineResolutionModeToString(PipelineResolutionMode mode);
bool PipelineResolutionModeFromString(const char *value, PipelineResolutionMode *mode);

const char *PixelFormatToPipelineString(PixelFormat pixelFormat);
bool PixelFormatFromPipelineString(const char *value, PixelFormat *pixelFormat);

const char *TextureFilterToPipelineString(TextureFilter filter);
bool TextureFilterFromPipelineString(const char *value, TextureFilter *filter);

const char *TextureWrapToPipelineString(TextureWrap wrap);
bool TextureWrapFromPipelineString(const char *value, TextureWrap *wrap);

#ifdef __cplusplus
}
#endif

#endif /* _PIPELINE_DESCRIPTION_H_ */

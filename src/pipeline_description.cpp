/* PipelineDescription implementation
 *
 * Provides a data-only description of render/compute passes so the existing
 * fixed pipeline can migrate toward a frame-graph style workflow.
 */

#include "common.h"
#include "config.h"
#include "pipeline_description.h"
#include "external/cJSON/cJSON.h"
#include "external/cJSON/cJSON_Utils.h"
#include <stdarg.h>
#include <string.h>

typedef struct {
    const char *name;
    PipelinePassType value;
} PipelinePassTypeEntry;

typedef struct {
    const char *name;
    PipelineResourceAccess value;
} PipelineResourceAccessEntry;

typedef struct {
    const char *name;
    PipelineResolutionMode value;
} PipelineResolutionModeEntry;

typedef struct {
    const char *name;
    PixelFormat value;
} PipelinePixelFormatEntry;

typedef struct {
    const char *name;
    TextureFilter value;
} PipelineTextureFilterEntry;

typedef struct {
    const char *name;
    TextureWrap value;
} PipelineTextureWrapEntry;

static const PipelinePassTypeEntry s_passTypeTable[] = {
    {"fragment", PipelinePassTypeFragment},
    {"compute",  PipelinePassTypeCompute},
    {"present",  PipelinePassTypePresent},
};

static const PipelineResourceAccessEntry s_resourceAccessTable[] = {
    {"sampled",        PipelineResourceAccessSampled},
    {"image_read",     PipelineResourceAccessImageRead},
    {"image_write",    PipelineResourceAccessImageWrite},
    {"history_read",   PipelineResourceAccessHistoryRead},
    {"color_attachment", PipelineResourceAccessColorAttachment},
};

static const PipelineResolutionModeEntry s_resolutionModeTable[] = {
    {"framebuffer", PipelineResolutionModeFramebuffer},
    {"fixed",       PipelineResolutionModeFixed},
};

static const PipelinePixelFormatEntry s_pixelFormatTable[] = {
    {"unorm8_rgba", PixelFormatUnorm8Rgba},
    {"fp16_rgba",   PixelFormatFp16Rgba},
    {"fp32_rgba",   PixelFormatFp32Rgba},
    {"r32ui",       PixelFormatR32Ui},
};

static const PipelineTextureFilterEntry s_textureFilterTable[] = {
    {"nearest", TextureFilterNearest},
    {"linear",  TextureFilterLinear},
};

static const PipelineTextureWrapEntry s_textureWrapTable[] = {
    {"repeat",          TextureWrapRepeat},
    {"clamp_to_edge",   TextureWrapClampToEdge},
    {"mirrored_repeat", TextureWrapMirroredRepeat},
};

static void SetErrorMessage(
    char *errorMessage,
    size_t errorMessageSizeInBytes,
    const char *format,
    ...
){
    if (errorMessage == NULL || errorMessageSizeInBytes == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    _vsnprintf_s(
        errorMessage,
        errorMessageSizeInBytes,
        _TRUNCATE,
        format,
        args
    );
    va_end(args);
}

static int FindResourceIndexById(
    const PipelineDescription *description,
    const char *resourceId
){
    if (resourceId == NULL) {
        return -1;
    }
    for (int index = 0; index < description->numResources; ++index) {
        if (strcmp(description->resources[index].id, resourceId) == 0) {
            return index;
        }
    }
    return -1;
}

static void InitResourceDefaults(PipelineResource *resource){
    memset(resource, 0, sizeof(*resource));
    resource->pixelFormat = DEFAULT_PIXEL_FORMAT;
    resource->resolution.mode = PipelineResolutionModeFramebuffer;
    resource->historyLength = 1;
    resource->textureFilter = DEFAULT_TEXTURE_FILTER;
    resource->textureWrap = DEFAULT_TEXTURE_WRAP;
}

static void InitPassDefaults(PipelinePass *pass){
    memset(pass, 0, sizeof(*pass));
    pass->type = PipelinePassTypeFragment;
    pass->clear.clearColor[0] = 0.0f;
    pass->clear.clearColor[1] = 0.0f;
    pass->clear.clearColor[2] = 0.0f;
    pass->clear.clearColor[3] = 0.0f;
    pass->clear.clearDepth = 1.0f;
    pass->workGroupSize[0] = 0;
    pass->workGroupSize[1] = 0;
    pass->workGroupSize[2] = 0;
}

static bool ParseStringField(
    cJSON *jsonObject,
    const char *fieldName,
    char *dst,
    size_t dstSizeInBytes,
    bool required,
    char *errorMessage,
    size_t errorMessageSizeInBytes
){
    cJSON *node = cJSON_GetObjectItemCaseSensitive(jsonObject, fieldName);
    if (node == NULL || cJSON_IsString(node) == false || node->valuestring == NULL) {
        if (required) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"%s\" must be a string.",
                fieldName
            );
            return false;
        }
        return true;
    }
    strlcpy(dst, node->valuestring, dstSizeInBytes);
    return true;
}

static bool ParseNumberFieldAsInt(
    cJSON *jsonObject,
    const char *fieldName,
    int *dst,
    bool required,
    char *errorMessage,
    size_t errorMessageSizeInBytes
){
    cJSON *node = cJSON_GetObjectItemCaseSensitive(jsonObject, fieldName);
    if (node == NULL) {
        if (required) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"%s\" must be specified.",
                fieldName
            );
            return false;
        }
        return true;
    }
    if (cJSON_IsNumber(node) == false) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "\"%s\" must be a number.",
            fieldName
        );
        return false;
    }
    *dst = (int)cJSON_GetNumberValue(node);
    return true;
}

static bool ParseNumberFieldAsFloat(
    cJSON *jsonObject,
    const char *fieldName,
    float *dst,
    bool required,
    char *errorMessage,
    size_t errorMessageSizeInBytes
){
    cJSON *node = cJSON_GetObjectItemCaseSensitive(jsonObject, fieldName);
    if (node == NULL) {
        if (required) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"%s\" must be specified.",
                fieldName
            );
            return false;
        }
        return true;
    }
    if (cJSON_IsNumber(node) == false) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "\"%s\" must be a number.",
            fieldName
        );
        return false;
    }
    *dst = (float)cJSON_GetNumberValue(node);
    return true;
}

static const char *LookupNameByPassType(PipelinePassType type){
    for (size_t index = 0; index < sizeof(s_passTypeTable) / sizeof(s_passTypeTable[0]); ++index) {
        if (s_passTypeTable[index].value == type) {
            return s_passTypeTable[index].name;
        }
    }
    return NULL;
}

static const char *LookupNameByResourceAccess(PipelineResourceAccess access){
    for (size_t index = 0; index < sizeof(s_resourceAccessTable) / sizeof(s_resourceAccessTable[0]); ++index) {
        if (s_resourceAccessTable[index].value == access) {
            return s_resourceAccessTable[index].name;
        }
    }
    return NULL;
}

static const char *LookupNameByResolutionMode(PipelineResolutionMode mode){
    for (size_t index = 0; index < sizeof(s_resolutionModeTable) / sizeof(s_resolutionModeTable[0]); ++index) {
        if (s_resolutionModeTable[index].value == mode) {
            return s_resolutionModeTable[index].name;
        }
    }
    return NULL;
}

static const char *LookupNameByPixelFormat(PixelFormat pixelFormat){
    for (size_t index = 0; index < sizeof(s_pixelFormatTable) / sizeof(s_pixelFormatTable[0]); ++index) {
        if (s_pixelFormatTable[index].value == pixelFormat) {
            return s_pixelFormatTable[index].name;
        }
    }
    return NULL;
}

static const char *LookupNameByTextureFilter(TextureFilter filter){
    for (size_t index = 0; index < sizeof(s_textureFilterTable) / sizeof(s_textureFilterTable[0]); ++index) {
        if (s_textureFilterTable[index].value == filter) {
            return s_textureFilterTable[index].name;
        }
    }
    return NULL;
}

static const char *LookupNameByTextureWrap(TextureWrap wrap){
    for (size_t index = 0; index < sizeof(s_textureWrapTable) / sizeof(s_textureWrapTable[0]); ++index) {
        if (s_textureWrapTable[index].value == wrap) {
            return s_textureWrapTable[index].name;
        }
    }
    return NULL;
}

void PipelineDescriptionInit(PipelineDescription *description){
    if (description == NULL) {
        return;
    }
    memset(description, 0, sizeof(*description));
    for (int resourceIndex = 0; resourceIndex < PIPELINE_MAX_RESOURCES; ++resourceIndex) {
        InitResourceDefaults(&description->resources[resourceIndex]);
    }
    for (int passIndex = 0; passIndex < PIPELINE_MAX_PASSES; ++passIndex) {
        InitPassDefaults(&description->passes[passIndex]);
    }
    description->numResources = 0;
    description->numPasses = 0;
}

bool PipelinePassTypeFromString(const char *value, PipelinePassType *type){
    if (value == NULL || type == NULL) {
        return false;
    }
    for (size_t index = 0; index < sizeof(s_passTypeTable) / sizeof(s_passTypeTable[0]); ++index) {
        if (strcmp(value, s_passTypeTable[index].name) == 0) {
            *type = s_passTypeTable[index].value;
            return true;
        }
    }
    return false;
}

const char *PipelinePassTypeToString(PipelinePassType type){
    return LookupNameByPassType(type);
}

bool PipelineResourceAccessFromString(const char *value, PipelineResourceAccess *access){
    if (value == NULL || access == NULL) {
        return false;
    }
    for (size_t index = 0; index < sizeof(s_resourceAccessTable) / sizeof(s_resourceAccessTable[0]); ++index) {
        if (strcmp(value, s_resourceAccessTable[index].name) == 0) {
            *access = s_resourceAccessTable[index].value;
            return true;
        }
    }
    return false;
}

const char *PipelineResourceAccessToString(PipelineResourceAccess access){
    return LookupNameByResourceAccess(access);
}

bool PipelineResolutionModeFromString(const char *value, PipelineResolutionMode *mode){
    if (value == NULL || mode == NULL) {
        return false;
    }
    for (size_t index = 0; index < sizeof(s_resolutionModeTable) / sizeof(s_resolutionModeTable[0]); ++index) {
        if (strcmp(value, s_resolutionModeTable[index].name) == 0) {
            *mode = s_resolutionModeTable[index].value;
            return true;
        }
    }
    return false;
}

const char *PipelineResolutionModeToString(PipelineResolutionMode mode){
    return LookupNameByResolutionMode(mode);
}

bool PixelFormatFromPipelineString(const char *value, PixelFormat *pixelFormat){
    if (value == NULL || pixelFormat == NULL) {
        return false;
    }
    for (size_t index = 0; index < sizeof(s_pixelFormatTable) / sizeof(s_pixelFormatTable[0]); ++index) {
        if (strcmp(value, s_pixelFormatTable[index].name) == 0) {
            *pixelFormat = s_pixelFormatTable[index].value;
            return true;
        }
    }
    return false;
}

const char *PixelFormatToPipelineString(PixelFormat pixelFormat){
    return LookupNameByPixelFormat(pixelFormat);
}

bool TextureFilterFromPipelineString(const char *value, TextureFilter *filter){
    if (value == NULL || filter == NULL) {
        return false;
    }
    for (size_t index = 0; index < sizeof(s_textureFilterTable) / sizeof(s_textureFilterTable[0]); ++index) {
        if (strcmp(value, s_textureFilterTable[index].name) == 0) {
            *filter = s_textureFilterTable[index].value;
            return true;
        }
    }
    return false;
}

const char *TextureFilterToPipelineString(TextureFilter filter){
    return LookupNameByTextureFilter(filter);
}

bool TextureWrapFromPipelineString(const char *value, TextureWrap *wrap){
    if (value == NULL || wrap == NULL) {
        return false;
    }
    for (size_t index = 0; index < sizeof(s_textureWrapTable) / sizeof(s_textureWrapTable[0]); ++index) {
        if (strcmp(value, s_textureWrapTable[index].name) == 0) {
            *wrap = s_textureWrapTable[index].value;
            return true;
        }
    }
    return false;
}

const char *TextureWrapToPipelineString(TextureWrap wrap){
    return LookupNameByTextureWrap(wrap);
}

static bool DeserializeResource(
    PipelineDescription *description,
    cJSON *jsonResource,
    char *errorMessage,
    size_t errorMessageSizeInBytes
){
    if (description->numResources >= PIPELINE_MAX_RESOURCES) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "Too many resources. Max supported is %d.",
            PIPELINE_MAX_RESOURCES
        );
        return false;
    }

    PipelineResource *resource = &description->resources[description->numResources];
    InitResourceDefaults(resource);

    if (ParseStringField(
            jsonResource,
            "id",
            resource->id,
            sizeof(resource->id),
            true,
            errorMessage,
            errorMessageSizeInBytes
        ) == false
    ) {
        return false;
    }

    cJSON *jsonPixelFormat = cJSON_GetObjectItemCaseSensitive(jsonResource, "pixelFormat");
    if (jsonPixelFormat != NULL) {
        if (cJSON_IsString(jsonPixelFormat) == false || jsonPixelFormat->valuestring == NULL) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"pixelFormat\" must be a string in resource \"%s\".",
                resource->id
            );
            return false;
        }
        if (!PixelFormatFromPipelineString(jsonPixelFormat->valuestring, &resource->pixelFormat)) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "Unknown pixel format \"%s\" in resource \"%s\".",
                jsonPixelFormat->valuestring,
                resource->id
            );
            return false;
        }
    }

    cJSON *jsonResolution = cJSON_GetObjectItemCaseSensitive(jsonResource, "resolution");
    if (jsonResolution != NULL) {
        if (cJSON_IsObject(jsonResolution) == false) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"resolution\" must be an object in resource \"%s\".",
                resource->id
            );
            return false;
        }
        char modeString[32] = {0};
        if (ParseStringField(
                jsonResolution,
                "mode",
                modeString,
                sizeof(modeString),
                true,
                errorMessage,
                errorMessageSizeInBytes
            ) == false
        ) {
            return false;
        }
        if (!PipelineResolutionModeFromString(modeString, &resource->resolution.mode)) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "Unknown resolution mode \"%s\" in resource \"%s\".",
                modeString,
                resource->id
            );
            return false;
        }
        if (resource->resolution.mode == PipelineResolutionModeFixed) {
            if (!ParseNumberFieldAsInt(
                    jsonResolution,
                    "width",
                    &resource->resolution.width,
                    true,
                    errorMessage,
                    errorMessageSizeInBytes
                )
            ) {
                return false;
            }
            if (!ParseNumberFieldAsInt(
                    jsonResolution,
                    "height",
                    &resource->resolution.height,
                    true,
                    errorMessage,
                    errorMessageSizeInBytes
                )
            ) {
                return false;
            }
        }
    }

    cJSON *jsonHistoryLength = cJSON_GetObjectItemCaseSensitive(jsonResource, "historyLength");
    if (jsonHistoryLength != NULL) {
        if (cJSON_IsNumber(jsonHistoryLength) == false) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"historyLength\" must be a positive integer in resource \"%s\".",
                resource->id
            );
            return false;
        }
        int historyLength = (int)cJSON_GetNumberValue(jsonHistoryLength);
        if (historyLength <= 0 || historyLength > PIPELINE_MAX_HISTORY_LENGTH) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"historyLength\" of resource \"%s\" must be between 1 and %d.",
                resource->id,
                PIPELINE_MAX_HISTORY_LENGTH
            );
            return false;
        }
        resource->historyLength = historyLength;
    }

    cJSON *jsonSampler = cJSON_GetObjectItemCaseSensitive(jsonResource, "sampler");
    if (jsonSampler != NULL) {
        if (cJSON_IsObject(jsonSampler) == false) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"sampler\" must be an object in resource \"%s\".",
                resource->id
            );
            return false;
        }
        cJSON *jsonFilter = cJSON_GetObjectItemCaseSensitive(jsonSampler, "filter");
        if (jsonFilter != NULL) {
            if (cJSON_IsString(jsonFilter) == false || jsonFilter->valuestring == NULL) {
                SetErrorMessage(
                    errorMessage,
                    errorMessageSizeInBytes,
                    "\"sampler.filter\" must be a string in resource \"%s\".",
                    resource->id
                );
                return false;
            }
            if (!TextureFilterFromPipelineString(jsonFilter->valuestring, &resource->textureFilter)) {
                SetErrorMessage(
                    errorMessage,
                    errorMessageSizeInBytes,
                    "Unknown sampler filter \"%s\" in resource \"%s\".",
                    jsonFilter->valuestring,
                    resource->id
                );
                return false;
            }
        }
        cJSON *jsonWrap = cJSON_GetObjectItemCaseSensitive(jsonSampler, "wrap");
        if (jsonWrap != NULL) {
            if (cJSON_IsString(jsonWrap) == false || jsonWrap->valuestring == NULL) {
                SetErrorMessage(
                    errorMessage,
                    errorMessageSizeInBytes,
                    "\"sampler.wrap\" must be a string in resource \"%s\".",
                    resource->id
                );
                return false;
            }
            if (!TextureWrapFromPipelineString(jsonWrap->valuestring, &resource->textureWrap)) {
                SetErrorMessage(
                    errorMessage,
                    errorMessageSizeInBytes,
                    "Unknown sampler wrap \"%s\" in resource \"%s\".",
                    jsonWrap->valuestring,
                    resource->id
                );
                return false;
            }
        }
    }

    description->numResources++;
    return true;
}

static bool DeserializeBinding(
    PipelineDescription *description,
    cJSON *jsonBinding,
    PipelineResourceBinding *binding,
    char *errorMessage,
    size_t errorMessageSizeInBytes
){
    char resourceId[PIPELINE_MAX_RESOURCE_ID_LENGTH] = {0};
    if (ParseStringField(
            jsonBinding,
            "resource",
            resourceId,
            sizeof(resourceId),
            true,
            errorMessage,
            errorMessageSizeInBytes
        ) == false
    ) {
        return false;
    }
    int resourceIndex = FindResourceIndexById(description, resourceId);
    if (resourceIndex < 0) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "Binding references undefined resource \"%s\".",
            resourceId
        );
        return false;
    }
    binding->resourceIndex = resourceIndex;

    cJSON *jsonUsage = cJSON_GetObjectItemCaseSensitive(jsonBinding, "usage");
    if (jsonUsage != NULL) {
        if (cJSON_IsString(jsonUsage) == false || jsonUsage->valuestring == NULL) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"usage\" must be a string for resource \"%s\".",
                resourceId
            );
            return false;
        }
        if (!PipelineResourceAccessFromString(jsonUsage->valuestring, &binding->access)) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "Unknown usage \"%s\" for resource \"%s\".",
                jsonUsage->valuestring,
                resourceId
            );
            return false;
        }
    } else {
        binding->access = PipelineResourceAccessSampled;
    }

    binding->historyOffset = 0;
    cJSON *jsonHistoryOffset = cJSON_GetObjectItemCaseSensitive(jsonBinding, "historyOffset");
    if (jsonHistoryOffset != NULL) {
        if (cJSON_IsNumber(jsonHistoryOffset) == false) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"historyOffset\" must be an integer for resource \"%s\".",
                resourceId
            );
            return false;
        }
        binding->historyOffset = (int)cJSON_GetNumberValue(jsonHistoryOffset);
    }
    return true;
}

static bool DeserializeBindingsArray(
    PipelineDescription *description,
    cJSON *jsonArray,
    PipelineResourceBinding *bindings,
    int *numBindings,
    const char *arrayName,
    char *errorMessage,
    size_t errorMessageSizeInBytes
){
    if (jsonArray == NULL) {
        *numBindings = 0;
        return true;
    }
    if (cJSON_IsArray(jsonArray) == false) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "\"%s\" must be an array.",
            arrayName
        );
        return false;
    }

    int count = 0;
    cJSON *jsonBinding = NULL;
    cJSON_ArrayForEach(jsonBinding, jsonArray) {
        if (count >= PIPELINE_MAX_BINDINGS_PER_PASS) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"%s\" has too many bindings. Max supported is %d.",
                arrayName,
                PIPELINE_MAX_BINDINGS_PER_PASS
            );
            return false;
        }
        if (cJSON_IsObject(jsonBinding) == false) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "Each element in \"%s\" must be an object.",
                arrayName
            );
            return false;
        }
        if (!DeserializeBinding(
                description,
                jsonBinding,
                &bindings[count],
                errorMessage,
                errorMessageSizeInBytes
            )
        ) {
            return false;
        }
        ++count;
    }
    *numBindings = count;
    return true;
}

static bool DeserializePass(
    PipelineDescription *description,
    cJSON *jsonPass,
    char *errorMessage,
    size_t errorMessageSizeInBytes
){
    if (description->numPasses >= PIPELINE_MAX_PASSES) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "Too many passes. Max supported is %d.",
            PIPELINE_MAX_PASSES
        );
        return false;
    }

    PipelinePass *pass = &description->passes[description->numPasses];
    InitPassDefaults(pass);

    if (!ParseStringField(
            jsonPass,
            "name",
            pass->name,
            sizeof(pass->name),
            true,
            errorMessage,
            errorMessageSizeInBytes
        )
    ) {
        return false;
    }

    char typeString[32] = {0};
    if (!ParseStringField(
            jsonPass,
            "type",
            typeString,
            sizeof(typeString),
            true,
            errorMessage,
            errorMessageSizeInBytes
        )
    ) {
        return false;
    }
    if (!PipelinePassTypeFromString(typeString, &pass->type)) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "Unknown pass type \"%s\" (pass \"%s\").",
            typeString,
            pass->name
        );
        return false;
    }

    if (!ParseStringField(
            jsonPass,
            "shader",
            pass->shaderPath,
            sizeof(pass->shaderPath),
            false,
            errorMessage,
            errorMessageSizeInBytes
        )
    ) {
        return false;
    }

    cJSON *jsonInputs = cJSON_GetObjectItemCaseSensitive(jsonPass, "inputs");
    if (!DeserializeBindingsArray(
            description,
            jsonInputs,
            pass->inputs,
            &pass->numInputs,
            "inputs",
            errorMessage,
            errorMessageSizeInBytes
        )
    ) {
        return false;
    }

    cJSON *jsonOutputs = cJSON_GetObjectItemCaseSensitive(jsonPass, "outputs");
    if (!DeserializeBindingsArray(
            description,
            jsonOutputs,
            pass->outputs,
            &pass->numOutputs,
            "outputs",
            errorMessage,
            errorMessageSizeInBytes
        )
    ) {
        return false;
    }

    cJSON *jsonClear = cJSON_GetObjectItemCaseSensitive(jsonPass, "clear");
    if (jsonClear != NULL) {
        if (cJSON_IsObject(jsonClear) == false) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"clear\" must be an object in pass \"%s\".",
                pass->name
            );
            return false;
        }
        cJSON *jsonColor = cJSON_GetObjectItemCaseSensitive(jsonClear, "color");
        if (jsonColor != NULL) {
            if (cJSON_IsArray(jsonColor) == false || cJSON_GetArraySize(jsonColor) != 4) {
                SetErrorMessage(
                    errorMessage,
                    errorMessageSizeInBytes,
                    "\"clear.color\" must be an array[4] in pass \"%s\".",
                    pass->name
                );
                return false;
            }
            pass->clear.enableColorClear = true;
            for (int element = 0; element < 4; ++element) {
                cJSON *value = cJSON_GetArrayItem(jsonColor, element);
                if (cJSON_IsNumber(value) == false) {
                    SetErrorMessage(
                        errorMessage,
                        errorMessageSizeInBytes,
                        "\"clear.color[%d]\" must be a number in pass \"%s\".",
                        element,
                        pass->name
                    );
                    return false;
                }
                pass->clear.clearColor[element] = (float)cJSON_GetNumberValue(value);
            }
        }
        cJSON *jsonDepth = cJSON_GetObjectItemCaseSensitive(jsonClear, "depth");
        if (jsonDepth != NULL) {
            if (cJSON_IsNumber(jsonDepth) == false) {
                SetErrorMessage(
                    errorMessage,
                    errorMessageSizeInBytes,
                    "\"clear.depth\" must be a number in pass \"%s\".",
                    pass->name
                );
                return false;
            }
            pass->clear.enableDepthClear = true;
            pass->clear.clearDepth = (float)cJSON_GetNumberValue(jsonDepth);
        }
    }

    cJSON *jsonWorkGroupSize = cJSON_GetObjectItemCaseSensitive(jsonPass, "workGroupSize");
    if (jsonWorkGroupSize != NULL) {
        if (cJSON_IsArray(jsonWorkGroupSize) == false || cJSON_GetArraySize(jsonWorkGroupSize) != 3) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "\"workGroupSize\" must be an array[3] in pass \"%s\".",
                pass->name
            );
            return false;
        }
        pass->overrideWorkGroupSize = true;
        for (int dimension = 0; dimension < 3; ++dimension) {
            cJSON *value = cJSON_GetArrayItem(jsonWorkGroupSize, dimension);
            if (cJSON_IsNumber(value) == false) {
                SetErrorMessage(
                    errorMessage,
                    errorMessageSizeInBytes,
                    "\"workGroupSize[%d]\" must be a number in pass \"%s\".",
                    dimension,
                    pass->name
                );
                return false;
            }
            int intValue = (int)cJSON_GetNumberValue(value);
            if (intValue < 0) {
                SetErrorMessage(
                    errorMessage,
                    errorMessageSizeInBytes,
                    "\"workGroupSize[%d]\" must be >= 0 in pass \"%s\".",
                    dimension,
                    pass->name
                );
                return false;
            }
            pass->workGroupSize[dimension] = (GLuint)intValue;
        }
    }

    description->numPasses++;
    return true;
}

static void PipelineDescriptionEmitWarnings(const PipelineDescription *description){
    if (description == NULL) {
        return;
    }
    for (int passIndex = 0; passIndex < description->numPasses; ++passIndex) {
        const PipelinePass *pass = &description->passes[passIndex];
        if (pass->type != PipelinePassTypeCompute) {
            continue;
        }
        for (int outputIndex = 0; outputIndex < pass->numOutputs; ++outputIndex) {
            const PipelineResourceBinding *output = &pass->outputs[outputIndex];
            if (output->access != PipelineResourceAccessImageWrite) {
                continue;
            }
            bool hasMatchingInput = false;
            for (int inputIndex = 0; inputIndex < pass->numInputs; ++inputIndex) {
                const PipelineResourceBinding *input = &pass->inputs[inputIndex];
                if (input->resourceIndex == output->resourceIndex) {
                    hasMatchingInput = true;
                    break;
                }
            }
            if (!hasMatchingInput) {
                const PipelineResource *resource = NULL;
                if (output->resourceIndex >= 0 && output->resourceIndex < description->numResources) {
                    resource = &description->resources[output->resourceIndex];
                }
                const char *resourceId = (resource != NULL && resource->id[0] != '\0') ? resource->id : "(unnamed)";
                printf(
                    "[Pipeline Warning] compute pass \"%s\" writes to resource \"%s\" without an input binding. "
                    "Exported executables will fall back to GL_READ_WRITE for this resource at runtime; add a history_read input if the shader needs the previous contents (otherwise you can ignore this warning).\n",
                    pass->name,
                    resourceId
                );
            }
        }
    }
}

bool PipelineDescriptionDeserializeFromJson(
    PipelineDescription *description,
    cJSON *jsonRoot,
    char *errorMessage,
    size_t errorMessageSizeInBytes
){
    if (description == NULL || jsonRoot == NULL) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "Invalid arguments."
        );
        return false;
    }

    if (cJSON_IsObject(jsonRoot) == false) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "Pipeline root must be an object."
        );
        return false;
    }

    PipelineDescriptionInit(description);

    cJSON *jsonResources = cJSON_GetObjectItemCaseSensitive(jsonRoot, "resources");
    if (jsonResources == NULL || cJSON_IsArray(jsonResources) == false) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "\"resources\" array is required."
        );
        return false;
    }

    cJSON *jsonResource = NULL;
    cJSON_ArrayForEach(jsonResource, jsonResources) {
        if (cJSON_IsObject(jsonResource) == false) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "Each resource entry must be an object."
            );
            return false;
        }
        if (!DeserializeResource(
                description,
                jsonResource,
                errorMessage,
                errorMessageSizeInBytes
            )
        ) {
            return false;
        }
    }

    cJSON *jsonPasses = cJSON_GetObjectItemCaseSensitive(jsonRoot, "passes");
    if (jsonPasses == NULL || cJSON_IsArray(jsonPasses) == false) {
        SetErrorMessage(
            errorMessage,
            errorMessageSizeInBytes,
            "\"passes\" array is required."
        );
        return false;
    }

    cJSON *jsonPass = NULL;
    cJSON_ArrayForEach(jsonPass, jsonPasses) {
        if (cJSON_IsObject(jsonPass) == false) {
            SetErrorMessage(
                errorMessage,
                errorMessageSizeInBytes,
                "Each pass entry must be an object."
            );
            return false;
        }
        if (!DeserializePass(
                description,
                jsonPass,
                errorMessage,
                errorMessageSizeInBytes
            )
        ) {
            return false;
        }
    }

    PipelineDescriptionEmitWarnings(description);
    return true;
}

static cJSON *SerializeResolution(const PipelineResource *resource){
    cJSON *jsonResolution = cJSON_CreateObject();
    if (jsonResolution == NULL) {
        return NULL;
    }
    const char *modeString = PipelineResolutionModeToString(resource->resolution.mode);
    if (modeString == NULL) {
        modeString = "framebuffer";
    }
    cJSON_AddStringToObject(jsonResolution, "mode", modeString);
    if (resource->resolution.mode == PipelineResolutionModeFixed) {
        cJSON_AddNumberToObject(jsonResolution, "width", resource->resolution.width);
        cJSON_AddNumberToObject(jsonResolution, "height", resource->resolution.height);
    }
    return jsonResolution;
}

static cJSON *SerializeSampler(const PipelineResource *resource){
    cJSON *jsonSampler = cJSON_CreateObject();
    if (jsonSampler == NULL) {
        return NULL;
    }
    const char *filterString = TextureFilterToPipelineString(resource->textureFilter);
    if (filterString == NULL) {
        filterString = TextureFilterToPipelineString(DEFAULT_TEXTURE_FILTER);
    }
    const char *wrapString = TextureWrapToPipelineString(resource->textureWrap);
    if (wrapString == NULL) {
        wrapString = TextureWrapToPipelineString(DEFAULT_TEXTURE_WRAP);
    }
    cJSON_AddStringToObject(jsonSampler, "filter", filterString);
    cJSON_AddStringToObject(jsonSampler, "wrap", wrapString);
    return jsonSampler;
}

static cJSON *SerializeBinding(
    const PipelineDescription *description,
    const PipelineResourceBinding *binding
){
    if (binding->resourceIndex < 0 || binding->resourceIndex >= description->numResources) {
        return NULL;
    }
    const PipelineResource *resource = &description->resources[binding->resourceIndex];

    cJSON *jsonBinding = cJSON_CreateObject();
    if (jsonBinding == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(jsonBinding, "resource", resource->id);
    const char *usageString = PipelineResourceAccessToString(binding->access);
    if (usageString != NULL) {
        cJSON_AddStringToObject(jsonBinding, "usage", usageString);
    }
    if (binding->historyOffset != 0) {
        cJSON_AddNumberToObject(jsonBinding, "historyOffset", binding->historyOffset);
    }
    return jsonBinding;
}

static cJSON *SerializeBindingsArray(
    const PipelineDescription *description,
    const PipelineResourceBinding *bindings,
    int numBindings
){
    if (numBindings <= 0) {
        return NULL;
    }
    cJSON *jsonArray = cJSON_CreateArray();
    if (jsonArray == NULL) {
        return NULL;
    }
    for (int index = 0; index < numBindings; ++index) {
        cJSON *jsonBinding = SerializeBinding(description, &bindings[index]);
        if (jsonBinding == NULL) {
            cJSON_Delete(jsonArray);
            return NULL;
        }
        cJSON_AddItemToArray(jsonArray, jsonBinding);
    }
    return jsonArray;
}

static cJSON *SerializeClear(const PipelinePass *pass){
    if (pass->clear.enableColorClear == false && pass->clear.enableDepthClear == false) {
        return NULL;
    }
    cJSON *jsonClear = cJSON_CreateObject();
    if (jsonClear == NULL) {
        return NULL;
    }
    if (pass->clear.enableColorClear) {
        cJSON *jsonColor = cJSON_CreateArray();
        if (jsonColor == NULL) {
            cJSON_Delete(jsonClear);
            return NULL;
        }
        for (int index = 0; index < 4; ++index) {
            cJSON_AddItemToArray(
                jsonColor,
                cJSON_CreateNumber(pass->clear.clearColor[index])
            );
        }
        cJSON_AddItemToObject(jsonClear, "color", jsonColor);
    }
    if (pass->clear.enableDepthClear) {
        cJSON_AddNumberToObject(jsonClear, "depth", pass->clear.clearDepth);
    }
    return jsonClear;
}

static cJSON *SerializeWorkGroupSize(const PipelinePass *pass){
    if (pass->overrideWorkGroupSize == false) {
        return NULL;
    }
    cJSON *jsonWorkGroupSize = cJSON_CreateArray();
    if (jsonWorkGroupSize == NULL) {
        return NULL;
    }
    for (int dimension = 0; dimension < 3; ++dimension) {
        cJSON_AddItemToArray(
            jsonWorkGroupSize,
            cJSON_CreateNumber(pass->workGroupSize[dimension])
        );
    }
    return jsonWorkGroupSize;
}

cJSON *PipelineDescriptionSerializeToJson(const PipelineDescription *description){
    if (description == NULL) {
        return NULL;
    }
    cJSON *jsonRoot = cJSON_CreateObject();
    if (jsonRoot == NULL) {
        return NULL;
    }

    cJSON *jsonResources = cJSON_CreateArray();
    if (jsonResources == NULL) {
        cJSON_Delete(jsonRoot);
        return NULL;
    }
    cJSON_AddItemToObject(jsonRoot, "resources", jsonResources);

    for (int resourceIndex = 0; resourceIndex < description->numResources; ++resourceIndex) {
        const PipelineResource *resource = &description->resources[resourceIndex];
        cJSON *jsonResource = cJSON_CreateObject();
        if (jsonResource == NULL) {
            cJSON_Delete(jsonRoot);
            return NULL;
        }
        cJSON_AddItemToArray(jsonResources, jsonResource);
        cJSON_AddStringToObject(jsonResource, "id", resource->id);
        const char *pixelFormatString = PixelFormatToPipelineString(resource->pixelFormat);
        if (pixelFormatString != NULL) {
            cJSON_AddStringToObject(jsonResource, "pixelFormat", pixelFormatString);
        }
        cJSON *jsonResolution = SerializeResolution(resource);
        if (jsonResolution != NULL) {
            cJSON_AddItemToObject(jsonResource, "resolution", jsonResolution);
        }
        cJSON_AddNumberToObject(jsonResource, "historyLength", resource->historyLength);
        cJSON *jsonSampler = SerializeSampler(resource);
        if (jsonSampler != NULL) {
            cJSON_AddItemToObject(jsonResource, "sampler", jsonSampler);
        }
    }

    cJSON *jsonPasses = cJSON_CreateArray();
    if (jsonPasses == NULL) {
        cJSON_Delete(jsonRoot);
        return NULL;
    }
    cJSON_AddItemToObject(jsonRoot, "passes", jsonPasses);

    for (int passIndex = 0; passIndex < description->numPasses; ++passIndex) {
        const PipelinePass *pass = &description->passes[passIndex];
        cJSON *jsonPass = cJSON_CreateObject();
        if (jsonPass == NULL) {
            cJSON_Delete(jsonRoot);
            return NULL;
        }
        cJSON_AddItemToArray(jsonPasses, jsonPass);
        cJSON_AddStringToObject(jsonPass, "name", pass->name);
        const char *typeString = PipelinePassTypeToString(pass->type);
        if (typeString != NULL) {
            cJSON_AddStringToObject(jsonPass, "type", typeString);
        }
        if (pass->shaderPath[0] != '\0') {
            cJSON_AddStringToObject(jsonPass, "shader", pass->shaderPath);
        }

        cJSON *jsonInputs = SerializeBindingsArray(description, pass->inputs, pass->numInputs);
        if (jsonInputs != NULL) {
            cJSON_AddItemToObject(jsonPass, "inputs", jsonInputs);
        }

        cJSON *jsonOutputs = SerializeBindingsArray(description, pass->outputs, pass->numOutputs);
        if (jsonOutputs != NULL) {
            cJSON_AddItemToObject(jsonPass, "outputs", jsonOutputs);
        }

        cJSON *jsonClear = SerializeClear(pass);
        if (jsonClear != NULL) {
            cJSON_AddItemToObject(jsonPass, "clear", jsonClear);
        }

        cJSON *jsonWorkGroupSize = SerializeWorkGroupSize(pass);
        if (jsonWorkGroupSize != NULL) {
            cJSON_AddItemToObject(jsonPass, "workGroupSize", jsonWorkGroupSize);
        }
    }

    return jsonRoot;
}

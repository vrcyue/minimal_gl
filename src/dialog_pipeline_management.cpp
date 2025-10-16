/* Copyright (C) 2018 Yosshin(@yosshin4004) */

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "config.h"
#include "common.h"
#include "app.h"
#include "graphics.h"
#include "pipeline_description.h"
#include "dialog_pipeline_management.h"
#include "resource/resource.h"

static char s_pipelineFeedback[256] = {0};

static void AppendFormattedText(
	char *dst,
	size_t dstSizeInBytes,
	const char *format,
	...
){
	size_t currentLength = strlen(dst);
	if (currentLength >= dstSizeInBytes) {
		return;
	}

	va_list args;
	va_start(args, format);
	_vsnprintf_s(
		dst + currentLength,
		dstSizeInBytes - currentLength,
		_TRUNCATE,
		format,
		args
	);
	va_end(args);
}

static void SetPipelineFeedback(
	const char *format,
	...
){
	s_pipelineFeedback[0] = '\0';
	if (format == NULL) {
		return;
	}

	va_list args;
	va_start(args, format);
	_vsnprintf_s(
		s_pipelineFeedback,
		sizeof(s_pipelineFeedback),
		_TRUNCATE,
		format,
		args
	);
	va_end(args);
}

static void FormatResourceSummary(
	const PipelineDescription *pipeline,
	int resourceIndex,
	char *buffer,
	size_t bufferSizeInBytes
){
	if (pipeline == NULL || resourceIndex < 0 || resourceIndex >= pipeline->numResources) {
		strlcpy(buffer, "(invalid resource)", bufferSizeInBytes);
		return;
	}
	const PipelineResource *resource = &pipeline->resources[resourceIndex];
	const char *pixelFormat = PixelFormatToPipelineString(resource->pixelFormat);
	const char *resolutionMode = PipelineResolutionModeToString(resource->resolution.mode);

	buffer[0] = '\0';
	_snprintf_s(
		buffer,
		bufferSizeInBytes,
		_TRUNCATE,
		"%d: %s | format=%s",
		resourceIndex,
		resource->id[0] != '\0' ? resource->id : "(unnamed)",
		pixelFormat != NULL ? pixelFormat : "unknown"
	);

	switch (resource->resolution.mode) {
		case PipelineResolutionModeFixed: {
			AppendFormattedText(
				buffer,
				bufferSizeInBytes,
				" | resolution=%dx%d",
				resource->resolution.width,
				resource->resolution.height
			);
		} break;
		case PipelineResolutionModeFramebuffer:
		default: {
			AppendFormattedText(
				buffer,
				bufferSizeInBytes,
				" | resolution=%s",
				resolutionMode != NULL ? resolutionMode : "framebuffer"
			);
		} break;
	}

	AppendFormattedText(
		buffer,
		bufferSizeInBytes,
		" | history=%d",
		resource->historyLength
	);
}

static void AppendBindingSummary(
	const PipelineDescription *pipeline,
	const PipelineResourceBinding *binding,
	char *buffer,
	size_t bufferSizeInBytes
){
	if (binding == NULL) {
		return;
	}

	const PipelineResource *resource = NULL;
	if (
		pipeline != NULL
		&& binding->resourceIndex >= 0
		&& binding->resourceIndex < pipeline->numResources
	) {
		resource = &pipeline->resources[binding->resourceIndex];
	}

	const char *resourceName = (resource != NULL && resource->id[0] != '\0') ? resource->id : "(invalid)";
	const char *usage = PipelineResourceAccessToString(binding->access);

	AppendFormattedText(buffer, bufferSizeInBytes, "%s", resourceName);
	if (usage != NULL) {
		AppendFormattedText(buffer, bufferSizeInBytes, ":%s", usage);
	}
	if (binding->historyOffset != 0) {
		AppendFormattedText(buffer, bufferSizeInBytes, "(%d)", binding->historyOffset);
	}
}

static void FormatPassSummary(
	const PipelineDescription *pipeline,
	int passIndex,
	char *buffer,
	size_t bufferSizeInBytes
){
	if (pipeline == NULL || passIndex < 0 || passIndex >= pipeline->numPasses) {
		strlcpy(buffer, "(invalid pass)", bufferSizeInBytes);
		return;
	}

	const PipelinePass *pass = &pipeline->passes[passIndex];
	const char *type = PipelinePassTypeToString(pass->type);

	buffer[0] = '\0';
	_snprintf_s(
		buffer,
		bufferSizeInBytes,
		_TRUNCATE,
		"%d: %s [%s]",
		passIndex,
		pass->name[0] != '\0' ? pass->name : "(unnamed)",
		type != NULL ? type : "unknown"
	);

	if (pass->numInputs > 0) {
		AppendFormattedText(buffer, bufferSizeInBytes, " | In: ");
		for (int index = 0; index < pass->numInputs; ++index) {
			if (index > 0) {
				AppendFormattedText(buffer, bufferSizeInBytes, ", ");
			}
			AppendBindingSummary(
				pipeline,
				&pass->inputs[index],
				buffer,
				bufferSizeInBytes
			);
		}
	}
	if (pass->numOutputs > 0) {
		AppendFormattedText(buffer, bufferSizeInBytes, " | Out: ");
		for (int index = 0; index < pass->numOutputs; ++index) {
			if (index > 0) {
				AppendFormattedText(buffer, bufferSizeInBytes, ", ");
			}
			AppendBindingSummary(
				pipeline,
				&pass->outputs[index],
				buffer,
				bufferSizeInBytes
			);
		}
	}
}

static void UpdatePipelineDialogControls(HWND hDwnd){
	HWND resourcesList = GetDlgItem(hDwnd, IDC_PIPELINE_MANAGEMENT_RESOURCES);
	HWND passesList = GetDlgItem(hDwnd, IDC_PIPELINE_MANAGEMENT_PASSES);

	SendMessage(resourcesList, LB_RESETCONTENT, 0, 0);
	SendMessage(passesList, LB_RESETCONTENT, 0, 0);

	const PipelineDescription *activePipeline = GraphicsGetActivePipelineDescription();
	if (activePipeline != NULL) {
		char buffer[512] = {0};
		for (int resourceIndex = 0; resourceIndex < activePipeline->numResources; ++resourceIndex) {
			FormatResourceSummary(activePipeline, resourceIndex, buffer, sizeof(buffer));
			SendMessage(resourcesList, LB_ADDSTRING, 0, (LPARAM)buffer);
		}
		for (int passIndex = 0; passIndex < activePipeline->numPasses; ++passIndex) {
			FormatPassSummary(activePipeline, passIndex, buffer, sizeof(buffer));
			SendMessage(passesList, LB_ADDSTRING, 0, (LPARAM)buffer);
		}
	}

	char status[512] = {0};
	if (AppPipelineHasCustomDescription()) {
		const PipelineDescription *projectPipeline = AppPipelineGetProjectDescription();
		int resourceCount = projectPipeline != NULL ? projectPipeline->numResources : (activePipeline != NULL ? activePipeline->numResources : 0);
		int passCount = projectPipeline != NULL ? projectPipeline->numPasses : (activePipeline != NULL ? activePipeline->numPasses : 0);
		const char *lastFile = AppPipelineGetLastFileName();
		if (lastFile != NULL && lastFile[0] != '\0') {
			char baseName[MAX_PATH] = {0};
			SplitFileNameFromFilePath(baseName, sizeof(baseName), lastFile);
			const char *sourceLabel = baseName[0] != '\0' ? baseName : lastFile;
			_snprintf_s(
				status,
				sizeof(status),
				_TRUNCATE,
				"Custom pipeline active (%d resources / %d passes). Source: %s.",
				resourceCount,
				passCount,
				sourceLabel
			);
		} else {
			_snprintf_s(
				status,
				sizeof(status),
				_TRUNCATE,
				"Custom pipeline active (%d resources / %d passes).",
				resourceCount,
				passCount
			);
		}
	} else {
		int resourceCount = activePipeline != NULL ? activePipeline->numResources : 0;
		int passCount = activePipeline != NULL ? activePipeline->numPasses : 0;
		_snprintf_s(
			status,
			sizeof(status),
			_TRUNCATE,
			"Legacy pipeline active (%d resources / %d passes).",
			resourceCount,
			passCount
		);
	}

	if (s_pipelineFeedback[0] != '\0') {
		AppendFormattedText(status, sizeof(status), "  %s", s_pipelineFeedback);
	}
	SetDlgItemText(hDwnd, IDD_PIPELINE_MANAGEMENT_STATUS, status);
}

static void HandleLoadPipeline(HWND hDwnd){
	char fileName[MAX_PATH] = {0};
	const char *lastFile = AppPipelineGetLastFileName();
	if (lastFile != NULL && lastFile[0] != '\0') {
		strlcpy(fileName, lastFile, sizeof(fileName));
	}

	OPENFILENAME ofn = {0};
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hDwnd;
	ofn.lpstrFilter =
		"Pipeline JSON (*.json)\0*.json\0"
		"All files (*.*)\0*.*\0"
		"\0";
	ofn.lpstrFile = fileName;
	ofn.nMaxFile = sizeof(fileName);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
	ofn.lpstrTitle = (LPSTR)"Load pipeline description";

	if (GetOpenFileName(&ofn)) {
		char errorMessage[512] = {0};
		if (AppPipelineLoadFromFile(fileName, errorMessage, sizeof(errorMessage))) {
			char baseName[MAX_PATH] = {0};
			SplitFileNameFromFilePath(baseName, sizeof(baseName), fileName);
			SetPipelineFeedback("Loaded pipeline from %s.", baseName[0] != '\0'? baseName : fileName);
			UpdatePipelineDialogControls(hDwnd);
		} else {
			if (errorMessage[0] == '\0') {
				strlcpy(errorMessage, "Failed to load pipeline.", sizeof(errorMessage));
			}
			AppErrorMessageBox(APP_NAME, "%s", errorMessage);
		}
	}
}

static void HandleSavePipeline(HWND hDwnd){
	char fileName[MAX_PATH] = {0};
	const char *lastFile = AppPipelineGetLastFileName();
	if (lastFile != NULL && lastFile[0] != '\0') {
		strlcpy(fileName, lastFile, sizeof(fileName));
	}

	OPENFILENAME ofn = {0};
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hDwnd;
	ofn.lpstrFilter =
		"Pipeline JSON (*.json)\0*.json\0"
		"All files (*.*)\0*.*\0"
		"\0";
	ofn.lpstrFile = fileName;
	ofn.nMaxFile = sizeof(fileName);
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
	ofn.lpstrDefExt = (LPSTR)"json";
	ofn.lpstrTitle = (LPSTR)"Save pipeline description";

	if (GetSaveFileName(&ofn)) {
		char errorMessage[512] = {0};
		if (AppPipelineSaveToFile(fileName, errorMessage, sizeof(errorMessage))) {
			char baseName[MAX_PATH] = {0};
			SplitFileNameFromFilePath(baseName, sizeof(baseName), fileName);
			SetPipelineFeedback("Saved pipeline to %s.", baseName[0] != '\0'? baseName : fileName);
			UpdatePipelineDialogControls(hDwnd);
		} else {
			if (errorMessage[0] == '\0') {
				strlcpy(errorMessage, "Failed to save pipeline.", sizeof(errorMessage));
			}
			AppErrorMessageBox(APP_NAME, "%s", errorMessage);
		}
	}
}

static void HandleApplySample(HWND hDwnd){
	char errorMessage[512] = {0};
	if (AppPipelineApplySample(errorMessage, sizeof(errorMessage))) {
		SetPipelineFeedback("Applied sample pipeline.");
		UpdatePipelineDialogControls(hDwnd);
	} else {
		if (errorMessage[0] == '\0') {
			strlcpy(errorMessage, "Failed to apply sample pipeline.", sizeof(errorMessage));
		}
		AppErrorMessageBox(APP_NAME, "%s", errorMessage);
	}
}

static void HandleClearPipeline(HWND hDwnd){
	AppPipelineClearDescription();
	SetPipelineFeedback("Cleared custom pipeline.");
	UpdatePipelineDialogControls(hDwnd);
}

static LRESULT CALLBACK DialogFunc(
	HWND hDwnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam
){
	switch (uMsg) {
		case WM_INITDIALOG: {
			SetPipelineFeedback(NULL);
			UpdatePipelineDialogControls(hDwnd);
			return 1;
		} break;

		case WM_COMMAND: {
			switch (LOWORD(wParam)) {
				case IDC_PIPELINE_MANAGEMENT_LOAD_BUTTON: {
					HandleLoadPipeline(hDwnd);
					return 1;
				} break;

				case IDC_PIPELINE_MANAGEMENT_SAVE_BUTTON: {
					HandleSavePipeline(hDwnd);
					return 1;
				} break;

				case IDC_PIPELINE_MANAGEMENT_APPLY_SAMPLE_BUTTON: {
					HandleApplySample(hDwnd);
					return 1;
				} break;

				case IDC_PIPELINE_MANAGEMENT_CLEAR_BUTTON: {
					HandleClearPipeline(hDwnd);
					return 1;
				} break;

				case IDOK:
				case IDCANCEL: {
					EndDialog(hDwnd, DialogPipelineManagementResult_Closed);
					return 1;
				} break;
			}
		} break;
	}

	return 0;
}

DialogPipelineManagementResult
DialogPipelineManagement(void)
{
	return (DialogPipelineManagementResult)DialogBox(
		AppGetCurrentInstance(),
		"PIPELINE_MANAGEMENT",
		AppGetMainWindowHandle(),
		DialogFunc
	);
}

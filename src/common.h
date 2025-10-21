﻿/* Copyright (C) 2018 Yosshin(@yosshin4004) */

#ifndef _COMMON_H_
#define _COMMON_H_


#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#include <windows.h>
#include <shlobj.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <GL/gl3w.h>
#include <GL/glext.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

/* レガシー API に対する警告の抑制 */
#pragma warning(disable:4996)


#define PI 3.14159265359f


/* BSD 系に存在する strlcpy 関数の互換実装 */
size_t strlcpy(
			char	*dst,
	const	char	*src,
			size_t	siz
);

/* ceil アライメント */
int CeilAlign(
	int x,
	int align
);

/* 2 のべき乗 ceil アライメント */
int32_t Pow2CeilAlign(
	int32_t x
);

/* 解像度からミップマップレベル総数を求める */
int CalcNumMipmapLevelsFromResolution(
	int xReso,
	int yReso
);

/* ファイルパスからディレクトリ名部分を抽出 */
void SplitDirectoryPathFromFilePath(
	char *directoryPath,
	size_t directoryPathSizeInBytes,
	const char *filePath
);

/* ファイルパスからファイル名部分を抽出 */
void SplitFileNameFromFilePath(
	char *fileName,
	size_t fileNameSizeInBytes,
	const char *filePath
);

/* ディレクトリからディレクトリへの相対パス生成 */
void GenerateRelativePathFromDirectoryToDirectory(
	char *relativePath,
	size_t relativePathSizeInBytes,
	const char *fromDirectoryPath,
	const char *toDirectoryPath
);

/* ディレクトリからファイルへの相対パス生成 */
void GenerateRelativePathFromDirectoryToFile(
	char *relativePath,
	size_t relativePathSizeInBytes,
	const char *fromDirectoryPath,
	const char *toFilePath
);

/* ディレクトリパスとファイル名の結合 */
void GenerateCombinedPath(
	char *combinedPath,
	size_t combinedPathSizeInBytes,
	const char *directoryPath,
	const char *filePath
);

/* 妥当なファイルパスであるか確認 */
bool IsValidFileName(
	const char *fileName
);

/* 妥当なディレクトリパスであるか確認 */
bool IsValidDirectoryName(
	const char *directoryName
);

/*
	ファイルは更新されたか？
	*fileStat と現在のタイムスタンプを比較する。
	*fileStat は現在のファイルの状態で上書きされる。
*/
bool IsFileUpdated(
	const char *fileName,
	struct stat *fileStat
);

/* ファイルの拡張子を検査 */
bool IsSuffix(
	const char *fileName,
	const char *suffix
);

/* シェーダの作成（エラーチェック付き）*/
GLuint CreateShader(
	GLenum type,
	GLsizei count,
	const GLchar* const *strings
);

/* シェーダ入出力インターフェースを解析し TTY に出力する */
void DumpShaderInterfaces(
	GLuint programId
);

/* 指定のシェーダ入出力インターフェースが存在することを確認する */
bool
ExistsShaderUniform(
	GLuint	programId,
	GLint	location,
	GLint	typeEnum			/* GL_FLOAT, GL_FLOAT_VEC2/3/4, etc... */
);

/* エラーチェック */
void CheckGlError(
	const char *string
);

/* ファイルサイズを取得する */
size_t GetFileSize(
	const char *fileName
);

/* メモリ確保してファイル読み込み */
char *MallocReadFile(
	const char *fileName,
	size_t *sizeRet
);

/* メモリ確保してファイル読み込み（文字列終端の \0 が付加される）*/
char *MallocReadTextFile(
	const char *fileName
);

/* メモリ確保して文字列をコピーする */
char *MallocCopyString(
	const char *string
);

/* 指定文字のいずれかが見つかるまで読み飛ばす */
char *StrFindChars(
	char *string,
	const char *chars
);

/* 指定文字以外が見つかるまで読み飛ばす */
char *StrSkipChars(
	char *string,
	const char *chars
);

/* BOM をスキップする */
char *SkipBom(
	char *string
);
const char *SkipBomConst(
	const char *string
);

/* ディレクトリの選択 */
bool SelectDirectory(
	const char *title,
	const char *defaultDirectoryName,
	char *selectedDirectoryName,
	size_t selectedDirectoryNameSizeInBytes
);

/* ダイアログアイテムに float 値を設定 */
BOOL SetDlgItemFloat(
	HWND hDlg,
	int nIDDlgItem,
	float fValue,
	BOOL bSigned
);

/* ダイアログアイテムから float 値を取得 */
float GetDlgItemFloat(
	HWND hDlg,
	int nIDDlgItem,
	BOOL *lpTranslated,
	BOOL bSigned
);

/* チェックボックスを設定 */
void SetDlgItemCheck(
	HWND hDlg,
	int nIDDlgItem,
	BOOL check
);

/* チェックボックスを取得 */
BOOL GetDlgItemCheck(
	HWND hDlg,
	int nIDDlgItem
);

/* メニューアイテムのチェックを切り替え */
bool ToggleMenuItemCheck(
	HMENU hMenu,
	UINT idCheckItem
);

/* メニューアイテムのチェックを設定 */
void SetMenuItemCheck(
	HMENU hMenu,
	UINT idCheckItem,
	bool flag
);

#ifdef __cplusplus
#include <string>
#include <unordered_set>
#include <vector>

bool ExpandShaderIncludes(
 const std::string &filePath,
 std::unordered_set<std::string> &stack,
 std::unordered_set<std::string> &included,
 std::string &output,
 std::string *errorMessage = NULL
);
bool ExpandShaderIncludes(
 const std::string &filePath,
 std::unordered_set<std::string> &stack,
 std::string &output,
 std::string *errorMessage = NULL
);
bool ExpandShaderIncludes(
 const std::string &filePath,
 std::string &output,
 std::string *errorMessage = NULL
);
bool ExpandShaderIncludes(
 const std::string &filePath,
 std::string &output,
 std::vector<std::string> &includedFiles,
 std::string *errorMessage = NULL
);
#endif

#define SIZE_OF_ARRAY(a) (sizeof(a) / sizeof(a[0]))


#endif

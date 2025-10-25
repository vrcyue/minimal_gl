/* Copyright (C) 2018 Yosshin(@yosshin4004) */

#ifndef _GL3W_WORK_AROUND_H_
#define _GL3W_WORK_AROUND_H_

#include <GL/glext.h>

bool CallGl3wInit();
void *CallGl3wGetProcAddress(const char *procName);
PFNGLBINDIMAGETEXTUREPROC CallGl3wGetBindImageTexture();

#endif

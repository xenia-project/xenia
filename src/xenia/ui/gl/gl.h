/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#ifndef XENIA_UI_GL_GL_H_
#define XENIA_UI_GL_GL_H_

#include "xenia/base/platform.h"

#include "third_party/GL/glew.h"
#include "third_party/GL/wglew.h"

extern "C" GLEWContext* glewGetContext();
extern "C" WGLEWContext* wglewGetContext();

#endif  // XENIA_UI_GL_GL_H_

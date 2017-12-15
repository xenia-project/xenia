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

typedef struct GLEWContextStruct GLEWContext;
extern "C" GLEWContext* glewGetContext();

#if XE_PLATFORM_WIN32
// We avoid including wglew.h here as it includes windows.h and pollutes the
// global namespace. As we don't need wglew most places we only do that as
// required.
typedef struct WGLEWContextStruct WGLEWContext;
extern "C" WGLEWContext* wglewGetContext();
#elif XE_PLATFORM_LINUX
typedef struct GLXEWContextStruct GLXEWContext;
extern "C" GLXEWContext* glxewGetContext();

#endif

#endif  // XENIA_UI_GL_GL_H_

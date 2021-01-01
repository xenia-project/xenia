/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_PLATFORM_WIN_H_
#define XENIA_BASE_PLATFORM_WIN_H_

// NOTE: if you're including this file it means you are explicitly depending
// on Windows-specific headers. This is bad for portability and should be
// avoided!

#include "xenia/base/platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <ObjBase.h>
#include <SDKDDKVer.h>
#include <bcrypt.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <tpcshrd.h>
#include <windows.h>
#include <windowsx.h>
#undef DeleteBitmap
#undef DeleteFile
#undef GetFirstChild

#endif  // XENIA_BASE_PLATFORM_WIN_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_PLATFORM_INCLUDES_H_
#define XENIA_PLATFORM_INCLUDES_H_

#include <xenia/platform.h>


#if XE_PLATFORM(WINCE) || XE_PLATFORM(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <SDKDDKVer.h>
#include <windows.h>
#undef min
#undef max
#endif  // WINCE || WIN32

#if XE_PLATFORM(XBOX360)
#include <xtl.h>
#include <xboxmath.h>
#endif  // XBOX360

#if XE_COMPILER(MSVC)
// Disable warning C4068: unknown pragma
#pragma warning(disable : 4068)
#endif  // MSVC

#include <stddef.h>
#include <memory.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>
#include <float.h>
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <locale.h>
#include <errno.h>
#include <string>
#include <assert.h>
#include <stdint.h>

#if XE_COMPILER(MSVC)
#include <memory>
#include <unordered_map>
#else
#include <tr1/memory>
#include <tr1/unordered_map>
#endif  // MSVC


#endif  // XENIA_PLATFORM_INCLUDES_H_

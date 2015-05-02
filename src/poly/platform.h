/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_PLATFORM_H_
#define POLY_PLATFORM_H_

// NOTE: ordering matters here as sometimes multiple flags are defined on
// certain platforms.

// Great resource on predefined macros: http://predef.sourceforge.net/preos.html

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(TARGET_OS_MAC) && TARGET_OS_MAC
#define XE_PLATFORM_MAC 1
#elif defined(WIN32) || defined(_WIN32)
#define XE_PLATFORM_WIN32 1
#else
#define XE_PLATFORM_LINUX 1
#endif

#if defined(__clang__)
#define XE_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define XE_COMPILER_GNUC 1
#elif defined(_MSC_VER)
#define XE_COMPILER_MSVC 1
#elif defined(__MINGW32)
#define XE_COMPILER_MINGW32 1
#elif defined(__INTEL_COMPILER)
#define XE_COMPILER_INTEL 1
#else
#define XE_COMPILER_UNKNOWN 1
#endif

#if XE_PLATFORM_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <SDKDDKVer.h>
#include <windows.h>
#include <ObjBase.h>
#undef min
#undef max
#define strdup _strdup
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif  // XE_PLATFORM_WIN32

#if XE_COMPILER_MSVC
#include <intrin.h>
#else
#include <x86intrin.h>
#endif  // XE_COMPILER_MSVC

namespace poly {

#if XE_PLATFORM_WIN32
const char path_separator = '\\';
const size_t max_path = _MAX_PATH;
#else
const char path_separator = '/';
const size_t max_path = 1024;  // PATH_MAX
#endif  // XE_PLATFORM_WIN32

}  // namespace poly

#endif  // POLY_PLATFORM_H_

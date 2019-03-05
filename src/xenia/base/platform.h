/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_PLATFORM_H_
#define XENIA_BASE_PLATFORM_H_

// This file contains the main platform switches used by xenia as well as any
// fixups required to normalize the environment. Everything in here should be
// largely portable.
// Platform-specific headers, like platform_win.h, are used to house any
// super platform-specific stuff that implies code is not platform-agnostic.
//
// NOTE: ordering matters here as sometimes multiple flags are defined on
// certain platforms.
//
// Great resource on predefined macros:
// https://sourceforge.net/p/predef/wiki/OperatingSystems/
// Original link: https://predef.sourceforge.net/preos.html

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

#if defined(_M_AMD64) || defined(__amd64__)
#define XE_ARCH_AMD64 1
#elif defined(_M_IX86)
#error "Xenia is not supported on 32-bit platforms."
#elif defined(_M_PPC) || defined(__powerpc__)
#define XE_ARCH_PPC 1
#endif

#if XE_PLATFORM_WIN32
#define strdup _strdup
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // Don't want windows.h including min/max macros.
#endif            // XE_PLATFORM_WIN32

#if XE_PLATFORM_WIN32
#include <intrin.h>
#elif XE_ARCH_AMD64
#include <x86intrin.h>
#endif  // XE_PLATFORM_WIN32

#if XE_PLATFORM_MAC
#include <libkern/OSByteOrder.h>
#endif  // XE_PLATFORM_MAC

#if XE_COMPILER_MSVC
#define XEPACKEDSTRUCT(name, value)                                  \
  __pragma(pack(push, 1)) struct name##_s value __pragma(pack(pop)); \
  typedef struct name##_s name;
#define XEPACKEDSTRUCTANONYMOUS(value) \
  __pragma(pack(push, 1)) struct value __pragma(pack(pop));
#define XEPACKEDUNION(name, value)                                  \
  __pragma(pack(push, 1)) union name##_s value __pragma(pack(pop)); \
  typedef union name##_s name;
#else
#define XEPACKEDSTRUCT(name, value) struct __attribute__((packed)) name value;
#define XEPACKEDSTRUCTANONYMOUS(value) struct __attribute__((packed)) value;
#define XEPACKEDUNION(name, value) union __attribute__((packed)) name value;
#endif  // XE_PLATFORM_WIN32

namespace xe {

#if XE_PLATFORM_WIN32
const char kPathSeparator = '\\';
const wchar_t kWPathSeparator = L'\\';
const size_t kMaxPath = 260;  // _MAX_PATH
#else
const char kPathSeparator = '/';
const wchar_t kWPathSeparator = L'/';
const size_t kMaxPath = 1024;  // PATH_MAX
#endif  // XE_PLATFORM_WIN32

// Launches a web browser to the given URL.
void LaunchBrowser(const char* url);

}  // namespace xe

#endif  // XENIA_BASE_PLATFORM_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_PLATFORM_H_
#define XENIA_PLATFORM_H_

// NOTE: ordering matters here as sometimes multiple flags are defined on
// certain platforms.

// Great resource on predefined macros: http://predef.sourceforge.net/preos.html

/*
XE_PLATFORM:    IOS | OSX | XBOX360 | WINCE | WIN32 | ANDROID | NACL | UNIX
XE_LIKE:        OSX | WIN32 | POSIX
XE_PROFILE:     EMBEDDED | DESKTOP (+ _SIMULATOR)
XE_COMPILER:    GNUC | MSVC | INTEL | UNKNOWN
XE_CPU:         32BIT | 64BIT | BIGENDIAN | LITTLEENDIAN
*/


#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if (defined(TARGET_OS_EMBEDDED)      && TARGET_OS_EMBEDDED     ) || \
    (defined(TARGET_OS_IPHONE)        && TARGET_OS_IPHONE       ) || \
    (defined(TARGET_IPHONE_SIMULATOR) && TARGET_IPHONE_SIMULATOR)

#define XE_PLATFORM_IOS         1
#define XE_LIKE_OSX             1
#define XE_PROFILE_EMBEDDED     1

#if defined(TARGET_IPHONE_SIMULATOR) && TARGET_IPHONE_SIMULATOR
// EMBEDDED *and* SIMULATOR
#define XE_PROFILE_SIMULATOR    1
#endif

#elif defined(TARGET_OS_MAC) && TARGET_OS_MAC

#define XE_PLATFORM_OSX         1
#define XE_LIKE_OSX             1
#define XE_PROFILE_DESKTOP      1

#elif defined(_XBOX)

#define XE_PLATFORM_XBOX360     1
#define XE_LIKE_WIN32           1
#define XE_PROFILE_EMBEDDED     1

#elif defined(_WIN32_WCE)

#define XE_PLATFORM_WINCE       1
#define XE_LIKE_WIN32           1
#define XE_PROFILE_EMBEDDED     1

#elif defined(__CYGWIN__)

#define XE_PLATFORM_CYGWIN      1
#define XE_LIKE_POSIX           1
#define XE_PROFILE_DESKTOP      1

#elif defined(WIN32) || defined(_WIN32)

#define XE_PLATFORM_WIN32       1
#define XE_LIKE_WIN32           1
#define XE_PROFILE_DESKTOP      1

#elif defined(ANDROID)

#define XE_PLATFORM_ANDROID     1
#define XE_LIKE_POSIX           1
#define XE_PROFILE_EMBEDDED     1

#elif defined(__native_client__)

#define XE_PLATFORM_NACL        1
#define XE_LIKE_POSIX           1
#define XE_PROFILE_DESKTOP      1

#else

#define XE_PLATFORM_UNIX        1
#define XE_LIKE_POSIX           1
#define XE_PROFILE_DESKTOP      1

#endif

#if defined(__GNUC__)
#define XE_COMPILER_GNUC        1
#elif defined(_MSC_VER)
#define XE_COMPILER_MSVC        1
#elif defined(__MINGW32)
#define XE_COMPILER_MINGW32     1
#elif defined(__INTEL_COMPILER)
#define XE_COMPILER_INTEL       1
#else
#define XE_COMPILER_UNKNOWN     1
#endif

#if defined(__ia64__) || defined(_M_IA64) || \
    defined(__ppc64__) || defined(__PPC64__) || \
    defined(__arch64__) || \
    defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64) || \
    defined(__LP64__) || defined(__LLP64) || \
    defined(_WIN64) || \
    (__WORDSIZE == 64)
#define XE_CPU_64BIT            1
#else
#define XE_CPU_32BIT            1
#endif  // [64bit flags]

#if defined(__ppc__) || defined(__PPC__) || defined(__powerpc__) || defined(__powerpc) || defined(__POWERPC__) || defined(_M_PPC) || defined(__PPC) || \
    defined(__ppc64__) || defined(__PPC64__) || \
    defined(__ARMEB__) || \
    defined(__BIG_ENDIAN) || defined(__BIG_ENDIAN__)
#define XE_CPU_BIGENDIAN        1
#else
#define XE_CPU_LITTLEENDIAN     1
#endif  // [big endian flags]

#if defined(DEBUG) || defined(_DEBUG)
#define XE_DEBUG                1
#endif  // DEBUG

#if XE_CPU_32BIT
#define XE_ALIGNMENT            8
#else
#define XE_ALIGNMENT            16
#endif  // 32BIT

bool xe_has_console();
#if XE_LIKE_WIN32 && defined(UNICODE) && UNICODE
int xe_main_thunk(
    int argc, wchar_t* argv[],
    void* user_main, const char* usage);
#define XE_MAIN_THUNK(NAME, USAGE) \
    int wmain(int argc, wchar_t *argv[]) { \
      return xe_main_thunk(argc, argv, NAME, USAGE); \
    }
int xe_main_window_thunk(
    wchar_t* command_line,
    void* user_main, const wchar_t* name, const char* usage);
#define XE_MAIN_WINDOW_THUNK(NAME, APP_NAME, USAGE) \
    int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPTSTR command_line, int) { \
      return xe_main_window_thunk(command_line, NAME, APP_NAME, USAGE); \
    }
#else
int xe_main_thunk(
    int argc, char** argv,
    void* user_main, const char* usage);
#define XE_MAIN_THUNK(NAME, USAGE) \
    int main(int argc, char **argv) { \
      return xe_main_thunk(argc, argv, (void*)NAME, USAGE); \
    }
#define XE_MAIN_WINDOW_THUNK(NAME, APP_NAME, USAGE) \
    XE_MAIN_THUNK(NAME, USAGE)
#endif  // WIN32


#endif  // XENIA_PLATFORM_H_

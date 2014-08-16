/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_STRING_H_
#define XENIA_STRING_H_

#include <poly/string.h>

// NOTE: these differing implementations should behave pretty much the same.
// If they don't, then they will have to be abstracted out.

#if !XE_LIKE_WIN32
int strncpy_s(char* dest, size_t destLength, const char* source, size_t count);
#define strcpy_s(dest, destLength, source)              !(strcpy(dest, source) == dest + (destLength*0))
#define _snprintf_s(dest, destLength, x, format, ...)   snprintf(dest, destLength, format, ##__VA_ARGS__)
#endif  // !WIN32

#define xesnprintfw(buffer, bufferCount, format, ...)   _snwprintf_s(buffer, bufferCount, (bufferCount) ? (bufferCount - 1) : 0, format, ##__VA_ARGS__)

#define xestrcpya(dest, destLength, source)             (strcpy_s(dest, destLength, source) == 0)
#define xesnprintfa(buffer, bufferCount, format, ...)   _snprintf_s(buffer, bufferCount, bufferCount, format, ##__VA_ARGS__)
#define xevsnprintfa(buffer, bufferCount, format, args) vsnprintf(buffer, bufferCount, format, args)

#if XE_PLATFORM_WIN32 && defined(UNICODE) && UNICODE

typedef wchar_t xechar_t;

// xestrcpy fs + module
// xesnprintf many uses - only remove some?

#define xesnprintf          xesnprintfw

#else

typedef char xechar_t;

#define xesnprintf          xesnprintfa

#endif  // WIN32

#endif  // XENIA_STRING_H_

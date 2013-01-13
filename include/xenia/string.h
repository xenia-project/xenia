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

#include <xenia/platform.h>
#include <xenia/platform_includes.h>


// NOTE: these differing implementations should behave pretty much the same.
// If they don't, then they will have to be abstracted out.

#define XEInvalidSize                                   ((size_t)(-1))

#if !XE_LIKE(WIN32)
int strncpy_s(char* dest, size_t destLength, const char* source, size_t count);
#define strcpy_s(dest, destLength, source)              !(strcpy(dest, source) == dest + (destLength*0))
#define strcat_s(dest, destLength, source)              !(strcat(dest, source) == dest + (destLength*0))
#define _snprintf_s(dest, destLength, format, ...)      snprintf(dest, destLength, format, ##__VA_ARGS__)
#else
#define strcasecmp                                      _stricmp
#endif

#define xestrlenw                                       wcslen
#define xestrcmpw                                       wcscmp
#define xestrcasecmpw                                   _wcsicmp
#define xestrchrw                                       wcschr
#define xestrrchrw                                      wcsrchr
#define xestrstrw                                       wcsstr
#define xestrcpyw(dest, destLength, source)             (wcscpy_s(dest, destLength, source) == 0)
#define xestrncpyw(dest, destLength, source, count)     (wcsncpy_s(dest, destLength, source, count) == 0)
#define xestrcatw(dest, destLength, source)             (wcscat_s(dest, destLength, source) == 0)
#define xesnprintfw(buffer, bufferCount, format, ...)   _snwprintf_s(buffer, bufferCount, (bufferCount) ? (bufferCount - 1) : 0, format, ##__VA_ARGS__)
#define xevsnprintfw(buffer, bufferCount, format, args) _vsnwprintf_s(buffer, bufferCount, (bufferCount) ? (bufferCount - 1) : 0, format, args)
#define xevscprintfw(format, args)                      _vscwprintf(format, args)

#define xestrlena                                       strlen
#define xestrcmpa                                       strcmp
#define xestrcasecmpa                                   strcasecmp
#define xestrchra                                       strchr
#define xestrrchra                                      strrchr
#define xestrstra                                       strstr
#define xestrcpya(dest, destLength, source)             (strcpy_s(dest, destLength, source) == 0)
#define xestrncpya(dest, destLength, source, count)     (strncpy_s(dest, destLength, source, count) == 0)
#define xestrcata(dest, destLength, source)             (strcat_s(dest, destLength, source) == 0)
#define xesnprintfa(buffer, bufferCount, format, ...)   _snprintf_s(buffer, bufferCount, format, ##__VA_ARGS__)
#define xevsnprintfa(buffer, bufferCount, format, args) vsnprintf(buffer, bufferCount, format, args)

#if XE_PLATFORM(WIN32)

typedef wchar_t xechar_t;
#define XE_WCHAR            1
#define XETEXT(s)           L ## s

#define xestrlen            xestrlenw
#define xestrcmp            xestrcmpw
#define xestrcasecmp        xestrcasecmpw
#define xestrchr            xestrchrw
#define xestrrchr           xestrrchrw
#define xestrstr            xestrstrw
#define xestrcpy            xestrcpyw
#define xestrncpy           xestrncpyw
#define xestrcat            xestrcatw
#define xesnprintf          xesnprintfw
#define xevsnprintf         xevsnprintfw
#define xestrnarrow(dest, destLength, source)   (wcstombs_s(NULL, dest, destLength, source, _TRUNCATE) == 0)
#define xestrwiden(dest, destLength, source)    (mbstowcs_s(NULL, dest, destLength, source, _TRUNCATE) == 0)

#else

typedef char xechar_t;
#define XE_CHAR             1
#define XETEXT(s)           s

#define xestrlen            xestrlena
#define xestrcmp            xestrcmpa
#define xestrcasecmp        xestrcasecmpa
#define xestrchr            xestrchra
#define xestrrchr           xestrrchra
#define xestrstr            xestrstra
#define xestrcpy            xestrcpya
#define xestrncpy           xestrncpya
#define xestrcat            xestrcata
#define xesnprintf          xesnprintfa
#define xevsnprintf         xevsnprintfa
#define xestrnarrow(dest, destLength, source)   xestrCpy(dest, destLength, source)
#define xestrwiden(dest, destLength, source)    xestrCpy(dest, destLength, source)

#endif  // WIN32

#define XT XETEXT

#if XE_LIKE(WIN32)
#define XE_PATH_SEPARATOR   ((xechar_t)'\\')
#else
#define XE_PATH_SEPARATOR   ((xechar_t)'/')
#endif  // WIN32

#endif  // XENIA_STRING_H_

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

#define XE_USE_NTDLL_FUNCTIONS 1
// chrispy: disabling this for now, more research needs to be done imo, although
// it does work very well on my machine
//
#define XE_USE_KUSER_SHARED 0
#if XE_USE_NTDLL_FUNCTIONS == 1
/*
        ntdll versions of functions often skip through a lot of extra garbage in
   KernelBase
*/
#define XE_NTDLL_IMPORT(name, cls, clsvar)                                   \
  static class cls {                                                         \
   public:                                                                   \
    FARPROC fn;                                                              \
    cls() : fn(nullptr) {                                                    \
      auto ntdll = GetModuleHandleA("ntdll.dll");                            \
      if (ntdll) {                                                           \
        fn = GetProcAddress(ntdll, #name);                                   \
      }                                                                      \
    }                                                                        \
    template <typename TRet = void, typename... TArgs>                       \
    inline TRet invoke(TArgs... args) {                                      \
      return reinterpret_cast<NTSYSAPI TRet(NTAPI*)(TArgs...)>(fn)(args...); \
    }                                                                        \
    inline operator bool() const { return fn != nullptr; }                   \
  } clsvar
#else
#define XE_NTDLL_IMPORT(name, cls, clsvar) static constexpr bool clsvar = false

#endif
static constexpr size_t KSUER_SHARED_SYSTEMTIME_OFFSET = 0x14;

static constexpr size_t KUSER_SHARED_INTERRUPTTIME_OFFSET = 8;
static unsigned char* KUserShared() { return (unsigned char*)0x7FFE0000ULL; }
#if XE_USE_KUSER_SHARED == 1
// KUSER_SHARED
struct __declspec(align(4)) _KSYSTEM_TIME {
  unsigned int LowPart;
  int High1Time;
  int High2Time;
};

static volatile _KSYSTEM_TIME* GetKUserSharedSystemTime() {
  return reinterpret_cast<volatile _KSYSTEM_TIME*>(
      KUserShared() + KSUER_SHARED_SYSTEMTIME_OFFSET);
}
#endif
#endif  // XENIA_BASE_PLATFORM_WIN_H_

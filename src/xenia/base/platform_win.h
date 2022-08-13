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

#define	XE_USE_NTDLL_FUNCTIONS 1
#if XE_USE_NTDLL_FUNCTIONS==1
/*
	ntdll versions of functions often skip through a lot of extra garbage in KernelBase
*/
#define XE_NTDLL_IMPORT(name, cls, clsvar) \
  static class cls {                       \
   public:                                 \
	FARPROC fn;\
    cls() : fn(nullptr) {\
		auto ntdll = GetModuleHandleA("ntdll.dll");\
		if (ntdll) {                                \
			fn = GetProcAddress(ntdll, #name );\
		}\
	}                                  \
	template <typename TRet = void, typename... TArgs> \
    inline TRet invoke(TArgs... args) {\
		return reinterpret_cast<NTSYSAPI TRet(NTAPI*)(TArgs...)>(fn)(args...);\
	}\
	inline operator bool() const {\
		return fn!=nullptr;\
	}\
  } clsvar
#else
#define XE_NTDLL_IMPORT(name, cls, clsvar) static constexpr bool clsvar = false

#endif
#endif  // XENIA_BASE_PLATFORM_WIN_H_

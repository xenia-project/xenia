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
#define XE_USE_KUSER_SHARED 1
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

// KUSER_SHARED
struct __declspec(align(4)) _KSYSTEM_TIME {
  unsigned int LowPart;
  int High1Time;
  int High2Time;
};
enum _NT_PRODUCT_TYPE {
  NtProductWinNt = 0x1,
  NtProductLanManNt = 0x2,
  NtProductServer = 0x3,
};
enum _ALTERNATIVE_ARCHITECTURE_TYPE {
  StandardDesign = 0x0,
  NEC98x86 = 0x1,
  EndAlternatives = 0x2,
};

#pragma pack(push, 1)
struct $3D940D5D03EF7F98CEE6737EDE752E57 {
  __int8 _bf_0;
};

union $DA7A7E727E24E4DD62317E27558CCADA {
  unsigned __int8 MitigationPolicies;
  $3D940D5D03EF7F98CEE6737EDE752E57 __s1;
};
struct __declspec(align(4)) $4BF4056B39611650D41923F164DAFA52 {
  __int32 _bf_0;
};

union __declspec(align(4)) $BB68545E345A5F8046EF3BC0FE928142 {
  unsigned int SharedDataFlags;
  $4BF4056B39611650D41923F164DAFA52 __s1;
};
union $5031D289C483414B89DA3F368D1FE62C {
  volatile _KSYSTEM_TIME TickCount;
  volatile unsigned __int64 TickCountQuad;
  unsigned int ReservedTickCountOverlay[3];
};
struct $F91ACE6F13277DFC9425B9B8BBCB30F7 {
  volatile unsigned __int8 QpcBypassEnabled;
  unsigned __int8 QpcShift;
};

union __declspec(align(2)) $3C927F8BB7EAEE13CF0CFC3E60EDC8A9 {
  unsigned __int16 QpcData;
  $F91ACE6F13277DFC9425B9B8BBCB30F7 __s1;
};

struct __declspec(align(8)) _KUSER_SHARED_DATA {
  unsigned int TickCountLowDeprecated;
  unsigned int TickCountMultiplier;
  volatile _KSYSTEM_TIME InterruptTime;
  volatile _KSYSTEM_TIME SystemTime;
  volatile _KSYSTEM_TIME TimeZoneBias;
  unsigned __int16 ImageNumberLow;
  unsigned __int16 ImageNumberHigh;
  wchar_t NtSystemRoot[260];
  unsigned int MaxStackTraceDepth;
  unsigned int CryptoExponent;
  unsigned int TimeZoneId;
  unsigned int LargePageMinimum;
  unsigned int AitSamplingValue;
  unsigned int AppCompatFlag;
  unsigned __int64 RNGSeedVersion;
  unsigned int GlobalValidationRunlevel;
  volatile int TimeZoneBiasStamp;
  unsigned int NtBuildNumber;
  _NT_PRODUCT_TYPE NtProductType;
  unsigned __int8 ProductTypeIsValid;
  unsigned __int8 Reserved0[1];
  unsigned __int16 NativeProcessorArchitecture;
  unsigned int NtMajorVersion;
  unsigned int NtMinorVersion;
  unsigned __int8 ProcessorFeatures[64];
  unsigned int Reserved1;
  unsigned int Reserved3;
  volatile unsigned int TimeSlip;
  _ALTERNATIVE_ARCHITECTURE_TYPE AlternativeArchitecture;
  unsigned int BootId;
  _LARGE_INTEGER SystemExpirationDate;
  unsigned int SuiteMask;
  unsigned __int8 KdDebuggerEnabled;
  $DA7A7E727E24E4DD62317E27558CCADA ___u33;
  unsigned __int8 Reserved6[2];
  volatile unsigned int ActiveConsoleId;
  volatile unsigned int DismountCount;
  unsigned int ComPlusPackage;
  unsigned int LastSystemRITEventTickCount;
  unsigned int NumberOfPhysicalPages;
  unsigned __int8 SafeBootMode;
  unsigned __int8 VirtualizationFlags;
  unsigned __int8 Reserved12[2];
  $BB68545E345A5F8046EF3BC0FE928142 ___u43;
  unsigned int DataFlagsPad[1];
  unsigned __int64 TestRetInstruction;
  __int64 QpcFrequency;
  unsigned int SystemCall;
  unsigned int SystemCallPad0;
  unsigned __int64 SystemCallPad[2];
  $5031D289C483414B89DA3F368D1FE62C ___u50;
  unsigned int TickCountPad[1];
  unsigned int Cookie;
  unsigned int CookiePad[1];
  __int64 ConsoleSessionForegroundProcessId;
  unsigned __int64 TimeUpdateLock;
  unsigned __int64 BaselineSystemTimeQpc;
  unsigned __int64 BaselineInterruptTimeQpc;
  unsigned __int64 QpcSystemTimeIncrement;
  unsigned __int64 QpcInterruptTimeIncrement;
  unsigned __int8 QpcSystemTimeIncrementShift;
  unsigned __int8 QpcInterruptTimeIncrementShift;
  unsigned __int16 UnparkedProcessorCount;
  unsigned int EnclaveFeatureMask[4];
  unsigned int TelemetryCoverageRound;
  unsigned __int16 UserModeGlobalLogger[16];
  unsigned int ImageFileExecutionOptions;
  unsigned int LangGenerationCount;
  unsigned __int64 Reserved4;
  volatile unsigned __int64 InterruptTimeBias;
  volatile unsigned __int64 QpcBias;
  unsigned int ActiveProcessorCount;
  volatile unsigned __int8 ActiveGroupCount;
  unsigned __int8 Reserved9;
  $3C927F8BB7EAEE13CF0CFC3E60EDC8A9 ___u74;
  _LARGE_INTEGER TimeZoneBiasEffectiveStart;
  _LARGE_INTEGER TimeZoneBiasEffectiveEnd;
  _XSTATE_CONFIGURATION XState;
};
static constexpr unsigned KUSER_SIZE = sizeof(_KUSER_SHARED_DATA);

static_assert(KUSER_SIZE == 1808, "yay");
#pragma pack(pop)

static _KUSER_SHARED_DATA* KUserShared() {
  return (_KUSER_SHARED_DATA*)0x7FFE0000;
}
#endif  // XENIA_BASE_PLATFORM_WIN_H_

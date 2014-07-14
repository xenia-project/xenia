/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/pal.h>


namespace {
typedef struct xe_pal_win {
  bool    use_high_perf_timer;
  double  inv_ticks_per_sec;
} xe_pal_win_t;

xe_pal_win_t* pal;
}


void xe_pal_dealloc();
int xe_pal_init(xe_pal_options_t options) {
  pal = (xe_pal_win_t*)xe_calloc(sizeof(xe_pal_win_t));

  // Get QPC timing frequency... hopefully stable over the life of the app,
  // but likely not.
  // TODO(benvanik): requery periodically/etc.
  LARGE_INTEGER ticks_per_sec;
  if (QueryPerformanceFrequency(&ticks_per_sec)) {
    pal->use_high_perf_timer  = true;
    pal->inv_ticks_per_sec    = 1.0 / (double)ticks_per_sec.QuadPart;
  } else {
    pal->use_high_perf_timer  = false;
    XELOGW("Falling back from high performance timer");
  }

  // Setup COM on the main thread.
  // NOTE: this may fail if COM has already been initialized - that's OK.
  XEIGNORE(CoInitializeEx(NULL, COINIT_MULTITHREADED));

  atexit(xe_pal_dealloc);
  return 0;
}

void xe_pal_dealloc() {
  //

  xe_free(pal);
}

// http://msdn.microsoft.com/en-us/library/ms683194.aspx
namespace {
DWORD CountSetBits(ULONG_PTR bitMask) {
  DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
  DWORD bitSetCount = 0;
  ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;
  for (DWORD i = 0; i <= LSHIFT; ++i, bitTest /= 2) {
    bitSetCount += ((bitMask & bitTest)?1:0);
  }
  return bitSetCount;
}
}
int xe_pal_get_system_info(xe_system_info* out_info) {
  int result_code = 1;
  xe_zero_struct(out_info, sizeof(xe_system_info));
  out_info->processors.physical_count = 1;
  out_info->processors.logical_count  = 1;

  typedef BOOL (WINAPI *LPFN_GLPI)(
      PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

  HMODULE   kernel32  = NULL;
  LPFN_GLPI glpi      = NULL;
  PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;

  kernel32 = GetModuleHandle(L"kernel32");
  XEEXPECTNOTNULL(kernel32);
  glpi = (LPFN_GLPI)GetProcAddress(kernel32, "GetLogicalProcessorInformation");
  XEEXPECTNOTNULL(glpi);

  // Call GLPI once to get the buffer size, allocate it, then call again.
  DWORD buffer_length = 0;
  XEEXPECTFALSE(glpi(NULL, &buffer_length));
  XEEXPECT(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)xe_malloc(buffer_length);
  XEEXPECTNOTNULL(buffer);
  XEEXPECTTRUE(glpi(buffer, &buffer_length));
  XEEXPECTNOTZERO(buffer_length);

  out_info->processors.physical_count = 0;
  out_info->processors.logical_count  = 0;

  size_t info_count = buffer_length /
      sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
  for (size_t n = 0; n < info_count; n++) {
    switch (buffer[n].Relationship) {
      case RelationProcessorPackage:
        out_info->processors.physical_count++;
        break;
      case RelationProcessorCore:
        if (buffer[n].ProcessorCore.Flags == 1) {
          // Hyper-threaded.
          // The number of processors is set as bits in ProcessorMask.
          out_info->processors.logical_count +=
              CountSetBits(buffer[n].ProcessorMask);
        } else {
          // A real core - just count as one.
          out_info->processors.logical_count++;
        }
        break;
    }
  }

  out_info->processors.physical_count =
      MAX(1, out_info->processors.physical_count);
  out_info->processors.logical_count  =
      MAX(1, out_info->processors.logical_count);

  result_code = 0;
XECLEANUP:
  xe_free(buffer);
  if (kernel32) {
    FreeLibrary(kernel32);
  }
  return result_code;
}

double xe_pal_now() {
  if (pal->use_high_perf_timer) {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart * pal->inv_ticks_per_sec;
  } else {
    // Using GetSystemTimeAsFileTime instead of GetSystemTime() as it has a
    // 100ns resolution.
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart   = ft.dwLowDateTime;
    uli.HighPart  = ft.dwHighDateTime;
    return uli.QuadPart / 10000000.0;
  }
}

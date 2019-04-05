/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/clock.h"

#include "xenia/base/platform_win.h"

namespace xe {

uint64_t Clock::host_tick_frequency() {
  static LARGE_INTEGER frequency = {{0}};
  if (!frequency.QuadPart) {
    QueryPerformanceFrequency(&frequency);
  }
  return frequency.QuadPart;
}

uint64_t Clock::QueryHostTickCount() {
  LARGE_INTEGER counter;
  uint64_t time = 0;
  if (QueryPerformanceCounter(&counter)) {
    time = counter.QuadPart;
  }
  return time;
}

uint64_t Clock::QueryHostSystemTime() {
  FILETIME t;
  GetSystemTimeAsFileTime(&t);
  return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

uint64_t Clock::QueryHostUptimeMillis() {
  return QueryHostTickCount() / (host_tick_frequency() / 1000);
}

}  // namespace xe

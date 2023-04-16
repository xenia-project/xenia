/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/clock.h"

#include "xenia/base/platform_win.h"

namespace xe {
#if XE_USE_KUSER_SHARED == 1
uint64_t Clock::host_tick_frequency_platform() { return 10000000ULL; }

uint64_t Clock::host_tick_count_platform() {
  return *reinterpret_cast<volatile uint64_t*>(GetKUserSharedSystemTime());
}
uint64_t Clock::QueryHostSystemTime() {
  return *reinterpret_cast<volatile uint64_t*>(GetKUserSharedSystemTime());
}

#else
uint64_t Clock::host_tick_frequency_platform() {
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  return frequency.QuadPart;
}

uint64_t Clock::host_tick_count_platform() {
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

#endif
uint64_t Clock::QueryHostUptimeMillis() {
  return host_tick_count_platform() * 1000 / host_tick_frequency_platform();
}
// todo: we only take the low part of interrupttime! this is actually a 96-bit
// int!
uint64_t Clock::QueryHostInterruptTime() {
  return *reinterpret_cast<uint64_t*>(KUserShared() +
                                      KUSER_SHARED_INTERRUPTTIME_OFFSET);
}
}  // namespace xe

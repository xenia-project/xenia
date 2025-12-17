/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <sys/time.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"

namespace xe {

uint64_t Clock::host_tick_frequency_platform() {
#ifdef __APPLE__
  mach_timebase_info_data_t info;
  mach_timebase_info(&info);
  // Convert timebase to frequency in Hz.
  return (uint64_t)((1000000000ull * (uint64_t)info.denom) /
                    (uint64_t)info.numer);
#else
  timespec res;
  int error = clock_getres(CLOCK_MONOTONIC_RAW, &res);
  assert_zero(error);
  assert_zero(res.tv_sec);  // Sub second resolution is required.

  // Convert nano seconds to hertz. Resolution is 1ns on most systems.
  return 1000000000ull / res.tv_nsec;
#endif
}

uint64_t Clock::host_tick_count_platform() {
#ifdef __APPLE__
  return mach_absolute_time();
#else
  timespec tp;
  int error = clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
  assert_zero(error);

  return tp.tv_nsec + tp.tv_sec * 1000000000ull;
#endif
}

uint64_t Clock::QueryHostSystemTime() {
  // https://docs.microsoft.com/en-us/windows/win32/sysinfo/converting-a-time-t-value-to-a-file-time
  constexpr uint64_t seconds_per_day = 3600 * 24;
  // Don't forget the 89 leap days.
  constexpr uint64_t seconds_1601_to_1970 =
      ((369 * 365 + 89) * seconds_per_day);

  timeval now;
  int error = gettimeofday(&now, nullptr);
  assert_zero(error);

  // NT systems use 100ns intervals.
  return static_cast<uint64_t>(
      (static_cast<int64_t>(now.tv_sec) + seconds_1601_to_1970) * 10000000ull +
      now.tv_usec * 10);
}

uint64_t Clock::QueryHostUptimeMillis() {
  return host_tick_count_platform() * 1000 / host_tick_frequency_platform();
}

}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <mach/mach_time.h>
#include <sys/time.h>

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"

namespace xe {

uint64_t Clock::host_tick_frequency_platform() {
  mach_timebase_info_data_t info;
  mach_timebase_info(&info);
  // Convert the timebase info to frequency in Hz.
  return (uint64_t)((1e9 * (uint64_t)info.denom) / (uint64_t)info.numer);
}

uint64_t Clock::host_tick_count_platform() {
  return mach_absolute_time();
}

uint64_t Clock::QueryHostSystemTime() {
  // Number of days from 1601-01-01 to 1970-01-01
  constexpr uint64_t seconds_per_day = 3600 * 24;
  constexpr uint64_t days_per_year = 365;
  constexpr uint64_t years = 369;  // 1970 - 1601
  constexpr uint64_t leap_days = 89;
  constexpr uint64_t days = years * days_per_year + leap_days;
  constexpr uint64_t seconds_1601_to_1970 = days * seconds_per_day;

  timeval now;
  int error = gettimeofday(&now, nullptr);
  assert_zero(error);

  // Total seconds since 1601-01-01
  uint64_t total_seconds = seconds_1601_to_1970 + now.tv_sec;

  // Convert to 100ns intervals
  uint64_t filetime =
      total_seconds * 10000000ull + static_cast<uint64_t>(now.tv_usec) * 10ull;

  return filetime;
}

uint64_t Clock::QueryHostUptimeMillis() {
  uint64_t ticks = host_tick_count_platform();
  uint64_t frequency = host_tick_frequency_platform();

  // Convert ticks to milliseconds
  return (ticks * 1000ull) / frequency;
}

}  // namespace xe

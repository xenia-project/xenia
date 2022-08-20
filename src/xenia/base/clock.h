/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_CLOCK_H_
#define XENIA_BASE_CLOCK_H_

#include <chrono>
#include <cstdint>

#include "xenia/base/cvar.h"
#include "xenia/base/platform.h"

#if XE_ARCH_AMD64
#define XE_CLOCK_RAW_AVAILABLE 1
#endif

DECLARE_bool(clock_no_scaling);
DECLARE_bool(clock_source_raw);

namespace xe {

// chrono APIs in xenia/base/chrono.h are preferred

class Clock {
 public:
  // Host ticks-per-second. Generally QueryHostTickFrequency should be used.
  // Either from platform suplied time source or from hardware directly.
  static uint64_t host_tick_frequency_platform();
#if XE_CLOCK_RAW_AVAILABLE
  XE_NOINLINE
  static uint64_t host_tick_frequency_raw();
#endif
  // Host tick count. Generally QueryHostTickCount() should be used.
  static uint64_t host_tick_count_platform();
#if XE_CLOCK_RAW_AVAILABLE
  //chrispy: the way msvc was ordering the branches was causing rdtsc to be speculatively executed each time
  //the branch history was lost
  XE_NOINLINE
  static uint64_t host_tick_count_raw();
#endif

  // Queries the host tick frequency.
  static uint64_t QueryHostTickFrequency();
  // Queries the current host tick count.
  static uint64_t QueryHostTickCount();
  // Host time, in FILETIME format.
  static uint64_t QueryHostSystemTime();
  // Queries the milliseconds since the host began.
  static uint64_t QueryHostUptimeMillis();

  // Guest time scalar.
  static double guest_time_scalar();
  // Sets the guest time scalar, adjusting tick and wall clock speed.
  // Ex: 1x=normal, 2x=double speed, 1/2x=half speed.
  static void set_guest_time_scalar(double scalar);
  // Get the tick ration between host and guest including time scaling if set.
  static std::pair<uint64_t, uint64_t> guest_tick_ratio();
  // Guest ticks-per-second.
  static uint64_t guest_tick_frequency();
  // Sets the guest ticks-per-second.
  static void set_guest_tick_frequency(uint64_t frequency);
  // Time based used for the guest system time.
  static uint64_t guest_system_time_base();
  // Sets the guest time base, used for computing the system time.
  // By default this is the current system time.
  static void set_guest_system_time_base(uint64_t time_base);

  // Queries the current guest tick count, accounting for frequency adjustment
  // and scaling.
  static uint64_t QueryGuestTickCount();

  static uint64_t* GetGuestTickCountPointer();
  // Queries the guest time, in FILETIME format, accounting for scaling.
  static uint64_t QueryGuestSystemTime();
  // Queries the milliseconds since the guest began, accounting for scaling.
  static uint32_t QueryGuestUptimeMillis();

  // Sets the system time of the guest.
  static void SetGuestSystemTime(uint64_t system_time);

  // Scales a time duration in milliseconds, from guest time.
  static uint32_t ScaleGuestDurationMillis(uint32_t guest_ms);
  // Scales a time duration in 100ns ticks like FILETIME, from guest time.
  static int64_t ScaleGuestDurationFileTime(int64_t guest_file_time);
  // Scales a time duration represented as a timeval, from guest time.
  static void ScaleGuestDurationTimeval(int32_t* tv_sec, int32_t* tv_usec);
};

}  // namespace xe

#endif  // XENIA_BASE_CLOCK_H_

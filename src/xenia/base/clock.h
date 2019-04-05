/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_CLOCK_H_
#define XENIA_BASE_CLOCK_H_

#include <cstdint>

namespace xe {

class Clock {
 public:
  // Host ticks-per-second.
  static uint64_t host_tick_frequency();
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
  // Queries the guest time, in FILETIME format, accounting for scaling.
  static uint64_t QueryGuestSystemTime();
  // Queries the milliseconds since the guest began, accounting for scaling.
  static uint32_t QueryGuestUptimeMillis();

  // Sets the guest tick count for the current thread.
  static void SetGuestTickCount(uint64_t tick_count);
  // Sets the system time for the current thread.
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

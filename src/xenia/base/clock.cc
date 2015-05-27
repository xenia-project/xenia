/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/clock.h"

#include "xenia/base/platform.h"

namespace xe {

double guest_time_scalar_ = 1.0;
uint64_t guest_tick_frequency_ = Clock::host_tick_frequency();
uint64_t guest_system_time_base_ = Clock::QueryHostSystemTime();

uint64_t Clock::host_tick_frequency() {
  static LARGE_INTEGER frequency = {0};
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

uint32_t Clock::QueryHostUptimeMillis() { return ::GetTickCount(); }

double Clock::guest_time_scalar() { return guest_time_scalar_; }

void Clock::set_guest_time_scalar(double scalar) {
  guest_time_scalar_ = scalar;
}

uint64_t Clock::guest_tick_frequency() { return guest_tick_frequency_; }

void Clock::set_guest_tick_frequency(uint64_t frequency) {
  guest_tick_frequency_ = frequency;
}

uint64_t Clock::guest_system_time_base() { return guest_system_time_base_; }

void Clock::set_guest_system_time_base(uint64_t time_base) {
  guest_system_time_base_ = time_base;
}

uint64_t Clock::QueryGuestTickCount() {
  // TODO(benvanik): adjust.
  return QueryHostTickCount();
}

uint64_t Clock::QueryGuestSystemTime() {
  // TODO(benvanik): adjust.
  return QueryHostSystemTime();
}

uint32_t Clock::QueryGuestUptimeMillis() {
  // TODO(benvanik): adjust.
  return QueryHostUptimeMillis();
}

uint32_t Clock::ScaleGuestDurationMillis(uint32_t guest_ms) {
  // TODO(benvanik): adjust.
  // if -1, return -1
  // if 0, return 0
  return guest_ms;
}

int64_t Clock::ScaleGuestDurationFileTime(int64_t guest_file_time) {
  // TODO(benvanik): adjust.
  // negative = relative times
  // positive = absolute times
  return guest_file_time;
}

void Clock::ScaleGuestDurationTimeval(long* tv_sec, long* tv_usec) {
  // TODO(benvanik): adjust.
}

}  // namespace xe

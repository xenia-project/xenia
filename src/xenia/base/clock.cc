/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/clock.h"

#include <algorithm>
#include <climits>

#include "xenia/base/assert.h"

namespace xe {

// Time scalar applied to all time operations.
double guest_time_scalar_ = 1.0;
// Tick frequency of guest.
uint64_t guest_tick_frequency_ = Clock::host_tick_frequency();
// Base FILETIME of the guest system from app start.
uint64_t guest_system_time_base_ = Clock::QueryHostSystemTime();
// Combined time and frequency scalar (computed by RecomputeGuestTickScalar).
double guest_tick_scalar_ = 1.0;
// Native guest ticks.
thread_local uint64_t guest_tick_count_ = 0;
// 100ns ticks, relative to guest_system_time_base_.
thread_local uint64_t guest_time_filetime_ = 0;
// Last sampled host tick count.
thread_local uint64_t last_host_tick_count_ = Clock::QueryHostTickCount();

void RecomputeGuestTickScalar() {
  guest_tick_scalar_ = (guest_tick_frequency_ * guest_time_scalar_) /
                       static_cast<double>(Clock::host_tick_frequency());
}

void UpdateGuestClock() {
  uint64_t host_tick_count = Clock::QueryHostTickCount();
  uint64_t host_tick_delta = host_tick_count > last_host_tick_count_
                                 ? host_tick_count - last_host_tick_count_
                                 : 0;
  last_host_tick_count_ = host_tick_count;
  uint64_t guest_tick_delta = uint64_t(host_tick_delta * guest_tick_scalar_);
  guest_tick_count_ += guest_tick_delta;
  guest_time_filetime_ += (guest_tick_delta * 10000000) / guest_tick_frequency_;
}

double Clock::guest_time_scalar() { return guest_time_scalar_; }

void Clock::set_guest_time_scalar(double scalar) {
  guest_time_scalar_ = scalar;
  RecomputeGuestTickScalar();
}

uint64_t Clock::guest_tick_frequency() { return guest_tick_frequency_; }

void Clock::set_guest_tick_frequency(uint64_t frequency) {
  guest_tick_frequency_ = frequency;
  RecomputeGuestTickScalar();
}

uint64_t Clock::guest_system_time_base() { return guest_system_time_base_; }

void Clock::set_guest_system_time_base(uint64_t time_base) {
  guest_system_time_base_ = time_base;
}

uint64_t Clock::QueryGuestTickCount() {
  UpdateGuestClock();
  return guest_tick_count_;
}

uint64_t Clock::QueryGuestSystemTime() {
  UpdateGuestClock();
  return guest_system_time_base_ + guest_time_filetime_;
}

uint32_t Clock::QueryGuestUptimeMillis() {
  UpdateGuestClock();
  uint64_t uptime_millis = guest_tick_count_ / (guest_tick_frequency_ / 1000);
  uint32_t result = uint32_t(std::min(uptime_millis, uint64_t(UINT_MAX)));
  return result;
}

uint32_t Clock::ScaleGuestDurationMillis(uint32_t guest_ms) {
  if (guest_ms == UINT_MAX) {
    return UINT_MAX;
  } else if (!guest_ms) {
    return 0;
  }
  uint64_t scaled_ms = uint64_t(uint64_t(guest_ms) * guest_time_scalar_);
  return uint32_t(std::min(scaled_ms, uint64_t(UINT_MAX)));
}

int64_t Clock::ScaleGuestDurationFileTime(int64_t guest_file_time) {
  if (!guest_file_time) {
    return 0;
  } else if (guest_file_time > 0) {
    // Absolute time.
    uint64_t guest_time = Clock::QueryGuestSystemTime();
    int64_t relative_time = guest_file_time - static_cast<int64_t>(guest_time);
    int64_t scaled_time =
        static_cast<int64_t>(relative_time * guest_time_scalar_);
    return static_cast<int64_t>(guest_time) + scaled_time;
  } else {
    // Relative time.
    uint64_t scaled_file_time =
        uint64_t(uint64_t(guest_file_time) * guest_time_scalar_);
    // TODO(benvanik): check for overflow?
    return scaled_file_time;
  }
}

void Clock::ScaleGuestDurationTimeval(int32_t* tv_sec, int32_t* tv_usec) {
  uint64_t scaled_sec = uint64_t(uint64_t(*tv_sec) * guest_tick_scalar_);
  uint64_t scaled_usec = uint64_t(uint64_t(*tv_usec) * guest_time_scalar_);
  if (scaled_usec > UINT_MAX) {
    uint64_t overflow_sec = scaled_usec / 1000000;
    scaled_usec -= overflow_sec * 1000000;
    scaled_sec += overflow_sec;
  }
  *tv_sec = int32_t(scaled_sec);
  *tv_usec = int32_t(scaled_usec);
}

}  // namespace xe

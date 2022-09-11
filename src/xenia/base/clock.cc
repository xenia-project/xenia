/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/clock.h"

#include <algorithm>
#include <limits>
#include <mutex>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/mutex.h"

#if defined(_WIN32)

#include "xenia/base/platform_win.h"

#endif

DEFINE_bool(clock_no_scaling, false,
            "Disable scaling code. Time management and locking is bypassed. "
            "Guest system time is directly pulled from host.",
            "CPU");
DEFINE_bool(clock_source_raw, false,
            "Use the RDTSC instruction as the time source. "
            "Host CPU must support invariant TSC.",
            "CPU");

namespace xe {

// Time scalar applied to all time operations.
double guest_time_scalar_ = 1.0;
// Tick frequency of guest.
uint64_t guest_tick_frequency_ = Clock::host_tick_frequency_platform();
// Base FILETIME of the guest system from app start.
uint64_t guest_system_time_base_ = Clock::QueryHostSystemTime();
// Combined time and frequency ratio between host and guest.
// Split in numerator (first) and denominator (second).
// Computed by RecomputeGuestTickScalar.
std::pair<uint64_t, uint64_t> guest_tick_ratio_ = std::make_pair(1, 1);

// Native guest ticks.
uint64_t last_guest_tick_count_ = 0;
// Last sampled host tick count.
uint64_t last_host_tick_count_ = Clock::QueryHostTickCount();


using tick_mutex_type = std::mutex;  

// Mutex to ensure last_host_tick_count_ and last_guest_tick_count_ are in sync
// std::mutex tick_mutex_;
static tick_mutex_type tick_mutex_;

void RecomputeGuestTickScalar() {
  // Create a rational number with numerator (first) and denominator (second)
  auto frac =
      std::make_pair(guest_tick_frequency_, Clock::QueryHostTickFrequency());
  // Doing it this way ensures we don't mess up our frequency scaling and
  // precisely controls the precision the guest_time_scalar_ can have.
  if (guest_time_scalar_ > 1.0) {
    frac.first *= static_cast<uint64_t>(guest_time_scalar_ * 10.0);
    frac.second *= 10;
  } else {
    frac.first *= 10;
    frac.second *= static_cast<uint64_t>(10.0 / guest_time_scalar_);
  }
  // Keep this a rational calculation and reduce the fraction
  reduce_fraction(frac);

  std::lock_guard<tick_mutex_type> lock(tick_mutex_);
  guest_tick_ratio_ = frac;
}

// Update the guest timer for all threads.
// Return a copy of the value so locking is reduced.
uint64_t UpdateGuestClock() {
  uint64_t host_tick_count = Clock::QueryHostTickCount();

  if (cvars::clock_no_scaling) {
    // Nothing to update, calculate on the fly
    return host_tick_count * guest_tick_ratio_.first / guest_tick_ratio_.second;
  }

  std::unique_lock<tick_mutex_type> lock(tick_mutex_, std::defer_lock);
  if (lock.try_lock()) {
    // Translate host tick count to guest tick count.
    uint64_t host_tick_delta = host_tick_count > last_host_tick_count_
                                   ? host_tick_count - last_host_tick_count_
                                   : 0;
    last_host_tick_count_ = host_tick_count;
    uint64_t guest_tick_delta =
        host_tick_delta * guest_tick_ratio_.first / guest_tick_ratio_.second;
    last_guest_tick_count_ += guest_tick_delta;
    return last_guest_tick_count_;
  } else {
    // Wait until another thread has finished updating the clock.
    lock.lock();
    return last_guest_tick_count_;
  }
}

// Offset of the current guest system file time relative to the guest base time.
inline uint64_t QueryGuestSystemTimeOffset() {
  if (cvars::clock_no_scaling) {
    return Clock::QueryHostSystemTime() - guest_system_time_base_;
  }

  auto guest_tick_count = UpdateGuestClock();

  uint64_t numerator = 10000000;  // 100ns/10MHz resolution
  uint64_t denominator = guest_tick_frequency_;
  reduce_fraction(numerator, denominator);

  return guest_tick_count * numerator / denominator;
}
uint64_t Clock::QueryHostTickFrequency() {
#if XE_CLOCK_RAW_AVAILABLE
  if (cvars::clock_source_raw) {
    return host_tick_frequency_raw();
  }
#endif
  return host_tick_frequency_platform();
}
uint64_t Clock::QueryHostTickCount() {
#if XE_CLOCK_RAW_AVAILABLE
  if (cvars::clock_source_raw) {
    return host_tick_count_raw();
  }
#endif
  return host_tick_count_platform();
}

double Clock::guest_time_scalar() { return guest_time_scalar_; }

void Clock::set_guest_time_scalar(double scalar) {
  if (cvars::clock_no_scaling) {
    return;
  }

  guest_time_scalar_ = scalar;
  RecomputeGuestTickScalar();
}

std::pair<uint64_t, uint64_t> Clock::guest_tick_ratio() {
  std::lock_guard<tick_mutex_type> lock(tick_mutex_);
  return guest_tick_ratio_;
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
  auto guest_tick_count = UpdateGuestClock();
  return guest_tick_count;
}

uint64_t* Clock::GetGuestTickCountPointer() { return &last_guest_tick_count_; }
uint64_t Clock::QueryGuestSystemTime() {
  if (cvars::clock_no_scaling) {
    return Clock::QueryHostSystemTime();
  }

  auto guest_system_time_offset = QueryGuestSystemTimeOffset();
  return guest_system_time_base_ + guest_system_time_offset;
}

uint32_t Clock::QueryGuestUptimeMillis() {
  return static_cast<uint32_t>(
      std::min<uint64_t>(QueryGuestSystemTimeOffset() / 10000,
                         std::numeric_limits<uint32_t>::max()));
}

void Clock::SetGuestSystemTime(uint64_t system_time) {
  if (cvars::clock_no_scaling) {
    // Time is fixed to host time.
    return;
  }

  // Query the filetime offset to calculate a new base time.
  auto guest_system_time_offset = QueryGuestSystemTimeOffset();
  guest_system_time_base_ = system_time - guest_system_time_offset;
}

uint32_t Clock::ScaleGuestDurationMillis(uint32_t guest_ms) {
  if (cvars::clock_no_scaling) {
    return guest_ms;
  }

  constexpr uint64_t max = std::numeric_limits<uint32_t>::max();

  if (guest_ms >= max) {
    return max;
  } else if (!guest_ms) {
    return 0;
  }
  uint64_t scaled_ms = static_cast<uint64_t>(
      (static_cast<uint64_t>(guest_ms) * guest_time_scalar_));
  return static_cast<uint32_t>(std::min(scaled_ms, max));
}

int64_t Clock::ScaleGuestDurationFileTime(int64_t guest_file_time) {
  if (cvars::clock_no_scaling) {
    return static_cast<uint64_t>(guest_file_time);
  }

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
    uint64_t scaled_file_time = static_cast<uint64_t>(
        (static_cast<uint64_t>(guest_file_time) * guest_time_scalar_));
    // TODO(benvanik): check for overflow?
    return scaled_file_time;
  }
}

void Clock::ScaleGuestDurationTimeval(int32_t* tv_sec, int32_t* tv_usec) {
  if (cvars::clock_no_scaling) {
    return;
  }

  uint64_t scaled_sec = static_cast<uint64_t>(static_cast<uint64_t>(*tv_sec) *
                                              guest_time_scalar_);
  uint64_t scaled_usec = static_cast<uint64_t>(static_cast<uint64_t>(*tv_usec) *
                                               guest_time_scalar_);
  if (scaled_usec > std::numeric_limits<uint32_t>::max()) {
    uint64_t overflow_sec = scaled_usec / 1000000;
    scaled_usec -= overflow_sec * 1000000;
    scaled_sec += overflow_sec;
  }
  *tv_sec = int32_t(scaled_sec);
  *tv_usec = int32_t(scaled_usec);
}

}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_CHRONO_STEADY_CAST_H_
#define XENIA_BASE_CHRONO_STEADY_CAST_H_

#include <atomic>

#include "xenia/base/chrono.h"

// This is in a separate header because casting to and from steady time points
// usually doesn't make sense and is imprecise. However, NT uses the FileTime
// epoch as a steady clock in waits. In such cases, include this header and use
// clock_cast<>().

namespace date {

// This conveniently works only for Host time domain because Guest needs
// additional scaling. Convert XSystemClock to WinSystemClock first if
// necessary.
template <>
struct clock_time_conversion<::xe::chrono::WinSystemClock,
                             std::chrono::steady_clock> {
  // using NtSystemClock_ = ::xe::chrono::internal::NtSystemClock<domain_>;
  using WinSystemClock_ = ::xe::chrono::WinSystemClock;
  using steady_clock_ = std::chrono::steady_clock;

  template <typename Duration>
  typename WinSystemClock_::time_point operator()(
      const std::chrono::time_point<steady_clock_, Duration>& t) const {
    // Since there is no known epoch for steady_clock and even if, since it can
    // progress differently than other common clocks (e.g. stopping when the
    // computer is suspended), we need to use now() which introduces
    // imprecision.
    // Memory fences to keep the clock fetches close together to
    // minimize drift. This pattern was benchmarked to give the lowest
    // conversion error: error = sty_tpoint -
    // clock_cast<sty>(clock_cast<nt>(sty_tpoint));
    std::atomic_thread_fence(std::memory_order_acq_rel);
    auto steady_now = steady_clock_::now();
    auto nt_now = WinSystemClock_::now();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    auto delta = std::chrono::floor<WinSystemClock_::duration>(t - steady_now);
    return nt_now + delta;
  }
};

template <>
struct clock_time_conversion<std::chrono::steady_clock,
                             ::xe::chrono::WinSystemClock> {
  using WinSystemClock_ = ::xe::chrono::WinSystemClock;
  using steady_clock_ = std::chrono::steady_clock;

  template <typename Duration>
  steady_clock_::time_point operator()(
      const std::chrono::time_point<WinSystemClock_, Duration>& t) const {
    std::atomic_thread_fence(std::memory_order_acq_rel);
    auto steady_now = steady_clock_::now();
    auto nt_now = WinSystemClock_::now();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    auto delta = t - nt_now;
    return steady_now + delta;
  }
};

}  // namespace date

#endif

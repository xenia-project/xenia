/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_CHRONO_H_
#define XENIA_BASE_CHRONO_H_

#include <chrono>
#include <cstdint>

// https://github.com/HowardHinnant/date/commit/5ba1c1ad8514362dba596f228eb20eb13f63d948#r33275526
#define HAS_UNCAUGHT_EXCEPTIONS 1
#include "third_party/date/include/date/tz.h"

#include "xenia/base/clock.h"

namespace xe {
using hundrednano = std::ratio<1, 10000000>;

namespace chrono {

using hundrednanoseconds = std::chrono::duration<int64_t, hundrednano>;

// https://docs.microsoft.com/en-us/windows/win32/sysinfo/converting-a-time-t-value-to-a-file-time
//  Don't forget the 89 leap days.
static constexpr std::chrono::seconds seconds_1601_to_1970 =
    (396 * 365 + 89) * std::chrono::seconds(60 * 60 * 24);

// TODO(JoelLinn) define xstead_clock xsystem_clock etc.

namespace internal {
// Trick to reduce code duplication and keep all the chrono template magic
// working
enum class Domain {
  // boring host clock:
  Host,
  // adheres to guest scaling (differrent speed, changing clock drift etc):
  Guest
};

template <Domain domain_>
struct NtSystemClock {
  using rep = int64_t;
  using period = hundrednano;
  using duration = hundrednanoseconds;
  using time_point = std::chrono::time_point<NtSystemClock<domain_>>;
  // This really depends on the context the clock is used in:
  // static constexpr bool is_steady = false;

 public:
  // The delta between std::chrono::system_clock (Jan 1 1970) and NT file
  // time (Jan 1 1601), in seconds. In the spec std::chrono::system_clock's
  // epoch is undefined, but C++20 cements it as Jan 1 1970.
  static constexpr std::chrono::seconds unix_epoch_delta() {
    using std::chrono::steady_clock;
    auto filetime_epoch = date::year{1601} / date::month{1} / date::day{1};
    auto system_clock_epoch = date::year{1970} / date::month{1} / date::day{1};
    auto fp = static_cast<date::sys_seconds>(
        static_cast<date::sys_days>(filetime_epoch));
    auto sp = static_cast<date::sys_seconds>(
        static_cast<date::sys_days>(system_clock_epoch));
    return fp.time_since_epoch() - sp.time_since_epoch();
  }

 public:
  static constexpr uint64_t to_file_time(time_point const& tp) noexcept {
    return static_cast<uint64_t>(tp.time_since_epoch().count());
  }

  static constexpr time_point from_file_time(uint64_t const& tp) noexcept {
    return time_point{duration{tp}};
  }

  // To convert XSystemClock to sys, do clock_cast<WinSystemTime>(tp) first
  // SFINAE hack https://stackoverflow.com/a/58813009
  template <Domain domain_fresh_ = domain_>
  static constexpr std::enable_if_t<domain_fresh_ == Domain::Host,
                                    std::chrono::system_clock::time_point>
  to_sys(const time_point& tp) {
    using sys_duration = std::chrono::system_clock::duration;
    using sys_time = std::chrono::system_clock::time_point;

    auto dp = tp;
    dp += unix_epoch_delta();
    auto cdp = std::chrono::time_point_cast<sys_duration>(dp);
    return sys_time{cdp.time_since_epoch()};
  }

  template <Domain domain_fresh_ = domain_>
  static constexpr std::enable_if_t<
      domain_fresh_ == Domain::Host,
      std::chrono::local_time<std::chrono::system_clock::duration>>
  to_local(const time_point& tp) {
    return std::chrono::current_zone()->to_local(to_sys(tp));
  }

  template <Domain domain_fresh_ = domain_>
  static constexpr std::enable_if_t<domain_fresh_ == Domain::Host, time_point>
  from_sys(const std::chrono::system_clock::time_point& tp) {
    auto ctp = std::chrono::time_point_cast<duration>(tp);
    auto dp = time_point{ctp.time_since_epoch()};
    dp -= unix_epoch_delta();
    return dp;
  }

  [[nodiscard]] static time_point now() noexcept {
    if constexpr (domain_ == Domain::Host) {
      // QueryHostSystemTime() returns windows epoch times even on POSIX
      return from_file_time(Clock::QueryHostSystemTime());
    } else if constexpr (domain_ == Domain::Guest) {
      return from_file_time(Clock::QueryGuestSystemTime());
    }
  }
};
}  // namespace internal

// Unscaled system clock which can be used for filetime <-> system_clock
// conversion
using WinSystemClock = internal::NtSystemClock<internal::Domain::Host>;

// Guest system clock, scaled
using XSystemClock = internal::NtSystemClock<internal::Domain::Guest>;

}  // namespace chrono
}  // namespace xe

namespace date {

template <>
struct clock_time_conversion<::xe::chrono::WinSystemClock,
                             ::xe::chrono::XSystemClock> {
  using WClock_ = ::xe::chrono::WinSystemClock;
  using XClock_ = ::xe::chrono::XSystemClock;

  template <typename Duration>
  typename WClock_::time_point operator()(
      const std::chrono::time_point<XClock_, Duration>& t) const {
    // Consult chrono_steady_cast.h for explanation on this:
    std::atomic_thread_fence(std::memory_order_acq_rel);
    auto w_now = WClock_::now();
    auto x_now = XClock_::now();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    auto delta = (t - x_now);
    if (!::cvars::clock_no_scaling) {
      delta = std::chrono::floor<WClock_::duration>(
          delta * xe::Clock::guest_time_scalar());
    }
    return w_now + delta;
  }
};

template <>
struct clock_time_conversion<::xe::chrono::XSystemClock,
                             ::xe::chrono::WinSystemClock> {
  using WClock_ = ::xe::chrono::WinSystemClock;
  using XClock_ = ::xe::chrono::XSystemClock;

  template <typename Duration>
  typename XClock_::time_point operator()(
      const std::chrono::time_point<WClock_, Duration>& t) const {
    // Consult chrono_steady_cast.h for explanation on this:
    std::atomic_thread_fence(std::memory_order_acq_rel);
    auto w_now = WClock_::now();
    auto x_now = XClock_::now();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    xe::chrono::hundrednanoseconds delta = (t - w_now);
    if (!::cvars::clock_no_scaling) {
      delta = std::chrono::floor<WClock_::duration>(
          delta / xe::Clock::guest_time_scalar());
    }
    return x_now + delta;
  }
};

}  // namespace date

#endif

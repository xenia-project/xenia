/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XCLOCK_H_
#define XENIA_KERNEL_XCLOCK_H_

#include <chrono>

#include "xenia/base/clock.h"

// https://github.com/HowardHinnant/date/commit/5ba1c1ad8514362dba596f228eb20eb13f63d948#r33275526
#define HAS_UNCAUGHT_EXCEPTIONS 1
#include "third_party/date/include/date/date.h"

namespace xe {
namespace kernel {

struct XClock {
  using rep = int64_t;
  using period = std::ratio_multiply<std::ratio<100>, std::nano>;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<XClock>;
  static constexpr bool is_steady = false;

  static time_point now() noexcept {
    return from_file_time(Clock::QueryGuestSystemTime());
  }

  static uint64_t to_file_time(time_point const& tp) noexcept {
    return static_cast<uint64_t>(tp.time_since_epoch().count());
  }

  static time_point from_file_time(uint64_t const& tp) noexcept {
    return time_point{duration{tp}};
  }

  static std::chrono::system_clock::time_point to_sys(time_point const& tp) {
    // TODO(gibbed): verify behavior under Linux
    using sys_duration = std::chrono::system_clock::duration;
    using sys_time = std::chrono::system_clock::time_point;
    auto dp = tp;
    dp += system_clock_delta();
    auto cdp = std::chrono::time_point_cast<sys_duration>(dp);
    return sys_time{cdp.time_since_epoch()};
  }

  static time_point from_sys(std::chrono::system_clock::time_point const& tp) {
    // TODO(gibbed): verify behavior under Linux
    auto ctp = std::chrono::time_point_cast<duration>(tp);
    auto dp = time_point{ctp.time_since_epoch()};
    dp -= system_clock_delta();
    return dp;
  }

 private:
  // The delta between std::chrono::system_clock (Jan 1 1970) and Xenon file
  // time (Jan 1 1601), in seconds. In the spec std::chrono::system_clock's
  // epoch is undefined, but C++20 cements it as Jan 1 1970.
  static constexpr std::chrono::seconds system_clock_delta() {
    auto filetime_epoch = date::year{1601} / date::month{1} / date::day{1};
    auto system_clock_epoch = date::year{1970} / date::month{1} / date::day{1};
    std::chrono::system_clock::time_point fp{
        static_cast<date::sys_days>(filetime_epoch)};
    std::chrono::system_clock::time_point sp{
        static_cast<date::sys_days>(system_clock_epoch)};
    return std::chrono::floor<std::chrono::seconds>(fp.time_since_epoch() -
                                                    sp.time_since_epoch());
  }
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XCLOCK_H_

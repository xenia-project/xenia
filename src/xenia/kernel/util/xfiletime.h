/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_XFILETIME_H_
#define XENIA_KERNEL_UTIL_XFILETIME_H_

#include "xenia/base/byte_order.h"
#include "xenia/base/chrono.h"

namespace xe {
namespace kernel {

struct X_FILETIME {
  static constexpr uint64_t minimal_valid_time = 125911584000000000;
  static constexpr uint64_t maximal_valid_time = 157469184000000000;

  xe::be<uint32_t> high_part;
  xe::be<uint32_t> low_part;

  X_FILETIME() {
    high_part = 0;
    low_part = 0;
  }

  X_FILETIME(uint64_t filetime) {
    high_part = static_cast<uint32_t>(filetime >> 32);
    low_part = static_cast<uint32_t>(filetime);
  }

  X_FILETIME(std::time_t time) {
    const auto file_time =
        chrono::WinSystemClock::to_file_time(chrono::WinSystemClock::from_sys(
            std::chrono::system_clock::from_time_t(time)));

    high_part = static_cast<uint32_t>(file_time >> 32);
    low_part = static_cast<uint32_t>(file_time);
  }

  chrono::WinSystemClock::time_point to_time_point() const {
    const uint64_t filetime =
        (static_cast<uint64_t>(high_part) << 32) | low_part;

    return chrono::WinSystemClock::from_file_time(filetime);
  }

  bool is_valid() const {
    const uint64_t filetime =
        (static_cast<uint64_t>(high_part) << 32) | low_part;

    return filetime >= minimal_valid_time && filetime <= maximal_valid_time;
  }
};
static_assert_size(X_FILETIME, 0x8);

}  // namespace kernel
}  // namespace xe

#endif

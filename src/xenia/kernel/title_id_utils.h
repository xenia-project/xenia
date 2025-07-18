/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_TITLE_ID_UTILS_H_
#define XENIA_KERNEL_UTIL_TITLE_ID_UTILS_H_

#include <cstdint>
#include <utility>

namespace xe {
namespace kernel {

inline constexpr uint32_t kXN_2001 = 0x584E07D1;
inline constexpr uint32_t kXN_2002 = 0x584E07D2;
inline constexpr uint32_t kDashboardID = 0xFFFE07D1;

inline constexpr uint16_t GetGameId(const uint32_t title_id) {
  return title_id >> 16;
}

inline constexpr bool IsValidGameId(const uint32_t title_id) {
  return (title_id >> 16) != 0xFFFE;
}

inline constexpr std::pair<char, char> GetTitlePublisher(
    const uint32_t title_id) {
  const char first_char = title_id >> 24;
  const char second_char = (title_id >> 16) & 0xFF;

  return {first_char, second_char};
}

inline constexpr bool IsXboxTitle(const uint32_t title_id) {
  const auto publisher = GetTitlePublisher(title_id);

  return publisher.first == 'X';
}

inline constexpr bool IsXblaTitle(const uint32_t title_id) {
  const auto publisher = GetTitlePublisher(title_id);

  return publisher.first == 'X' && publisher.second == 'A';
}

static_assert(IsXblaTitle(0x5841127D));   // XBLA Game
static_assert(!IsXblaTitle(0x4D5309C9));  // Non-XBLA Game

inline constexpr bool IsAppTitle(const uint32_t title_id) {
  const auto publisher = GetTitlePublisher(title_id);

  return publisher.first == 'X' && publisher.second == 'H' ||
         publisher.first == 'X' && publisher.second == 'J';
}

inline constexpr bool IsXNTitle(const uint32_t title_id) {
  return title_id == kXN_2001 || title_id == kXN_2002;
}

inline constexpr bool IsSystemExperienceTitle(const uint32_t title_id) {
  if (IsAppTitle(title_id)) {
    return true;
  }

  return IsXNTitle(title_id);
};

inline constexpr bool IsSystemTitle(const uint32_t title_id) {
  if (!title_id) {
    return true;
  }

  if (!IsXboxTitle(title_id)) {
    return !IsValidGameId(title_id);
  }

  if (IsXblaTitle(title_id)) {
    return !IsValidGameId(title_id);
  }

  return true;
};

static_assert(IsSystemTitle(kDashboardID));  // Dashboard check
static_assert(!IsSystemTitle(0x4D5308BC));   // Non-XBLA Game
static_assert(!IsSystemTitle(0x5841089A));   // XBLA Game

inline constexpr bool IsOriginalXboxTitle(const uint32_t title_id) {
  if (!IsValidGameId(title_id)) {
    return true;
  }

  if (title_id >> 24 == 0xFF) {
    return false;
  }

  return (title_id & 0x7FFF) < 0x7D0;
};

static_assert(IsOriginalXboxTitle(0x41430006));   // OG-Xbox Game
static_assert(!IsOriginalXboxTitle(0x4D5308BC));  // 360 Game

}  // namespace kernel
}  // namespace xe

#endif

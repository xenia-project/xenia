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

static constexpr uint32_t kXN_2001 = 0x584E07D1;
static constexpr uint32_t kXN_2002 = 0x584E07D2;
static constexpr uint32_t kDashboardID = 0xFFFE07D1;

static constexpr uint16_t GetGameId(const uint32_t title_id) {
  return title_id >> 16;
}

static constexpr bool IsValidGameId(const uint32_t title_id) {
  return (title_id >> 16) != 0xFFFE;
}

static constexpr std::pair<char, char> GetTitlePublisher(
    const uint32_t title_id) {
  const char first_char = title_id >> 24;
  const char second_char = (title_id >> 16) & 0xFF;

  return {first_char, second_char};
}

static constexpr bool IsXboxTitle(const uint32_t title_id) {
  const auto publisher = GetTitlePublisher(title_id);

  return publisher.first == 'X';
}

static constexpr bool IsXblaTitle(const uint32_t title_id) {
  const auto publisher = GetTitlePublisher(title_id);

  return publisher.first == 'X' && publisher.second == 'A';
}

static constexpr bool IsAppTitle(const uint32_t title_id) {
  const auto publisher = GetTitlePublisher(title_id);

  return publisher.first == 'X' && publisher.second == 'H' ||
         publisher.first == 'X' && publisher.second == 'H';
}

static constexpr bool IsXNTitle(const uint32_t title_id) {
  return title_id == kXN_2001 || title_id == kXN_2002;
}

static constexpr bool IsSystemExperienceTitle(const uint32_t title_id) {
  if (IsAppTitle(title_id)) {
    return true;
  }

  return IsXNTitle(title_id);
};

static constexpr bool IsSystemTitle(const uint32_t title_id) {
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

static constexpr bool IsOriginalXboxTitle(const uint32_t title_id) {
  if (!IsValidGameId(title_id)) {
    return true;
  }

  if (!title_id || (title_id >> 24) == 0xFF) {
    return false;
  }

  return (title_id >> 17) < 0x7D0;
};

}  // namespace kernel
}  // namespace xe

#endif
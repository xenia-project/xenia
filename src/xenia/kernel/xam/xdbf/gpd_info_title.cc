/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xdbf/gpd_info_title.h"
#include <ranges>

namespace xe {
namespace kernel {
namespace xam {

X_XDBF_GPD_ACHIEVEMENT* GpdInfoTitle::GetAchievementEntry(const uint32_t id) {
  Entry* entry = GetEntry(static_cast<uint16_t>(GpdSection::kAchievement), id);

  if (!entry) {
    return nullptr;
  }

  return reinterpret_cast<X_XDBF_GPD_ACHIEVEMENT*>(entry->data.data());
}

const char16_t* GpdInfoTitle::GetAchievementTitlePtr(const uint32_t id) {
  X_XDBF_GPD_ACHIEVEMENT* achievement_ptr = GetAchievementEntry(id);
  if (!achievement_ptr) {
    return nullptr;
  }

  return reinterpret_cast<const char16_t*>(++achievement_ptr);
}

const char16_t* GpdInfoTitle::GetAchievementDescriptionPtr(const uint32_t id) {
  // We need to get ptr to first string. These are one after another in memory.
  const char16_t* title_ptr = GetAchievementTitlePtr(id);
  if (!title_ptr) {
    return nullptr;
  }

  return reinterpret_cast<const char16_t*>(title_ptr +
                                           GetAchievementTitle(id).length());
}

const char16_t* GpdInfoTitle::GetAchievementUnachievedDescriptionPtr(
    const uint32_t id) {
  const char16_t* title_ptr = GetAchievementDescriptionPtr(id);
  if (!title_ptr) {
    return nullptr;
  }

  return reinterpret_cast<const char16_t*>(
      title_ptr + GetAchievementDescription(id).length());
}

std::u16string GpdInfoTitle::GetAchievementTitle(const uint32_t id) {
  auto title_ptr = GetAchievementTitlePtr(id);

  if (!title_ptr) {
    return std::u16string();
  }

  return string_util::read_u16string_and_swap(title_ptr);
}

std::u16string GpdInfoTitle::GetAchievementDescription(const uint32_t id) {
  auto description_ptr = GetAchievementDescriptionPtr(id);

  if (!description_ptr) {
    return std::u16string();
  }

  return string_util::read_u16string_and_swap(description_ptr);
}

std::u16string GpdInfoTitle::GetAchievementUnachievedDescription(
    const uint32_t id) {
  auto description_ptr = GetAchievementUnachievedDescriptionPtr(id);

  if (!description_ptr) {
    return std::u16string();
  }

  return string_util::read_u16string_and_swap(description_ptr);
}

std::vector<uint32_t> GpdInfoTitle::GetAchievementsIds() const {
  std::vector<uint32_t> ids;

  auto achievements =
      entries_ | std::views::filter([](const auto& entry) {
        return !IsSyncEntry(&entry);
      }) |
      std::views::filter([](const auto& entry) {
        return IsEntryOfSection(&entry, GpdSection::kAchievement);
      });

  for (const auto& achievement : achievements) {
    ids.push_back(static_cast<uint32_t>(achievement.info.id));
  }
  return ids;
}

void GpdInfoTitle::AddAchievement(const AchievementDetails* header) {
  Entry* entry =
      GetEntry(static_cast<uint16_t>(GpdSection::kAchievement), header->id);

  if (entry) {
    return;
  }

  X_XDBF_GPD_ACHIEVEMENT internal_info;
  internal_info.magic = sizeof(X_XDBF_GPD_ACHIEVEMENT);
  internal_info.id = header->id;
  internal_info.image_id = header->image_id;
  internal_info.gamerscore = header->gamerscore;
  internal_info.flags = header->flags;
  internal_info.unlock_time = 0;

  const uint32_t strings_size =
      static_cast<uint32_t>(string_util::size_in_bytes(header->label) +
                            string_util::size_in_bytes(header->description) +
                            string_util::size_in_bytes(header->unachieved));

  const uint32_t entry_size = sizeof(X_XDBF_GPD_ACHIEVEMENT) + strings_size;

  Entry new_entry(header->id, static_cast<uint16_t>(GpdSection::kAchievement),
                  entry_size);

  uint8_t* write_ptr = new_entry.data.data();
  memcpy(write_ptr, &internal_info, sizeof(X_XDBF_GPD_ACHIEVEMENT));

  write_ptr += sizeof(X_XDBF_GPD_ACHIEVEMENT);

  string_util::copy_and_swap_truncating(reinterpret_cast<char16_t*>(write_ptr),
                                        header->label,
                                        header->label.length() + 1);

  write_ptr += string_util::size_in_bytes(header->label);

  string_util::copy_and_swap_truncating(reinterpret_cast<char16_t*>(write_ptr),
                                        header->description,
                                        header->description.length() + 1);

  write_ptr += string_util::size_in_bytes(header->description);

  string_util::copy_and_swap_truncating(reinterpret_cast<char16_t*>(write_ptr),
                                        header->unachieved,
                                        header->unachieved.length() + 1);

  UpsertEntry(&new_entry);
}

uint32_t GpdInfoTitle::GetTotalGamerscore() {
  const auto ids = GetAchievementsIds();

  uint32_t gamerscore = 0;
  for (const auto id : ids) {
    gamerscore += GetAchievementEntry(id)->gamerscore;
  }

  return gamerscore;
}
uint32_t GpdInfoTitle::GetGamerscore() {
  const auto ids = GetAchievementsIds();
  uint32_t gamerscore = 0;
  for (const auto id : ids) {
    const auto entry = GetAchievementEntry(id);
    if (entry->is_achievement_unlocked()) {
      gamerscore += GetAchievementEntry(id)->gamerscore;
    }
  }
  return gamerscore;
}

uint32_t GpdInfoTitle::GetAchievementCount() {
  return static_cast<uint32_t>(GetAchievementsIds().size());
}

uint32_t GpdInfoTitle::GetUnlockedAchievementCount() {
  const auto ids = GetAchievementsIds();
  uint32_t count = 0;
  for (const auto id : ids) {
    const auto entry = GetAchievementEntry(id);
    if (entry->is_achievement_unlocked()) {
      count += 1;
    }
  }
  return count;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
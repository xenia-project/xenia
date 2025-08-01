/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xdbf/gpd_info_profile.h"

#include "xenia/base/string_util.h"

#include <ranges>

namespace xe {
namespace kernel {
namespace xam {

const std::vector<const X_XDBF_GPD_TITLE_PLAYED*>
GpdInfoProfile::GetTitlesInfo() const {
  std::vector<const X_XDBF_GPD_TITLE_PLAYED*> entries;

  auto titles = entries_ | std::views::filter([](const auto& entry) {
                  return !IsSyncEntry(&entry);
                }) |
                std::views::filter([](const auto& entry) {
                  return IsEntryOfSection(&entry, GpdSection::kTitle);
                });

  for (const auto& title : titles) {
    entries.push_back(
        reinterpret_cast<const X_XDBF_GPD_TITLE_PLAYED*>(title.data.data()));
  }
  return entries;
};

X_XDBF_GPD_TITLE_PLAYED* GpdInfoProfile::GetTitleInfo(const uint32_t title_id) {
  auto title = entries_ | std::views::filter([](const auto& entry) {
                 return !IsSyncEntry(&entry);
               }) |
               std::views::filter([](const auto& entry) {
                 return IsEntryOfSection(&entry, GpdSection::kTitle);
               }) |
               std::views::filter([title_id](const auto& entry) {
                 return static_cast<uint32_t>(entry.info.id) == title_id;
               });

  if (title.empty()) {
    return nullptr;
  }

  return reinterpret_cast<X_XDBF_GPD_TITLE_PLAYED*>(title.begin()->data.data());
}

std::u16string GpdInfoProfile::GetTitleName(const uint32_t title_id) const {
  const Entry* entry =
      GetEntry(static_cast<uint16_t>(GpdSection::kTitle), title_id);

  if (!entry) {
    return std::u16string();
  }

  return string_util::read_u16string_and_swap(reinterpret_cast<const char16_t*>(
      entry->data.data() + sizeof(X_XDBF_GPD_TITLE_PLAYED)));
}

void GpdInfoProfile::AddNewTitle(const SpaInfo* title_data) {
  const X_XDBF_GPD_TITLE_PLAYED title_gpd_data =
      FillTitlePlayedData(title_data);

  const std::u16string title_name = xe::to_utf16(title_data->title_name());

  const uint32_t entry_size =
      sizeof(X_XDBF_GPD_TITLE_PLAYED) +
      static_cast<uint32_t>(string_util::size_in_bytes(title_name));

  Entry entry(title_data->title_id(), static_cast<uint16_t>(GpdSection::kTitle),
              entry_size);

  memcpy(entry.data.data(), &title_gpd_data, sizeof(X_XDBF_GPD_TITLE_PLAYED));

  string_util::copy_and_swap_truncating(
      reinterpret_cast<char16_t*>(entry.data.data() +
                                  sizeof(X_XDBF_GPD_TITLE_PLAYED)),
      title_name, title_name.size() + 1);

  UpsertEntry(&entry);
}

bool GpdInfoProfile::RemoveTitle(const uint32_t title_id) {
  const Entry* entry =
      GetEntry(static_cast<uint16_t>(GpdSection::kTitle), title_id);
  if (!entry) {
    return false;
  }

  DeleteEntry(entry);
  return true;
}

X_XDBF_GPD_TITLE_PLAYED GpdInfoProfile::FillTitlePlayedData(
    const SpaInfo* title_data) const {
  X_XDBF_GPD_TITLE_PLAYED title_gpd_data = {};

  title_gpd_data.title_id = title_data->title_id();
  title_gpd_data.achievements_count = title_data->achievement_count();
  title_gpd_data.gamerscore_total = title_data->total_gamerscore();

  return title_gpd_data;
}

void GpdInfoProfile::UpdateTitleInfo(const uint32_t title_id,
                                     X_XDBF_GPD_TITLE_PLAYED* title_data) {
  X_XDBF_GPD_TITLE_PLAYED* current_info = GetTitleInfo(title_id);
  if (!current_info) {
    return;
  }

  memcpy(current_info, title_data, sizeof(X_XDBF_GPD_TITLE_PLAYED));
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

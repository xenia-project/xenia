/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XDBF_GPD_INFO_H_
#define XENIA_KERNEL_XAM_XDBF_GPD_INFO_H_

#include <span>
#include <string>
#include <vector>

#include "xenia/base/byte_order.h"
#include "xenia/kernel/util/xfiletime.h"
#include "xenia/kernel/xam/user_data.h"
#include "xenia/kernel/xam/xdbf/xdbf_io.h"

namespace xe {
namespace kernel {
namespace xam {

class UserSetting;

enum class GpdSection : uint16_t {
  kAchievement = 0x1,  // TitleGpd exclusive
  kImage = 0x2,
  kSetting = 0x3,
  kTitle = 0x4,  // Dashboard Gpd exclusive
  kString = 0x5,
  kProtectedAchievement = 0x6,  // GFWL only
};

enum class AchievementFlags : uint32_t {
  kTypeMask = 0x7,
  kShowUnachieved = 0x8,
  kAchievedOnline = 0x10000,
  kAchieved = 0x20000,
  kNotAchievable = 0x40000,
  kWasNotAchievable = 0x80000,
  kPlatformMask = 0x700000,
  kColorizable = 0x1000000,  // avatar awards only?
};

struct X_XDBF_AVATARAWARDS_COUNTER {
  uint8_t earned;
  uint8_t possible;
};
static_assert_size(X_XDBF_AVATARAWARDS_COUNTER, 2);

#pragma pack(push, 1)
struct X_XDBF_GPD_ACHIEVEMENT {
  xe::be<uint32_t> magic;
  xe::be<uint32_t> id;
  xe::be<uint32_t> image_id;
  xe::be<uint32_t> gamerscore;
  xe::be<uint32_t> flags;
  xe::be<uint64_t> unlock_time;
  // wchar_t* title;
  // wchar_t* description;
  // wchar_t* unlocked_description;

  bool is_achievement_unlocked() const {
    return flags & static_cast<uint32_t>(AchievementFlags::kAchieved);
  }
};
static_assert_size(X_XDBF_GPD_ACHIEVEMENT, 0x1C);

struct X_XDBF_GPD_TITLE_PLAYED {
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> achievements_count;
  xe::be<uint32_t> achievements_unlocked;
  xe::be<uint32_t> gamerscore_total;
  xe::be<uint32_t> gamerscore_earned;
  xe::be<uint16_t> online_achievement_count;

  X_XDBF_AVATARAWARDS_COUNTER all_avatar_awards;
  X_XDBF_AVATARAWARDS_COUNTER male_avatar_awards;
  X_XDBF_AVATARAWARDS_COUNTER female_avatar_awards;
  xe::be<uint32_t>
      flags;  // 1 - Offline unlocked, must be synced. 2 - Achievement Unlocked.
              // Image missing. 0x10 - Avatar unlocked. Avatar missing.
  X_FILETIME last_played;
  // xe::be<char16_t> title_name[64]; // size seems to be variable inside GPDs.

  bool include_in_enumerator() const { return achievements_count != 0; }
};
static_assert_size(X_XDBF_GPD_TITLE_PLAYED, 0x28);

struct X_XDBF_GPD_SETTING_HEADER {
  xe::be<uint32_t> setting_id;
  xe::be<uint32_t> unknown_1;
  X_USER_DATA_TYPE setting_type;
  char unknown[7];
  X_USER_DATA_UNION base_data;

  bool RequiresBuffer() const {
    return setting_type == X_USER_DATA_TYPE::BINARY ||
           setting_type == X_USER_DATA_TYPE::WSTRING;
  }
};
static_assert_size(X_XDBF_GPD_SETTING_HEADER, 0x18);
#pragma pack(pop)

class GpdInfo : public XdbfFile {
 public:
  GpdInfo();
  GpdInfo(const uint32_t title_id);
  GpdInfo(const uint32_t title_id, const std::vector<uint8_t> buffer);

  ~GpdInfo() = default;

  // Normally GPD ALWAYS contains one free entry that indicates EOF
  bool IsValid() const { return !free_entries_.empty(); }
  // r/w image, setting, string.
  std::span<const uint8_t> GetImage(uint64_t id) const;
  void AddImage(uint32_t id, std::span<const uint8_t> image_data);

  X_XDBF_GPD_SETTING_HEADER* GetSetting(uint32_t id);
  std::span<const uint8_t> GetSettingData(uint32_t id);

  void UpsertSetting(const UserSetting* setting_data);

  std::u16string GetString(uint32_t id) const;
  void AddString(uint32_t id, std::u16string string_data);

  std::vector<uint8_t> Serialize() const;

 protected:
  static bool IsSyncEntry(const Entry* const entry);
  static bool IsEntryOfSection(const Entry* const entry,
                               const GpdSection section);

  void UpsertEntry(Entry* entry);

  uint32_t FindFreeLocation(const uint32_t entry_size);

 private:
  static constexpr uint32_t base_entry_count = 512;

  uint32_t title_id_ = 0;

  void InsertEntry(Entry* entry);
  void DeleteEntry(const Entry* entry);

  std::vector<const Entry*> GetSortedEntries() const;

  void ResizeEntryTable();
  void ReallocateEntry(Entry* entry, uint32_t required_size);
  void MarkSpaceAsFree(uint32_t offset, uint32_t size);
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XDBF_GPD_INFO_H_

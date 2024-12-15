/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_ACHIEVEMENT_MANAGER_H_
#define XENIA_KERNEL_XAM_ACHIEVEMENT_MANAGER_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "xenia/base/chrono.h"
#include "xenia/kernel/xam/xdbf/gpd_info.h"
#include "xenia/kernel/xam/xdbf/spa_info.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

enum class AchievementType : uint32_t {
  kCompletion = 1,
  kLeveling = 2,
  kUnlock = 3,
  kEvent = 4,
  kTournament = 5,
  kCheckpoint = 6,
  kOther = 7,
};

enum class AchievementPlatform : uint32_t {
  kX360 = 0x100000,
  kPC = 0x200000,
  kMobile = 0x300000,
  kWebGames = 0x400000,
};

struct X_ACHIEVEMENT_DETAILS {
  xe::be<uint32_t> id;
  xe::be<uint32_t> label_ptr;
  xe::be<uint32_t> description_ptr;
  xe::be<uint32_t> unachieved_ptr;
  xe::be<uint32_t> image_id;
  xe::be<uint32_t> gamerscore;
  X_FILETIME unlock_time;
  xe::be<uint32_t> flags;

  static const size_t kStringBufferSize = 464;
};
static_assert_size(X_ACHIEVEMENT_DETAILS, 36);

// Host structures
struct AchievementDetails {
  uint32_t id;
  std::u16string label;
  std::u16string description;
  std::u16string unachieved;
  uint32_t image_id;
  uint32_t gamerscore;
  X_FILETIME unlock_time;
  uint32_t flags;

  AchievementDetails(uint32_t id, std::u16string label,
                     std::u16string description, std::u16string unachieved,
                     uint32_t image_id, uint32_t gamerscore,
                     X_FILETIME unlock_time, uint32_t flags)
      : id(id),
        label(label),
        description(description),
        unachieved(unachieved),
        image_id(image_id),
        gamerscore(gamerscore),
        unlock_time(unlock_time),
        flags(flags) {};

  AchievementDetails(const AchievementTableEntry* entry,
                     const SpaInfo* spa_data, const XLanguage language) {
    id = entry->id;
    image_id = entry->image_id;
    gamerscore = entry->gamerscore;
    flags = entry->flags;
    unlock_time = {};

    label =
        xe::to_utf16(spa_data->GetStringTableEntry(language, entry->label_id));
    description = xe::to_utf16(
        spa_data->GetStringTableEntry(language, entry->description_id));
    unachieved = xe::to_utf16(
        spa_data->GetStringTableEntry(language, entry->unachieved_id));
  }
};

struct TitleAchievementsProfileInfo {
  uint32_t achievements_count;
  uint32_t unlocked_achievements_count;
  uint32_t gamerscore;
};

// This is structure used inside GPD file.
// GPD is writeable XDBF.
// There are two info instances
// 1. In Dashboard ID which contains single GPD that contains info about any
// booted game (name, title_id, last boot time etc)
// 2. In specific Title ID directory GPD contains there structure below for
// every achievement. (unlocked or not)
struct Achievement {
  Achievement() {};

  Achievement(const X_XDBF_GPD_ACHIEVEMENT* xdbf_ach) {
    if (!xdbf_ach) {
      return;
    }

    achievement_id = xdbf_ach->id;
    image_id = xdbf_ach->image_id;
    flags = xdbf_ach->flags;
    gamerscore = xdbf_ach->gamerscore;
    unlock_time = static_cast<uint64_t>(xdbf_ach->unlock_time);
  }

  Achievement(const XLanguage language, const SpaInfo xdbf,
              const AchievementTableEntry& xdbf_entry) {
    const std::string label = "";
    xdbf.GetStringTableEntry(language, xdbf_entry.label_id);
    const std::string desc = "";
    xdbf.GetStringTableEntry(language, xdbf_entry.description_id);
    const std::string locked_desc = "";
    xdbf.GetStringTableEntry(language, xdbf_entry.unachieved_id);

    achievement_id = static_cast<xe::be<uint32_t>>(xdbf_entry.id);
    image_id = xdbf_entry.image_id;
    gamerscore = static_cast<xe::be<uint32_t>>(xdbf_entry.gamerscore);
    flags = xdbf_entry.flags;
    unlock_time = {};
    achievement_name =
        xe::load_and_swap<std::u16string>(xe::to_utf16(label).c_str());
    unlocked_description =
        xe::load_and_swap<std::u16string>(xe::to_utf16(desc).c_str());
    locked_description =
        xe::load_and_swap<std::u16string>(xe::to_utf16(locked_desc).c_str());
  }

  uint32_t achievement_id = 0;
  uint32_t image_id = 0;
  uint32_t gamerscore = 0;
  uint32_t flags = 0;
  X_FILETIME unlock_time;
  std::u16string achievement_name;
  std::u16string unlocked_description;
  std::u16string locked_description;

  bool IsUnlocked() const {
    return (flags & static_cast<uint32_t>(AchievementFlags::kAchieved)) ||
           IsUnlockedOnline();
  }

  bool IsUnlockedOnline() const {
    return (flags & static_cast<uint32_t>(AchievementFlags::kAchievedOnline));
  }
};

class AchievementBackendInterface {
 public:
  virtual ~AchievementBackendInterface() {};

  virtual void EarnAchievement(const uint64_t xuid, const uint32_t title_id,
                               const uint32_t achievement_id) = 0;

  virtual bool IsAchievementUnlocked(const uint64_t xuid,
                                     const uint32_t title_id,
                                     const uint32_t achievement_id) const = 0;

  virtual const std::optional<Achievement> GetAchievementInfo(
      const uint64_t xuid, const uint32_t title_id,
      const uint32_t achievement_id) const = 0;
  virtual const std::vector<Achievement> GetTitleAchievements(
      const uint64_t xuid, const uint32_t title_id) const = 0;
  virtual const std::span<const uint8_t> GetAchievementIcon(
      const uint64_t xuid, const uint32_t title_id,
      const uint32_t achievement_id) const = 0;
  virtual bool LoadAchievementsData(const uint64_t xuid) = 0;

 private:
  virtual bool SaveAchievementsData(const uint64_t xuid,
                                    const uint32_t title_id) = 0;
  virtual bool SaveAchievementData(const uint64_t xuid, const uint32_t title_id,
                                   const Achievement* achievement) = 0;
};

class AchievementManager {
 public:
  AchievementManager();

  void LoadTitleAchievements(const uint64_t xuid) const;

  void EarnAchievement(const uint32_t user_index, const uint32_t title_id,
                       const uint32_t achievement_id) const;
  void EarnAchievement(const uint64_t xuid, const uint32_t title_id,
                       const uint32_t achievement_id) const;
  const std::optional<Achievement> GetAchievementInfo(
      const uint64_t xuid, const uint32_t title_id,
      const uint32_t achievement_id) const;
  const std::vector<Achievement> GetTitleAchievements(
      const uint64_t xuid, const uint32_t title_id) const;
  const std::optional<TitleAchievementsProfileInfo> GetTitleAchievementsInfo(
      const uint64_t xuid, const uint32_t title_id) const;
  const std::span<const uint8_t> GetAchievementIcon(
      const uint64_t xuid, const uint32_t title_id,
      const uint32_t achievement_id) const;

 private:
  bool DoesAchievementExist(const uint32_t achievement_id) const;
  void ShowAchievementEarnedNotification(const Achievement* achievement) const;

  // This contains all backends with exception of default storage.
  std::vector<std::unique_ptr<AchievementBackendInterface>>
      achievement_backends_;

  std::unique_ptr<AchievementBackendInterface> default_achievements_backend_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_ACHIEVEMENT_MANAGER_H_

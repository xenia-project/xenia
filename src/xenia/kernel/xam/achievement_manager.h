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
#include "xenia/kernel/util/xdbf_utils.h"
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

struct X_ACHIEVEMENT_UNLOCK_TIME {
  xe::be<uint32_t> high_part;
  xe::be<uint32_t> low_part;

  X_ACHIEVEMENT_UNLOCK_TIME() {
    high_part = 0;
    low_part = 0;
  }

  X_ACHIEVEMENT_UNLOCK_TIME(uint64_t filetime) {
    high_part = static_cast<uint32_t>(filetime >> 32);
    low_part = static_cast<uint32_t>(filetime);
  }

  X_ACHIEVEMENT_UNLOCK_TIME(std::time_t time) {
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
};

struct X_ACHIEVEMENT_DETAILS {
  xe::be<uint32_t> id;
  xe::be<uint32_t> label_ptr;
  xe::be<uint32_t> description_ptr;
  xe::be<uint32_t> unachieved_ptr;
  xe::be<uint32_t> image_id;
  xe::be<uint32_t> gamerscore;
  X_ACHIEVEMENT_UNLOCK_TIME unlock_time;
  xe::be<uint32_t> flags;

  static const size_t kStringBufferSize = 464;
};
static_assert_size(X_ACHIEVEMENT_DETAILS, 36);

// This is structure used inside GPD file.
// GPD is writeable XDBF.
// There are two info instances
// 1. In Dashboard ID which contains single GPD that contains info about any
// booted game (name, title_id, last boot time etc)
// 2. In specific Title ID directory GPD contains there structure below for
// every achievement. (unlocked or not)
struct AchievementGpdStructure {
  AchievementGpdStructure(const XLanguage language,
                          const util::XdbfGameData xdbf,
                          const util::XdbfAchievementTableEntry& xdbf_entry) {
    const std::string label =
        xdbf.GetStringTableEntry(language, xdbf_entry.label_id);
    const std::string desc =
        xdbf.GetStringTableEntry(language, xdbf_entry.description_id);
    const std::string locked_desc =
        xdbf.GetStringTableEntry(language, xdbf_entry.unachieved_id);

    struct_size = 0x1C;
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

  xe::be<uint32_t> struct_size;
  xe::be<uint32_t> achievement_id;
  xe::be<uint32_t> image_id;
  xe::be<uint32_t> gamerscore;
  xe::be<uint32_t> flags;
  X_ACHIEVEMENT_UNLOCK_TIME unlock_time;
  std::u16string achievement_name;
  std::u16string unlocked_description;
  std::u16string locked_description;

  bool IsUnlocked() const {
    return (flags & static_cast<uint32_t>(AchievementFlags::kAchieved)) ||
           flags & static_cast<uint32_t>(AchievementFlags::kAchievedOnline);
  }
};

struct TitleAchievementsProfileInfo {
  uint32_t achievements_count;
  uint32_t unlocked_achievements_count;
  uint32_t gamerscore;
};

class AchievementBackendInterface {
 public:
  virtual ~AchievementBackendInterface() {};

  virtual void EarnAchievement(const uint64_t xuid, const uint32_t title_id,
                               const uint32_t achievement_id) = 0;

  virtual bool IsAchievementUnlocked(const uint64_t xuid,
                                     const uint32_t title_id,
                                     const uint32_t achievement_id) const = 0;

  virtual const AchievementGpdStructure* GetAchievementInfo(
      const uint64_t xuid, const uint32_t title_id,
      const uint32_t achievement_id) const = 0;
  virtual const std::vector<AchievementGpdStructure>* GetTitleAchievements(
      const uint64_t xuid, const uint32_t title_id) const = 0;
  virtual bool LoadAchievementsData(const uint64_t xuid,
                                    const util::XdbfGameData title_data) = 0;

 private:
  virtual bool SaveAchievementsData(const uint64_t xuid,
                                    const uint32_t title_id) = 0;
  virtual bool SaveAchievementData(const uint64_t xuid, const uint32_t title_id,
                                   const uint32_t achievement_id) = 0;
};

class AchievementManager {
 public:
  AchievementManager();

  void LoadTitleAchievements(const uint64_t xuid,
                             const util::XdbfGameData title_id) const;

  void EarnAchievement(const uint32_t user_index, const uint32_t title_id,
                       const uint32_t achievement_id) const;
  void EarnAchievement(const uint64_t xuid, const uint32_t title_id,
                       const uint32_t achievement_id) const;
  const AchievementGpdStructure* GetAchievementInfo(
      const uint64_t xuid, const uint32_t title_id,
      const uint32_t achievement_id) const;
  const std::vector<AchievementGpdStructure>* GetTitleAchievements(
      const uint64_t xuid, const uint32_t title_id) const;
  const std::optional<TitleAchievementsProfileInfo> GetTitleAchievementsInfo(
      const uint64_t xuid, const uint32_t title_id) const;

 private:
  bool DoesAchievementExist(const uint32_t achievement_id) const;
  void ShowAchievementEarnedNotification(
      const AchievementGpdStructure* achievement) const;

  // This contains all backends with exception of default storage.
  std::vector<std::unique_ptr<AchievementBackendInterface>>
      achievement_backends_;

  std::unique_ptr<AchievementBackendInterface> default_achievements_backend_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_ACHIEVEMENT_MANAGER_H_

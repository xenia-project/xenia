/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_USER_TRACKER_H_
#define XENIA_KERNEL_XAM_USER_TRACKER_H_

#include <chrono>
#include <set>
#include <span>

#include "xenia/xbox.h"

#include "xenia/kernel/xam/user_settings.h"

namespace xe {
namespace kernel {
namespace xam {

struct TitleInfo {
  std::u16string title_name;
  uint32_t id;
  uint32_t unlocked_achievements_count;
  uint32_t achievements_count;
  uint32_t title_earned_gamerscore;
  uint32_t gamerscore_amount;
  uint32_t flags;
  uint16_t online_unlocked_achievements;
  X_XDBF_AVATARAWARDS_COUNTER all_avatar_awards;
  X_XDBF_AVATARAWARDS_COUNTER male_avatar_awards;
  X_XDBF_AVATARAWARDS_COUNTER female_avatar_awards;
  std::chrono::local_time<std::chrono::system_clock::duration> last_played;

  std::span<const uint8_t> icon;

  bool WasTitlePlayed() const {
    return last_played.time_since_epoch().count() != 0;
  }
};

class UserTracker {
 public:
  UserTracker();
  ~UserTracker();

  // UserTracker specific methods
  bool AddUser(uint64_t xuid);
  bool RemoveUser(uint64_t xuid);

  // SPA related methods
  void UpdateSpaInfo(SpaInfo* spa_info);

  // User related methods
  bool UnlockAchievement(uint64_t xuid, uint32_t achievement_id);
  void RefreshTitleSummary(uint64_t xuid, uint32_t title_id);

  // Context
  void UpdateContext(uint64_t xuid, uint32_t id, uint32_t value);
  std::optional<uint32_t> GetUserContext(uint64_t xuid, uint32_t id) const;
  std::vector<AttributeKey> GetUserContextIds(uint64_t xuid) const;

  // Property
  void AddProperty(const uint64_t xuid, const Property* property);
  X_STATUS GetProperty(const uint64_t xuid, uint32_t* property_size,
                       XUSER_PROPERTY* property);
  const Property* GetProperty(const uint64_t xuid, const uint32_t id) const;
  std::vector<AttributeKey> GetUserPropertyIds(uint64_t xuid) const;

  // Settings
  void UpsertSetting(uint64_t xuid, uint32_t title_id,
                     const UserSetting* setting);

  bool GetUserSetting(uint64_t xuid, uint32_t title_id, uint32_t setting_id,
                      X_USER_PROFILE_SETTING* setting_ptr,
                      uint32_t& extended_data_address) const;

  // Titles
  void AddTitleToPlayedList();
  std::vector<TitleInfo> GetPlayedTitles(uint64_t xuid) const;
  std::optional<TitleInfo> GetUserTitleInfo(uint64_t xuid,
                                            uint32_t title_id) const;

  // Achievements
  std::vector<Achievement> GetUserTitleAchievements(uint64_t xuid,
                                                    uint32_t title_id) const;
  std::span<const uint8_t> GetAchievementIcon(uint64_t xuid, uint32_t title_id,
                                              uint32_t achievement_id) const;

  // Images
  std::span<const uint8_t> GetIcon(uint64_t xuid, uint32_t title_id,
                                   XTileType tile_type, uint64_t tile_id) const;

 private:
  bool IsUserTracked(uint64_t xuid) const;

  void UpdateSettingValue(uint64_t xuid, uint32_t title_id,
                          UserSettingId setting_id, int32_t difference);

  std::optional<UserSetting> GetSetting(UserProfile* user, uint32_t title_id,
                                        uint32_t setting_id) const;
  std::optional<UserSetting> GetGpdSetting(UserProfile* user, uint32_t title_id,
                                           uint32_t setting_id) const;

  void AddTitleToPlayedList(uint64_t xuid);
  void UpdateTitleGpdFile();
  void UpdateProfileGpd();
  void UpdateMissingAchievemntsIcons();

  void FlushUserData(const uint64_t xuid);

  SpaInfo* spa_data_;

  std::set<uint64_t> tracked_xuids_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
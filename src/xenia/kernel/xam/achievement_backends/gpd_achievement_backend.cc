/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/achievement_backends/gpd_achievement_backend.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"

DECLARE_int32(user_language);

namespace xe {
namespace kernel {
namespace xam {

GpdAchievementBackend::GpdAchievementBackend() {}
GpdAchievementBackend::~GpdAchievementBackend() {}

void GpdAchievementBackend::EarnAchievement(const uint64_t xuid,
                                            const uint32_t title_id,
                                            const uint32_t achievement_id) {
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return;
  }

  auto achievement = GetAchievementInfoInternal(xuid, title_id, achievement_id);
  if (!achievement) {
    return;
  }

  XELOGI("Player: {} Unlocked Achievement: {}", user->name(),
         xe::to_utf8(xe::load_and_swap<std::u16string>(
             achievement->achievement_name.c_str())));

  const uint64_t unlock_time = Clock::QueryHostSystemTime();
  // We're adding achieved online flag because on console locally achieved
  // entries don't have valid unlock time.
  achievement->flags = achievement->flags |
                       static_cast<uint32_t>(AchievementFlags::kAchieved) |
                       static_cast<uint32_t>(AchievementFlags::kAchievedOnline);
  achievement->unlock_time = unlock_time;

  SaveAchievementData(xuid, title_id, achievement_id);
}

AchievementGpdStructure* GpdAchievementBackend::GetAchievementInfoInternal(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return nullptr;
  }

  return user->GetAchievement(title_id, achievement_id);
}

const AchievementGpdStructure* GpdAchievementBackend::GetAchievementInfo(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  return GetAchievementInfoInternal(xuid, title_id, achievement_id);
}

bool GpdAchievementBackend::IsAchievementUnlocked(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  const auto achievement =
      GetAchievementInfoInternal(xuid, title_id, achievement_id);

  if (!achievement) {
    return false;
  }

  return (achievement->flags &
          static_cast<uint32_t>(AchievementFlags::kAchieved)) != 0;
}

const std::vector<AchievementGpdStructure>*
GpdAchievementBackend::GetTitleAchievements(const uint64_t xuid,
                                            const uint32_t title_id) const {
  const auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return {};
  }

  return user->GetTitleAchievements(title_id);
}

bool GpdAchievementBackend::LoadAchievementsData(
    const uint64_t xuid, const util::XdbfGameData title_data) {
  auto user = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user) {
    return false;
  }

  // Question. Should loading for GPD for profile be directly done by profile or
  // here?
  if (!title_data.is_valid()) {
    return false;
  }

  const auto achievements = title_data.GetAchievements();
  if (achievements.empty()) {
    return true;
  }

  const auto title_id = title_data.GetTitleInformation().title_id;

  const XLanguage title_language = title_data.GetExistingLanguage(
      static_cast<XLanguage>(cvars::user_language));
  for (const auto& achievement : achievements) {
    AchievementGpdStructure achievementData(title_language, title_data,
                                            achievement);
    user->achievements_[title_id].push_back(achievementData);
  }

  // TODO(Gliniak): Here should be loader of GPD file for loaded title. That way
  // we can load flags and unlock_time from specific user.
  return true;
}

bool GpdAchievementBackend::SaveAchievementData(const uint64_t xuid,
                                                const uint32_t title_id,
                                                const uint32_t achievement_id) {
  return true;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

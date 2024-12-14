/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/achievement_manager.h"
#include "xenia/emulator.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/achievement_backends/gpd_achievement_backend.h"
#include "xenia/ui/imgui_guest_notification.h"

DEFINE_bool(show_achievement_notification, false,
            "Show achievement notification on screen.", "UI");

DEFINE_string(
    default_achievements_backend, "GPD",
    "Defines which achievements backend should be used as an default. "
    "Possible options: GPD.",
    "Kernel");

DECLARE_int32(user_language);

namespace xe {
namespace kernel {
namespace xam {

AchievementManager::AchievementManager() {
  default_achievements_backend_ = std::make_unique<GpdAchievementBackend>();

  // Add any optional backend here.
};
void AchievementManager::EarnAchievement(const uint32_t user_index,
                                         const uint32_t title_id,
                                         const uint32_t achievement_id) const {
  const auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return;
  }

  EarnAchievement(user->xuid(), title_id, achievement_id);
};

void AchievementManager::EarnAchievement(const uint64_t xuid,
                                         const uint32_t title_id,
                                         const uint32_t achievement_id) const {
  if (!DoesAchievementExist(achievement_id)) {
    XELOGW(
        "{}: Achievement with ID: {} for title: {:08X} doesn't exist in "
        "database!",
        __func__, achievement_id, title_id);
    return;
  }
  // Always send request to unlock in 3PP backends. It's up to them to check if
  // achievement was unlocked
  for (auto& backend : achievement_backends_) {
    backend->EarnAchievement(xuid, title_id, achievement_id);
  }

  if (default_achievements_backend_->IsAchievementUnlocked(xuid, title_id,
                                                           achievement_id)) {
    return;
  }

  default_achievements_backend_->EarnAchievement(xuid, title_id,
                                                 achievement_id);

  if (!cvars::show_achievement_notification) {
    return;
  }

  const auto achievement = default_achievements_backend_->GetAchievementInfo(
      xuid, title_id, achievement_id);

  if (!achievement) {
    // Something went really wrong!
    return;
  }
  ShowAchievementEarnedNotification(achievement);
}

void AchievementManager::LoadTitleAchievements(
    const uint64_t xuid, const util::XdbfGameData title_data) const {
  default_achievements_backend_->LoadAchievementsData(xuid, title_data);
}

const AchievementGpdStructure* AchievementManager::GetAchievementInfo(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  return default_achievements_backend_->GetAchievementInfo(xuid, title_id,
                                                           achievement_id);
}

const std::vector<AchievementGpdStructure>*
AchievementManager::GetTitleAchievements(const uint64_t xuid,
                                         const uint32_t title_id) const {
  return default_achievements_backend_->GetTitleAchievements(xuid, title_id);
}

const std::optional<TitleAchievementsProfileInfo>
AchievementManager::GetTitleAchievementsInfo(const uint64_t xuid,
                                             const uint32_t title_id) const {
  TitleAchievementsProfileInfo info = {};

  const auto achievements = GetTitleAchievements(xuid, title_id);

  if (!achievements) {
    return std::nullopt;
  }

  info.achievements_count = static_cast<uint32_t>(achievements->size());

  for (const auto& entry : *achievements) {
    if (!entry.IsUnlocked()) {
      continue;
    }

    info.unlocked_achievements_count++;
    info.gamerscore += entry.gamerscore;
  }

  return info;
}

bool AchievementManager::DoesAchievementExist(
    const uint32_t achievement_id) const {
  const util::XdbfGameData title_xdbf = kernel_state()->title_xdbf();
  const util::XdbfAchievementTableEntry achievement =
      title_xdbf.GetAchievement(achievement_id);

  if (!achievement.id) {
    return false;
  }
  return true;
}

void AchievementManager::ShowAchievementEarnedNotification(
    const AchievementGpdStructure* achievement) const {
  const std::string description =
      fmt::format("{}G - {}", achievement->gamerscore.get(),
                  xe::to_utf8(xe::load_and_swap<std::u16string>(
                      achievement->achievement_name.c_str())));

  const Emulator* emulator = kernel_state()->emulator();
  ui::WindowedAppContext& app_context =
      emulator->display_window()->app_context();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  app_context.CallInUIThread([imgui_drawer, description]() {
    new xe::ui::AchievementNotificationWindow(
        imgui_drawer, "Achievement unlocked", description, 0,
        kernel_state()->notification_position_);
  });
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

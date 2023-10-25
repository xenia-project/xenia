/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "achievement_manager.h"
#include "xenia/emulator.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/ui/imgui_notification.h"

DEFINE_bool(show_achievement_notification, false,
            "Show achievement notification on screen.", "UI");

DECLARE_int32(user_language);

namespace xe {
namespace kernel {

AchievementManager::AchievementManager() { unlocked_achievements.clear(); };

void AchievementManager::EarnAchievement(uint64_t xuid, uint32_t title_id,
                                         uint32_t achievement_id) {
  if (IsAchievementUnlocked(achievement_id)) {
    return;
  }

  const Emulator* emulator = kernel_state()->emulator();
  ui::WindowedAppContext& app_context =
      kernel_state()->emulator()->display_window()->app_context();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  const util::XdbfGameData title_xdbf = kernel_state()->title_xdbf();
  const std::vector<util::XdbfAchievementTableEntry> achievements =
      title_xdbf.GetAchievements();
  const XLanguage title_language = title_xdbf.GetExistingLanguage(
      static_cast<XLanguage>(cvars::user_language));

  for (const util::XdbfAchievementTableEntry& entry : achievements) {
    if (entry.id == achievement_id) {
      const std::string label =
          title_xdbf.GetStringTableEntry(title_language, entry.label_id);
      const std::string desc =
          title_xdbf.GetStringTableEntry(title_language, entry.description_id);

      XELOGI("Achievement unlocked: {}", label);
      const std::string description =
          fmt::format("{}G - {}", entry.gamerscore, label);

      unlocked_achievements[achievement_id] = Clock::QueryHostSystemTime();
      // Even if we disable popup we still should store info that this
      // achievement was earned.
      if (!cvars::show_achievement_notification) {
        continue;
      }

      app_context.CallInUIThread([imgui_drawer, description]() {
        new xe::ui::AchievementNotificationWindow(
            imgui_drawer, "Achievement unlocked", description, 0,
            kernel_state()->notification_position_);
      });
    }
  }
}

bool AchievementManager::IsAchievementUnlocked(uint32_t achievement_id) {
  auto itr = unlocked_achievements.find(achievement_id);

  return itr != unlocked_achievements.cend();
}

uint64_t AchievementManager::GetAchievementUnlockTime(uint32_t achievement_id) {
  auto itr = unlocked_achievements.find(achievement_id);
  if (itr == unlocked_achievements.cend()) {
    return 0;
  }

  return itr->second;
}

}  // namespace kernel
}  // namespace xe

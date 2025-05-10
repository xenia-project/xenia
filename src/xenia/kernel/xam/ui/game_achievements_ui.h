/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_GAME_ACHIEVEMENTS_UI_H_
#define XENIA_KERNEL_XAM_UI_GAME_ACHIEVEMENTS_UI_H_

#include "xenia/kernel/xam/xam_ui.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

class GameAchievementsUI final : public XamDialog {
 public:
  GameAchievementsUI(xe::ui::ImGuiDrawer* imgui_drawer,
                     const ImVec2 drawing_position, const TitleInfo* title_info,
                     const UserProfile* profile);

 private:
  ~GameAchievementsUI();
  bool LoadAchievementsData();

  std::string GetAchievementTitle(const Achievement& achievement_entry) const;

  std::string GetAchievementDescription(
      const Achievement& achievement_entry) const;

  xe::ui::ImmediateTexture* GetIcon(const Achievement& achievement_entry) const;

  std::string GetUnlockedTime(const Achievement& achievement_entry) const;

  void DrawTitleAchievementInfo(ImGuiIO& io,
                                const Achievement& achievement_entry) const;

  void OnDraw(ImGuiIO& io) override;

 private:
  bool show_locked_info_ = false;

  uint64_t window_id_;
  const ImVec2 drawing_position_ = {};

  const TitleInfo title_info_;
  const UserProfile* profile_;

  std::vector<Achievement> achievements_info_;
  std::map<uint32_t, std::unique_ptr<xe::ui::ImmediateTexture>>
      achievements_icons_;
};

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif

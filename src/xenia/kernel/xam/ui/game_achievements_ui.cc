/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/game_achievements_ui.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

GameAchievementsUI::GameAchievementsUI(xe::ui::ImGuiDrawer* imgui_drawer,
                                       const ImVec2 drawing_position,
                                       const TitleInfo* title_info,
                                       const UserProfile* profile)
    : XamDialog(imgui_drawer),
      drawing_position_(drawing_position),
      title_info_(*title_info),
      profile_(profile),
      window_id_(GetWindowId()) {
  LoadAchievementsData();
}

GameAchievementsUI::~GameAchievementsUI() {
  for (auto& entry : achievements_icons_) {
    entry.second.release();
  }
}

bool GameAchievementsUI::LoadAchievementsData() {
  achievements_info_ =
      kernel_state()->xam_state()->achievement_manager()->GetTitleAchievements(
          profile_->xuid(), title_info_.id);

  if (achievements_info_.empty()) {
    return false;
  }

  xe::ui::IconsData data;

  for (const Achievement& entry : achievements_info_) {
    const auto icon =
        kernel_state()->xam_state()->achievement_manager()->GetAchievementIcon(
            profile_->xuid(), title_info_.id, entry.achievement_id);

    data.insert({entry.image_id, icon});
  }

  achievements_icons_ = imgui_drawer()->LoadIcons(data);
  return true;
}

std::string GameAchievementsUI::GetAchievementTitle(
    const Achievement& achievement_entry) const {
  std::string title = "Secret trophy";

  if (achievement_entry.IsUnlocked() || show_locked_info_ ||
      achievement_entry.flags &
          static_cast<uint32_t>(AchievementFlags::kShowUnachieved)) {
    title = xe::to_utf8(achievement_entry.achievement_name);
  }

  return title;
}

std::string GameAchievementsUI::GetAchievementDescription(
    const Achievement& achievement_entry) const {
  std::string description = "Hidden description";

  if (achievement_entry.flags &
      static_cast<uint32_t>(AchievementFlags::kShowUnachieved)) {
    description = xe::to_utf8(achievement_entry.locked_description);
  }

  if (achievement_entry.IsUnlocked() || show_locked_info_) {
    description = xe::to_utf8(achievement_entry.unlocked_description);
  }

  return description;
}

xe::ui::ImmediateTexture* GameAchievementsUI::GetIcon(
    const Achievement& achievement_entry) const {
  if (!achievement_entry.IsUnlocked() && !show_locked_info_) {
    return imgui_drawer()->GetLockedAchievementIcon();
  }

  if (achievements_icons_.count(achievement_entry.image_id)) {
    return achievements_icons_.at(achievement_entry.image_id).get();
  }

  if (achievement_entry.IsUnlocked()) {
    return nullptr;
  }
  return imgui_drawer()->GetLockedAchievementIcon();
}

std::string GameAchievementsUI::GetUnlockedTime(
    const Achievement& achievement_entry) const {
  if (achievement_entry.IsUnlockedOnline()) {
    const auto unlock_time = chrono::WinSystemClock::to_local(
        achievement_entry.unlock_time.to_time_point());

    return fmt::format(
        "Unlocked: {:%Y-%m-%d %H:%M}",
        std::chrono::system_clock::time_point(unlock_time.time_since_epoch()));
  }

  if (achievement_entry.unlock_time.is_valid()) {
    const auto unlock_time = chrono::WinSystemClock::to_local(
        achievement_entry.unlock_time.to_time_point());

    return fmt::format(
        "Unlocked: Offline ({:%Y-%m-%d %H:%M})",
        std::chrono::system_clock::time_point(unlock_time.time_since_epoch()));
  }
  return fmt::format("Unlocked: Offline");
}

void GameAchievementsUI::DrawTitleAchievementInfo(
    ImGuiIO& io, const Achievement& achievement_entry) const {
  const auto start_drawing_pos = ImGui::GetCursorPos();

  ImGui::TableSetColumnIndex(0);

  const auto icon = GetIcon(achievement_entry);
  if (icon) {
    ImGui::Image(reinterpret_cast<ImTextureID>(GetIcon(achievement_entry)),
                 xe::ui::default_image_icon_size);
  } else {
    ImGui::Dummy(xe::ui::default_image_icon_size);
  }
  ImGui::TableNextColumn();

  ImGui::PushFont(imgui_drawer()->GetTitleFont());
  const auto primary_line_height = ImGui::GetTextLineHeight();
  ImGui::Text("%s", GetAchievementTitle(achievement_entry).c_str());
  ImGui::PopFont();

  ImGui::PushTextWrapPos(ImGui::GetMainViewport()->Size.x * 0.5f);
  ImGui::TextWrapped("%s",
                     GetAchievementDescription(achievement_entry).c_str());
  ImGui::PopTextWrapPos();

  ImGui::SetCursorPosY(start_drawing_pos.y + xe::ui::default_image_icon_size.x -
                       ImGui::GetTextLineHeight());

  if (achievement_entry.IsUnlocked()) {
    ImGui::Text("%s", GetUnlockedTime(achievement_entry).c_str());
  }

  ImGui::TableNextColumn();

  // TODO(Gliniak): There is no easy way to align text to middle, so I have to
  // do it manually.
  const float achievement_row_middle_alignment =
      ((xe::ui::default_image_icon_size.x / 2.f) -
       ImGui::GetTextLineHeight() / 2.f) *
      0.85f;

  ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                       achievement_row_middle_alignment);
  ImGui::PushFont(imgui_drawer()->GetTitleFont());
  ImGui::TextUnformatted(
      fmt::format("{} G", achievement_entry.gamerscore).c_str());
  ImGui::PopFont();
}

void GameAchievementsUI::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowPos(drawing_position_, ImGuiCond_FirstUseEver);

  const auto xenia_window_size = ImGui::GetMainViewport()->Size;

  ImGui::SetNextWindowSizeConstraints(
      ImVec2(xenia_window_size.x * 0.2f, xenia_window_size.y * 0.3f),
      ImVec2(xenia_window_size.x * 0.6f, xenia_window_size.y * 0.8f));
  ImGui::SetNextWindowBgAlpha(0.8f);

  bool dialog_open = true;

  std::string title_name = xe::to_utf8(title_info_.title_name);
  title_name.erase(std::remove(title_name.begin(), title_name.end(), '\0'),
                   title_name.end());

  const std::string window_name =
      fmt::format("{} Achievements###{}", title_name, window_id_);
  if (!ImGui::Begin(window_name.c_str(), &dialog_open,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    Close();
    ImGui::End();
    return;
  }

  ImGui::Checkbox("Show locked achievements information", &show_locked_info_);
  ImGui::Separator();

  if (achievements_info_.empty()) {
    ImGui::TextUnformatted(fmt::format("No achievements data!").c_str());
  } else {
    if (ImGui::BeginTable("", 3, ImGuiTableFlags_BordersInnerH)) {
      for (const auto& entry : achievements_info_) {
        ImGui::TableNextRow(0, xe::ui::default_image_icon_size.y);
        DrawTitleAchievementInfo(io, entry);
      }

      ImGui::EndTable();
    }
  }

  if (!dialog_open) {
    Close();
    ImGui::End();
    return;
  }

  ImGui::End();
};

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

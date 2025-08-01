/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/title_info_ui.h"
#include "xenia/kernel/xam/ui/game_achievements_ui.h"

#include "xenia/base/system.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

TitleListUI::TitleListUI(xe::ui::ImGuiDrawer* imgui_drawer,
                         const ImVec2 drawing_position,
                         const UserProfile* profile)
    : XamDialog(imgui_drawer),
      drawing_position_(drawing_position),
      profile_(profile),
      profile_manager_(kernel_state()->xam_state()->profile_manager()),
      dialog_name_(
          fmt::format("{}'s Games List###{}", profile->name(), GetWindowId())) {
  LoadProfileTitleList(imgui_drawer, profile);
}

TitleListUI::~TitleListUI() {
  for (auto& entry : title_icon) {
    entry.second.release();
  }
}

void TitleListUI::LoadProfileTitleList(xe::ui::ImGuiDrawer* imgui_drawer,
                                       const UserProfile* profile) {
  info_.clear();

  xe::ui::IconsData data;

  info_ = kernel_state()->xam_state()->user_tracker()->GetPlayedTitles(
      profile->xuid());
  for (const auto& title_info : info_) {
    if (!title_info.icon.empty()) {
      data[title_info.id] = title_info.icon;
    }
  }

  title_icon = imgui_drawer->LoadIcons(data);
}

void TitleListUI::DrawTitleEntry(ImGuiIO& io, TitleInfo& entry) {
  const auto start_position = ImGui::GetCursorPos();
  const ImVec2 next_window_position =
      ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + 20.f,
             ImGui::GetWindowPos().y);

  // First Column
  ImGui::TableSetColumnIndex(0);

  if (title_icon.count(entry.id)) {
    ImGui::Image(reinterpret_cast<ImTextureID>(title_icon.at(entry.id).get()),
                 xe::ui::default_image_icon_size);
  } else {
    ImGui::Dummy(xe::ui::default_image_icon_size);
  }

  // Second Column
  ImGui::TableNextColumn();
  ImGui::PushFont(imgui_drawer()->GetTitleFont());
  ImGui::TextUnformatted(xe::to_utf8(entry.title_name).c_str());
  ImGui::PopFont();

  ImGui::TextUnformatted(
      fmt::format("{}/{} Achievements unlocked ({} Gamerscore)",
                  entry.unlocked_achievements_count, entry.achievements_count,
                  entry.title_earned_gamerscore)
          .c_str());

  ImGui::SetCursorPosY(start_position.y + xe::ui::default_image_icon_size.y -
                       ImGui::GetTextLineHeight());

  if (entry.WasTitlePlayed()) {
    ImGui::TextUnformatted(
        fmt::format("Last played: {:%Y-%m-%d %H:%M}",
                    std::chrono::system_clock::time_point(
                        entry.last_played.time_since_epoch()))
            .c_str());
  } else {
    ImGui::TextUnformatted("Last played: Unknown");
  }
  ImGui::TableNextColumn();

  const ImVec2 end_draw_position =
      ImVec2(ImGui::GetCursorPos().x - start_position.x,
             ImGui::GetCursorPos().y - start_position.y);

  ImGui::SetCursorPos(start_position);

  if (ImGui::Selectable(fmt::format("##{:08X}Selectable", entry.id).c_str(),
                        selected_title_ == entry.id,
                        ImGuiSelectableFlags_SpanAllColumns,
                        end_draw_position)) {
    selected_title_ = entry.id;
    new GameAchievementsUI(imgui_drawer(), next_window_position, &entry,
                           profile_);
  }

  if (ImGui::BeginPopupContextItem(
          fmt::format("Title Menu {:08X}", entry.id).c_str())) {
    selected_title_ = entry.id;

    const auto savefile_path = profile_manager_->GetProfileContentPath(
        profile_->xuid(), entry.id, XContentType::kSavedGame);

    const auto dlc_path = profile_manager_->GetProfileContentPath(
        0, entry.id, XContentType::kMarketplaceContent);

    const auto tu_path = profile_manager_->GetProfileContentPath(
        0, entry.id, XContentType::kInstaller);

    if (ImGui::MenuItem("Open savefile directory", nullptr, nullptr,
                        std::filesystem::exists(savefile_path))) {
      std::thread path_open(LaunchFileExplorer, savefile_path);
      path_open.detach();
    }
    if (ImGui::MenuItem("Open DLC directory", nullptr, nullptr,
                        std::filesystem::exists(dlc_path))) {
      std::thread path_open(LaunchFileExplorer, dlc_path);
      path_open.detach();
    }
    if (ImGui::MenuItem("Open Title Update directory", nullptr, nullptr,
                        std::filesystem::exists(tu_path))) {
      std::thread path_open(LaunchFileExplorer, tu_path);
      path_open.detach();
    }

    ImGui::Separator();

    const auto title_info =
        kernel_state()->xam_state()->user_tracker()->GetUserTitleInfo(
            profile_->xuid(), entry.id);

    if (ImGui::MenuItem("Refresh title stats", nullptr, nullptr, true)) {
      kernel_state()->xam_state()->user_tracker()->RefreshTitleSummary(
          profile_->xuid(), entry.id);

      if (title_info) {
        entry = title_info.value();
      }
    }

    if (title_info) {
      if (ImGui::MenuItem("Delete title", nullptr, nullptr,
                          !title_info->unlocked_achievements_count)) {
        kernel_state()->xam_state()->user_tracker()->RemoveTitleFromPlayedList(
            profile_->xuid(), entry.id);
      }
    }

    ImGui::EndPopup();
  }
}

void TitleListUI::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowPos(drawing_position_, ImGuiCond_FirstUseEver);
  const auto xenia_window_size = ImGui::GetMainViewport()->Size;

  ImGui::SetNextWindowSizeConstraints(
      ImVec2(xenia_window_size.x * 0.05f, xenia_window_size.y * 0.05f),
      ImVec2(xenia_window_size.x * 0.4f, xenia_window_size.y * 0.5f));
  ImGui::SetNextWindowBgAlpha(0.8f);

  bool dialog_open = true;
  if (!ImGui::Begin(dialog_name_.c_str(), &dialog_open,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    Close();
    ImGui::End();
    return;
  }

  if (!info_.empty()) {
    if (info_.size() > 10) {
      ImGui::Text("Search: ");
      ImGui::SameLine();
      ImGui::InputText("##Search", title_name_filter_, title_name_filter_size);
      ImGui::Separator();
    }

    if (ImGui::BeginTable("", 2, ImGuiTableFlags_BordersInnerH)) {
      ImGui::TableNextRow(0, xe::ui::default_image_icon_size.y);
      for (auto& entry : info_) {
        std::string filter(title_name_filter_);
        if (!filter.empty()) {
          bool contains_filter =
              utf8::lower_ascii(xe::to_utf8(entry.title_name))
                  .find(utf8::lower_ascii(filter)) != std::string::npos;

          if (!contains_filter) {
            continue;
          }
        }
        DrawTitleEntry(io, entry);
      }
      ImGui::EndTable();
    }
  } else {
    // Align text to the center
    std::string no_entries_message = "There are no titles, so far.";

    ImGui::PushFont(imgui_drawer()->GetTitleFont());
    float windowWidth = ImGui::GetContentRegionAvail().x;
    ImVec2 textSize = ImGui::CalcTextSize(no_entries_message.c_str());
    float textOffsetX = (windowWidth - textSize.x) * 0.5f;
    if (textOffsetX > 0.0f) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textOffsetX);
    }

    ImGui::Text("%s", no_entries_message.c_str());
    ImGui::PopFont();
  }

  if (!dialog_open) {
    Close();
    ImGui::End();
    return;
  }

  ImGui::End();
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/profile_dialogs.h"
#include <algorithm>
#include "xenia/app/emulator_window.h"
#include "xenia/base/system.h"

namespace xe {
namespace app {

void CreateProfileDialog::OnDraw(ImGuiIO& io) {
  auto profile_manager = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager();

  const auto window_position =
      ImVec2(GetIO().DisplaySize.x * 0.40f, GetIO().DisplaySize.y * 0.44f);

  ImGui::SetNextWindowPos(window_position, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(1.0f);

  bool dialog_open = true;
  if (!ImGui::Begin("Create Profile", &dialog_open,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    ImGui::End();
    emulator_window_->profile_creation_dialog_.reset();
    return;
  }

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)) {
    ImGui::SetKeyboardFocusHere(0);
  }

  ImGui::TextUnformatted("Gamertag:");
  ImGui::InputText("##Gamertag", gamertag, sizeof(gamertag));

  const std::string gamertag_string = std::string(gamertag);

  if (profile_manager->IsGamertagValid(gamertag_string)) {
    if (ImGui::Button("Create")) {
      if (profile_manager->CreateProfile(gamertag_string, migration) &&
          migration) {
        emulator_window_->emulator()->DataMigration(0xB13EBABEBABEBABE);
      }
      std::fill(std::begin(gamertag), std::end(gamertag), '\0');
      dialog_open = false;
    }
    ImGui::SameLine();
  }

  if (ImGui::Button("Cancel")) {
    std::fill(std::begin(gamertag), std::end(gamertag), '\0');
    dialog_open = false;
  }

  if (!dialog_open) {
    ImGui::End();
    emulator_window_->profile_creation_dialog_.reset();
    return;
  }
  ImGui::End();
}

void NoProfileDialog::OnDraw(ImGuiIO& io) {
  auto profile_manager = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager();

  if (profile_manager->GetProfilesCount()) {
    delete this;
    return;
  }

  const auto window_position =
      ImVec2(GetIO().DisplaySize.x * 0.35f, GetIO().DisplaySize.y * 0.4f);

  ImGui::SetNextWindowPos(window_position, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(1.0f);

  bool dialog_open = true;
  if (!ImGui::Begin("No Profiles Found", &dialog_open,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    ImGui::End();
    delete this;
    return;
  }

  const std::string message =
      "There is no profile available! You will not be able to save without "
      "one.\n\nWould you like to create one?";

  ImGui::TextUnformatted(message.c_str());

  ImGui::Separator();
  ImGui::NewLine();

  const auto content_files = xe::filesystem::ListDirectories(
      emulator_window_->emulator()->content_root());

  if (content_files.empty()) {
    if (ImGui::Button("Create Profile") &&
        !emulator_window_->profile_creation_dialog_) {
      emulator_window_->profile_creation_dialog_ =
          std::make_unique<CreateProfileDialog>(
              emulator_window_->imgui_drawer(), emulator_window_);
    }
  } else {
    if (ImGui::Button("Create profile & migrate data") &&
        !emulator_window_->profile_creation_dialog_) {
      emulator_window_->profile_creation_dialog_ =
          std::make_unique<CreateProfileDialog>(
              emulator_window_->imgui_drawer(), emulator_window_, true);
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Open profile menu")) {
    emulator_window_->ToggleProfilesConfigDialog();
  }

  ImGui::SameLine();
  if (ImGui::Button("Close") || !dialog_open) {
    emulator_window_->SetHotkeysState(true);
    ImGui::End();
    delete this;
    return;
  }
  ImGui::End();
}

void ProfileConfigDialog::OnDraw(ImGuiIO& io) {
  if (!emulator_window_->emulator() ||
      !emulator_window_->emulator()->kernel_state() ||
      !emulator_window_->emulator()->kernel_state()->xam_state()) {
    return;
  }

  auto profile_manager = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager();
  if (!profile_manager) {
    return;
  }

  auto profiles = profile_manager->GetProfiles();

  ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.8f);

  bool dialog_open = true;
  if (!ImGui::Begin("Profiles Menu", &dialog_open,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    ImGui::End();
    return;
  }

  if (profiles->empty()) {
    ImGui::TextUnformatted("No profiles found!");
    ImGui::Spacing();
    ImGui::Separator();
  }

  for (auto& [xuid, account] : *profiles) {
    ImGui::PushID(static_cast<int>(xuid));

    const uint8_t user_index =
        profile_manager->GetUserIndexAssignedToProfile(xuid);

    if (!DrawProfileContent(xuid, user_index, &account)) {
      ImGui::PopID();
      ImGui::End();
      return;
    }

    ImGui::PopID();
    ImGui::Spacing();
    ImGui::Separator();
  }

  ImGui::Spacing();

  if (ImGui::Button("Create Profile") &&
      !emulator_window_->profile_creation_dialog_) {
    emulator_window_->profile_creation_dialog_ =
        std::make_unique<CreateProfileDialog>(emulator_window_->imgui_drawer(),
                                              emulator_window_);
  }

  ImGui::End();

  if (!dialog_open) {
    emulator_window_->ToggleProfilesConfigDialog();
    return;
  }
}

bool ProfileConfigDialog::DrawProfileContent(const uint64_t xuid,
                                             const uint8_t user_index,
                                             const X_XAMACCOUNTINFO* account) {
  auto profile_manager = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager();

  const float default_image_size = 75.0f;
  auto position = ImGui::GetCursorPos();
  const float selectable_height =
      ImGui::GetTextLineHeight() *
      5;  // 3 is for amount of lines of text behind image/object.
  const auto font = emulator_window_->imgui_drawer()->GetIO().Fonts->Fonts[0];

  const auto text_size = font->CalcTextSizeA(
      font->FontSize, FLT_MAX, -1.0f,
      fmt::format("XUID: {:016X}\n", 0xB13EBABEBABEBABE).c_str());

  const auto image_scale = selectable_height / default_image_size;
  const auto image_size = ImVec2(default_image_size * image_scale,
                                 default_image_size * image_scale);
  // This includes 10% to include empty spaces between border and elements.
  auto selectable_region_size =
      ImVec2((image_size.x + text_size.x) * 1.10f, selectable_height);

  if (ImGui::Selectable("##Selectable", selected_xuid_ == xuid,
                        ImGuiSelectableFlags_SpanAllColumns,
                        selectable_region_size)) {
    selected_xuid_ = xuid;
  }

  if (ImGui::BeginPopupContextItem("Profile Menu")) {
    if (user_index == static_cast<uint8_t>(-1)) {
      if (ImGui::MenuItem("Login")) {
        profile_manager->Login(xuid);
      }

      if (ImGui::BeginMenu("Login to slot:")) {
        for (uint8_t i = 0; i < XUserMaxUserCount; i++) {
          if (ImGui::MenuItem(fmt::format("slot {}", i).c_str())) {
            profile_manager->Login(xuid, i);
          }
        }
        ImGui::EndMenu();
      }
    } else {
      if (ImGui::MenuItem("Logout")) {
        profile_manager->Logout(user_index);
      }
    }

    ImGui::MenuItem("Modify (unsupported)");
    ImGui::MenuItem("Show Achievements (unsupported)");

    if (ImGui::MenuItem("Show Content Directory")) {
      const auto path = profile_manager->GetProfileContentPath(
          xuid, emulator_window_->emulator()->kernel_state()->title_id());

      if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
      }

      std::thread path_open(LaunchFileExplorer, path);
      path_open.detach();
    }

    if (!emulator_window_->emulator()->is_title_open()) {
      ImGui::Separator();
      if (ImGui::BeginMenu("Delete Profile")) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            fmt::format("You're about to delete profile: {} (XUID: {:016X}). "
                        "This will remove all data assigned to this profile "
                        "including savefiles. Are you sure?",
                        account->GetGamertagString(), xuid)
                .c_str());
        ImGui::EndTooltip();

        if (ImGui::MenuItem("Yes, delete it!")) {
          profile_manager->DeleteProfile(xuid);
          ImGui::EndMenu();
          ImGui::EndPopup();
          return false;
        }

        ImGui::EndMenu();
      }
    }
    ImGui::EndPopup();
  }

  ImGui::SameLine();
  ImGui::SetCursorPos(position);

  // In the future it can be replaced with profile icon.
  ImGui::Image(user_index < XUserMaxUserCount
                   ? imgui_drawer()->GetNotificationIcon(user_index)
                   : nullptr,
               ImVec2(default_image_size * image_scale,
                      default_image_size * image_scale));

  ImGui::SameLine();
  position = ImGui::GetCursorPos();
  ImGui::TextUnformatted(
      fmt::format("User: {}\n", account->GetGamertagString()).c_str());

  ImGui::SameLine();
  ImGui::SetCursorPos(position);
  ImGui::SetCursorPosY(position.y + ImGui::GetTextLineHeight());
  ImGui::TextUnformatted(fmt::format("XUID: {:016X}\n", xuid).c_str());

  if (user_index != static_cast<uint8_t>(-1)) {
    ImGui::SameLine();
    ImGui::SetCursorPos(position);
    ImGui::SetCursorPosY(position.y + 2 * ImGui::GetTextLineHeight());
    ImGui::TextUnformatted(
        fmt::format("Assigned to slot: {}\n", user_index).c_str());
  }
  return true;
}

}  // namespace app
}  // namespace xe
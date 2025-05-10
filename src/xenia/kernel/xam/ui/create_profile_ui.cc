/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/create_profile_ui.h"
#include "xenia/emulator.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

void CreateProfileUI::OnDraw(ImGuiIO& io) {
  if (!has_opened_) {
    ImGui::OpenPopup("Create Profile");
    has_opened_ = true;
  }

  auto profile_manager =
      emulator_->kernel_state()->xam_state()->profile_manager();

  bool dialog_open = true;
  if (!ImGui::BeginPopupModal("Create Profile", &dialog_open,
                              ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_HorizontalScrollbar)) {
    Close();
    return;
  }

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)) {
    ImGui::SetKeyboardFocusHere(0);
  }

  ImGui::TextUnformatted("Gamertag:");
  ImGui::InputText("##Gamertag", gamertag_, sizeof(gamertag_));

  const std::string gamertag_string = std::string(gamertag_);
  bool valid = profile_manager->IsGamertagValid(gamertag_string);

  ImGui::BeginDisabled(!valid);
  if (ImGui::Button("Create")) {
    bool autologin = (profile_manager->GetAccountCount() == 0);
    if (profile_manager->CreateProfile(gamertag_string, autologin,
                                       migration_) &&
        migration_) {
      emulator_->DataMigration(0xB13EBABEBABEBABE);
    }
    std::fill(std::begin(gamertag_), std::end(gamertag_), '\0');
    dialog_open = false;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();

  if (ImGui::Button("Cancel")) {
    std::fill(std::begin(gamertag_), std::end(gamertag_), '\0');
    dialog_open = false;
  }

  if (!dialog_open) {
    ImGui::CloseCurrentPopup();
    Close();
    ImGui::EndPopup();
    return;
  }
  ImGui::EndPopup();
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

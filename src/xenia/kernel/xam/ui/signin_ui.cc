/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/signin_ui.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

SigninUI::SigninUI(xe::ui::ImGuiDrawer* imgui_drawer,
                   ProfileManager* profile_manager, uint32_t last_used_slot,
                   uint32_t users_needed)
    : XamDialog(imgui_drawer),
      profile_manager_(profile_manager),
      last_user_(last_used_slot),
      users_needed_(users_needed),
      title_("Sign In") {}

void SigninUI::OnDraw(ImGuiIO& io) {
  bool first_draw = false;
  if (!has_opened_) {
    ImGui::OpenPopup(title_.c_str());
    has_opened_ = true;
    first_draw = true;
    ReloadProfiles(true);
  }
  if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    for (uint32_t i = 0; i < users_needed_; i++) {
      ImGui::BeginGroup();

      std::vector<const char*> combo_items;
      int items_count = 0;
      int current_item = 0;

      // Fill slot list.
      std::vector<uint8_t> slots;
      slots.push_back(0xFF);
      combo_items.push_back("---");
      for (auto& elem : slot_data_) {
        // Select the slot or skip it if it's already used.
        bool already_taken = false;
        for (uint32_t j = 0; j < users_needed_; j++) {
          if (chosen_slots_[j] == elem.first) {
            if (i == j) {
              current_item = static_cast<int>(combo_items.size());
            } else {
              already_taken = true;
            }
            break;
          }
        }

        if (already_taken) {
          continue;
        }

        slots.push_back(elem.first);
        combo_items.push_back(elem.second.c_str());
      }
      items_count = static_cast<int>(combo_items.size());

      ImGui::BeginDisabled(users_needed_ == 1);
      ImGui::Combo(fmt::format("##Slot{:d}", i).c_str(), &current_item,
                   combo_items.data(), items_count);
      chosen_slots_[i] = slots[current_item];
      ImGui::EndDisabled();
      ImGui::Spacing();

      combo_items.clear();
      current_item = 0;

      // Fill profile list.
      std::vector<uint64_t> xuids;
      xuids.push_back(0);
      combo_items.push_back("---");
      if (chosen_slots_[i] != 0xFF) {
        for (auto& elem : profile_data_) {
          // Select the profile or skip it if it's already used.
          bool already_taken = false;
          for (uint32_t j = 0; j < users_needed_; j++) {
            if (chosen_xuids_[j] == elem.first) {
              if (i == j) {
                current_item = static_cast<int>(combo_items.size());
              } else {
                already_taken = true;
              }
              break;
            }
          }

          if (already_taken) {
            continue;
          }

          xuids.push_back(elem.first);
          combo_items.push_back(elem.second.c_str());
        }
      }
      items_count = static_cast<int>(combo_items.size());

      ImGui::BeginDisabled(chosen_slots_[i] == 0xFF);
      ImGui::Combo(fmt::format("##Profile{:d}", i).c_str(), &current_item,
                   combo_items.data(), items_count);
      chosen_xuids_[i] = xuids[current_item];
      ImGui::EndDisabled();
      ImGui::Spacing();

      // Draw profile badge.
      uint8_t slot = chosen_slots_[i];
      uint64_t xuid = chosen_xuids_[i];
      const auto account = profile_manager_->GetAccount(xuid);

      if (slot == 0xFF || xuid == 0 || !account) {
        float ypos = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(ypos + ImGui::GetTextLineHeight() * 5);
      } else {
        xeDrawProfileContent(imgui_drawer(), xuid, slot, account, nullptr, {},
                             {}, nullptr);
      }

      ImGui::EndGroup();
      if (i != (users_needed_ - 1) && (i == 0 || i == 2)) {
        ImGui::SameLine();
      }
    }

    ImGui::Spacing();

    if (ImGui::Button("Create Profile")) {
      creating_profile_ = true;
      ImGui::OpenPopup("Create Profile");
      first_draw = true;
    }
    ImGui::Spacing();

    if (creating_profile_) {
      if (ImGui::BeginPopupModal("Create Profile", nullptr,
                                 ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_HorizontalScrollbar)) {
        if (first_draw) {
          ImGui::SetKeyboardFocusHere();
        }

        ImGui::TextUnformatted("Gamertag:");
        ImGui::InputText("##Gamertag", gamertag_, sizeof(gamertag_));

        const std::string gamertag_string = gamertag_;
        bool valid = profile_manager_->IsGamertagValid(gamertag_string);

        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Create")) {
          profile_manager_->CreateProfile(gamertag_string, false);
          std::fill(std::begin(gamertag_), std::end(gamertag_), '\0');
          ImGui::CloseCurrentPopup();
          creating_profile_ = false;
          ReloadProfiles(false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
          std::fill(std::begin(gamertag_), std::end(gamertag_), '\0');
          ImGui::CloseCurrentPopup();
          creating_profile_ = false;
        }

        ImGui::EndPopup();
      } else {
        creating_profile_ = false;
      }
    }

    if (ImGui::Button("OK")) {
      std::map<uint8_t, uint64_t> profile_map;
      for (uint32_t i = 0; i < users_needed_; i++) {
        uint8_t slot = chosen_slots_[i];
        uint64_t xuid = chosen_xuids_[i];
        if (slot != 0xFF && xuid != 0) {
          profile_map[slot] = xuid;
        }
      }
      profile_manager_->LoginMultiple(profile_map);

      ImGui::CloseCurrentPopup();
      Close();
    }
    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
      Close();
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::EndPopup();
  } else {
    Close();
  }
}

void SigninUI::ReloadProfiles(bool first_draw) {
  auto profile_manager = kernel_state()->xam_state()->profile_manager();
  auto profiles = profile_manager->GetAccounts();

  profile_data_.clear();
  for (auto& [xuid, account] : *profiles) {
    profile_data_.push_back({xuid, account.GetGamertagString()});
  }

  if (first_draw) {
    // If only one user is requested, request last used controller to sign in.
    if (users_needed_ == 1) {
      chosen_slots_[0] = last_user_;
    } else {
      for (uint32_t i = 0; i < users_needed_; i++) {
        // TODO: Not sure about this, needs testing on real hardware.
        chosen_slots_[i] = i;
      }
    }

    // Default profile selection to profile that is already signed in.
    for (auto& elem : profile_data_) {
      uint64_t xuid = elem.first;
      uint8_t slot = profile_manager->GetUserIndexAssignedToProfile(xuid);
      for (uint32_t j = 0; j < users_needed_; j++) {
        if (chosen_slots_[j] != XUserIndexAny && slot == chosen_slots_[j]) {
          chosen_xuids_[j] = xuid;
        }
      }
    }
  }
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

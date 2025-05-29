/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_SIGNIN_UI_H_
#define XENIA_KERNEL_XAM_UI_SIGNIN_UI_H_

#include "xenia/kernel/xam/xam_ui.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

class SigninUI final : public XamDialog {
 public:
  SigninUI(xe::ui::ImGuiDrawer* imgui_drawer, ProfileManager* profile_manager,
           uint32_t last_used_slot, uint32_t users_needed);

  ~SigninUI() = default;

 private:
  void OnDraw(ImGuiIO& io) override;

  void ReloadProfiles(bool first_draw);

  const std::map<uint8_t, std::string> slot_data_ = {
      {0, "Slot 0"}, {1, "Slot 1"}, {2, "Slot 2"}, {3, "Slot 3"}};

  ProfileManager* profile_manager_ = nullptr;

  bool has_opened_ = false;
  std::string title_;
  uint32_t users_needed_ = 1;
  uint32_t last_user_ = 0;

  std::vector<std::pair<uint64_t, std::string>> profile_data_;
  uint8_t chosen_slots_[XUserMaxUserCount] = {};
  uint64_t chosen_xuids_[XUserMaxUserCount] = {};

  bool creating_profile_ = false;
  char gamertag_[16] = "";
};

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif

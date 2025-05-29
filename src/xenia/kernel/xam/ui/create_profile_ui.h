/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_CREATE_PROFILE_UI_H_
#define XENIA_KERNEL_XAM_UI_CREATE_PROFILE_UI_H_

#include "xenia/kernel/xam/xam_ui.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

class CreateProfileUI final : public XamDialog {
 public:
  CreateProfileUI(xe::ui::ImGuiDrawer* imgui_drawer, Emulator* emulator,
                  bool with_migration = false)
      : XamDialog(imgui_drawer),
        emulator_(emulator),
        migration_(with_migration) {
    memset(gamertag_, 0, sizeof(gamertag_));
  }

  ~CreateProfileUI() = default;

 private:
  void OnDraw(ImGuiIO& io) override;

  bool has_opened_ = false;
  bool migration_ = false;
  char gamertag_[16] = "";
  Emulator* emulator_;
};

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif

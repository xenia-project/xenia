/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_PROFILE_DIALOGS_H_
#define XENIA_APP_PROFILE_DIALOGS_H_

#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {

class EmulatorWindow;

class CreateProfileDialog final : public ui::ImGuiDialog {
 public:
  CreateProfileDialog(ui::ImGuiDrawer* imgui_drawer,
                      EmulatorWindow* emulator_window,
                      bool with_migration = false)
      : ui::ImGuiDialog(imgui_drawer),
        emulator_window_(emulator_window),
        migration_(with_migration) {
    memset(gamertag_, 0, sizeof(gamertag_));
  }

 protected:
  void OnDraw(ImGuiIO& io) override;

  bool has_opened_ = false;
  bool migration_ = false;
  char gamertag_[16] = "";
  EmulatorWindow* emulator_window_;
};

class NoProfileDialog final : public ui::ImGuiDialog {
 public:
  NoProfileDialog(ui::ImGuiDrawer* imgui_drawer,
                  EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

  EmulatorWindow* emulator_window_;
};

class ProfileConfigDialog final : public ui::ImGuiDialog {
 public:
  ProfileConfigDialog(ui::ImGuiDrawer* imgui_drawer,
                      EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  uint64_t selected_xuid_ = 0;
  EmulatorWindow* emulator_window_;
};

}  // namespace app
}  // namespace xe

#endif

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMGUI_DIALOG_H_
#define XENIA_UI_IMGUI_DIALOG_H_

#include <memory>

#include "xenia/base/threading.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window_listener.h"

namespace xe {
namespace ui {

class ImGuiDialog {
 public:
  ~ImGuiDialog();

  // Shows a simple message box containing a text message.
  // Callers can want for the dialog to close with Wait().
  // Dialogs retain themselves and will delete themselves when closed.
  static ImGuiDialog* ShowMessageBox(ImGuiDrawer* imgui_drawer,
                                     std::string title, std::string body);

  // A fence to signal when the dialog is closed.
  void Then(xe::threading::Fence* fence);

  void Draw();

 protected:
  ImGuiDialog(ImGuiDrawer* imgui_drawer);

  ImGuiDrawer* imgui_drawer() const { return imgui_drawer_; }
  ImGuiIO& GetIO();

  // Closes the dialog and returns to any waiters.
  void Close();

  virtual void OnShow() {}
  virtual void OnClose() {}
  virtual void OnDraw(ImGuiIO& io) {}

 private:
  ImGuiDrawer* imgui_drawer_ = nullptr;
  bool has_close_pending_ = false;
  std::vector<xe::threading::Fence*> waiting_fences_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_IMGUI_DIALOG_H_

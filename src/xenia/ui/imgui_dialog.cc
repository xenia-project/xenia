/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/imgui_dialog.h"

#include "third_party/imgui/imgui.h"
#include "xenia/base/assert.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

ImGuiDialog::ImGuiDialog(Window* window) : window_(window) {
  window_->AttachListener(this);
  had_imgui_active_ = window_->is_imgui_input_enabled();
  window_->set_imgui_input_enabled(true);
}

ImGuiDialog::~ImGuiDialog() {
  window_->set_imgui_input_enabled(had_imgui_active_);
  window_->DetachListener(this);
  for (auto fence : waiting_fences_) {
    fence->Signal();
  }
}

void ImGuiDialog::Then(xe::threading::Fence* fence) {
  waiting_fences_.push_back(fence);
}

void ImGuiDialog::Close() { has_close_pending_ = true; }

ImGuiIO& ImGuiDialog::GetIO() { return window_->imgui_drawer()->GetIO(); }

void ImGuiDialog::OnPaint(UIEvent* e) {
  // Keep imgui rendering every frame.
  window_->Invalidate();

  // Draw UI.
  OnDraw(GetIO());

  // Check to see if the UI closed itself and needs to be deleted.
  if (has_close_pending_) {
    OnClose();
    delete this;
  }
}

class MessageBoxDialog : public ImGuiDialog {
 public:
  MessageBoxDialog(Window* window, std::string title, std::string body)
      : ImGuiDialog(window), title_(std::move(title)), body_(std::move(body)) {}

  void OnDraw(ImGuiIO& io) override {
    if (!has_opened_) {
      ImGui::OpenPopup(title_.c_str());
      has_opened_ = true;
    }
    if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      char* text = const_cast<char*>(body_.c_str());
      ImGui::InputTextMultiline(
          "##body", text, body_.size(), ImVec2(600, 0),
          ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
      if (ImGui::Button("OK")) {
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::EndPopup();
    } else {
      Close();
    }
  }

 private:
  bool has_opened_ = false;
  std::string title_;
  std::string body_;
};

ImGuiDialog* ImGuiDialog::ShowMessageBox(Window* window, std::string title,
                                         std::string body) {
  return new MessageBoxDialog(window, std::move(title), std::move(body));
}

}  // namespace ui
}  // namespace xe

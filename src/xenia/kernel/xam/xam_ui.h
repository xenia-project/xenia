/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_UI_H_
#define XENIA_KERNEL_XAM_XAM_UI_H_

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace kernel {
namespace xam {

class XamDialog : public xe::ui::ImGuiDialog {
 public:
  void set_close_callback(std::function<void()> close_callback) {
    close_callback_ = close_callback;
  }

 protected:
  XamDialog(xe::ui::ImGuiDrawer* imgui_drawer)
      : xe::ui::ImGuiDialog(imgui_drawer) {}

  virtual ~XamDialog() {}
  void OnClose() override {
    if (close_callback_) {
      close_callback_();
    }
  }

 private:
  std::function<void()> close_callback_ = nullptr;
};

class MessageBoxDialog : public XamDialog {
 public:
  MessageBoxDialog(xe::ui::ImGuiDrawer* imgui_drawer, std::string& title,
                   std::string& description, std::vector<std::string> buttons,
                   uint32_t default_button)
      : XamDialog(imgui_drawer),
        title_(title),
        description_(description),
        buttons_(std::move(buttons)),
        default_button_(default_button),
        chosen_button_(default_button) {
    if (!title_.size()) {
      title_ = "Message Box";
    }
  }

  uint32_t chosen_button() const { return chosen_button_; }

  void OnDraw(ImGuiIO& io) override;
  virtual ~MessageBoxDialog() {}

 private:
  bool has_opened_ = false;
  std::string title_;
  std::string description_;
  std::vector<std::string> buttons_;
  uint32_t default_button_ = 0;
  uint32_t chosen_button_ = 0;
};

class KeyboardInputDialog : public XamDialog {
 public:
  KeyboardInputDialog(xe::ui::ImGuiDrawer* imgui_drawer, std::string& title,
                      std::string& description, std::string& default_text,
                      size_t max_length)
      : XamDialog(imgui_drawer),
        title_(title),
        description_(description),
        default_text_(default_text),
        max_length_(max_length),
        text_buffer_() {
    if (!title_.size()) {
      if (!description_.size()) {
        title_ = "Keyboard Input";
      } else {
        title_ = description_;
        description_ = "";
      }
    }
    text_ = default_text;
    text_buffer_.resize(max_length);
    xe::string_util::copy_truncating(text_buffer_.data(), default_text_,
                                     text_buffer_.size());
  }
  virtual ~KeyboardInputDialog() {}

  const std::string& text() const { return text_; }
  bool cancelled() const { return cancelled_; }

  void OnDraw(ImGuiIO& io) override;

 private:
  bool has_opened_ = false;
  std::string title_;
  std::string description_;
  std::string default_text_;
  size_t max_length_ = 0;
  std::vector<char> text_buffer_;
  std::string text_ = "";
  bool cancelled_ = true;
};

bool xeDrawProfileContent(xe::ui::ImGuiDrawer* imgui_drawer,
                          const uint64_t xuid, const uint8_t user_index,
                          const X_XAMACCOUNTINFO* account,
                          const xe::ui::ImmediateTexture* profile_icon,
                          std::function<bool()> context_menu,
                          std::function<void()> on_profile_change,
                          uint64_t* selected_xuid);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif

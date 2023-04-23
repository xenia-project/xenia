/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "third_party/imgui/imgui.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_flags.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

// TODO(gibbed): This is all one giant WIP that seems to work better than the
// previous immediate synchronous completion of dialogs.
//
// The deferred execution of dialog handling is done in such a way that there is
// a pre-, peri- (completion), and post- callback steps.
//
// pre();
// result = completion();
// CompleteOverlapped(result);
// post();
//
// There are games that are batshit insane enough to wait for the X_OVERLAPPED
// to be completed (ie not X_ERROR_PENDING) before creating a listener to
// receive a notification, which is why we have distinct pre- and post- steps.
//
// We deliberately delay the XN_SYS_UI = false notification to give games time
// to create a listener (if they're insane enough do this).

extern std::atomic<int> xam_dialogs_shown_;

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

template <typename T>
X_RESULT xeXamDispatchDialog(T* dialog,
                             std::function<X_RESULT(T*)> close_callback,
                             uint32_t overlapped) {
  auto pre = []() {
    // Broadcast XN_SYS_UI = true
    kernel_state()->BroadcastNotification(0x9, true);
  };
  auto run = [dialog, close_callback]() -> X_RESULT {
    X_RESULT result;
    dialog->set_close_callback([&dialog, &result, &close_callback]() {
      result = close_callback(dialog);
    });
    xe::threading::Fence fence;
    xe::ui::WindowedAppContext& app_context =
        kernel_state()->emulator()->display_window()->app_context();
    if (app_context.CallInUIThreadSynchronous(
            [&dialog, &fence]() { dialog->Then(&fence); })) {
      ++xam_dialogs_shown_;
      fence.Wait();
      --xam_dialogs_shown_;
    } else {
      delete dialog;
    }
    // dialog should be deleted at this point!
    return result;
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    // Broadcast XN_SYS_UI = false
    kernel_state()->BroadcastNotification(0x9, false);
  };
  if (!overlapped) {
    pre();
    auto result = run();
    post();
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferred(run, overlapped, pre, post);
    return X_ERROR_IO_PENDING;
  }
}

template <typename T>
X_RESULT xeXamDispatchDialogEx(
    T* dialog, std::function<X_RESULT(T*, uint32_t&, uint32_t&)> close_callback,
    uint32_t overlapped) {
  auto pre = []() {
    // Broadcast XN_SYS_UI = true
    kernel_state()->BroadcastNotification(0x9, true);
  };
  auto run = [dialog, close_callback](uint32_t& extended_error,
                                      uint32_t& length) -> X_RESULT {
    auto display_window = kernel_state()->emulator()->display_window();
    X_RESULT result;
    dialog->set_close_callback(
        [&dialog, &result, &extended_error, &length, &close_callback]() {
          result = close_callback(dialog, extended_error, length);
        });
    xe::threading::Fence fence;
    if (display_window->app_context().CallInUIThreadSynchronous(
            [&dialog, &fence]() { dialog->Then(&fence); })) {
      ++xam_dialogs_shown_;
      fence.Wait();
      --xam_dialogs_shown_;
    } else {
      delete dialog;
    }
    // dialog should be deleted at this point!
    return result;
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    // Broadcast XN_SYS_UI = false
    kernel_state()->BroadcastNotification(0x9, false);
  };
  if (!overlapped) {
    pre();
    uint32_t extended_error, length;
    auto result = run(extended_error, length);
    post();
    // TODO(gibbed): do something with extended_error/length?
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run, overlapped, pre, post);
    return X_ERROR_IO_PENDING;
  }
}

X_RESULT xeXamDispatchHeadless(std::function<X_RESULT()> run_callback,
                               uint32_t overlapped) {
  auto pre = []() {
    // Broadcast XN_SYS_UI = true
    kernel_state()->BroadcastNotification(0x9, true);
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    // Broadcast XN_SYS_UI = false
    kernel_state()->BroadcastNotification(0x9, false);
  };
  if (!overlapped) {
    pre();
    auto result = run_callback();
    post();
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferred(run_callback, overlapped, pre,
                                               post);
    return X_ERROR_IO_PENDING;
  }
}

X_RESULT xeXamDispatchHeadlessEx(
    std::function<X_RESULT(uint32_t&, uint32_t&)> run_callback,
    uint32_t overlapped) {
  auto pre = []() {
    // Broadcast XN_SYS_UI = true
    kernel_state()->BroadcastNotification(0x9, true);
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    // Broadcast XN_SYS_UI = false
    kernel_state()->BroadcastNotification(0x9, false);
  };
  if (!overlapped) {
    pre();
    uint32_t extended_error, length;
    auto result = run_callback(extended_error, length);
    post();
    // TODO(gibbed): do something with extended_error/length?
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run_callback, overlapped, pre,
                                                 post);
    return X_ERROR_IO_PENDING;
  }
}

dword_result_t XamIsUIActive_entry() { return xeXamIsUIActive(); }
DECLARE_XAM_EXPORT2(XamIsUIActive, kUI, kImplemented, kHighFrequency);

class MessageBoxDialog : public XamDialog {
 public:
  MessageBoxDialog(xe::ui::ImGuiDrawer* imgui_drawer, std::string title,
                   std::string description, std::vector<std::string> buttons,
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

  void OnDraw(ImGuiIO& io) override {
    bool first_draw = false;
    if (!has_opened_) {
      ImGui::OpenPopup(title_.c_str());
      has_opened_ = true;
      first_draw = true;
    }
    if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      if (description_.size()) {
        ImGui::Text("%s", description_.c_str());
      }
      if (first_draw) {
        ImGui::SetKeyboardFocusHere();
      }
      for (size_t i = 0; i < buttons_.size(); ++i) {
        if (ImGui::Button(buttons_[i].c_str())) {
          chosen_button_ = static_cast<uint32_t>(i);
          ImGui::CloseCurrentPopup();
          Close();
        }
        ImGui::SameLine();
      }
      ImGui::Spacing();
      ImGui::Spacing();
      ImGui::EndPopup();
    } else {
      Close();
    }
  }
  virtual ~MessageBoxDialog() {}

 private:
  bool has_opened_ = false;
  std::string title_;
  std::string description_;
  std::vector<std::string> buttons_;
  uint32_t default_button_ = 0;
  uint32_t chosen_button_ = 0;
};

static dword_result_t XamShowMessageBoxUi(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, lpdword_t result_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  std::string title;
  if (title_ptr) {
    title = xe::to_utf8(title_ptr.value());
  } else {
    title = "";  // TODO(gibbed): default title based on flags?
  }

  std::vector<std::string> buttons;
  for (uint32_t i = 0; i < button_count; ++i) {
    uint32_t button_ptr = button_ptrs[i];
    auto button = xe::load_and_swap<std::u16string>(
        kernel_state()->memory()->TranslateVirtual(button_ptr));
    buttons.push_back(xe::to_utf8(button));
  }

  X_RESULT result;
  if (cvars::headless) {
    // Auto-pick the focused button.
    auto run = [result_ptr, active_button]() -> X_RESULT {
      *result_ptr = static_cast<uint32_t>(active_button);
      return X_ERROR_SUCCESS;
    };
    result = xeXamDispatchHeadless(run, overlapped);
  } else {
    // TODO(benvanik): setup icon states.
    switch (flags & 0xF) {
      case 0:
        // config.pszMainIcon = nullptr;
        break;
      case 1:
        // config.pszMainIcon = TD_ERROR_ICON;
        break;
      case 2:
        // config.pszMainIcon = TD_WARNING_ICON;
        break;
      case 3:
        // config.pszMainIcon = TD_INFORMATION_ICON;
        break;
    }
    auto close = [result_ptr](MessageBoxDialog* dialog) -> X_RESULT {
      *result_ptr = dialog->chosen_button();
      return X_ERROR_SUCCESS;
    };
    const Emulator* emulator = kernel_state()->emulator();
    ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
    result = xeXamDispatchDialog<MessageBoxDialog>(
        new MessageBoxDialog(imgui_drawer, title, xe::to_utf8(text_ptr.value()),
                             buttons, active_button),
        close, overlapped);
  }
  return result;
}

// https://www.se7ensins.com/forums/threads/working-xshowmessageboxui.844116/
dword_result_t XamShowMessageBoxUI_entry(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, lpdword_t result_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags, result_ptr,
                             overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMessageBoxUI, kUI, kImplemented);

dword_result_t XamShowMessageBoxUIEx_entry(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, dword_t unknown_unused, lpdword_t result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags, result_ptr,
                             overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMessageBoxUIEx, kUI, kImplemented);
class KeyboardInputDialog : public XamDialog {
 public:
  KeyboardInputDialog(xe::ui::ImGuiDrawer* imgui_drawer, std::string title,
                      std::string description, std::string default_text,
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

  void OnDraw(ImGuiIO& io) override {
    bool first_draw = false;
    if (!has_opened_) {
      ImGui::OpenPopup(title_.c_str());
      has_opened_ = true;
      first_draw = true;
    }
    if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      if (description_.size()) {
        ImGui::TextWrapped("%s", description_.c_str());
      }
      if (first_draw) {
        ImGui::SetKeyboardFocusHere();
      }
      if (ImGui::InputText("##body", text_buffer_.data(), text_buffer_.size(),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        text_ = std::string(text_buffer_.data(), text_buffer_.size());
        cancelled_ = false;
        ImGui::CloseCurrentPopup();
        Close();
      }
      if (ImGui::Button("OK")) {
        text_ = std::string(text_buffer_.data(), text_buffer_.size());
        cancelled_ = false;
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        text_ = "";
        cancelled_ = true;
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::Spacing();
      ImGui::EndPopup();
    } else {
      Close();
    }
  }

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

// https://www.se7ensins.com/forums/threads/release-how-to-use-xshowkeyboardui-release.906568/
dword_result_t XamShowKeyboardUI_entry(
    dword_t user_index, dword_t flags, lpu16string_t default_text,
    lpu16string_t title, lpu16string_t description, lpu16string_t buffer,
    dword_t buffer_length, pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!buffer) {
    return X_ERROR_INVALID_PARAMETER;
  }

  assert_not_null(overlapped);

  auto buffer_size = static_cast<size_t>(buffer_length) * 2;

  X_RESULT result;
  if (cvars::headless) {
    auto run = [default_text, buffer, buffer_length,
                buffer_size]() -> X_RESULT {
      // Redirect default_text back into the buffer.
      if (!default_text) {
        std::memset(buffer, 0, buffer_size);
      } else {
        string_util::copy_and_swap_truncating(buffer, default_text.value(),
                                              buffer_length);
      }
      return X_ERROR_SUCCESS;
    };
    result = xeXamDispatchHeadless(run, overlapped);
  } else {
    auto close = [buffer, buffer_length](KeyboardInputDialog* dialog,
                                         uint32_t& extended_error,
                                         uint32_t& length) -> X_RESULT {
      if (dialog->cancelled()) {
        extended_error = X_ERROR_CANCELLED;
        length = 0;
        return X_ERROR_SUCCESS;
      } else {
        // Zero the output buffer.
        auto text = xe::to_utf16(dialog->text());
        string_util::copy_and_swap_truncating(buffer, text, buffer_length);
        extended_error = X_ERROR_SUCCESS;
        length = 0;
        return X_ERROR_SUCCESS;
      }
    };
    const Emulator* emulator = kernel_state()->emulator();
    ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
    result = xeXamDispatchDialogEx<KeyboardInputDialog>(
        new KeyboardInputDialog(
            imgui_drawer, title ? xe::to_utf8(title.value()) : "",
            description ? xe::to_utf8(description.value()) : "",
            default_text ? xe::to_utf8(default_text.value()) : "",
            buffer_length),
        close, overlapped);
  }
  return result;
}
DECLARE_XAM_EXPORT1(XamShowKeyboardUI, kUI, kImplemented);

dword_result_t XamShowDeviceSelectorUI_entry(
    dword_t user_index, dword_t content_type, dword_t content_flags,
    qword_t total_requested, lpdword_t device_id_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  return xeXamDispatchHeadless(
      [device_id_ptr]() -> X_RESULT {
        // NOTE: 0x00000001 is our dummy device ID from xam_content.cc
        *device_id_ptr = 0x00000001;
        return X_ERROR_SUCCESS;
      },
      overlapped);
}
DECLARE_XAM_EXPORT1(XamShowDeviceSelectorUI, kUI, kImplemented);

void XamShowDirtyDiscErrorUI_entry(dword_t user_index) {
  if (cvars::headless) {
    assert_always();
    exit(1);
    return;
  }
  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(
          imgui_drawer, "Disc Read Error",
          "There's been an issue reading content from the game disc.\nThis is "
          "likely caused by bad or unimplemented file IO calls.",
          {"OK"}, 0),
      [](MessageBoxDialog*) -> X_RESULT { return X_ERROR_SUCCESS; }, 0);
  // This is death, and should never return.
  // TODO(benvanik): cleaner exit.
  exit(1);
}
DECLARE_XAM_EXPORT1(XamShowDirtyDiscErrorUI, kUI, kImplemented);

dword_result_t XamShowPartyUI_entry(unknown_t r3, unknown_t r4) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamShowPartyUI, kNone, kStub);

dword_result_t XamShowCommunitySessionsUI_entry(unknown_t r3, unknown_t r4) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamShowCommunitySessionsUI, kNone, kStub);

// this is supposed to do a lot more, calls another function that triggers some
// cbs
dword_result_t XamSetDashContext_entry(dword_t value,
                                       const ppc_context_t& ctx) {
  ctx->kernel_state->dash_context_ = value;
  return 0;
}

DECLARE_XAM_EXPORT1(XamSetDashContext, kNone, kImplemented);

dword_result_t XamGetDashContext_entry(const ppc_context_t& ctx) {
  return ctx->kernel_state->dash_context_;
}

DECLARE_XAM_EXPORT1(XamGetDashContext, kNone, kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(UI);

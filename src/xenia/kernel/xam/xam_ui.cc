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
#include "xenia/base/system.h"
#include "xenia/emulator.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_flags.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_content_device.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/imgui_guest_notification.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/xbox.h"

DEFINE_bool(storage_selection_dialog, false,
            "Show storage device selection dialog when the game requests it.",
            "UI");

DECLARE_int32(license_mask);

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
    kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, true);
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
    kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, false);
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
    kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, true);
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
    kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, false);
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
    kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, true);
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, false);
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
    kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, true);
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, false);
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

template <typename T>
X_RESULT xeXamDispatchDialogAsync(T* dialog,
                                  std::function<void(T*)> close_callback) {
  kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, true);
  ++xam_dialogs_shown_;

  // Important to pass captured vars by value here since we return from this
  // without waiting for the dialog to close so the original local vars will be
  // destroyed.
  dialog->set_close_callback([dialog, close_callback]() {
    close_callback(dialog);

    --xam_dialogs_shown_;

    auto run = []() -> void {
      xe::threading::Sleep(std::chrono::milliseconds(100));
      kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, false);
    };

    std::thread thread(run);
    thread.detach();
  });

  return X_ERROR_SUCCESS;
}

X_RESULT xeXamDispatchHeadlessAsync(std::function<void()> run_callback) {
  kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, true);
  ++xam_dialogs_shown_;

  auto display_window = kernel_state()->emulator()->display_window();
  display_window->app_context().CallInUIThread([run_callback]() {
    run_callback();

    --xam_dialogs_shown_;

    auto run = []() -> void {
      xe::threading::Sleep(std::chrono::milliseconds(100));
      kernel_state()->BroadcastNotification(kXNotificationIDSystemUI, false);
    };

    std::thread thread(run);
    thread.detach();
  });

  return X_ERROR_SUCCESS;
}

dword_result_t XamIsUIActive_entry() { return xeXamIsUIActive(); }
DECLARE_XAM_EXPORT2(XamIsUIActive, kUI, kImplemented, kHighFrequency);

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

class GamertagModifyDialog final : public ui::ImGuiDialog {
 public:
  GamertagModifyDialog(ui::ImGuiDrawer* imgui_drawer,
                       ProfileManager* profile_manager, uint64_t xuid)
      : ui::ImGuiDialog(imgui_drawer),
        profile_manager_(profile_manager),
        xuid_(xuid) {
    memset(gamertag_, 0, sizeof(gamertag_));
  }

 private:
  void OnDraw(ImGuiIO& io) override {
    if (!has_opened_) {
      ImGui::OpenPopup("Modify Gamertag");
      has_opened_ = true;
    }

    bool dialog_open = true;
    if (!ImGui::BeginPopupModal("Modify Gamertag", &dialog_open,
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

    ImGui::TextUnformatted("New gamertag:");
    ImGui::InputText("##Gamertag", gamertag_, sizeof(gamertag_));

    const std::string gamertag_string = std::string(gamertag_);
    bool valid = profile_manager_->IsGamertagValid(gamertag_string);

    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Update")) {
      profile_manager_->ModifyGamertag(xuid_, gamertag_string);
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
  };

  bool has_opened_ = false;
  char gamertag_[16] = "";
  const uint64_t xuid_;
  ProfileManager* profile_manager_;
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

dword_result_t XNotifyQueueUI_entry(dword_t exnq, dword_t dwUserIndex,
                                    qword_t qwAreas,
                                    lpu16string_t displayText_ptr,
                                    lpvoid_t contextData) {
  std::string displayText = "";
  const uint8_t position_id = static_cast<uint8_t>(qwAreas);

  if (displayText_ptr) {
    displayText = xe::to_utf8(displayText_ptr.value());
  }

  XELOGI("XNotifyQueueUI: {}", displayText);

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  new xe::ui::XNotifyWindow(imgui_drawer, "", displayText, dwUserIndex,
                            position_id);

  // XNotifyQueueUI -> XNotifyQueueUIEx -> XMsgProcessRequest ->
  // XMsgStartIORequestEx & XMsgInProcessCall
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XNotifyQueueUI, kUI, kSketchy);

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
  if (!overlapped) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if ((user_index >= XUserMaxUserCount && user_index != XUserIndexAny) ||
      (content_flags & 0x83F00008) != 0 || !device_id_ptr) {
    XOverlappedSetExtendedError(overlapped, X_ERROR_INVALID_PARAMETER);
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index != XUserIndexAny &&
      !kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    kernel_state()->CompleteOverlappedImmediate(overlapped,
                                                X_ERROR_NO_SUCH_USER);
    return X_ERROR_IO_PENDING;
  }

  std::vector<const DummyDeviceInfo*> devices = ListStorageDevices();

  if (cvars::headless || !cvars::storage_selection_dialog) {
    // Default to the first storage device (HDD) if headless.
    return xeXamDispatchHeadless(
        [device_id_ptr, devices]() -> X_RESULT {
          if (devices.empty()) return X_ERROR_CANCELLED;

          const DummyDeviceInfo* device_info = devices.front();
          *device_id_ptr = static_cast<uint32_t>(device_info->device_id);
          return X_ERROR_SUCCESS;
        },
        overlapped);
  }

  auto close = [device_id_ptr, devices](MessageBoxDialog* dialog) -> X_RESULT {
    uint32_t button = dialog->chosen_button();
    if (button >= devices.size()) return X_ERROR_CANCELLED;

    const DummyDeviceInfo* device_info = devices.at(button);
    *device_id_ptr = static_cast<uint32_t>(device_info->device_id);
    return X_ERROR_SUCCESS;
  };

  std::string title = "Select storage device";
  std::string desc = "";

  cxxopts::OptionNames buttons;
  for (auto& device_info : devices) {
    buttons.push_back(to_utf8(device_info->name));
  }
  buttons.push_back("Cancel");

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close,
      overlapped);
}
DECLARE_XAM_EXPORT1(XamShowDeviceSelectorUI, kUI, kImplemented);

void XamShowDirtyDiscErrorUI_entry(dword_t user_index) {
  if (cvars::headless) {
    assert_always();
    exit(1);
    return;
  }

  std::string title = "Disc Read Error";
  std::string desc =
      "There's been an issue reading content from the game disc.\nThis is "
      "likely caused by bad or unimplemented file IO calls.";

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, {"OK"}, 0),
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

dword_result_t XamShowMarketplaceUI_entry(dword_t user_index, dword_t ui_type,
                                          qword_t offer_id,
                                          dword_t content_types) {
  // ui_type:
  // 0 - view all content for the current title
  // 1 - view content specified by offer id
  // content_types:
  // game specific, usually just -1
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadlessAsync([]() {});
  }

  auto close = [ui_type](MessageBoxDialog* dialog) -> void {
    if (ui_type == 1) {
      uint32_t button = dialog->chosen_button();
      if (button == 0) {
        cvars::license_mask = 1;

        kernel_state()->BroadcastNotification(
            kXNotificationIDLiveContentInstalled, 0);
      }
    }
  };

  std::string title = "Xbox Marketplace";
  std::string desc = "";
  cxxopts::OptionNames buttons;

  switch (ui_type) {
    case 0:
      desc =
          "Game requested to open marketplace page with all content for the "
          "current title ID.";
      break;
    case 1:
      desc = fmt::format(
          "Game requested to open marketplace page for offer ID 0x{:016X}.",
          offer_id);
      break;
    default:
      desc = fmt::format("Unknown marketplace op {:d}", ui_type);
      break;
  }

  desc +=
      "\nNote that since Xenia cannot access Xbox Marketplace, any DLC must be "
      "installed manually using File -> Install Content.";

  switch (ui_type) {
    case 0:
    default:
      buttons.push_back("OK");
      break;
    case 1:
      desc +=
          "\n\nTo start trial games in full mode, set license_mask to 1 in "
          "Xenia config file.\n\nDo you wish to change license_mask to 1 for "
          "*this session*?";
      buttons.push_back("Yes");
      buttons.push_back("No");
      break;
  }

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialogAsync<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceUI, kUI, kSketchy);

dword_result_t XamShowMarketplaceDownloadItemsUI_entry(
    dword_t user_index, dword_t ui_type, lpqword_t offers, dword_t num_offers,
    lpdword_t hresult_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  // ui_type:
  // 1000 - free
  // 1001 - paid
  if (user_index >= XUserMaxUserCount || !offers || num_offers > 6) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(overlapped,
                                                  X_ERROR_NO_SUCH_USER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_NO_SUCH_USER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadless(
        [hresult_ptr]() -> X_RESULT {
          if (hresult_ptr) {
            *hresult_ptr = X_E_SUCCESS;
          }
          return X_ERROR_SUCCESS;
        },
        overlapped);
  }

  auto close = [hresult_ptr](MessageBoxDialog* dialog) -> X_RESULT {
    if (hresult_ptr) {
      // TODO
      *hresult_ptr = X_E_SUCCESS;
    }
    return X_ERROR_SUCCESS;
  };

  std::string title = "Xbox Marketplace";
  std::string desc = "";
  cxxopts::OptionNames buttons = {"OK"};

  switch (ui_type) {
    case 1000:
      desc =
          "Game requested to open download page for the following free offer "
          "IDs:";
      break;
    case 1001:
      desc =
          "Game requested to open download page for the following offer IDs:";
      break;
    default:
      return X_ERROR_INVALID_PARAMETER;
  }

  for (uint32_t i = 0; i < num_offers; i++) {
    desc += fmt::format("\n0x{:16X}", offers[i]);
  }

  desc +=
      "\n\nNote that since Xenia cannot access Xbox Marketplace, any DLC "
      "must "
      "be installed manually using File -> Install Content.";

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close,
      overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceDownloadItemsUI, kUI, kSketchy);

bool xeDrawProfileContent(ui::ImGuiDrawer* imgui_drawer, const uint64_t xuid,
                          const uint8_t user_index,
                          const X_XAMACCOUNTINFO* account,
                          uint64_t* selected_xuid) {
  auto profile_manager = kernel_state()->xam_state()->profile_manager();

  const float default_image_size = 75.0f;
  auto position = ImGui::GetCursorPos();
  const float selectable_height =
      ImGui::GetTextLineHeight() *
      5;  // 3 is for amount of lines of text behind image/object.
  const auto font = imgui_drawer->GetIO().Fonts->Fonts[0];

  const auto text_size = font->CalcTextSizeA(
      font->FontSize, FLT_MAX, -1.0f,
      fmt::format("XUID: {:016X}\n", 0xB13EBABEBABEBABE).c_str());

  const auto image_scale = selectable_height / default_image_size;
  const auto image_size = ImVec2(default_image_size * image_scale,
                                 default_image_size * image_scale);

  if (xuid && selected_xuid) {
    // This includes 10% to include empty spaces between border and elements.
    auto selectable_region_size =
        ImVec2((image_size.x + text_size.x) * 1.10f, selectable_height);

    if (ImGui::Selectable("##Selectable", *selected_xuid == xuid,
                          ImGuiSelectableFlags_SpanAllColumns,
                          selectable_region_size)) {
      *selected_xuid = xuid;
    }

    if (ImGui::BeginPopupContextItem("Profile Menu")) {
      if (user_index == static_cast<uint8_t>(-1)) {
        if (ImGui::MenuItem("Login")) {
          profile_manager->Login(xuid);
        }

        if (ImGui::BeginMenu("Login to slot:")) {
          for (uint8_t i = 1; i <= XUserMaxUserCount; i++) {
            if (ImGui::MenuItem(fmt::format("slot {}", i).c_str())) {
              profile_manager->Login(xuid, i - 1);
            }
          }
          ImGui::EndMenu();
        }
      } else {
        if (ImGui::MenuItem("Logout")) {
          profile_manager->Logout(user_index);
        }
      }

      ImGui::BeginDisabled(kernel_state()->emulator()->is_title_open());
      if (ImGui::BeginMenu("Modify")) {
        if (ImGui::MenuItem("Gamertag")) {
          new GamertagModifyDialog(imgui_drawer, profile_manager, xuid);
        }

        ImGui::MenuItem("Profile Icon (Unsupported)");
        ImGui::EndMenu();
      }
      ImGui::EndDisabled();

      ImGui::MenuItem("Show Achievements (unsupported)");

      if (ImGui::MenuItem("Show Content Directory")) {
        const auto path = profile_manager->GetProfileContentPath(
            xuid, kernel_state()->title_id());

        if (!std::filesystem::exists(path)) {
          std::filesystem::create_directories(path);
        }

        std::thread path_open(LaunchFileExplorer, path);
        path_open.detach();
      }

      if (!kernel_state()->emulator()->is_title_open()) {
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
  }

  ImGui::SameLine();
  ImGui::SetCursorPos(position);

  // In the future it can be replaced with profile icon.
  ImGui::Image(user_index < XUserMaxUserCount
                   ? imgui_drawer->GetNotificationIcon(user_index)
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

  ImGui::SameLine();
  ImGui::SetCursorPos(position);
  ImGui::SetCursorPosY(position.y + 2 * ImGui::GetTextLineHeight());

  if (user_index != static_cast<uint8_t>(-1)) {
    ImGui::TextUnformatted(
        fmt::format("Assigned to slot: {}\n", user_index + 1).c_str());
  } else {
    ImGui::TextUnformatted(fmt::format("Profile is not signed in").c_str());
  }

  return true;
}

class SigninDialog : public XamDialog {
 public:
  SigninDialog(xe::ui::ImGuiDrawer* imgui_drawer, uint32_t users_needed)
      : XamDialog(imgui_drawer),
        users_needed_(users_needed),
        title_("Sign In") {
    last_user_ = kernel_state()->emulator()->input_system()->GetLastUsedSlot();

    for (uint8_t slot = 0; slot < XUserMaxUserCount; slot++) {
      std::string name = fmt::format("Slot {:d}", slot + 1);
      slot_data_.push_back({slot, name});
    }
  }

  virtual ~SigninDialog() {}

  void OnDraw(ImGuiIO& io) override {
    bool first_draw = false;
    if (!has_opened_) {
      ImGui::OpenPopup(title_.c_str());
      has_opened_ = true;
      first_draw = true;
      ReloadProfiles(true);
    }
    if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      auto profile_manager = kernel_state()->xam_state()->profile_manager();
      auto profiles = profile_manager->GetProfiles();

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

        if (slot == 0xFF || xuid == 0 || profiles->count(xuid) == 0) {
          float ypos = ImGui::GetCursorPosY();
          ImGui::SetCursorPosY(ypos + ImGui::GetTextLineHeight() * 5);
        } else {
          const X_XAMACCOUNTINFO* account = &profiles->at(xuid);
          xeDrawProfileContent(imgui_drawer(), xuid, slot, account, nullptr);
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
          bool valid = profile_manager->IsGamertagValid(gamertag_string);

          ImGui::BeginDisabled(!valid);
          if (ImGui::Button("Create")) {
            profile_manager->CreateProfile(gamertag_string, false);
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
        profile_manager->LoginMultiple(profile_map);

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

  void ReloadProfiles(bool first_draw) {
    auto profile_manager = kernel_state()->xam_state()->profile_manager();
    auto profiles = profile_manager->GetProfiles();

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
          if (chosen_slots_[j] != 0xFF && slot == chosen_slots_[j]) {
            chosen_xuids_[j] = xuid;
          }
        }
      }
    }
  }

 private:
  bool has_opened_ = false;
  std::string title_;
  uint32_t users_needed_ = 1;
  uint32_t last_user_ = 0;

  std::vector<std::pair<uint8_t, std::string>> slot_data_;
  std::vector<std::pair<uint64_t, std::string>> profile_data_;
  uint8_t chosen_slots_[XUserMaxUserCount] = {};
  uint64_t chosen_xuids_[XUserMaxUserCount] = {};

  bool creating_profile_ = false;
  char gamertag_[16] = "";
};

dword_result_t XamShowSigninUI_entry(dword_t users_needed, dword_t unk_mask) {
  // Mask values vary. Probably matching user types? Local/remote?
  // Games seem to sit and loop until we trigger sign in notification.
  if (users_needed != 1 && users_needed != 2 && users_needed != 4) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadlessAsync([users_needed]() {
      std::map<uint8_t, uint64_t> xuids;

      for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
        UserProfile* profile = kernel_state()->xam_state()->GetUserProfile(i);
        if (profile) {
          xuids[i] = profile->xuid();
          if (xuids.size() >= users_needed) break;
        }
      }

      kernel_state()->xam_state()->profile_manager()->LoginMultiple(xuids);
    });
  }

  auto close = [](SigninDialog* dialog) -> void {};

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialogAsync<SigninDialog>(
      new SigninDialog(imgui_drawer, users_needed), close);
}
DECLARE_XAM_EXPORT1(XamShowSigninUI, kUserProfiles, kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(UI);

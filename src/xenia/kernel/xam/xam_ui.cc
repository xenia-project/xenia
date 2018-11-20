/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "third_party/imgui/imgui.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/window.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

std::atomic<int> xam_dialogs_shown_ = {0};

SHIM_CALL XamIsUIActive_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  XELOGD("XamIsUIActive()");
  SHIM_SET_RETURN_32(xam_dialogs_shown_ > 0 ? 1 : 0);
}

class MessageBoxDialog : public xe::ui::ImGuiDialog {
 public:
  MessageBoxDialog(xe::ui::Window* window, std::wstring title,
                   std::wstring description, std::vector<std::wstring> buttons,
                   uint32_t default_button, uint32_t* out_chosen_button)
      : ImGuiDialog(window),
        title_(xe::to_string(title)),
        description_(xe::to_string(description)),
        buttons_(std::move(buttons)),
        default_button_(default_button),
        out_chosen_button_(out_chosen_button) {
    if (out_chosen_button) {
      *out_chosen_button = default_button;
    }
  }

  void OnDraw(ImGuiIO& io) override {
    bool first_draw = false;
    if (!has_opened_) {
      ImGui::OpenPopup(title_.c_str());
      has_opened_ = true;
      first_draw = true;
    }
    if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("%s", description_.c_str());
      if (first_draw) {
        ImGui::SetKeyboardFocusHere();
      }
      for (size_t i = 0; i < buttons_.size(); ++i) {
        auto button_name = xe::to_string(buttons_[i]);
        if (ImGui::Button(button_name.c_str())) {
          if (out_chosen_button_) {
            *out_chosen_button_ = static_cast<uint32_t>(i);
          }
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

 private:
  bool has_opened_ = false;
  std::string title_;
  std::string description_;
  std::vector<std::wstring> buttons_;
  uint32_t default_button_ = 0;
  uint32_t* out_chosen_button_ = nullptr;
};

// http://www.se7ensins.com/forums/threads/working-xshowmessageboxui.844116/?jdfwkey=sb0vm
SHIM_CALL XamShowMessageBoxUI_shim(PPCContext* ppc_context,
                                   KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t title_ptr = SHIM_GET_ARG_32(1);
  uint32_t text_ptr = SHIM_GET_ARG_32(2);
  uint32_t button_count = SHIM_GET_ARG_32(3);
  uint32_t button_ptrs = SHIM_GET_ARG_32(4);
  uint32_t active_button = SHIM_GET_ARG_32(5);
  uint32_t flags = SHIM_GET_ARG_32(6);
  uint32_t result_ptr = SHIM_GET_ARG_32(7);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(8);

  std::wstring title;
  if (title_ptr) {
    title = xe::load_and_swap<std::wstring>(SHIM_MEM_ADDR(title_ptr));
  } else {
    title = L"";  // TODO(gibbed): default title based on flags?
  }
  auto text = xe::load_and_swap<std::wstring>(SHIM_MEM_ADDR(text_ptr));
  std::vector<std::wstring> buttons;
  std::wstring all_buttons;
  for (uint32_t j = 0; j < button_count; ++j) {
    uint32_t button_ptr = SHIM_MEM_32(button_ptrs + j * 4);
    auto button = xe::load_and_swap<std::wstring>(SHIM_MEM_ADDR(button_ptr));
    all_buttons.append(button);
    if (j + 1 < button_count) {
      all_buttons.append(L" | ");
    }
    buttons.push_back(button);
  }

  XELOGD(
      "XamShowMessageBoxUI(%d, %.8X(%S), %.8X(%S), %d, %.8X(%S), %d, %X, %.8X, "
      "%.8X)",
      user_index, title_ptr, title.c_str(), text_ptr, text.c_str(),
      button_count, button_ptrs, all_buttons.c_str(), active_button, flags,
      result_ptr, overlapped_ptr);

  uint32_t chosen_button;
  if (FLAGS_headless) {
    // Auto-pick the focused button.
    chosen_button = active_button;
  } else {
    auto display_window = kernel_state->emulator()->display_window();
    xe::threading::Fence fence;
    display_window->loop()->PostSynchronous([&]() {
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
      (new MessageBoxDialog(display_window, title, text, buttons, active_button,
                            &chosen_button))
          ->Then(&fence);
    });
    ++xam_dialogs_shown_;
    fence.Wait();
    --xam_dialogs_shown_;
  }
  SHIM_SET_MEM_32(result_ptr, chosen_button);

  kernel_state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
  SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
}

class KeyboardInputDialog : public xe::ui::ImGuiDialog {
 public:
  KeyboardInputDialog(xe::ui::Window* window, std::wstring title,
                      std::wstring description, std::wstring default_text,
                      std::wstring* out_text, size_t max_length)
      : ImGuiDialog(window),
        title_(xe::to_string(title)),
        description_(xe::to_string(description)),
        default_text_(xe::to_string(default_text)),
        out_text_(out_text),
        max_length_(max_length) {
    if (out_text_) {
      *out_text_ = default_text;
    }
    text_buffer_.resize(max_length);
    std::strncpy(text_buffer_.data(), default_text_.c_str(),
                 std::min(text_buffer_.size() - 1, default_text_.size()));
  }

  void OnDraw(ImGuiIO& io) override {
    bool first_draw = false;
    if (!has_opened_) {
      ImGui::OpenPopup(title_.c_str());
      has_opened_ = true;
      first_draw = true;
    }
    if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", description_.c_str());
      if (first_draw) {
        ImGui::SetKeyboardFocusHere();
      }
      if (ImGui::InputText("##body", text_buffer_.data(), text_buffer_.size(),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (out_text_) {
          *out_text_ = xe::to_wstring(text_buffer_.data());
        }
        ImGui::CloseCurrentPopup();
        Close();
      }
      if (ImGui::Button("OK")) {
        if (out_text_) {
          *out_text_ = xe::to_wstring(text_buffer_.data());
        }
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
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
  std::wstring* out_text_ = nullptr;
  std::vector<char> text_buffer_;
  size_t max_length_ = 0;
};

// http://www.se7ensins.com/forums/threads/release-how-to-use-xshowkeyboardui-release.906568/
dword_result_t XamShowKeyboardUI(dword_t user_index, dword_t flags,
                                 lpwstring_t default_text, lpwstring_t title,
                                 lpwstring_t description, lpwstring_t buffer,
                                 dword_t buffer_length,
                                 pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!buffer) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (FLAGS_headless) {
    // Redirect default_text back into the buffer.
    std::memset(buffer, 0, buffer_length * 2);
    if (default_text) {
      xe::store_and_swap<std::wstring>(buffer, default_text.value());
    }

    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(overlapped, X_ERROR_SUCCESS);
      return X_ERROR_IO_PENDING;
    } else {
      return X_ERROR_SUCCESS;
    }
  }

  std::wstring out_text;

  auto display_window = kernel_state()->emulator()->display_window();
  xe::threading::Fence fence;
  display_window->loop()->PostSynchronous([&]() {
    (new KeyboardInputDialog(display_window, title ? title.value() : L"",
                             description ? description.value() : L"",
                             default_text ? default_text.value() : L"",
                             &out_text, buffer_length))
        ->Then(&fence);
  });
  ++xam_dialogs_shown_;
  fence.Wait();
  --xam_dialogs_shown_;

  // Zero the output buffer.
  std::memset(buffer, 0, buffer_length * 2);

  // Truncate the string.
  out_text = out_text.substr(0, buffer_length - 1);
  xe::store_and_swap<std::wstring>(buffer, out_text);

  if (overlapped) {
    kernel_state()->CompleteOverlappedImmediate(overlapped, X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  } else {
    return X_ERROR_SUCCESS;
  }
}
DECLARE_XAM_EXPORT1(XamShowKeyboardUI, kUI, kImplemented);

dword_result_t XamShowDeviceSelectorUI(dword_t user_index, dword_t content_type,
                                       dword_t content_flags,
                                       qword_t total_requested,
                                       lpdword_t device_id_ptr,
                                       pointer_t<XAM_OVERLAPPED> overlapped) {
  // NOTE: 0xF00D0000 magic from xam_content.cc
  switch (content_type) {
    case 1:  // save game
      *device_id_ptr = 0xF00D0000 | 0x0001;
      break;
    case 2:  // marketplace
      *device_id_ptr = 0xF00D0000 | 0x0002;
      break;
    case 3:  // title/publisher update?
      *device_id_ptr = 0xF00D0000 | 0x0003;
      break;
    default:
      assert_unhandled_case(content_type);
      *device_id_ptr = 0xF00D0000 | 0x0001;
      break;
  }

  if (overlapped) {
    kernel_state()->CompleteOverlappedImmediate(overlapped, X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  } else {
    return X_ERROR_SUCCESS;
  }
}
DECLARE_XAM_EXPORT1(XamShowDeviceSelectorUI, kUI, kImplemented);

SHIM_CALL XamShowDirtyDiscErrorUI_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);

  XELOGD("XamShowDirtyDiscErrorUI(%d)", user_index);

  if (FLAGS_headless) {
    assert_always();
    exit(1);
    return;
  }

  auto display_window = kernel_state->emulator()->display_window();
  xe::threading::Fence fence;
  display_window->loop()->PostSynchronous([&]() {
    xe::ui::ImGuiDialog::ShowMessageBox(
        display_window, "Disc Read Error",
        "There's been an issue reading content from the game disc.\nThis is "
        "likely caused by bad or unimplemented file IO calls.")
        ->Then(&fence);
  });
  ++xam_dialogs_shown_;
  fence.Wait();
  --xam_dialogs_shown_;

  // This is death, and should never return.
  // TODO(benvanik): cleaner exit.
  exit(1);
}

void RegisterUIExports(xe::cpu::ExportResolver* export_resolver,
                       KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", XamIsUIActive, state);
  SHIM_SET_MAPPING("xam.xex", XamShowMessageBoxUI, state);
  SHIM_SET_MAPPING("xam.xex", XamShowDirtyDiscErrorUI, state);
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

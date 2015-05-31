/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam_private.h"
#include "xenia/xbox.h"

#include <commctrl.h>

namespace xe {
namespace kernel {

SHIM_CALL XamIsUIActive_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  XELOGD("XamIsUIActive()");
  SHIM_SET_RETURN_32(0);
}

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

  auto title = xe::load_and_swap<std::wstring>(SHIM_MEM_ADDR(title_ptr));
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
    TASKDIALOGCONFIG config = {0};
    config.cbSize = sizeof(config);
    config.hInstance = GetModuleHandle(nullptr);
    config.hwndParent = kernel_state->emulator()->main_window()->hwnd();
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION |   // esc to exit
                     TDF_POSITION_RELATIVE_TO_WINDOW;  // center in window
    config.dwCommonButtons = 0;
    config.pszWindowTitle = title.c_str();
    switch (flags & 0xF) {
      case 0:
        config.pszMainIcon = nullptr;
        break;
      case 1:
        config.pszMainIcon = TD_ERROR_ICON;
        break;
      case 2:
        config.pszMainIcon = TD_WARNING_ICON;
        break;
      case 3:
        config.pszMainIcon = TD_INFORMATION_ICON;
        break;
    }
    config.pszMainInstruction = text.c_str();
    config.pszContent = nullptr;
    std::vector<TASKDIALOG_BUTTON> taskdialog_buttons;
    for (uint32_t i = 0; i < button_count; ++i) {
      taskdialog_buttons.push_back({1000 + int(i), buttons[i].c_str()});
    }
    config.pButtons = taskdialog_buttons.data();
    config.cButtons = button_count;
    config.nDefaultButton = active_button;
    int button_pressed = 0;
    TaskDialogIndirect(&config, &button_pressed, nullptr, nullptr);
    switch (button_pressed) {
      default:
        chosen_button = button_pressed - 1000;
        break;
      case IDCANCEL:
        // User cancelled, just pick default.
        chosen_button = active_button;
        break;
    }
  }
  SHIM_SET_MEM_32(result_ptr, chosen_button);

  kernel_state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
  SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
}

SHIM_CALL XamShowDirtyDiscErrorUI_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);

  XELOGD("XamShowDirtyDiscErrorUI(%d)", user_index);

  int button_pressed = 0;
  TaskDialog(kernel_state->emulator()->main_window()->hwnd(),
             GetModuleHandle(nullptr), L"Disc Read Error",
             L"Game is claiming to be unable to read game data!", nullptr,
             TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, &button_pressed);

  // This is death, and should never return.
  assert_always();
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterUIExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", XamIsUIActive, state);
  SHIM_SET_MAPPING("xam.xex", XamShowMessageBoxUI, state);
  SHIM_SET_MAPPING("xam.xex", XamShowDirtyDiscErrorUI, state);
}

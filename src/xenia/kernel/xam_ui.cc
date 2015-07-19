/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "el/elemental_forms.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam_private.h"
#include "xenia/ui/window.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

SHIM_CALL XamIsUIActive_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  XELOGD("XamIsUIActive()");
  SHIM_SET_RETURN_32(el::ModalWindow::is_any_visible());
}

class MessageBoxWindow : public el::ModalWindow {
 public:
  MessageBoxWindow(xe::threading::Fence* fence)
      : ModalWindow([fence]() { fence->Signal(); }) {}
  ~MessageBoxWindow() override = default;

  // TODO(benvanik): icon.
  void Show(el::Element* root_element, std::wstring title, std::wstring message,
            std::vector<std::wstring> buttons, uint32_t default_button,
            uint32_t* out_chosen_button) {
    title_ = std::move(title);
    message_ = std::move(message);
    buttons_ = std::move(buttons);
    default_button_ = default_button;
    out_chosen_button_ = out_chosen_button;

    // In case we are cancelled, always set default button.
    *out_chosen_button_ = default_button;

    ModalWindow::Show(root_element);

    EnsureFocus();
  }

 protected:
  void BuildUI() override {
    using namespace el::dsl;

    set_text(xe::to_string(title_));

    auto button_bar = LayoutBoxNode().distribution_position(
        LayoutDistributionPosition::kRightBottom);
    for (int32_t i = 0; i < buttons_.size(); ++i) {
      button_bar.child(ButtonNode(xe::to_string(buttons_[i]).c_str())
                           .id(100 + i)
                           .data(100 + i));
    }

    LoadNodeTree(
        LayoutBoxNode()
            .axis(Axis::kY)
            .gravity(Gravity::kAll)
            .position(LayoutPosition::kLeftTop)
            .distribution(LayoutDistribution::kAvailable)
            .distribution_position(LayoutDistributionPosition::kLeftTop)
            .child(LabelNode(xe::to_string(message_).c_str()))
            .child(button_bar));

    auto default_button = GetElementById<el::Button>(100 + default_button_);
    el::Button::set_auto_focus_state(true);
    default_button->set_focus(el::FocusReason::kNavigation);
  }

  bool OnEvent(const el::Event& ev) override {
    if (ev.target->IsOfType<el::Button>() && ev.type == el::EventType::kClick) {
      int data_value = ev.target->data.as_integer();
      if (data_value) {
        *out_chosen_button_ = data_value - 100;
      }
      Die();
      return true;
    }
    return ModalWindow::OnEvent(ev);
  }

  std::wstring title_;
  std::wstring message_;
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
    auto display_window = kernel_state->emulator()->display_window();
    xe::threading::Fence fence;
    display_window->loop()->PostSynchronous([&]() {
      auto root_element = display_window->root_element();
      auto window = new MessageBoxWindow(&fence);
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
      window->Show(root_element, title, text, buttons, active_button,
                   &chosen_button);
    });
    fence.Wait();
  }
  SHIM_SET_MEM_32(result_ptr, chosen_button);

  kernel_state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
  SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
}

class DirtyDiscWindow : public el::ModalWindow {
 public:
  DirtyDiscWindow(xe::threading::Fence* fence)
      : ModalWindow([fence]() { fence->Signal(); }) {}
  ~DirtyDiscWindow() override = default;

 protected:
  void BuildUI() override {
    using namespace el::dsl;

    set_text("Disc Read Error");

    LoadNodeTree(
        LayoutBoxNode()
            .axis(Axis::kY)
            .gravity(Gravity::kAll)
            .position(LayoutPosition::kLeftTop)
            .distribution(LayoutDistribution::kAvailable)
            .distribution_position(LayoutDistributionPosition::kLeftTop)
            .child(LabelNode(
                "There's been an issue reading content from the game disc."))
            .child(LabelNode(
                "This is likely caused by bad or unimplemented file IO "
                "calls."))
            .child(LayoutBoxNode()
                       .distribution_position(
                           LayoutDistributionPosition::kRightBottom)
                       .child(ButtonNode("Exit").id("exit_button"))));
  }

  bool OnEvent(const el::Event& ev) override {
    if (ev.target->id() == TBIDC("exit_button") &&
        ev.type == el::EventType::kClick) {
      Die();
      return true;
    }
    return ModalWindow::OnEvent(ev);
  }
};

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
    auto root_element = display_window->root_element();
    auto window = new DirtyDiscWindow(&fence);
    window->Show(root_element);
  });
  fence.Wait();

  // This is death, and should never return.
  // TODO(benvanik): cleaner exit.
  assert_always();
  exit(1);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterUIExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", XamIsUIActive, state);
  SHIM_SET_MAPPING("xam.xex", XamShowMessageBoxUI, state);
  SHIM_SET_MAPPING("xam.xex", XamShowDirtyDiscErrorUI, state);
}

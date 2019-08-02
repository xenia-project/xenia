/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

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

extern std::atomic<int> xam_dialogs_shown_;

struct X_NUI_DEVICE_STATUS {
  xe::be<uint32_t> unk0;
  xe::be<uint32_t> unk1;
  xe::be<uint32_t> unk2;
  xe::be<uint32_t> status;
  xe::be<uint32_t> unk4;
  xe::be<uint32_t> unk5;
};
static_assert(sizeof(X_NUI_DEVICE_STATUS) == 24, "Size matters");

void XamNuiGetDeviceStatus(pointer_t<X_NUI_DEVICE_STATUS> status_ptr) {
  status_ptr.Zero();
  status_ptr->status = 0;  // Not connected.
}
DECLARE_XAM_EXPORT1(XamNuiGetDeviceStatus, kNone, kStub);

dword_result_t XamShowNuiTroubleshooterUI(unknown_t unk1, unknown_t unk2,
                                          unknown_t unk3) {
  // unk1 is 0xFF - possibly user index?
  // unk2, unk3 appear to always be zero.

  if (FLAGS_headless) {
    return 0;
  }

  auto display_window = kernel_state()->emulator()->display_window();
  xe::threading::Fence fence;
  display_window->loop()->PostSynchronous([&]() {
    xe::ui::ImGuiDialog::ShowMessageBox(
        display_window, "NUI Troubleshooter",
        "The game has indicated there is a problem with NUI (Kinect).")
        ->Then(&fence);
  });
  ++xam_dialogs_shown_;
  fence.Wait();
  --xam_dialogs_shown_;

  return 0;
}
DECLARE_XAM_EXPORT1(XamShowNuiTroubleshooterUI, kNone, kStub);

void RegisterNuiExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

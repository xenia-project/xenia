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
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

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
DECLARE_XAM_EXPORT(XamNuiGetDeviceStatus, ExportTag::kStub);

void RegisterNuiExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

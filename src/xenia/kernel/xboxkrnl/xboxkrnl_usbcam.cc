/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t XUsbcamCreate(unknown_t unk1,  // E
                             unknown_t unk2,  // 0x4B000
                             lpunknown_t unk3_ptr) {
  // 0 = success.
  return X_ERROR_DEVICE_NOT_CONNECTED;
}
DECLARE_XBOXKRNL_EXPORT(XUsbcamCreate, ExportTag::kStub);

dword_result_t XUsbcamGetState() {
  // 0 = not connected.
  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XUsbcamGetState, ExportTag::kStub);

void RegisterUsbcamExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

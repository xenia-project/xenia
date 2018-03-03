/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

dword_result_t XamAvatarInitialize(
    dword_t unk1,              // 1, 4, etc
    dword_t unk2,              // 0 or 1
    dword_t processor_number,  // for thread creation?
    lpdword_t function_ptrs,   // 20b, 5 pointers
    lpunknown_t unk5,          // ptr in data segment
    dword_t unk6               // flags - 0x00300000, 0x30, etc
) {
  // Negative to fail. Game should immediately call XamAvatarShutdown.
  return ~0u;
}
DECLARE_XAM_EXPORT(XamAvatarInitialize, ExportTag::kStub);

void XamAvatarShutdown() {
  // No-op.
}
DECLARE_XAM_EXPORT(XamAvatarShutdown, ExportTag::kStub);

void RegisterAvatarExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

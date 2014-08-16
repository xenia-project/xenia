/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/common.h>
#include <xenia/core.h>
#include <xenia/xbox.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xam_private.h>
#include <xenia/kernel/util/shim_utils.h>


namespace xe {
namespace kernel {


// TODO(benvanik): actually check to see if these are the same.
void xeVdQueryVideoMode(X_VIDEO_MODE* video_mode);
SHIM_CALL XGetVideoMode_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t video_mode_ptr = SHIM_GET_ARG_32(0);
  X_VIDEO_MODE* video_mode = (X_VIDEO_MODE*)SHIM_MEM_ADDR(video_mode_ptr);
  xeVdQueryVideoMode(video_mode);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterVideoExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", XGetVideoMode, state);
}

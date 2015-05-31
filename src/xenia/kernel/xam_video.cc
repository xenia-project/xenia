/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

// TODO(benvanik): actually check to see if these are the same.
void xeVdQueryVideoMode(X_VIDEO_MODE* video_mode);
SHIM_CALL XGetVideoMode_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t video_mode_ptr = SHIM_GET_ARG_32(0);
  X_VIDEO_MODE* video_mode = (X_VIDEO_MODE*)SHIM_MEM_ADDR(video_mode_ptr);

  XELOGD("XGetVideoMode(%.8X)", video_mode_ptr);

  xeVdQueryVideoMode(video_mode);
}

SHIM_CALL XGetVideoCapabilities_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  XELOGD("XGetVideoCapabilities()");
  SHIM_SET_RETURN_32(0);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterVideoExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", XGetVideoCapabilities, state);
  SHIM_SET_MAPPING("xam.xex", XGetVideoMode, state);
}

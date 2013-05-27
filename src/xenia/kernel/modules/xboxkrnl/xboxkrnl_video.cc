/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_video.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/modules/xboxkrnl/kernel_state.h>
#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_private.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xboxkrnl {


VOID xeVdQueryVideoMode(X_VIDEO_MODE *video_mode, bool swap) {
  if (video_mode == NULL) {
    return;
  }

  // TODO: get info from actual display
  video_mode->display_width  = 1280;
  video_mode->display_height = 720;
  video_mode->is_interlaced  = 0;
  video_mode->is_widescreen  = 1;
  video_mode->is_hi_def      = 1;
  video_mode->refresh_rate   = 60.0f;
  video_mode->video_standard = 1;

  if (swap) {
    video_mode->display_width  = XESWAP32BE(video_mode->display_width);
    video_mode->display_height = XESWAP32BE(video_mode->display_height);
    video_mode->is_interlaced  = XESWAP32BE(video_mode->is_interlaced);
    video_mode->is_widescreen  = XESWAP32BE(video_mode->is_widescreen);
    video_mode->is_hi_def      = XESWAP32BE(video_mode->is_hi_def);
    video_mode->refresh_rate   = XESWAPF32BE(video_mode->refresh_rate);
    video_mode->video_standard = XESWAP32BE(video_mode->video_standard);
  }
}

SHIM_CALL VdQueryVideoMode_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  X_VIDEO_MODE *video_mode = (X_VIDEO_MODE*)SHIM_MEM_ADDR(SHIM_GET_ARG_32(0));
  xeVdQueryVideoMode(video_mode, true);
}


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterVideoExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", VdQueryVideoMode, state);
}

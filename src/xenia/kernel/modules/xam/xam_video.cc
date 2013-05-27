/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xam/xam_video.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/modules/xam/xam_private.h>
#include <xenia/kernel/modules/xam/xam_state.h>

#include <xenia/kernel/modules/modules.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {
namespace xam {


SHIM_CALL XGetVideoMode_shim(
	xe_ppc_state_t* ppc_state, XamState* state) {
  xe::kernel::xboxkrnl::X_VIDEO_MODE *video_mode = (xe::kernel::xboxkrnl::X_VIDEO_MODE*)SHIM_MEM_ADDR(SHIM_GET_ARG_32(0));
  xeVdQueryVideoMode(video_mode, true);
}


}  // namespace xam
}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterVideoExports(
    ExportResolver* export_resolver, XamState* state) {
  SHIM_SET_MAPPING("xam.xex", XGetVideoMode, state);
}

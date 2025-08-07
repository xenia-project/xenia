/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_video.h"

DEFINE_bool(enable_3d_mode, false,
            "Enables usage of stereoscopic mode in titles.", "Video");

namespace xe {
namespace kernel {
namespace xam {

enum X_VIDEO_CAPABILITIES : uint32_t {
  X_VIDEO_ENABLE_STEREOSCOPIC_3D_MODE = 0x1,
  X_VIDEO_ENABLE_STEREOSCOPIC_720P_60HZ_MODE = 0x10
};

void XGetVideoMode_entry(pointer_t<X_VIDEO_MODE> video_mode) {
  // TODO(benvanik): actually check to see if these are the same.
  xboxkrnl::VdQueryVideoMode(video_mode);
}
DECLARE_XAM_EXPORT1(XGetVideoMode, kVideo, ExportTag::kSketchy);

dword_result_t XGetVideoCapabilities_entry() {
  uint32_t capabilities = 0;

  if (cvars::enable_3d_mode) {
    capabilities =
        static_cast<uint32_t>(
            X_VIDEO_CAPABILITIES::X_VIDEO_ENABLE_STEREOSCOPIC_3D_MODE) |
        static_cast<uint32_t>(
            X_VIDEO_CAPABILITIES::X_VIDEO_ENABLE_STEREOSCOPIC_720P_60HZ_MODE);
  }

  return capabilities;
}
DECLARE_XAM_EXPORT1(XGetVideoCapabilities, kVideo, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Video);

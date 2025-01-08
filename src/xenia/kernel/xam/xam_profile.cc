/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"

namespace xe {
namespace kernel {
namespace xam {

dword_result_t XamProfileOpen_entry(qword_t xuid, lpstring_t mount_path) {
  const bool result =
      kernel_state()->xam_state()->profile_manager()->MountProfile(
          xuid, mount_path.value());

  return result ? X_ERROR_SUCCESS : X_ERROR_INVALID_PARAMETER;
}
DECLARE_XAM_EXPORT1(XamProfileOpen, kNone, kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Profile);
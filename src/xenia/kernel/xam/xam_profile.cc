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

dword_result_t XamProfileFindAccount_entry(
    qword_t offline_xuid, pointer_t<X_XAMACCOUNTINFO> account_ptr,
    lpdword_t device_id) {
  if (!account_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  account_ptr.Zero();

  const auto& account =
      kernel_state()->xam_state()->profile_manager()->GetAccount(offline_xuid);

  if (!account) {
    return X_ERROR_NO_SUCH_USER;
  }

  std::memcpy(account_ptr, &account, sizeof(X_XAMACCOUNTINFO));

  xe::string_util::copy_and_swap_truncating(
      account_ptr->gamertag, account->gamertag, sizeof(account->gamertag));

  if (device_id) {
    *device_id = 1;
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamProfileFindAccount, kUserProfiles, kImplemented);

dword_result_t XamProfileOpen_entry(qword_t xuid, lpstring_t mount_path,
                                    dword_t flags, lpvoid_t content_data) {
  /* Notes:
      - If xuid is not local then returns X_ERROR_INVALID_PARAMETER
  */
  const bool result =
      kernel_state()->xam_state()->profile_manager()->MountProfile(
          xuid, mount_path.value());

  return result ? X_ERROR_SUCCESS : X_ERROR_INVALID_PARAMETER;
}
DECLARE_XAM_EXPORT1(XamProfileOpen, kNone, kImplemented);

dword_result_t XamProfileCreate_entry(dword_t flags, lpdword_t device_id,
                                      qword_t xuid,
                                      pointer_t<X_XAMACCOUNTINFO> account,
                                      dword_t unk1, dword_t unk2, dword_t unk3,
                                      dword_t unk4) {
  // **unk4

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamProfileCreate, kNone, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Profile);
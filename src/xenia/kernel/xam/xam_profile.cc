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

dword_result_t XamProfileCreate_entry(
    dword_t flags, lpdword_t device_id, qword_t xuid,
    pointer_t<X_XAMACCOUNTINFO> account,
    pointer_t<X_USER_PAYMENT_INFO> payment_info,
    pointer_t<X_PASSPORT_SESSION_TOKEN> user_token,
    pointer_t<X_PASSPORT_SESSION_TOKEN> owner_token, lpvoid_t unk1) {
  if (device_id) {
    *device_id = 0x1;
  }

  if (xuid != 0) {
    assert_always();
    return X_E_INVALIDARG;
  }

  X_XAMACCOUNTINFO account_info_data;
  memcpy(&account_info_data, account, sizeof(X_XAMACCOUNTINFO));
  xe::copy_and_swap<char16_t>(account_info_data.gamertag,
                              account_info_data.gamertag, 16);

  bool result = kernel_state()->xam_state()->profile_manager()->CreateProfile(
      &account_info_data, xuid);

  return result ? X_ERROR_SUCCESS : X_ERROR_INVALID_PARAMETER;
}
DECLARE_XAM_EXPORT1(XamProfileCreate, kNone, kSketchy);

dword_result_t XamProfileClose_entry(lpstring_t mount_name) {
  std::string guest_name = mount_name.value();
  const bool result =
      kernel_state()->file_system()->UnregisterDevice(guest_name + ':');

  return result ? X_ERROR_SUCCESS : X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamProfileClose, kNone, kStub);

dword_result_t XamProfileGetCreationStatus_entry(lpvoid_t unk1,
                                                 lpqword_t offline_xuid) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamProfileGetCreationStatus, kNone, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Profile);

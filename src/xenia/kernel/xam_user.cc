/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam_user.h>

#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xam_private.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {


SHIM_CALL XamUserGetXUID_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t unk = SHIM_GET_ARG_32(1);
  uint32_t xuid_ptr = SHIM_GET_ARG_32(2);

  XELOGD(
      "XamUserGetXUID(%d, %.8X, %.8X)",
      user_index,
      unk,
      xuid_ptr);

  if (user_index == 0) {
    if (xuid_ptr) {
      SHIM_SET_MEM_32(xuid_ptr, 0xBABEBABE);
    }
    SHIM_SET_RETURN_32(0);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_NO_SUCH_USER);
  }
}


SHIM_CALL XamUserGetSigninState_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);

  XELOGD(
      "XamUserGetSigninState(%d)",
      user_index);

  // Lie and say we are signed in, but local-only.
  // This should keep games from asking us to sign in and also keep them
  // from initializing the network.
  if (user_index == 0) {
    SHIM_SET_RETURN_64(1);
  } else {
    SHIM_SET_RETURN_64(0);
  }
}


SHIM_CALL XamUserGetSigninInfo_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t flags = SHIM_GET_ARG_32(1);
  uint32_t info_ptr = SHIM_GET_ARG_32(2);

  XELOGD(
      "XamUserGetSigninInfo(%d, %.8X, %.8X)",
      user_index, flags, info_ptr);

  if (user_index == 0) {
    SHIM_SET_MEM_32(info_ptr + 0, 0xBABEBABE); // XUID
    SHIM_SET_MEM_32(info_ptr + 4, 1); // maybe zero?
    SHIM_SET_MEM_32(info_ptr + 8, 1); // signin state, same as above
    SHIM_SET_MEM_32(info_ptr + 12, 0); // ?
    SHIM_SET_MEM_32(info_ptr + 16, 0); // ?
    char* buffer = (char*)SHIM_MEM_ADDR(info_ptr + 20);
    xestrncpya(buffer, 0x10, "User0", 5);
    SHIM_SET_RETURN_32(0);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_NO_SUCH_USER);
  }
}


SHIM_CALL XamUserGetName_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(1);
  uint32_t buffer_len = SHIM_GET_ARG_32(2);

  XELOGD(
      "XamUserGetName(%d, %.8X, %d)",
      user_index, buffer_ptr, buffer_len);

  if (user_index == 0) {
    char* buffer = (char*)SHIM_MEM_ADDR(buffer_ptr);
    xestrncpya(buffer, buffer_len, "User0", 5);
    SHIM_SET_RETURN_32(0);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_NO_SUCH_USER);
  }
}


// http://freestyledash.googlecode.com/svn/trunk/Freestyle/Tools/Generic/xboxtools.cpp
SHIM_CALL XamUserReadProfileSettings_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t title_id = SHIM_GET_ARG_32(0);
  uint32_t user_index = SHIM_GET_ARG_32(1);
  uint32_t unk_0 = SHIM_GET_ARG_32(2);
  uint32_t unk_1 = SHIM_GET_ARG_32(3);
  uint32_t setting_count = SHIM_GET_ARG_32(4);
  uint32_t setting_ids_ptr = SHIM_GET_ARG_32(5);
  uint32_t buffer_size_ptr = SHIM_GET_ARG_32(6);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(7);
  // arg8 is in stack!
  uint32_t sp = (uint32_t)ppc_state->r[1];
  uint32_t overlapped_ptr = SHIM_MEM_32(sp + 0x54);

  uint32_t buffer_size = SHIM_MEM_32(buffer_size_ptr);

  XELOGD(
      "XamUserReadProfileSettings(%.8X, %d, %d, %d, %d, %.8X, %.8X(%d), %.8X, %.8X)",
      title_id, user_index, unk_0, unk_1, setting_count, setting_ids_ptr,
      buffer_size_ptr, buffer_size, buffer_ptr, overlapped_ptr);

  // Title ID = 0 means us.
  // 0xfffe07d1 = profile?

  // TODO(benvanik): implement overlapped support
  XEASSERTZERO(overlapped_ptr);

  // First call asks for size (fill buffer_size_ptr).
  // Second call asks for buffer contents with that size.

  // Result buffer is:
  // uint32_t setting_count
  // [repeated X_USER_PROFILE_SETTING structs]

  typedef union {
    struct {
      uint32_t id : 14;
      uint32_t unk : 2;
      uint32_t size : 12;
      uint32_t type : 4;
    };
    uint32_t value;
  } x_profile_setting_t;

  // Compute sizes.
  uint32_t size_needed = 4;
  for (uint32_t n = 0; n < setting_count; n++) {
    x_profile_setting_t setting;
    setting.value = SHIM_MEM_32(setting_ids_ptr + n * 4);
    size_needed += setting.size;
  }
  SHIM_SET_MEM_32(buffer_size_ptr, size_needed);
  if (buffer_size < size_needed) {
    SHIM_SET_RETURN_32(X_ERROR_INSUFFICIENT_BUFFER);
    return;
  }

  // TODO(benvanik): read profile data.
  // For now, just return signed out.
  SHIM_SET_RETURN_32(X_ERROR_NOT_FOUND);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterUserExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", XamUserGetXUID, state);
  SHIM_SET_MAPPING("xam.xex", XamUserGetSigninState, state);
  SHIM_SET_MAPPING("xam.xex", XamUserGetSigninInfo, state);
  SHIM_SET_MAPPING("xam.xex", XamUserGetName, state);
  SHIM_SET_MAPPING("xam.xex", XamUserReadProfileSettings, state);
}

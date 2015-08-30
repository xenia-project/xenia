/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/objects/xenumerator.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

SHIM_CALL XamUserGetXUID_shim(PPCContext* ppc_context,
                              KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t unk = SHIM_GET_ARG_32(1);
  uint32_t xuid_ptr = SHIM_GET_ARG_32(2);

  XELOGD("XamUserGetXUID(%d, %.8X, %.8X)", user_index, unk, xuid_ptr);

  if (user_index == 0) {
    const auto& user_profile = kernel_state->user_profile();
    if (xuid_ptr) {
      SHIM_SET_MEM_64(xuid_ptr, user_profile->xuid());
    }
    SHIM_SET_RETURN_32(0);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_NO_SUCH_USER);
  }
}

SHIM_CALL XamUserGetSigninState_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);

  XELOGD("XamUserGetSigninState(%d)", user_index);

  // Yield, as some games spam this.
  xe::threading::MaybeYield();

  // Lie and say we are signed in, but local-only.
  // This should keep games from asking us to sign in and also keep them
  // from initializing the network.
  if (user_index == 0 || (user_index & 0xFF) == 0xFF) {
    const auto& user_profile = kernel_state->user_profile();
    auto signin_state = user_profile->signin_state();
    SHIM_SET_RETURN_32(signin_state);
  } else {
    SHIM_SET_RETURN_32(0);
  }
}

SHIM_CALL XamUserGetSigninInfo_shim(PPCContext* ppc_context,
                                    KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t flags = SHIM_GET_ARG_32(1);
  uint32_t info_ptr = SHIM_GET_ARG_32(2);

  XELOGD("XamUserGetSigninInfo(%d, %.8X, %.8X)", user_index, flags, info_ptr);

  if (user_index == 0) {
    const auto& user_profile = kernel_state->user_profile();
    SHIM_SET_MEM_64(info_ptr + 0, user_profile->xuid());
    SHIM_SET_MEM_32(info_ptr + 8, 0);  // maybe zero?
    SHIM_SET_MEM_32(info_ptr + 12, user_profile->signin_state());
    SHIM_SET_MEM_32(info_ptr + 16, 0);  // ?
    SHIM_SET_MEM_32(info_ptr + 20, 0);  // ?
    char* buffer = reinterpret_cast<char*>(SHIM_MEM_ADDR(info_ptr + 24));
    std::strcpy(buffer, user_profile->name().data());
    SHIM_SET_RETURN_32(0);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_NO_SUCH_USER);
  }
}

SHIM_CALL XamUserGetName_shim(PPCContext* ppc_context,
                              KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(1);
  uint32_t buffer_len = SHIM_GET_ARG_32(2);

  XELOGD("XamUserGetName(%d, %.8X, %d)", user_index, buffer_ptr, buffer_len);

  if (user_index == 0) {
    const auto& user_profile = kernel_state->user_profile();
    char* buffer = reinterpret_cast<char*>(SHIM_MEM_ADDR(buffer_ptr));
    std::strcpy(buffer, user_profile->name().data());
    SHIM_SET_RETURN_32(0);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_NO_SUCH_USER);
  }
}

typedef struct {
  xe::be<uint32_t> setting_count;
  xe::be<uint32_t> settings_ptr;
} X_USER_READ_PROFILE_SETTINGS;
static_assert_size(X_USER_READ_PROFILE_SETTINGS, 8);

typedef struct {
  xe::be<uint32_t> from;
  xe::be<uint32_t> unk04;
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> unk0C;
  xe::be<uint32_t> setting_id;
  xe::be<uint32_t> unk14;
  uint8_t setting_data[16];
} X_USER_READ_PROFILE_SETTING;
static_assert_size(X_USER_READ_PROFILE_SETTING, 40);

// http://freestyledash.googlecode.com/svn/trunk/Freestyle/Tools/Generic/xboxtools.cpp
SHIM_CALL XamUserReadProfileSettings_shim(PPCContext* ppc_context,
                                          KernelState* kernel_state) {
  uint32_t title_id = SHIM_GET_ARG_32(0);
  uint32_t user_index = SHIM_GET_ARG_32(1);
  uint32_t unk_0 = SHIM_GET_ARG_32(2);
  uint32_t unk_1 = SHIM_GET_ARG_32(3);
  uint32_t setting_count = SHIM_GET_ARG_32(4);
  uint32_t setting_ids_ptr = SHIM_GET_ARG_32(5);
  uint32_t buffer_size_ptr = SHIM_GET_ARG_32(6);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(7);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(8);

  uint32_t buffer_size = !buffer_size_ptr ? 0 : SHIM_MEM_32(buffer_size_ptr);

  XELOGD(
      "XamUserReadProfileSettings(%.8X, %d, %d, %d, %d, %.8X, %.8X(%d), %.8X, "
      "%.8X)",
      title_id, user_index, unk_0, unk_1, setting_count, setting_ids_ptr,
      buffer_size_ptr, buffer_size, buffer_ptr, overlapped_ptr);

  assert_zero(unk_0);
  assert_zero(unk_1);

  // TODO(gibbed): why is this a thing?
  if (user_index == 255) {
    user_index = 0;
  }

  // Title ID = 0 means us.
  // 0xfffe07d1 = profile?

  if (user_index) {
    // Only support user 0.
    if (overlapped_ptr) {
      kernel_state->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_NOT_FOUND);
      SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
    } else {
      SHIM_SET_RETURN_32(X_ERROR_NOT_FOUND);
    }
    return;
  }

  const auto& user_profile = kernel_state->user_profile();

  // First call asks for size (fill buffer_size_ptr).
  // Second call asks for buffer contents with that size.

  auto setting_ids = (xe::be<uint32_t>*)SHIM_MEM_ADDR(setting_ids_ptr);

  // Compute required base size.
  uint32_t base_size_needed = sizeof(X_USER_READ_PROFILE_SETTINGS);
  base_size_needed += setting_count * sizeof(X_USER_READ_PROFILE_SETTING);

  // Compute required extra size.
  uint32_t size_needed = base_size_needed;
  bool any_missing = false;
  for (uint32_t n = 0; n < setting_count; ++n) {
    uint32_t setting_id = setting_ids[n];
    auto setting = user_profile->GetSetting(setting_id);
    if (setting) {
      auto extra_size = static_cast<uint32_t>(setting->extra_size());
      size_needed += extra_size;
    } else {
      any_missing = true;
      XELOGE("XamUserReadProfileSettings requested unimplemented setting %.8X",
             setting_id);
    }
  }

  if (any_missing) {
    // TODO(benvanik): don't fail? most games don't even check!
    if (overlapped_ptr) {
      kernel_state->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_INVALID_PARAMETER);
      SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
    } else {
      SHIM_SET_RETURN_32(X_ERROR_INVALID_PARAMETER);
    }
    return;
  }

  SHIM_SET_MEM_32(buffer_size_ptr, size_needed);
  if (!buffer_ptr || buffer_size < size_needed) {
    if (overlapped_ptr) {
      kernel_state->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_INSUFFICIENT_BUFFER);
      SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
    } else {
      SHIM_SET_RETURN_32(X_ERROR_INSUFFICIENT_BUFFER);
    }
    return;
  }

  auto out_header = reinterpret_cast<X_USER_READ_PROFILE_SETTINGS*>(
      SHIM_MEM_ADDR(buffer_ptr));
  out_header->setting_count = setting_count;
  out_header->settings_ptr = buffer_ptr + 8;

  auto out_setting = reinterpret_cast<X_USER_READ_PROFILE_SETTING*>(
      SHIM_MEM_ADDR(out_header->settings_ptr));

  size_t buffer_offset = base_size_needed;
  for (uint32_t n = 0; n < setting_count; ++n) {
    uint32_t setting_id = setting_ids[n];
    auto setting = user_profile->GetSetting(setting_id);

    std::memset(out_setting, 0, sizeof(X_USER_READ_PROFILE_SETTING));
    out_setting->from = !setting ? 0 : setting->is_title_specific() ? 2 : 1;
    out_setting->user_index = user_index;
    out_setting->setting_id = setting_id;

    if (setting) {
      buffer_offset =
          setting->Append(&out_setting->setting_data[0],
                          SHIM_MEM_ADDR(buffer_ptr), buffer_ptr, buffer_offset);
    }
    // TODO(benvanik): why did I do this?
    /*else {
      std::memset(&out_setting->setting_data[0], 0,
                  sizeof(out_setting->setting_data));
    }*/
    ++out_setting;
  }

  if (overlapped_ptr) {
    kernel_state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
    SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
  }
}

SHIM_CALL XamUserWriteProfileSettings_shim(PPCContext* ppc_context,
                                           KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t unknown = SHIM_GET_ARG_32(1);
  uint32_t setting_count = SHIM_GET_ARG_32(2);
  uint32_t settings_ptr = SHIM_GET_ARG_32(3);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(4);

  XELOGD(
      "XamUserWriteProfileSettings(%.8X, %d, %d, %d, %d, %.8X, %.8X(%d), %.8X, "
      "%.8X)",
      user_index, unknown, setting_count, settings_ptr, overlapped_ptr);

  if (!setting_count || !settings_ptr) {
    if (overlapped_ptr) {
      kernel_state->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_INVALID_PARAMETER);
      SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
    } else {
      SHIM_SET_RETURN_32(X_ERROR_INVALID_PARAMETER);
    }
    return;
  }

  if (user_index == 255) {
    user_index = 0;
  }

  if (user_index) {
    // Only support user 0.
    SHIM_SET_RETURN_32(X_ERROR_NOT_FOUND);
    return;
  }

  // TODO(benvanik): update and save settings.
  // const auto& user_profile = kernel_state->user_profile();

  if (overlapped_ptr) {
    kernel_state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
    SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
  }
}

SHIM_CALL XamUserCheckPrivilege_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t mask = SHIM_GET_ARG_32(1);
  uint32_t out_value_ptr = SHIM_GET_ARG_32(2);

  XELOGD("XamUserCheckPrivilege(%d, %.8X, %.8X)", user_index, mask,
         out_value_ptr);

  // If we deny everything, games should hopefully not try to do stuff.
  SHIM_SET_MEM_32(out_value_ptr, 0);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamUserContentRestrictionGetFlags_shim(PPCContext* ppc_context,
                                                 KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t out_flags_ptr = SHIM_GET_ARG_32(1);

  XELOGD("XamUserContentRestrictionGetFlags(%d, %.8X)", user_index,
         out_flags_ptr);

  // No restrictions?
  SHIM_SET_MEM_32(out_flags_ptr, 0);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamUserContentRestrictionGetRating_shim(PPCContext* ppc_context,
                                                  KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t unk1 = SHIM_GET_ARG_32(1);
  uint32_t out_unk2_ptr = SHIM_GET_ARG_32(2);
  uint32_t out_unk3_ptr = SHIM_GET_ARG_32(3);

  XELOGD("XamUserContentRestrictionGetRating(%d, %.8X, %.8X, %.8X)", user_index,
         unk1, out_unk2_ptr, out_unk3_ptr);

  // Some games have special case paths for 3F that differ from the failure
  // path, so my guess is that's 'don't care'.
  SHIM_SET_MEM_32(out_unk2_ptr, 0x3F);
  SHIM_SET_MEM_32(out_unk3_ptr, 0);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamUserContentRestrictionCheckAccess_shim(PPCContext* ppc_context,
                                                    KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t unk1 = SHIM_GET_ARG_32(1);
  uint32_t unk2 = SHIM_GET_ARG_32(2);
  uint32_t unk3 = SHIM_GET_ARG_32(3);
  uint32_t unk4 = SHIM_GET_ARG_32(4);
  uint32_t out_unk5_ptr = SHIM_GET_ARG_32(5);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(6);

  XELOGD(
      "XamUserContentRestrictionCheckAccess(%d, %.8X, %.8X, %.8X, %.8X, %.8X, "
      "%.8X)",
      user_index, unk1, unk2, unk3, unk4, out_unk5_ptr, overlapped_ptr);

  SHIM_SET_MEM_32(out_unk5_ptr, 1);

  if (overlapped_ptr) {
    // TODO(benvanik): does this need the access arg on it?
    kernel_state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
  }

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamUserAreUsersFriends_shim(PPCContext* ppc_context,
                                      KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t unk1 = SHIM_GET_ARG_32(1);
  uint32_t unk2 = SHIM_GET_ARG_32(2);
  uint32_t out_value_ptr = SHIM_GET_ARG_32(3);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(4);

  XELOGD("XamUserAreUsersFriends(%d, %.8X, %.8X, %.8X, %.8X)", user_index, unk1,
         unk2, out_value_ptr, overlapped_ptr);

  if (user_index == 255) {
    user_index = 0;
  }

  SHIM_SET_MEM_32(out_value_ptr, 0);

  if (user_index) {
    // Only support user 0.
    SHIM_SET_RETURN_32(X_ERROR_NOT_LOGGED_ON);
    return;
  }

  X_RESULT result = X_ERROR_SUCCESS;

  const auto& user_profile = kernel_state->user_profile();
  if (user_profile->signin_state() == 0) {
    result = X_ERROR_NOT_LOGGED_ON;
  }

  // No friends!
  SHIM_SET_MEM_32(out_value_ptr, 0);

  if (overlapped_ptr) {
    kernel_state->CompleteOverlappedImmediate(overlapped_ptr, result);
    SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
  } else {
    SHIM_SET_RETURN_32(result);
  }
}

SHIM_CALL XamShowSigninUI_shim(PPCContext* ppc_context,
                               KernelState* kernel_state) {
  uint32_t unk_0 = SHIM_GET_ARG_32(0);
  uint32_t unk_mask = SHIM_GET_ARG_32(1);

  XELOGD("XamShowSigninUI(%d, %.8X)", unk_0, unk_mask);

  // Mask values vary. Probably matching user types? Local/remote?
  // Games seem to sit and loop until we trigger this notification.
  kernel_state->BroadcastNotification(0x00000009, 0);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamUserCreateAchievementEnumerator_shim(PPCContext* ppc_context,
                                                  KernelState* kernel_state) {
  uint32_t title_id = SHIM_GET_ARG_32(0);
  uint32_t user_index = SHIM_GET_ARG_32(1);
  uint32_t xuid = SHIM_GET_ARG_32(2);
  uint32_t flags = SHIM_GET_ARG_32(3);
  uint32_t offset = SHIM_GET_ARG_32(4);
  uint32_t count = SHIM_GET_ARG_32(5);
  uint32_t buffer_size_ptr = SHIM_GET_ARG_32(6);
  uint32_t handle_ptr = SHIM_GET_ARG_32(7);

  XELOGD(
      "XamUserCreateAchievementEnumerator(%.8X, %d, %.8X, %.8X, %d, %d, %.8X, "
      "%.8X)",
      title_id, user_index, xuid, flags, offset, count, buffer_size_ptr,
      handle_ptr);

  if (buffer_size_ptr) {
    // TODO(benvanik): struct size/etc.
    SHIM_SET_MEM_32(buffer_size_ptr, 64 * count);
  }

  auto e = new XStaticEnumerator(kernel_state, count, 64);
  e->Initialize();

  SHIM_SET_MEM_32(handle_ptr, e->handle());

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamParseGamerTileKey_shim(PPCContext* ppc_context,
                                    KernelState* kernel_state) {
  uint32_t key_ptr = SHIM_GET_ARG_32(0);
  uint32_t out0_ptr = SHIM_GET_ARG_32(1);
  uint32_t out1_ptr = SHIM_GET_ARG_32(2);
  uint32_t out2_ptr = SHIM_GET_ARG_32(3);

  XELOGD("XamParseGamerTileKey(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X)", key_ptr,
         out0_ptr, out1_ptr, out2_ptr);

  SHIM_SET_MEM_32(out0_ptr, 0xC0DE0001);
  SHIM_SET_MEM_32(out1_ptr, 0xC0DE0002);
  SHIM_SET_MEM_32(out2_ptr, 0xC0DE0003);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamReadTileToTexture_shim(PPCContext* ppc_context,
                                    KernelState* kernel_state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);  // const?
  uint32_t unk1 = SHIM_GET_ARG_32(1);  // out0 from XamParseGamerTileKey
  uint32_t unk2 = SHIM_GET_ARG_32(2);  // some variant of out1/out2
  uint32_t unk3 = SHIM_GET_ARG_32(3);  // const?
  uint32_t buffer_ptr = SHIM_GET_ARG_32(4);
  uint32_t stride = SHIM_GET_ARG_32(5);
  uint32_t height = SHIM_GET_ARG_32(6);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(7);

  XELOGD("XamReadTileToTexture(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X)", unk0, unk1,
         unk2, unk3, buffer_ptr, stride, height, overlapped_ptr);

  if (overlapped_ptr) {
    kernel_state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
    SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
  }
}

SHIM_CALL XamWriteGamerTile_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t arg1 = SHIM_GET_ARG_32(1);
  uint32_t arg2 = SHIM_GET_ARG_32(2);
  uint32_t arg3 = SHIM_GET_ARG_32(3);
  uint32_t arg4 = SHIM_GET_ARG_32(4);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(5);

  XELOGD("XamWriteGamerTile(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X)", arg0, arg1,
         arg2, arg3, arg4, overlapped_ptr);

  if (overlapped_ptr) {
    kernel_state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
    SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
  }
}

SHIM_CALL XamSessionCreateHandle_shim(PPCContext* ppc_context,
                                      KernelState* kernel_state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XamSessionCreateHandle(%.8X)", handle_ptr);

  SHIM_SET_MEM_32(handle_ptr, 0xCAFEDEAD);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamSessionRefObjByHandle_shim(PPCContext* ppc_context,
                                        KernelState* kernel_state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t obj_ptr = SHIM_GET_ARG_32(1);

  XELOGD("XamSessionRefObjByHandle(%.8X, %.8X)", handle, obj_ptr);

  assert_true(handle == 0xCAFEDEAD);

  SHIM_SET_MEM_32(obj_ptr, 0);

  SHIM_SET_RETURN_32(X_ERROR_FUNCTION_FAILED);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterUserExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", XamUserGetXUID, state);
  SHIM_SET_MAPPING("xam.xex", XamUserGetSigninState, state);
  SHIM_SET_MAPPING("xam.xex", XamUserGetSigninInfo, state);
  SHIM_SET_MAPPING("xam.xex", XamUserGetName, state);
  SHIM_SET_MAPPING("xam.xex", XamUserReadProfileSettings, state);
  SHIM_SET_MAPPING("xam.xex", XamUserWriteProfileSettings, state);
  SHIM_SET_MAPPING("xam.xex", XamUserCheckPrivilege, state);
  SHIM_SET_MAPPING("xam.xex", XamUserContentRestrictionGetFlags, state);
  SHIM_SET_MAPPING("xam.xex", XamUserContentRestrictionGetRating, state);
  SHIM_SET_MAPPING("xam.xex", XamUserContentRestrictionCheckAccess, state);
  SHIM_SET_MAPPING("xam.xex", XamUserAreUsersFriends, state);
  SHIM_SET_MAPPING("xam.xex", XamShowSigninUI, state);
  SHIM_SET_MAPPING("xam.xex", XamUserCreateAchievementEnumerator, state);
  SHIM_SET_MAPPING("xam.xex", XamParseGamerTileKey, state);
  SHIM_SET_MAPPING("xam.xex", XamReadTileToTexture, state);
  SHIM_SET_MAPPING("xam.xex", XamWriteGamerTile, state);
  SHIM_SET_MAPPING("xam.xex", XamSessionCreateHandle, state);
  SHIM_SET_MAPPING("xam.xex", XamSessionRefObjByHandle, state);
}

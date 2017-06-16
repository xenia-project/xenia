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
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

dword_result_t XamUserGetXUID(dword_t user_index, unknown_t unk,
                              lpqword_t xuid_ptr) {
  if (user_index == 0) {
    const auto& user_profile = kernel_state()->user_profile();
    if (xuid_ptr) {
      *xuid_ptr = user_profile->xuid();
    }
    return 0;
  } else {
    return X_ERROR_NO_SUCH_USER;
  }
}
DECLARE_XAM_EXPORT(XamUserGetXUID, ExportTag::kImplemented);

dword_result_t XamUserGetSigninState(dword_t user_index) {
  // Yield, as some games spam this.
  xe::threading::MaybeYield();

  // Lie and say we are signed in, but local-only.
  // This should keep games from asking us to sign in and also keep them
  // from initializing the network.
  if (user_index == 0 || (user_index & 0xFF) == 0xFF) {
    const auto& user_profile = kernel_state()->user_profile();
    auto signin_state = user_profile->signin_state();
    return signin_state;
  } else {
    return 0;
  }
}
DECLARE_XAM_EXPORT(XamUserGetSigninState, ExportTag::kImplemented);

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

dword_result_t XamUserGetName(dword_t user_index, lpstring_t buffer_ptr,
                              dword_t buffer_len) {
  if (user_index == 0) {
    const auto& user_profile = kernel_state()->user_profile();
    char* buffer = (buffer_ptr);
    std::strcpy(buffer, user_profile->name().data());
    return 0;
  } else {
    return X_ERROR_NO_SUCH_USER;
  }
}
DECLARE_XAM_EXPORT(XamUserGetName, ExportTag::kImplemented);

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
      if (setting->is_set) {
        auto extra_size = static_cast<uint32_t>(setting->extra_size());
        size_needed += extra_size;
      }
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
    out_setting->from =
        !setting || !setting->is_set ? 0 : setting->is_title_specific() ? 2 : 1;
    out_setting->user_index = user_index;
    out_setting->setting_id = setting_id;

    if (setting && setting->is_set) {
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

typedef struct {
  xe::be<uint32_t> from;
  xe::be<uint32_t> unk_04;
  xe::be<uint32_t> unk_08;
  xe::be<uint32_t> unk_0c;
  xe::be<uint32_t> setting_id;
  xe::be<uint32_t> unk_14;

  // UserProfile::Setting::Type. Appears to be 8-in-32 field, and the upper 24
  // are not always zeroed by the game.
  uint8_t type;

  xe::be<uint32_t> unk_1c;
  // TODO(sabretooth): not sure if this is a union, but it seems likely.
  // Haven't run into cases other than "binary data" yet.

  union {
    struct {
      xe::be<uint32_t> length;
      xe::be<uint32_t> ptr;

    } binary;
  };

} X_USER_WRITE_PROFILE_SETTING;

SHIM_CALL XamUserWriteProfileSettings_shim(PPCContext* ppc_context,
                                           KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t unknown = SHIM_GET_ARG_32(1);
  uint32_t setting_count = SHIM_GET_ARG_32(2);
  uint32_t settings_ptr = SHIM_GET_ARG_32(3);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(4);

  XELOGD("XamUserWriteProfileSettings(%.8X, %d, %d, %.8X, %.8X)", user_index,
         unknown, setting_count, settings_ptr, overlapped_ptr);

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

  // Update and save settings.

  const auto& user_profile = kernel_state->user_profile();
  auto settings_data_arr = reinterpret_cast<X_USER_WRITE_PROFILE_SETTING*>(
      SHIM_MEM_ADDR(settings_ptr));

  for (uint32_t n = 0; n < setting_count; ++n) {
    const X_USER_WRITE_PROFILE_SETTING& settings_data = settings_data_arr[n];
    XELOGD(
        "XamUserWriteProfileSettings: setting index [%d]:"
        " from=%d setting_id=%.8X data.type=%d",
        n, (uint32_t)settings_data.from, (uint32_t)settings_data.setting_id,
        settings_data.type);

    xam::UserProfile::Setting::Type settingType =
        static_cast<xam::UserProfile::Setting::Type>(settings_data.type);

    switch (settingType) {
      case UserProfile::Setting::Type::BINARY: {
        uint8_t* settings_data_ptr = SHIM_MEM_ADDR(settings_data.binary.ptr);
        size_t settings_data_len = settings_data.binary.length;
        std::vector<uint8_t> data_vec;

        if (settings_data.binary.ptr) {
          // Copy provided data
          data_vec.resize(settings_data_len);
          memcpy(data_vec.data(), settings_data_ptr, settings_data_len);
        } else {
          // Data pointer was NULL, so just fill with zeroes
          data_vec.resize(settings_data_len, 0);
        }

        user_profile->AddSetting(
            std::make_unique<xam::UserProfile::BinarySetting>(
                settings_data.setting_id, data_vec));

      } break;

      case UserProfile::Setting::Type::WSTRING:
      case UserProfile::Setting::Type::DOUBLE:
      case UserProfile::Setting::Type::FLOAT:
      case UserProfile::Setting::Type::INT32:
      case UserProfile::Setting::Type::INT64:
      case UserProfile::Setting::Type::DATETIME:

      default:

        XELOGE("XamUserWriteProfileSettings: Unimplemented data type %d",
               settingType);
        break;
    };
  }

  if (overlapped_ptr) {
    kernel_state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
    SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
  }
}

dword_result_t XamUserCheckPrivilege(dword_t user_index, dword_t mask,
                                     lpdword_t out_value_ptr) {
  // If we deny everything, games should hopefully not try to do stuff.
  *out_value_ptr = 0;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamUserCheckPrivilege, ExportTag::kStub);

dword_result_t XamUserContentRestrictionGetFlags(dword_t user_index,
                                                 lpdword_t out_flags_ptr) {
  // No restrictions?
  *out_flags_ptr = 0;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamUserContentRestrictionGetFlags, ExportTag::kStub);

dword_result_t XamUserContentRestrictionGetRating(dword_t user_index,
                                                  unknown_t unk1,
                                                  lpdword_t out_unk2_ptr,
                                                  lpdword_t out_unk3_ptr) {
  // Some games have special case paths for 3F that differ from the failure
  // path, so my guess is that's 'don't care'.
  *out_unk2_ptr = 0x3F;
  *out_unk3_ptr = 0;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamUserContentRestrictionGetRating, ExportTag::kImplemented);

dword_result_t XamUserContentRestrictionCheckAccess(dword_t user_index,
	                                                  unknown_t unk1, unknown_t unk2,
	                                                  unknown_t unk3, unknown_t unk4,
	                                                  lpdword_t out_unk5_ptr,
                                                    pointer_t<XAM_OVERLAPPED> overlapped_ptr) {

dword_result_t XamUserContentRestrictionCheckAccess(dword_t user_index, unknown_t unk1,
                                                    unknown_t unk2, unknown_t unk3, 
                                                    unknown_t unk4, lpdword_t out_unk5_ptr,
                                                    pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  *out_unk5_ptr = 1;

  if (overlapped_ptr) {
    // TODO(benvanik): does this need the access arg on it?
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamUserContentRestrictionCheckAccess,
                   ExportTag::kImplemented);

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

dword_result_t XamShowSigninUI(unknown_t unk_0, unknown_t unk_mask) {
  // Mask values vary. Probably matching user types? Local/remote?
  // Games seem to sit and loop until we trigger this notification.
  kernel_state()->BroadcastNotification(0x00000009, 0);

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamShowSigninUI, ExportTag::kImplemented);

dword_result_t XamUserCreateAchievementEnumerator(dword_t title_id,
                                                  dword_t user_index,
                                                  dword_t xuid, dword_t flags,
                                                  dword_t offset, dword_t count,
                                                  lpdword_t buffer_size_ptr,
                                                  lpdword_t handle_ptr) {
  if (buffer_size_ptr) {
    // TODO(benvanik): struct size/etc.
    *buffer_size_ptr = 64 * count;
  }

  auto e = new XStaticEnumerator(kernel_state(), count, 64);
  e->Initialize();

  *handle_ptr = e->handle();

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamUserCreateAchievementEnumerator, ExportTag::kImplemented);

dword_result_t XamParseGamerTileKey(lpdword_t key_ptr, lpdword_t out0_ptr,
                                    lpdword_t out1_ptr, lpdword_t out2_ptr) {
  *out0_ptr = 0xC0DE0001;
  *out1_ptr = 0xC0DE0002;
  *out2_ptr = 0xC0DE0003;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamParseGamerTileKey, ExportTag::kImplemented);

dword_result_t XamReadTileToTexture(unknown_t unk0, unknown_t unk1,
                                    unknown_t unk2, unknown_t unk3,
                                    lpdword_t buffer_ptr, dword_t stride,
                                    dword_t height,
                                    pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  // unk0 const?
  // unk1 out0 from XamParseGamerTileKey
  // unk2 some variant of out1/out2
  // unk3 const?
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  } else {
    return X_ERROR_SUCCESS;
  }
}
DECLARE_XAM_EXPORT(XamReadTileToTexture, ExportTag::kImplemented);

dword_result_t XamWriteGamerTile(dword_t arg0, dword_t arg1, dword_t arg2,
                                 dword_t arg3, dword_t arg4,
                                 pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  } else {
    return X_ERROR_SUCCESS;
  }
}
DECLARE_XAM_EXPORT(XamWriteGamerTile, ExportTag::kImplemented);

dword_result_t XamSessionCreateHandle(lpdword_t handle_ptr) {
  *handle_ptr = 0xCAFEDEAD;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamSessionCreateHandle, ExportTag::kImplemented);

dword_result_t XamSessionRefObjByHandle(dword_t handle, lpdword_t obj_ptr) {
  assert_true(handle == 0xCAFEDEAD);

  *obj_ptr = 0;

  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT(XamSessionRefObjByHandle, ExportTag::kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterUserExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", XamUserReadProfileSettings, state);
  SHIM_SET_MAPPING("xam.xex", XamUserWriteProfileSettings, state);
  SHIM_SET_MAPPING("xam.xex", XamUserGetSigninInfo, state);
  SHIM_SET_MAPPING("xam.xex", XamUserAreUsersFriends, state);
}

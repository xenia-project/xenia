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
#include "xenia/base/cvar.h"


DEFINE_bool(signin_state, true,
            "User signed in", "Kernel");

namespace xe {
namespace kernel {
namespace xam {

struct X_PROFILEENUMRESULT {
  xe::be<uint64_t> xuid_offline;  // E0.....
  X_XAMACCOUNTINFO account;
  xe::be<uint32_t> device_id;
};
// static_assert_size(X_PROFILEENUMRESULT, 0x188);

dword_result_t XamProfileCreateEnumerator(dword_t device_id,
                                          lpdword_t handle_out) {
  assert_not_null(handle_out);

  auto e =
      new XStaticEnumerator(kernel_state(), 1, sizeof(X_PROFILEENUMRESULT));

  e->Initialize();

  const auto& user_profile = kernel_state()->user_profile();

  X_PROFILEENUMRESULT* profile = (X_PROFILEENUMRESULT*)e->AppendItem();
  memset(profile, 0, sizeof(X_PROFILEENUMRESULT));
  profile->xuid_offline = user_profile->xuid();
  profile->device_id = 0xF00D0000;

  auto tag = xe::to_wstring(user_profile->name());
  xe::copy_and_swap<wchar_t>(profile->account.gamertag, tag.c_str(),
                             tag.length());
  profile->account.xuid_online = user_profile->xuid();

  *handle_out = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamProfileCreateEnumerator, kUserProfiles, kImplemented);

dword_result_t XamProfileEnumerate(dword_t handle, dword_t flags,
                                   lpvoid_t buffer,
                                   pointer_t<XAM_OVERLAPPED> overlapped) {
  assert_true(flags == 0);

  auto e = kernel_state()->object_table()->LookupObject<XEnumerator>(handle);
  if (!e) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediateEx(
          overlapped, X_ERROR_INVALID_HANDLE, X_ERROR_INVALID_HANDLE, 0);
      return X_ERROR_IO_PENDING;
    } else {
      return X_ERROR_INVALID_HANDLE;
    }
  }

  buffer.Zero(sizeof(X_PROFILEENUMRESULT));

  X_RESULT result;

  if (e->current_item() >= e->item_count()) {
    result = X_ERROR_NO_MORE_FILES;
  } else {
    auto item_buffer = buffer.as<uint8_t*>();
    if (!e->WriteItem(item_buffer)) {
      result = X_ERROR_NO_MORE_FILES;
    } else {
      result = X_ERROR_SUCCESS;
    }
  }

  // Return X_ERROR_NO_MORE_FILES in HRESULT form.
  X_HRESULT extended_result = result != 0 ? X_HRESULT_FROM_WIN32(result) : 0;
  if (overlapped) {
    kernel_state()->CompleteOverlappedImmediateEx(
        overlapped, result, extended_result, result == X_ERROR_SUCCESS ? 1 : 0);
    return X_ERROR_IO_PENDING;
  } else {
    assert_always();
    return X_ERROR_INVALID_PARAMETER;
  }
}
DECLARE_XAM_EXPORT1(XamProfileEnumerate, kUserProfiles, kImplemented);

X_HRESULT_result_t XamUserGetXUID(dword_t user_index, dword_t unk,
                                  lpqword_t xuid_ptr) {
  if (user_index) {
    return X_E_NO_SUCH_USER;
  }
  const auto& user_profile = kernel_state()->user_profile();
  if (xuid_ptr) {
    *xuid_ptr = user_profile->xuid();
  }
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetSigninState(dword_t user_index) {
  // Yield, as some games spam this.
  xe::threading::MaybeYield();

  // Lie and say we are signed in, but local-only.
  // This should keep games from asking us to sign in and also keep them
  // from initializing the network.

  if (user_index == 0 || (user_index & 0xFF) == 0xFF) {
    const auto& user_profile = kernel_state()->user_profile();
	return ((cvars::signin_state) ? 1 : 0);
  } else {
    return 0;
  }
}
DECLARE_XAM_EXPORT2(XamUserGetSigninState, kUserProfiles, kImplemented,
                    kHighFrequency);

typedef struct {
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> unk08;  // maybe zero?
  xe::be<uint32_t> signin_state;
  xe::be<uint32_t> unk10;  // ?
  xe::be<uint32_t> unk14;  // ?
  char name[16];
} X_USER_SIGNIN_INFO;
static_assert_size(X_USER_SIGNIN_INFO, 40);

X_HRESULT_result_t XamUserGetSigninInfo(dword_t user_index, dword_t flags,
                                        pointer_t<X_USER_SIGNIN_INFO> info) {
  if (!info) {
    return X_E_INVALIDARG;
  }

  std::memset(info, 0, sizeof(X_USER_SIGNIN_INFO));
  if (user_index) {
    return X_E_NO_SUCH_USER;
  }

  const auto& user_profile = kernel_state()->user_profile();
  info->xuid = user_profile->xuid();
  info->signin_state = ((cvars::signin_state) ? 1 : 0);
  std::strncpy(info->name, user_profile->name().data(), 15);
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetSigninInfo, kUserProfiles, kImplemented);

dword_result_t XamUserGetName(dword_t user_index, lpstring_t buffer,
                              dword_t buffer_len) {
  if (user_index) {
    return X_ERROR_NO_SUCH_USER;
  }

  if (!buffer_len) {
    return X_ERROR_SUCCESS;
  }

  const auto& user_profile = kernel_state()->user_profile();
  const auto& user_name = user_profile->name();

  // Real XAM will only copy a maximum of 15 characters out.
  size_t copy_length = std::min(
      {size_t(15), user_name.size(), static_cast<size_t>(buffer_len) - 1});
  std::memcpy(buffer, user_name.data(), copy_length);
  buffer[copy_length] = '\0';
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetName, kUserProfiles, kImplemented);

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

// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/Generic/xboxtools.cpp
dword_result_t XamUserReadProfileSettings(
    dword_t title_id, dword_t user_index, dword_t unk_0, dword_t unk_1,
    dword_t setting_count, lpdword_t setting_ids, lpdword_t buffer_size_ptr,
    lpvoid_t buffer_ptr, dword_t overlapped_ptr) {
  uint32_t buffer_size =
      !buffer_size_ptr ? 0u : static_cast<uint32_t>(*buffer_size_ptr);

  assert_zero(unk_0);
  assert_zero(unk_1);

  // TODO(gibbed): why is this a thing?
  uint32_t actual_user_index = user_index;
  if (actual_user_index == 255) {
    actual_user_index = 0;
  }

  // Title ID = 0 means us.
  // 0xfffe07d1 = profile?

  if (actual_user_index) {
    // Only support user 0.
    if (overlapped_ptr) {
      kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                  X_ERROR_NOT_FOUND);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_NOT_FOUND;
  }

  const auto& user_profile = kernel_state()->user_profile();

  // First call asks for size (fill buffer_size_ptr).
  // Second call asks for buffer contents with that size.

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
      kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                  X_ERROR_INVALID_PARAMETER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_INVALID_PARAMETER;
  }

  *buffer_size_ptr = size_needed;
  if (!buffer_ptr || buffer_size < size_needed) {
    if (overlapped_ptr) {
      kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                  X_ERROR_INSUFFICIENT_BUFFER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_INSUFFICIENT_BUFFER;
  }

  auto out_header = buffer_ptr.as<X_USER_READ_PROFILE_SETTINGS*>();
  out_header->setting_count = static_cast<uint32_t>(setting_count);
  out_header->settings_ptr = buffer_ptr.guest_address() + 8;

  auto out_setting =
      reinterpret_cast<X_USER_READ_PROFILE_SETTING*>(buffer_ptr + 8);

  size_t buffer_offset = base_size_needed;
  for (uint32_t n = 0; n < setting_count; ++n) {
    uint32_t setting_id = setting_ids[n];
    auto setting = user_profile->GetSetting(setting_id);

    std::memset(out_setting, 0, sizeof(X_USER_READ_PROFILE_SETTING));
    out_setting->from =
        !setting || !setting->is_set ? 0 : setting->is_title_specific() ? 2 : 1;
    out_setting->user_index = actual_user_index;
    out_setting->setting_id = setting_id;

    if (setting && setting->is_set) {
      buffer_offset =
          setting->Append(&out_setting->setting_data[0], buffer_ptr,
                          buffer_ptr.guest_address(), buffer_offset);
    }
    // TODO(benvanik): why did I do this?
    /*else {
      std::memset(&out_setting->setting_data[0], 0,
                  sizeof(out_setting->setting_data));
    }*/
    ++out_setting;
  }

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserReadProfileSettings, kUserProfiles, kImplemented);

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

dword_result_t XamUserWriteProfileSettings(
    dword_t user_index, dword_t unk, dword_t setting_count,
    pointer_t<X_USER_WRITE_PROFILE_SETTING> settings, dword_t overlapped_ptr) {
  if (!setting_count || !settings) {
    if (overlapped_ptr) {
      kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                  X_ERROR_INVALID_PARAMETER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_INVALID_PARAMETER;
  }

  uint32_t actual_user_index = user_index;
  if (actual_user_index == 255) {
    actual_user_index = 0;
  }

  if (actual_user_index) {
    // Only support user 0.
    return X_ERROR_NOT_FOUND;
  }

  // Update and save settings.
  const auto& user_profile = kernel_state()->user_profile();

  for (uint32_t n = 0; n < setting_count; ++n) {
    const X_USER_WRITE_PROFILE_SETTING& settings_data = settings[n];
    XELOGD(
        "XamUserWriteProfileSettings: setting index [%d]:"
        " from=%d setting_id=%.8X data.type=%d",
        n, (uint32_t)settings_data.from, (uint32_t)settings_data.setting_id,
        settings_data.type);

    xam::UserProfile::Setting::Type settingType =
        static_cast<xam::UserProfile::Setting::Type>(settings_data.type);

    switch (settingType) {
      case UserProfile::Setting::Type::CONTENT:
      case UserProfile::Setting::Type::BINARY: {
        uint8_t* settings_data_ptr = kernel_state()->memory()->TranslateVirtual(
            settings_data.binary.ptr);
        size_t settings_data_len = settings_data.binary.length;
        std::vector<uint8_t> data_vec;

        if (settings_data.binary.ptr) {
          // Copy provided data
          data_vec.resize(settings_data_len);
          std::memcpy(data_vec.data(), settings_data_ptr, settings_data_len);
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
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserWriteProfileSettings, kUserProfiles, kImplemented);

dword_result_t XamUserCheckPrivilege(dword_t user_index, dword_t mask,
                                     lpdword_t out_value) {
  uint32_t actual_user_index = user_index;
  if ((actual_user_index & 0xFF) == 0xFF) {
    // Always pin user to 0.
    actual_user_index = 0;
  }

  if (actual_user_index) {
    return X_ERROR_NO_SUCH_USER;
  }

  // If we deny everything, games should hopefully not try to do stuff.
  *out_value = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCheckPrivilege, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionGetFlags(dword_t user_index,
                                                 lpdword_t out_flags) {
  uint32_t actual_user_index = user_index;
  if ((actual_user_index & 0xFF) == 0xFF) {
    // Always pin user to 0.
    actual_user_index = 0;
  }

  if (actual_user_index) {
    return X_ERROR_NO_SUCH_USER;
  }

  // No restrictions?
  *out_flags = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionGetFlags, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionGetRating(dword_t user_index,
                                                  dword_t unk1,
                                                  lpdword_t out_unk2,
                                                  lpdword_t out_unk3) {
  uint32_t actual_user_index = user_index;
  if ((actual_user_index & 0xFF) == 0xFF) {
    // Always pin user to 0.
    actual_user_index = 0;
  }

  if (actual_user_index) {
    return X_ERROR_NO_SUCH_USER;
  }

  // Some games have special case paths for 3F that differ from the failure
  // path, so my guess is that's 'don't care'.
  *out_unk2 = 0x3F;
  *out_unk3 = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionGetRating, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionCheckAccess(dword_t user_index,
                                                    dword_t unk1, dword_t unk2,
                                                    dword_t unk3, dword_t unk4,
                                                    lpdword_t out_unk5,
                                                    dword_t overlapped_ptr) {
  *out_unk5 = 1;

  if (overlapped_ptr) {
    // TODO(benvanik): does this need the access arg on it?
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionCheckAccess, kUserProfiles, kStub);

dword_result_t XamUserAreUsersFriends(dword_t user_index, dword_t unk1,
                                      dword_t unk2, lpdword_t out_value,
                                      dword_t overlapped_ptr) {
  uint32_t actual_user_index = user_index;
  if ((actual_user_index & 0xFF) == 0xFF) {
    // Always pin user to 0.
    actual_user_index = 0;
  }

  *out_value = 0;

  if (user_index) {
    // Only support user 0.
    return X_ERROR_NOT_LOGGED_ON;
  }

  X_RESULT result = X_ERROR_SUCCESS;

  const auto& user_profile = kernel_state()->user_profile();
  if (((cvars::signin_state) ? 1 : 0) == 0) {
    result = X_ERROR_NOT_LOGGED_ON;
  } else {
    // No friends!
    *out_value = 0;
  }

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  }
  return result;
}
DECLARE_XAM_EXPORT1(XamUserAreUsersFriends, kUserProfiles, kStub);

dword_result_t XamShowSigninUI(dword_t unk, dword_t unk_mask) {
  // Mask values vary. Probably matching user types? Local/remote?
  // Games seem to sit and loop until we trigger this notification.
  kernel_state()->BroadcastNotification(0x00000009, 0);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamShowSigninUI, kUserProfiles, kStub);

#pragma pack(push, 1)
struct X_XACHIEVEMENT_DETAILS {
  xe::be<uint32_t> id;
  xe::be<uint32_t> label_ptr;
  xe::be<uint32_t> description_ptr;
  xe::be<uint32_t> unachieved_ptr;
  xe::be<uint32_t> image_id;
  xe::be<uint32_t> gamerscore;
  xe::be<uint64_t> unlock_time;
  xe::be<uint32_t> flags;
};
static_assert_size(X_XACHIEVEMENT_DETAILS, 36);
#pragma pack(pop)

dword_result_t XamUserCreateAchievementEnumerator(dword_t title_id,
                                                  dword_t user_index,
                                                  dword_t xuid, dword_t flags,
                                                  dword_t offset, dword_t count,
                                                  lpdword_t buffer_size_ptr,
                                                  lpdword_t handle_ptr) {
  if (buffer_size_ptr) {
    *buffer_size_ptr = sizeof(X_XACHIEVEMENT_DETAILS) * count;
  }

  auto e = new XStaticEnumerator(kernel_state(), count,
                                 sizeof(X_XACHIEVEMENT_DETAILS));
  e->Initialize();

  *handle_ptr = e->handle();

  // Copy achievements into the enumerator if game GPD is loaded
  auto* game_gpd = kernel_state()->user_profile()->GetTitleGpd(title_id);
  if (!game_gpd) {
    XELOGE(
        "XamUserCreateAchievementEnumerator failed to find GPD for title %X!",
        title_id);
    return X_ERROR_SUCCESS;
  }

  static uint32_t placeholder = 0;

  if (!placeholder) {
    const wchar_t* placeholder_val = L"<placeholder>";

    placeholder = kernel_memory()->SystemHeapAlloc(
        ((uint32_t)wcslen(placeholder_val) + 1) * 2);
    auto* place_addr = kernel_memory()->TranslateVirtual<wchar_t*>(placeholder);

    memset(place_addr, 0, (wcslen(placeholder_val) + 1) * 2);
    xe::copy_and_swap(place_addr, placeholder_val, wcslen(placeholder_val));
  }

  std::vector<xdbf::Achievement> achievements;
  game_gpd->GetAchievements(&achievements);

  for (auto ach : achievements) {
    auto* details = (X_XACHIEVEMENT_DETAILS*)e->AppendItem();
    details->id = ach.id;
    details->image_id = ach.image_id;
    details->gamerscore = ach.gamerscore;
    details->unlock_time = ach.unlock_time;
    details->flags = ach.flags;

    // TODO: these, allocating guest mem for them every CreateEnum call would be
    // very bad...

    // maybe we could alloc these in guest when the title GPD is first loaded?
    details->label_ptr = placeholder;
    details->description_ptr = placeholder;
    details->unachieved_ptr = placeholder;
  }

  XELOGD("XamUserCreateAchievementEnumerator: added %d items to enumerator",
         e->item_count());

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCreateAchievementEnumerator, kUserProfiles,
                    kSketchy);

dword_result_t XamParseGamerTileKey(lpdword_t key_ptr, lpdword_t out1_ptr,
                                    lpdword_t out2_ptr, lpdword_t out3_ptr) {
  *out1_ptr = 0xC0DE0001;
  *out2_ptr = 0xC0DE0002;
  *out3_ptr = 0xC0DE0003;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamParseGamerTileKey, kUserProfiles, kStub);

dword_result_t XamReadTileToTexture(dword_t unk1, dword_t unk2, dword_t unk3,
                                    dword_t unk4, lpvoid_t buffer_ptr,
                                    dword_t stride, dword_t height,
                                    dword_t overlapped_ptr) {
  // unk1: const?
  // unk2: out0 from XamParseGamerTileKey
  // unk3: some variant of out1/out2
  // unk4: const?

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamReadTileToTexture, kUserProfiles, kStub);

dword_result_t XamWriteGamerTile(dword_t arg1, dword_t arg2, dword_t arg3,
                                 dword_t arg4, dword_t arg5,
                                 dword_t overlapped_ptr) {
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamWriteGamerTile, kUserProfiles, kStub);

dword_result_t XamSessionCreateHandle(lpdword_t handle_ptr) {
  *handle_ptr = 0xCAFEDEAD;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamSessionCreateHandle, kUserProfiles, kStub);

dword_result_t XamSessionRefObjByHandle(dword_t handle, lpdword_t obj_ptr) {
  assert_true(handle == 0xCAFEDEAD);
  // TODO(PermaNull): Implement this properly,
  // For the time being returning 0xDEADF00D will prevent crashing.
  *obj_ptr = 0xDEADF00D;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamSessionRefObjByHandle, kUserProfiles, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterUserExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {}

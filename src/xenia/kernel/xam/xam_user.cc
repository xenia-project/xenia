/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DECLARE_int32(user_language);

namespace xe {
namespace kernel {
namespace xam {

X_HRESULT_result_t XamUserGetXUID_entry(dword_t user_index, dword_t type_mask,
                                        lpqword_t xuid_ptr) {
  assert_true(type_mask == 1 || type_mask == 2 || type_mask == 3 ||
              type_mask == 4 || type_mask == 7);
  if (!xuid_ptr) {
    return X_E_INVALIDARG;
  }
  uint32_t result = X_E_NO_SUCH_USER;
  uint64_t xuid = 0;
  if (user_index < 4) {
    if (kernel_state()->IsUserSignedIn(user_index)) {
      const auto& user_profile = kernel_state()->user_profile(user_index);
      auto type = user_profile->type() & type_mask;
      if (type & (2 | 4)) {
        // maybe online profile?
        xuid = user_profile->xuid();
        result = X_E_SUCCESS;
      } else if (type & 1) {
        // maybe offline profile?
        xuid = user_profile->xuid();
        result = X_E_SUCCESS;
      }
    }
  } else {
    result = X_E_INVALIDARG;
  }
  *xuid_ptr = xuid;
  return result;
}
DECLARE_XAM_EXPORT1(XamUserGetXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetSigninState_entry(dword_t user_index) {
  // Yield, as some games spam this.
  xe::threading::MaybeYield();
  uint32_t signin_state = 0;
  if (user_index < 4) {
    if (kernel_state()->IsUserSignedIn(user_index)) {
      const auto& user_profile = kernel_state()->user_profile(user_index);
      signin_state = user_profile->signin_state();
    }
  }
  return signin_state;
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

X_HRESULT_result_t XamUserGetSigninInfo_entry(
    dword_t user_index, dword_t flags, pointer_t<X_USER_SIGNIN_INFO> info) {
  if (!info) {
    return X_E_INVALIDARG;
  }

  std::memset(info, 0, sizeof(X_USER_SIGNIN_INFO));
  if (user_index > 3) {
    return X_E_NO_SUCH_USER;
  }

  kernel_state()->UpdateUsedUserProfiles();

  if (kernel_state()->IsUserSignedIn(user_index)) {
    const auto& user_profile = kernel_state()->user_profile(user_index);
    info->xuid = user_profile->xuid();
    info->signin_state = user_profile->signin_state();
    xe::string_util::copy_truncating(info->name, user_profile->name(),
                                     xe::countof(info->name));
  } else {
    return X_E_NO_SUCH_USER;
  }
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetSigninInfo, kUserProfiles, kImplemented);

dword_result_t XamUserGetName_entry(dword_t user_index, lpstring_t buffer,
                                    dword_t buffer_len) {
  if (user_index >= 4) {
    return X_E_INVALIDARG;
  }

  if (kernel_state()->IsUserSignedIn(user_index)) {
    const auto& user_profile = kernel_state()->user_profile(user_index);
    const auto& user_name = user_profile->name();
    xe::string_util::copy_truncating(
        buffer, user_name, std::min(buffer_len.value(), uint32_t(16)));
  } else {
    return X_E_NO_SUCH_USER;
  }
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetName, kUserProfiles, kImplemented);

dword_result_t XamUserGetGamerTag_entry(dword_t user_index,
                                        lpu16string_t buffer,
                                        dword_t buffer_len) {
  if (user_index >= 4) {
    return X_E_INVALIDARG;
  }

  if (!buffer || buffer_len < 16) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->IsUserSignedIn(user_index)) {
    return X_E_INVALIDARG;
  }

  const auto& user_profile = kernel_state()->user_profile(user_index);
  auto user_name = xe::to_utf16(user_profile->name());
  xe::string_util::copy_and_swap_truncating(
      buffer, user_name, std::min(buffer_len.value(), uint32_t(16)));
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetGamerTag, kUserProfiles, kImplemented);

typedef struct {
  xe::be<uint32_t> setting_count;
  xe::be<uint32_t> settings_ptr;
} X_USER_READ_PROFILE_SETTINGS;
static_assert_size(X_USER_READ_PROFILE_SETTINGS, 8);

// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/Generic/xboxtools.cpp
uint32_t XamUserReadProfileSettingsEx(uint32_t title_id, uint32_t user_index,
                                      uint32_t xuid_count, be<uint64_t>* xuids,
                                      uint32_t setting_count,
                                      be<uint32_t>* setting_ids, uint32_t unk,
                                      be<uint32_t>* buffer_size_ptr,
                                      uint8_t* buffer,
                                      XAM_OVERLAPPED* overlapped) {
  if (!xuid_count) {
    assert_null(xuids);
  } else {
    assert_true(xuid_count == 1);
    assert_not_null(xuids);
    // TODO(gibbed): allow proper lookup of arbitrary XUIDs
    // TODO(gibbed): we assert here, but in case a title passes xuid_count > 1
    // until it's implemented for release builds...
    xuid_count = 1;
    if (kernel_state()->IsUserSignedIn(user_index)) {
      const auto& user_profile = kernel_state()->user_profile(user_index);
      assert_true(static_cast<uint64_t>(xuids[0]) == user_profile->xuid());
    }
  }
  assert_zero(unk);  // probably flags

  // must have at least 1 to 32 settings
  if (setting_count < 1 || setting_count > 32) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // buffer size pointer must be valid
  if (!buffer_size_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // if buffer size is non-zero, buffer pointer must be valid
  auto buffer_size = static_cast<uint32_t>(*buffer_size_ptr);
  if (buffer_size && !buffer) {
    return X_ERROR_INVALID_PARAMETER;
  }

  uint32_t needed_header_size = 0;
  uint32_t needed_data_size = 0;
  for (uint32_t i = 0; i < setting_count; ++i) {
    needed_header_size += sizeof(X_USER_PROFILE_SETTING);
    UserData::Key setting_key;
    setting_key.value = static_cast<uint32_t>(setting_ids[i]);
    switch (static_cast<X_USER_DATA_TYPE>(setting_key.type)) {
      case X_USER_DATA_TYPE::WSTRING:
      case X_USER_DATA_TYPE::BINARY:
        needed_data_size += setting_key.size;
        break;
      default:
        break;
    }
  }
  if (xuids) {
    needed_header_size *= xuid_count;
    needed_data_size *= xuid_count;
  }
  needed_header_size += sizeof(X_USER_READ_PROFILE_SETTINGS);

  uint32_t needed_size = needed_header_size + needed_data_size;
  if (!buffer || buffer_size < needed_size) {
    if (!buffer_size) {
      *buffer_size_ptr = needed_size;
    }
    return X_ERROR_INSUFFICIENT_BUFFER;
  }

  // Title ID = 0 means us.
  // 0xfffe07d1 = profile?
  if (!kernel_state()->IsUserSignedIn(user_index) && !xuids) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(
          kernel_state()->memory()->HostToGuestVirtual(overlapped),
          X_ERROR_NO_SUCH_USER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_NO_SUCH_USER;
  }

  auto user_profile = kernel_state()->user_profile(user_index);

  if (xuids) {
    uint64_t user_xuid = static_cast<uint64_t>(xuids[0]);
    if (!kernel_state()->IsUserSignedIn(user_xuid)) {
      if (overlapped) {
        kernel_state()->CompleteOverlappedImmediate(
            kernel_state()->memory()->HostToGuestVirtual(overlapped),
            X_ERROR_NO_SUCH_USER);
        return X_ERROR_IO_PENDING;
      }
      return X_ERROR_NO_SUCH_USER;
    }
    user_profile = kernel_state()->user_profile(user_xuid);
  }

  // First call asks for size (fill buffer_size_ptr).
  // Second call asks for buffer contents with that size.

  // TODO(gibbed): setting validity checking without needing a user profile
  // object.
  bool any_missing = false;
  for (uint32_t i = 0; i < setting_count; ++i) {
    auto setting_id = static_cast<uint32_t>(setting_ids[i]);
    auto setting = user_profile->GetSetting(setting_id);
    if (!setting) {
      any_missing = true;
      XELOGE(
          "xeXamUserReadProfileSettingsEx requested unimplemented setting "
          "{:08X}",
          setting_id);
    }
  }
  if (any_missing) {
    // TODO(benvanik): don't fail? most games don't even check!
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(
          kernel_state()->memory()->HostToGuestVirtual(overlapped),
          X_ERROR_INVALID_PARAMETER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_INVALID_PARAMETER;
  }

  auto out_header = reinterpret_cast<X_USER_READ_PROFILE_SETTINGS*>(buffer);
  auto out_setting = reinterpret_cast<X_USER_PROFILE_SETTING*>(&out_header[1]);
  out_header->setting_count = static_cast<uint32_t>(setting_count);
  out_header->settings_ptr =
      kernel_state()->memory()->HostToGuestVirtual(out_setting);

  DataByteStream out_stream(
      kernel_state()->memory()->HostToGuestVirtual(buffer), buffer, buffer_size,
      needed_header_size);
  for (uint32_t n = 0; n < setting_count; ++n) {
    uint32_t setting_id = setting_ids[n];
    auto setting = user_profile->GetSetting(setting_id);

    std::memset(out_setting, 0, sizeof(X_USER_PROFILE_SETTING));
    out_setting->from =
        !setting ? 0 : static_cast<uint32_t>(setting->GetSettingSource());
    if (xuids) {
      out_setting->xuid = user_profile->xuid();
    } else {
      out_setting->xuid = -1;
      out_setting->user_index = user_index;
    }
    out_setting->setting_id = setting_id;

    if (setting) {
      out_setting->data.type = static_cast<X_USER_DATA_TYPE>(
          setting->GetSettingHeader()->setting_type.value);
      setting->GetSettingData()->Append(&out_setting->data, &out_stream);
    }
    ++out_setting;
  }

  if (overlapped) {
    kernel_state()->CompleteOverlappedImmediate(
        kernel_state()->memory()->HostToGuestVirtual(overlapped),
        X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}

dword_result_t XamUserReadProfileSettings_entry(
    dword_t title_id, dword_t user_index, dword_t xuid_count, lpqword_t xuids,
    dword_t setting_count, lpdword_t setting_ids, lpdword_t buffer_size_ptr,
    lpvoid_t buffer_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamUserReadProfileSettingsEx(title_id, user_index, xuid_count, xuids,
                                      setting_count, setting_ids, 0,
                                      buffer_size_ptr, buffer_ptr, overlapped);
}
DECLARE_XAM_EXPORT1(XamUserReadProfileSettings, kUserProfiles, kImplemented);

dword_result_t XamUserReadProfileSettingsEx_entry(
    dword_t title_id, dword_t user_index, dword_t xuid_count, lpqword_t xuids,
    dword_t setting_count, lpdword_t setting_ids, lpdword_t buffer_size_ptr,
    dword_t unk_2, lpvoid_t buffer_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamUserReadProfileSettingsEx(title_id, user_index, xuid_count, xuids,
                                      setting_count, setting_ids, unk_2,
                                      buffer_size_ptr, buffer_ptr, overlapped);
}
DECLARE_XAM_EXPORT1(XamUserReadProfileSettingsEx, kUserProfiles, kImplemented);

dword_result_t XamUserWriteProfileSettings_entry(
    dword_t title_id, dword_t user_index, dword_t setting_count,
    pointer_t<X_USER_PROFILE_SETTING> settings,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!setting_count || !settings) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // Skip writing data about users with id != 0 they're not supported
  if (user_index > 0) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(
          kernel_state()->memory()->HostToGuestVirtual(overlapped),
          X_ERROR_NO_SUCH_USER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_SUCCESS;
  }
  // Update and save settings.
  const auto& user_profile = kernel_state()->user_profile(user_index);

  for (uint32_t n = 0; n < setting_count; ++n) {
    const X_USER_PROFILE_SETTING& setting = settings[n];

    auto setting_type = static_cast<X_USER_DATA_TYPE>(setting.data.type);
    if (setting_type == X_USER_DATA_TYPE::UNSET) {
      continue;
    }

    XELOGD(
        "XamUserWriteProfileSettings: setting index [{}]:"
        " from={} setting_id={:08X} data.type={}",
        n, (uint32_t)setting.from, (uint32_t)setting.setting_id,
        setting.data.type);

    switch (setting_type) {
      case X_USER_DATA_TYPE::CONTENT:
      case X_USER_DATA_TYPE::BINARY: {
        uint8_t* binary_ptr =
            kernel_state()->memory()->TranslateVirtual(setting.data.binary.ptr);

        size_t binary_size = setting.data.binary.size;
        std::vector<uint8_t> bytes;
        if (setting.data.binary.ptr) {
          // Copy provided data
          bytes.resize(binary_size);
          std::memcpy(bytes.data(), binary_ptr, binary_size);
        } else {
          // Data pointer was NULL, so just fill with zeroes
          bytes.resize(binary_size, 0);
        }

        auto user_setting =
            std::make_unique<UserSetting>(setting.setting_id, bytes);

        user_setting->SetNewSettingSource(X_USER_PROFILE_SETTING_SOURCE::TITLE);
        user_profile->AddSetting(std::move(user_setting));
      } break;
      case X_USER_DATA_TYPE::WSTRING:
      case X_USER_DATA_TYPE::DOUBLE:
      case X_USER_DATA_TYPE::FLOAT:
      case X_USER_DATA_TYPE::INT32:
      case X_USER_DATA_TYPE::INT64:
      case X_USER_DATA_TYPE::DATETIME:
      default: {
        XELOGE("XamUserWriteProfileSettings: Unimplemented data type {}",
               setting_type);
      } break;
    };
  }

  if (overlapped) {
    kernel_state()->CompleteOverlappedImmediate(overlapped, X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserWriteProfileSettings, kUserProfiles, kImplemented);

dword_result_t XamUserCheckPrivilege_entry(dword_t user_index, dword_t mask,
                                           lpdword_t out_value) {
  // checking all users?
  if (user_index != 0xFF) {
    if (user_index >= 4) {
      return X_ERROR_INVALID_PARAMETER;
    }

    if (!kernel_state()->IsUserSignedIn(user_index)) {
      return X_ERROR_NO_SUCH_USER;
    }
  }

  // If we deny everything, games should hopefully not try to do stuff.
  *out_value = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCheckPrivilege, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionGetFlags_entry(dword_t user_index,
                                                       lpdword_t out_flags) {
  if (!kernel_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  // No restrictions?
  *out_flags = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionGetFlags, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionGetRating_entry(dword_t user_index,
                                                        dword_t unk1,
                                                        lpdword_t out_unk2,
                                                        lpdword_t out_unk3) {
  if (!kernel_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  // Some games have special case paths for 3F that differ from the failure
  // path, so my guess is that's 'don't care'.
  *out_unk2 = 0x3F;
  *out_unk3 = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionGetRating, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionCheckAccess_entry(
    dword_t user_index, dword_t unk1, dword_t unk2, dword_t unk3, dword_t unk4,
    lpdword_t out_unk5, dword_t overlapped_ptr) {
  *out_unk5 = 1;

  if (overlapped_ptr) {
    // TODO(benvanik): does this need the access arg on it?
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionCheckAccess, kUserProfiles, kStub);

dword_result_t XamUserIsOnlineEnabled_entry(dword_t user_index) { return 1; }
DECLARE_XAM_EXPORT1(XamUserIsOnlineEnabled, kUserProfiles, kStub);

dword_result_t XamUserGetMembershipTier_entry(dword_t user_index) {
  if (user_index >= 4) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (kernel_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }
  return 6 /* 6 appears to be Gold */;
}
DECLARE_XAM_EXPORT1(XamUserGetMembershipTier, kUserProfiles, kStub);

dword_result_t XamUserAreUsersFriends_entry(dword_t user_index, dword_t unk1,
                                            dword_t unk2, lpdword_t out_value,
                                            dword_t overlapped_ptr) {
  uint32_t are_friends = 0;
  X_RESULT result;

  if (user_index >= 4) {
    result = X_ERROR_INVALID_PARAMETER;
  } else {
    if (kernel_state()->IsUserSignedIn(user_index)) {
      const auto& user_profile = kernel_state()->user_profile(user_index);
      if (user_profile->signin_state() == 0) {
        result = X_ERROR_NOT_LOGGED_ON;
      } else {
        // No friends!
        are_friends = 0;
        result = X_ERROR_SUCCESS;
      }
    } else {
      // Only support user 0.
      result =
          X_ERROR_NO_SUCH_USER;  // if user is local -> X_ERROR_NOT_LOGGED_ON
    }
  }

  if (out_value) {
    assert_true(!overlapped_ptr);
    *out_value = result == X_ERROR_SUCCESS ? are_friends : 0;
    return result;
  } else if (overlapped_ptr) {
    assert_true(!out_value);
    kernel_state()->CompleteOverlappedImmediateEx(
        overlapped_ptr,
        result == X_ERROR_SUCCESS ? X_ERROR_SUCCESS : X_ERROR_FUNCTION_FAILED,
        X_HRESULT_FROM_WIN32(result),
        result == X_ERROR_SUCCESS ? are_friends : 0);
    return X_ERROR_IO_PENDING;
  } else {
    assert_always();
    return X_ERROR_INVALID_PARAMETER;
  }
}
DECLARE_XAM_EXPORT1(XamUserAreUsersFriends, kUserProfiles, kStub);

dword_result_t XamShowSigninUI_entry(dword_t unk, dword_t unk_mask) {
  kernel_state()->BroadcastNotification(0x00000009, 1);
  kernel_state()->UpdateUsedUserProfiles();
  // Mask values vary. Probably matching user types? Local/remote?
  // Games seem to sit and loop until we trigger this notification:

  for (uint32_t i = 0; i < 4; i++) {
    if (kernel_state()->IsUserSignedIn(i)) {
      // XN_SYS_SIGNINCHANGED
      kernel_state()->BroadcastNotification(0xA, i);
    }
  }

  // XN_SYS_UI (off)
  kernel_state()->BroadcastNotification(0x00000009, 0);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamShowSigninUI, kUserProfiles, kStub);

dword_result_t XamUserCreateAchievementEnumerator_entry(
    dword_t title_id, dword_t user_index, dword_t xuid, dword_t flags,
    dword_t offset, dword_t count, lpdword_t buffer_size_ptr,
    lpdword_t handle_ptr) {
  if (!count || !buffer_size_ptr || !handle_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index >= 4) {
    return X_ERROR_INVALID_PARAMETER;
  }

  size_t entry_size = sizeof(X_ACHIEVEMENT_DETAILS);
  if (flags & 7) {
    entry_size += X_ACHIEVEMENT_DETAILS::kStringBufferSize;
  }

  if (buffer_size_ptr) {
    *buffer_size_ptr = static_cast<uint32_t>(entry_size) * count;
  }

  auto e = object_ref<XAchievementEnumerator>(
      new XAchievementEnumerator(kernel_state(), count, flags));
  auto result = e->Initialize(user_index, 0xFB, 0xB000A, 0xB000B, 0);
  if (XFAILED(result)) {
    return result;
  }

  const util::XdbfGameData db = kernel_state()->title_xdbf();

  if (db.is_valid()) {
    const XLanguage language =
        db.GetExistingLanguage(static_cast<XLanguage>(cvars::user_language));
    const std::vector<util::XdbfAchievementTableEntry> achievement_list =
        db.GetAchievements();

    for (const util::XdbfAchievementTableEntry& entry : achievement_list) {
      auto is_unlocked =
          kernel_state()->achievement_manager()->IsAchievementUnlocked(
              entry.id);
      auto unlock_time =
          kernel_state()->achievement_manager()->GetAchievementUnlockTime(
              entry.id);

      auto item = XAchievementEnumerator::AchievementDetails{
          entry.id,
          xe::to_utf16(db.GetStringTableEntry(language, entry.label_id)),
          xe::to_utf16(db.GetStringTableEntry(language, entry.description_id)),
          xe::to_utf16(db.GetStringTableEntry(language, entry.unachieved_id)),
          entry.image_id,
          entry.gamerscore,
          (uint32_t)(unlock_time << 31),
          (uint32_t)unlock_time,
          is_unlocked ? entry.flags | 0x20000 : entry.flags};

      e->AppendItem(item);
    }
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCreateAchievementEnumerator, kUserProfiles,
                    kSketchy);

dword_result_t XamParseGamerTileKey_entry(lpdword_t key_ptr, lpdword_t out1_ptr,
                                          lpdword_t out2_ptr,
                                          lpdword_t out3_ptr) {
  *out1_ptr = 0xC0DE0001;
  *out2_ptr = 0xC0DE0002;
  *out3_ptr = 0xC0DE0003;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamParseGamerTileKey, kUserProfiles, kStub);

dword_result_t XamReadTileToTexture_entry(dword_t unknown, dword_t title_id,
                                          qword_t tile_id, dword_t user_index,
                                          lpvoid_t buffer_ptr, dword_t stride,
                                          dword_t height,
                                          dword_t overlapped_ptr) {
  // TODO(gibbed): unknown=0,2,3,9
  if (!tile_id) {
    return X_ERROR_INVALID_PARAMETER;
  }

  size_t size = size_t(stride) * size_t(height);
  std::memset(buffer_ptr, 0xFF, size);

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamReadTileToTexture, kUserProfiles, kStub);

dword_result_t XamWriteGamerTile_entry(dword_t arg1, dword_t arg2, dword_t arg3,
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

dword_result_t XamSessionCreateHandle_entry(lpdword_t handle_ptr) {
  *handle_ptr = 0xCAFEDEAD;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamSessionCreateHandle, kUserProfiles, kStub);

dword_result_t XamSessionRefObjByHandle_entry(dword_t handle,
                                              lpdword_t obj_ptr) {
  assert_true(handle == 0xCAFEDEAD);
  // TODO(PermaNull): Implement this properly,
  // For the time being returning 0xDEADF00D will prevent crashing.
  *obj_ptr = 0xDEADF00D;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamSessionRefObjByHandle, kUserProfiles, kStub);

dword_result_t XamUserIsUnsafeProgrammingAllowed_entry(
    dword_t unk1, dword_t unk2, lpdword_t unk3, dword_t unk4, dword_t unk5,
    dword_t unk6) {
  if (!unk3 || unk1 != 255 && unk1 >= 4) {
    return 87;
  }
  *unk3 = 1;
  return 0;
}
DECLARE_XAM_EXPORT1(XamUserIsUnsafeProgrammingAllowed, kUserProfiles, kStub);

dword_result_t XamUserGetSubscriptionType_entry(dword_t user_index,
                                                dword_t unk2, dword_t unk3,
                                                dword_t unk4, dword_t unk5,
                                                dword_t unk6) {
  if (!unk2 || !unk3 || user_index > 4) {
    return 0x80070057;
  }

  return 0;
}
DECLARE_XAM_EXPORT1(XamUserGetSubscriptionType, kUserProfiles, kStub);

constexpr uint8_t kStatsMaxAmount = 64;

struct X_STATS_DETAILS {
  xe::be<uint32_t> id;
  xe::be<uint32_t> stats_amount;
  xe::be<uint16_t> stats[kStatsMaxAmount];
};
static_assert_size(X_STATS_DETAILS, 8 + kStatsMaxAmount * 2);

dword_result_t XamUserCreateStatsEnumerator_entry(
    dword_t title_id, dword_t user_index, dword_t count, dword_t flags,
    dword_t size, pointer_t<X_STATS_DETAILS> stats_ptr,
    lpdword_t buffer_size_ptr, lpdword_t handle_ptr) {
  if (!count || !buffer_size_ptr || !handle_ptr || !stats_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index >= 4) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!flags || flags > 0x64) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!size) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (buffer_size_ptr) {
    *buffer_size_ptr = 0;  // sizeof(X_STATS_DETAILS) * stats_ptr->stats_amount;
  }

  auto e = object_ref<XUserStatsEnumerator>(
      new XUserStatsEnumerator(kernel_state(), 0));
  const X_STATUS result = e->Initialize(user_index, 0xFB, 0xB0023, 0xB0024, 0);
  if (XFAILED(result)) {
    return result;
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCreateStatsEnumerator, kUserProfiles, kSketchy);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(User);

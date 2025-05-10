/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/kernel/xam/user_settings.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

#include "third_party/stb/stb_image.h"

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

  *xuid_ptr = 0;

  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  uint32_t result = X_E_NO_SUCH_USER;
  uint64_t xuid = 0;

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
  *xuid_ptr = xuid;
  return result;
}
DECLARE_XAM_EXPORT1(XamUserGetXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetIndexFromXUID_entry(qword_t xuid, dword_t flags,
                                             pointer_t<uint32_t> index) {
  if (!index) {
    return X_E_INVALIDARG;
  }

  const uint8_t user_index = kernel_state()
                                 ->xam_state()
                                 ->profile_manager()
                                 ->GetUserIndexAssignedToProfile(xuid);

  if (user_index == XUserIndexAny) {
    return X_E_NO_SUCH_USER;
  }

  *index = user_index;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetIndexFromXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetSigninState_entry(dword_t user_index) {
  // Yield, as some games spam this.
  xe::threading::MaybeYield();
  uint32_t signin_state = 0;
  if (user_index >= XUserMaxUserCount) {
    return signin_state;
  }

  if (kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    const auto& user_profile =
        kernel_state()->xam_state()->GetUserProfile(user_index);
    signin_state = user_profile->signin_state();
  }
  return signin_state;
}
DECLARE_XAM_EXPORT2(XamUserGetSigninState, kUserProfiles, kImplemented,
                    kHighFrequency);

typedef struct {
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> signin_state;
  xe::be<uint32_t> guest_num;
  xe::be<uint32_t> sponsor_user_index;
  char name[16];
} X_USER_SIGNIN_INFO;
static_assert_size(X_USER_SIGNIN_INFO, 40);

X_HRESULT_result_t XamUserGetSigninInfo_entry(
    dword_t user_index, dword_t flags, pointer_t<X_USER_SIGNIN_INFO> info) {
  if (!info) {
    return X_E_INVALIDARG;
  }

  std::memset(info, 0, sizeof(X_USER_SIGNIN_INFO));
  if (user_index >= XUserMaxUserCount) {
    return X_E_NO_SUCH_USER;
  }

  if (kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    const auto& user_profile =
        kernel_state()->xam_state()->GetUserProfile(user_index);
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

dword_result_t XamUserGetName_entry(dword_t user_index, dword_t buffer,
                                    dword_t buffer_len) {
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    // Based on XAM only first byte is cleared in case of lack of user.
    kernel_memory()->Zero(buffer, 1);
    return X_ERROR_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  // Because name is always limited to 15 characters we can assume length will
  // never exceed that limit.
  const auto& user_name = user_profile->name();

  // buffer_len includes null-terminator. user_name does not.
  const uint32_t bytes_to_copy = std::min(
      buffer_len.value(), static_cast<uint32_t>(user_name.length()) + 1);

  char* str_buffer = kernel_memory()->TranslateVirtual<char*>(buffer);
  xe::string_util::copy_truncating(str_buffer, user_name, bytes_to_copy);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetName, kUserProfiles, kImplemented);

dword_result_t XamUserGetGamerTag_entry(dword_t user_index, dword_t buffer,
                                        dword_t buffer_len) {
  if (!buffer || buffer_len < 16) {
    return X_E_INVALIDARG;
  }

  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);
  auto user_name = xe::to_utf16(user_profile->name());

  char16_t* str_buffer = kernel_memory()->TranslateVirtual<char16_t*>(buffer);

  xe::string_util::copy_and_swap_truncating(
      str_buffer, user_name, std::min(buffer_len.value(), uint32_t(16)));
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
                                      lpvoid_t overlapped_ptr) {
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
    AttributeKey setting_key;
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

  auto user_profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  if (!user_profile && !xuids) {
    return X_ERROR_NO_SUCH_USER;
  }

  if (xuids) {
    uint64_t user_xuid = static_cast<uint64_t>(xuids[0]);
    if (!kernel_state()->xam_state()->IsUserSignedIn(user_xuid)) {
      return X_ERROR_NO_SUCH_USER;
    }
    user_profile = kernel_state()->xam_state()->GetUserProfile(user_xuid);
  }

  if (!user_profile) {
    return X_ERROR_NO_SUCH_USER;
  }

  // First call asks for size (fill buffer_size_ptr).
  // Second call asks for buffer contents with that size.

  bool any_missing = false;
  for (uint32_t i = 0; i < setting_count; ++i) {
    auto setting_id = static_cast<uint32_t>(setting_ids[i]);
    if (!UserSetting::is_setting_valid(setting_id)) {
      XELOGE(
          "xeXamUserReadProfileSettingsEx requested unimplemented "
          "setting "
          "{:08X}",
          setting_id);
      any_missing = true;
    }
  }
  if (any_missing) {
    return X_ERROR_INVALID_PARAMETER;
    // TODO(benvanik): don't fail? most games don't even check!
  }

  auto out_header = reinterpret_cast<X_USER_READ_PROFILE_SETTINGS*>(buffer);
  auto out_setting = reinterpret_cast<X_USER_PROFILE_SETTING*>(&out_header[1]);
  out_header->setting_count = static_cast<uint32_t>(setting_count);
  out_header->settings_ptr =
      kernel_state()->memory()->HostToGuestVirtual(out_setting);

  uint32_t additional_data_buffer_ptr =
      out_header->settings_ptr +
      (setting_count * sizeof(X_USER_PROFILE_SETTING));

  std::fill_n(out_setting, setting_count, X_USER_PROFILE_SETTING{});

  for (uint32_t n = 0; n < setting_count; ++n) {
    uint32_t setting_id = setting_ids[n];

    const bool is_valid =
        kernel_state()->xam_state()->user_tracker()->GetUserSetting(
            user_profile->xuid(),
            title_id ? title_id : kernel_state()->title_id(), setting_id,
            out_setting, additional_data_buffer_ptr);

    if (is_valid) {
      if (xuids) {
        out_setting->xuid = user_profile->xuid();
      } else {
        out_setting->xuid = -1;
        out_setting->user_index = user_index;
      }
    }
    ++out_setting;
  }

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr.value(),
                                                X_STATUS_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_STATUS_SUCCESS;
}

dword_result_t XamUserReadProfileSettings_entry(
    dword_t title_id, dword_t user_index, dword_t xuid_count, lpqword_t xuids,
    dword_t setting_count, lpdword_t setting_ids, lpdword_t buffer_size_ptr,
    lpvoid_t buffer_ptr, lpvoid_t overlapped) {
  return XamUserReadProfileSettingsEx(title_id, user_index, xuid_count, xuids,
                                      setting_count, setting_ids, 0,
                                      buffer_size_ptr, buffer_ptr, overlapped);
}
DECLARE_XAM_EXPORT1(XamUserReadProfileSettings, kUserProfiles, kImplemented);

dword_result_t XamUserReadProfileSettingsEx_entry(
    dword_t title_id, dword_t user_index, dword_t xuid_count, lpqword_t xuids,
    dword_t setting_count, lpdword_t setting_ids, lpdword_t buffer_size_ptr,
    dword_t unk_2, lpvoid_t buffer_ptr, lpvoid_t overlapped) {
  return XamUserReadProfileSettingsEx(title_id, user_index, xuid_count, xuids,
                                      setting_count, setting_ids, unk_2,
                                      buffer_size_ptr, buffer_ptr, overlapped);
}
DECLARE_XAM_EXPORT1(XamUserReadProfileSettingsEx, kUserProfiles, kImplemented);

dword_result_t XamUserWriteProfileSettings_entry(
    dword_t title_id, dword_t user_index, dword_t setting_count,
    pointer_t<X_USER_PROFILE_SETTING> settings, lpvoid_t overlapped) {
  if (!setting_count || !settings) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto run = [=](uint32_t& extended_error, uint32_t& length) {
    // Update and save settings.
    const auto& user_profile =
        kernel_state()->xam_state()->GetUserProfile(user_index);

    // Skip writing data about users with id != 0 they're not supported
    if (!user_profile) {
      extended_error = X_HRESULT_FROM_WIN32(X_ERROR_NO_SUCH_USER);
      length = 0;
      return X_ERROR_NO_SUCH_USER;
    }

    for (uint32_t n = 0; n < setting_count; ++n) {
      const UserSetting setting = UserSetting(&settings[n]);

      if (!setting.is_valid_type()) {
        continue;
      }

      kernel_state()->xam_state()->user_tracker()->UpsertSetting(
          user_profile->xuid(), title_id, &setting);
    }

    extended_error = X_HRESULT_FROM_WIN32(X_STATUS_SUCCESS);
    length = 0;
    return X_STATUS_SUCCESS;
  };

  if (!overlapped) {
    uint32_t extended_error, length;
    return run(extended_error, length);
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamUserWriteProfileSettings, kUserProfiles, kImplemented);

dword_result_t XamUserCheckPrivilege_entry(dword_t user_index, dword_t mask,
                                           lpdword_t out_value) {
  // checking all users?
  if (user_index != XUserIndexAny) {
    if (user_index >= XUserMaxUserCount) {
      return X_ERROR_INVALID_PARAMETER;
    }

    if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
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
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
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
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
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

dword_result_t XamUserIsOnlineEnabled_entry(dword_t user_index) {
  if (user_index >= XUserMaxUserCount) {
    return 0;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return 0;
  }

  return kernel_state()
      ->xam_state()
      ->GetUserProfile(user_index)
      ->IsLiveEnabled();
}
DECLARE_XAM_EXPORT1(XamUserIsOnlineEnabled, kUserProfiles, kImplemented);

dword_result_t XamUserGetMembershipTier_entry(dword_t user_index) {
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  return kernel_state()
      ->xam_state()
      ->GetUserProfile(user_index)
      ->GetSubscriptionTier();
}
DECLARE_XAM_EXPORT1(XamUserGetMembershipTier, kUserProfiles, kImplemented);

dword_result_t XamUserGetMembershipTierFromXUID_entry(qword_t xuid) {
  const auto profile = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!profile) {
    return 0;
  }

  return profile->GetSubscriptionTier();
}
DECLARE_XAM_EXPORT1(XamUserGetMembershipTierFromXUID, kUserProfiles,
                    kImplemented);

dword_result_t XamUserAreUsersFriends_entry(dword_t user_index, dword_t unk1,
                                            dword_t unk2, lpdword_t out_value,
                                            dword_t overlapped_ptr) {
  uint32_t are_friends = 0;
  X_RESULT result;

  if (user_index >= XUserMaxUserCount) {
    result = X_ERROR_INVALID_PARAMETER;
  } else {
    if (kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
      const auto& user_profile =
          kernel_state()->xam_state()->GetUserProfile(user_index);
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

dword_result_t XamUserCreateAchievementEnumerator_entry(
    dword_t title_id, dword_t user_index, qword_t xuid, dword_t flags,
    dword_t offset, dword_t count, lpdword_t buffer_size_ptr,
    lpdword_t handle_ptr) {
  if (!count || !buffer_size_ptr || !handle_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index >= XUserMaxUserCount) {
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

  const auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_INVALID_PARAMETER;
  }

  uint64_t requester_xuid = user->xuid();
  if (xuid) {
    requester_xuid = xuid;
  }

  const uint32_t title_id_ =
      title_id ? static_cast<uint32_t>(title_id) : kernel_state()->title_id();

  const auto user_title_achievements =
      kernel_state()->achievement_manager()->GetTitleAchievements(
          requester_xuid, title_id_);

  if (!user_title_achievements.empty()) {
    for (const auto& entry : user_title_achievements) {
      auto unlock_time = X_FILETIME();
      if (entry.IsUnlocked() && entry.unlock_time.is_valid()) {
        unlock_time = entry.unlock_time;
      }

      auto item = AchievementDetails(
          entry.achievement_id, entry.achievement_name.c_str(),
          entry.unlocked_description.c_str(), entry.locked_description.c_str(),
          entry.image_id, entry.gamerscore, unlock_time, entry.flags);

      e->AppendItem(item);
    }
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCreateAchievementEnumerator, kUserProfiles,
                    kSketchy);

dword_result_t XamUserCreateTitlesPlayedEnumerator_entry(
    dword_t title_id, dword_t user_index, qword_t xuid, dword_t starting_index,
    dword_t game_count, lpdword_t buffer_size_ptr, lpdword_t handle_ptr) {
  if (user_index >= XUserMaxUserCount && game_count != 0 && !buffer_size_ptr &&
      !handle_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  const uint32_t kEntrySize = sizeof(XTitleEnumerator::XTITLE_PLAYED);
  if (buffer_size_ptr) {
    *buffer_size_ptr = kEntrySize * game_count;
  }

  const auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto e = object_ref<XTitleEnumerator>(
      new XTitleEnumerator(kernel_state(), game_count));
  auto result =
      e->Initialize(user_index, 0xFB, 0xB0050, 0xB000B, 0x20, game_count, 0);
  if (XFAILED(result)) {
    return result;
  }

  const auto user_titles =
      kernel_state()->xam_state()->user_tracker()->GetPlayedTitles(
          user->xuid());

  if (!user_titles.empty()) {
    for (const auto& title : user_titles) {
      if (title.id == kDashboardID) {
        continue;
      }
      if (!title.achievements_count || !title.gamerscore_amount) {
        continue;
      }
      e->AppendItem(title);
    }
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCreateTitlesPlayedEnumerator, kUserProfiles, kStub);

dword_result_t XamReadTile_entry(dword_t tile_type, dword_t title_id,
                                 qword_t item_id, dword_t user_index,
                                 lpdword_t output_ptr,
                                 lpdword_t buffer_size_ptr,
                                 lpvoid_t overlapped_ptr) {
  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    user = kernel_state()->xam_state()->GetUserProfile(item_id);
    if (!user) {
      return X_ERROR_INVALID_PARAMETER;
    }
  }

  if (!buffer_size_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto run = [=](uint32_t& extended_error, uint32_t& length) {
    std::span<const uint8_t> tile =
        kernel_state()->xam_state()->user_tracker()->GetIcon(
            user->xuid(), title_id, static_cast<XTileType>(tile_type.value()),
            item_id);

    auto result = X_ERROR_SUCCESS;

    if (tile.empty()) {
      result = X_ERROR_FILE_NOT_FOUND;
    }

    *buffer_size_ptr = static_cast<uint32_t>(tile.size());

    if (output_ptr) {
      memcpy(output_ptr, tile.data(), tile.size());
    } else {
      result = X_ERROR_INSUFFICIENT_BUFFER;
    }

    extended_error = X_HRESULT_FROM_WIN32(result);
    length = 0;
    return result;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    return run(extended_error, length);
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamReadTile, kUserProfiles, kSketchy);

dword_result_t XamReadTileEx_entry(dword_t tile_type, dword_t game_id,
                                   qword_t item_id, dword_t offset,
                                   dword_t unk1, dword_t unk2,
                                   lpdword_t output_ptr,
                                   lpdword_t buffer_size_ptr) {
  return XamReadTile_entry(tile_type, game_id, item_id, offset, output_ptr,
                           buffer_size_ptr, 0);
}
DECLARE_XAM_EXPORT1(XamReadTileEx, kUserProfiles, kSketchy);

dword_result_t XamParseGamerTileKey_entry(pointer_t<X_USER_DATA> key_ptr,
                                          lpdword_t title_id_ptr,
                                          lpdword_t big_tile_id_ptr,
                                          lpdword_t small_tile_id_ptr) {
  if (!key_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (key_ptr->type != X_USER_DATA_TYPE::WSTRING) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (key_ptr->data.unicode.size > 0x64) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!key_ptr->data.unicode.ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  std::string tile_key = xe::to_utf8(string_util::read_u16string_and_swap(
      kernel_memory()->TranslateVirtual<const char16_t*>(
          key_ptr->data.unicode.ptr)));

  // Default key size is 24 bytes, but we need to include null terminator
  if (tile_key.empty() || tile_key.size() != 25) {
    return X_ERROR_INVALID_PARAMETER;
  }

  const bool is_valid_hex_string =
      std::all_of(tile_key.begin(), --tile_key.end(),
                  [](unsigned char c) { return std::isxdigit(c); });

  if (!is_valid_hex_string) {
    return X_ERROR_INVALID_PARAMETER;
  }
  // Simple parser for key. Key (lower case) contains: title_id (8 chars),
  // big_tile_id (8 chars), small_tile_id (8 chars)
  std::string title_id = tile_key.substr(0, 8);
  std::string big_tile_id = tile_key.substr(8, 8);
  std::string small_tile_id = tile_key.substr(16, 8);

  if (title_id_ptr) {
    *title_id_ptr = string_util::from_string<uint32_t>(title_id, true);
  }

  if (big_tile_id_ptr) {
    *big_tile_id_ptr = string_util::from_string<uint32_t>(big_tile_id, true);
  }

  if (small_tile_id_ptr) {
    *small_tile_id_ptr =
        string_util::from_string<uint32_t>(small_tile_id, true);
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamParseGamerTileKey, kUserProfiles, kImplemented);

dword_result_t XamReadTileToTexture_entry(dword_t tile_type, dword_t title_id,
                                          qword_t tile_id, dword_t user_index,
                                          lpvoid_t buffer_ptr, dword_t stride,
                                          dword_t tile_height,
                                          dword_t overlapped_ptr) {
  if (!buffer_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  size_t buffer_size = size_t(stride) * size_t(tile_height);

  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_INVALID_PARAMETER;
  }

  std::span<const uint8_t> tile =
      kernel_state()->xam_state()->user_tracker()->GetIcon(
          user->xuid(), title_id, static_cast<XTileType>(tile_type.value()),
          tile_id);

  if (tile.empty()) {
    return X_ERROR_SUCCESS;
  }

  int width, height, channels;
  unsigned char* imageData =
      stbi_load_from_memory(tile.data(), static_cast<int>(tile.size()), &width,
                            &height, &channels, STBI_rgb_alpha);

  size_t icon_dimmension_size = width * height;
  std::fill_n(reinterpret_cast<uint8_t*>(buffer_ptr.host_address()),
              icon_dimmension_size * sizeof(uint32_t), 0);

  for (int i = 0; i < icon_dimmension_size; i++) {
    unsigned char* pixel = &imageData[i * sizeof(uint32_t)];

    // RGBA to ARGB. TODO: Find faster method!
    // RGBA->AGBR
    std::swap(pixel[0], pixel[3]);
    // AGBR->ARBG
    std::swap(pixel[1], pixel[3]);
    // ARBG->ARGB
    std::swap(pixel[2], pixel[3]);
  }

  memcpy(buffer_ptr, imageData,
         std::min(buffer_size, static_cast<size_t>(icon_dimmension_size *
                                                   sizeof(uint32_t))));

  stbi_image_free(imageData);

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamReadTileToTexture, kUserProfiles, kStub);

dword_result_t XamWriteGamerTile_entry(dword_t user_index, dword_t title_id,
                                       dword_t small_tile_id,
                                       dword_t big_tile_id, dword_t arg5,
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

dword_result_t XamUserIsUnsafeProgrammingAllowed_entry(dword_t user_index,
                                                       dword_t unk,
                                                       lpdword_t result_ptr) {
  if (!result_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index != XUserIndexAny && user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // uint32_t result = XamUserCheckPrivilege_entry(user_index, 0xD4u,
  // result_ptr);

  *result_ptr = 1;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserIsUnsafeProgrammingAllowed, kUserProfiles, kStub);

dword_result_t XamUserGetSubscriptionType_entry(dword_t user_index,
                                                lpdword_t subscription_ptr,
                                                lpdword_t r5,
                                                dword_t overlapped_ptr) {
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!subscription_ptr || !r5) {
    return X_E_INVALIDARG;
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_INVALID_PARAMETER;
  }

  *subscription_ptr = user->GetSubscriptionTier();
  *r5 = 0x0;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetSubscriptionType, kUserProfiles, kStub);

dword_result_t XamUserGetUserFlags_entry(dword_t user_index) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return 0;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  return user_profile->GetCachedFlags();
}
DECLARE_XAM_EXPORT1(XamUserGetUserFlags, kUserProfiles, kImplemented);

dword_result_t XamUserGetUserFlagsFromXUID_entry(qword_t xuid) {
  const auto& user_profile = kernel_state()->xam_state()->GetUserProfile(xuid);
  if (!user_profile) {
    return 0;
  }

  return user_profile->GetCachedFlags();
}
DECLARE_XAM_EXPORT1(XamUserGetUserFlagsFromXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetOnlineLanguageFromXUID_entry(qword_t xuid) {
  /* Notes:
     - Calls XamUserGetUserFlagsFromXUID and returns (ulonglong)(cached_flag <<
     0x20) >> 0x39 & 0x1f;
     - XamUserGetMembershipTierFromXUID and XamUserGetOnlineCountryFromXUID also
     call it
     - Removed in metro
  */
  return cvars::user_language;
}
DECLARE_XAM_EXPORT1(XamUserGetOnlineLanguageFromXUID, kUserProfiles, kStub);

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

  if (user_index >= XUserMaxUserCount) {
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

dword_result_t XamUserGetUserTenure_entry(dword_t user_index,
                                          lpdword_t tenure_level_ptr,
                                          lpdword_t milestone_ptr,
                                          lpqword_t milestone_date_ptr,
                                          dword_t overlap_ptr) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_INVALIDARG;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  if (const auto setting =
          kernel_state()->xam_state()->user_tracker()->GetSetting(
              user_profile, kDashboardID,
              static_cast<uint32_t>(UserSettingId::XPROFILE_TENURE_LEVEL))) {
    *tenure_level_ptr = std::get<int32_t>(setting->get_host_data());
  }

  if (const auto setting =
          kernel_state()->xam_state()->user_tracker()->GetSetting(
              user_profile, kDashboardID,
              static_cast<uint32_t>(
                  UserSettingId::XPROFILE_TENURE_MILESTONE))) {
    *milestone_ptr = std::get<int32_t>(setting->get_host_data());
  }

  if (const auto setting =
          kernel_state()->xam_state()->user_tracker()->GetSetting(
              user_profile, kDashboardID,
              static_cast<uint32_t>(
                  UserSettingId::XPROFILE_TENURE_NEXT_MILESTONE_DATE))) {
    *milestone_date_ptr = std::get<int64_t>(setting->get_host_data());
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetUserTenure, kUserProfiles, kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(User);
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/user_settings.h"

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xdbf/gpd_info.h"

namespace xe {
namespace kernel {
namespace xam {

const static std::array<UserSetting, 2> default_setting_values = {
    UserSetting(
        UserSettingId::XPROFILE_GAMER_TIER,
        X_XAMACCOUNTINFO::AccountSubscriptionTier::kSubscriptionTierGold),
    UserSetting(
        UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY,
        xe::string_util::read_u16string_and_swap(u"gamercard_picture_key"))};

UserSetting::UserSetting(UserSetting& setting) : UserData(setting) {
  setting_id_ = setting.setting_id_;
  setting_source_ = setting.setting_source_;
}

UserSetting::UserSetting(const UserSetting& setting) : UserData(setting) {
  setting_id_ = setting.setting_id_;
  setting_source_ = setting.setting_source_;
}

UserSetting::UserSetting(UserSettingId setting_id, UserDataTypes setting_data)
    : UserData(get_type(static_cast<uint32_t>(setting_id)), setting_data),
      setting_id_(setting_id),
      setting_source_(X_USER_PROFILE_SETTING_SOURCE::DEFAULT) {}

UserSetting::UserSetting(const X_USER_PROFILE_SETTING* profile_setting)
    : UserData(UserSetting::get_type(profile_setting->setting_id),
               UserSetting::get_max_size(profile_setting->setting_id),
               &profile_setting->data),
      setting_id_(
          static_cast<UserSettingId>(profile_setting->setting_id.get())),
      setting_source_(X_USER_PROFILE_SETTING_SOURCE::DEFAULT) {}

UserSetting::UserSetting(const X_XDBF_GPD_SETTING_HEADER* profile_setting,
                         std::span<const uint8_t> extended_data)
    : UserData(profile_setting->setting_type, &profile_setting->base_data,
               extended_data),
      setting_id_(
          static_cast<UserSettingId>(profile_setting->setting_id.get())),
      setting_source_(X_USER_PROFILE_SETTING_SOURCE::TITLE) {}

std::optional<UserSetting> UserSetting::GetDefaultSetting(
    const UserProfile* user, uint32_t setting_id) {
  for (const auto& setting : default_setting_values) {
    if (setting.get_setting_id() == setting_id) {
      return std::make_optional<UserSetting>(setting);
    }
  }

  const auto type = UserData::get_type(setting_id);

  switch (type) {
    case X_USER_DATA_TYPE::CONTEXT:
    case X_USER_DATA_TYPE::INT32:
    case X_USER_DATA_TYPE::UNSET:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), 0);
    case X_USER_DATA_TYPE::INT64:
    case X_USER_DATA_TYPE::DATETIME:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), static_cast<int64_t>(0));
    case X_USER_DATA_TYPE::DOUBLE:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), 0.0);
    case X_USER_DATA_TYPE::WSTRING:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), std::u16string());
    case X_USER_DATA_TYPE::FLOAT:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), 0.0f);
    case X_USER_DATA_TYPE::BINARY:
      return std::make_optional<UserSetting>(
          static_cast<UserSettingId>(setting_id), std::vector<uint8_t>());
    default:
      assert_always();
  }

  XELOGE("{}: Unknown X_USER_DATA_TYPE: {}", __func__,
         static_cast<uint8_t>(type));
  return std::nullopt;
}

void UserSetting::WriteToGuest(X_USER_PROFILE_SETTING* setting_ptr,
                               uint32_t& extended_data_address) {
  if (!setting_ptr) {
    return;
  }

  memcpy(&setting_ptr->data.data, &data_.data, sizeof(X_USER_DATA_UNION));
  setting_ptr->data.type = data_.type;

  if (requires_additional_data()) {
    const auto extended_data = get_extended_data();

    if (extended_data.empty()) {
      return;
    }

    setting_ptr->data.data.binary.size =
        static_cast<uint32_t>(extended_data_.size());
    setting_ptr->data.data.binary.ptr = extended_data_address;

    memcpy(kernel_memory()->TranslateVirtual(extended_data_address),
           extended_data_.data(), extended_data_.size());

    extended_data_address += static_cast<uint32_t>(extended_data_.size());
  }
}

std::vector<uint8_t> UserSetting::Serialize() const {
  std::vector<uint8_t> data(sizeof(X_XDBF_GPD_SETTING_HEADER) +
                            extended_data_.size());

  X_XDBF_GPD_SETTING_HEADER header = {};

  header.setting_id = static_cast<uint32_t>(setting_id_);
  header.setting_type = data_.type;

  memcpy(&header.base_data, &data_.data, sizeof(X_USER_DATA_UNION));

  // Copy header to vector
  memcpy(data.data(), &header, sizeof(X_XDBF_GPD_SETTING_HEADER));

  memcpy(data.data() + sizeof(X_XDBF_GPD_SETTING_HEADER), extended_data_.data(),
         extended_data_.size());

  return data;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

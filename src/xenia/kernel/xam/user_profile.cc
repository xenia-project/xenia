/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/user_profile.h"

#include <sstream>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
namespace xam {

UserProfile::UserProfile(uint64_t xuid, X_XAMACCOUNTINFO* account_info)
    : xuid_(xuid), account_info_(*account_info) {
  // 58410A1F checks the user XUID against a mask of 0x00C0000000000000 (3<<54),
  // if non-zero, it prevents the user from playing the game.
  // "You do not have permissions to perform this operation."

  // https://cs.rin.ru/forum/viewtopic.php?f=38&t=60668&hilit=gfwl+live&start=195
  // https://github.com/arkem/py360/blob/master/py360/constants.py
  // XPROFILE_GAMER_YAXIS_INVERSION
  AddSetting(std::make_unique<UserSetting>(0x10040002, 0));
  // XPROFILE_OPTION_CONTROLLER_VIBRATION
  AddSetting(std::make_unique<UserSetting>(0x10040003, 3));
  // XPROFILE_GAMERCARD_ZONE
  AddSetting(std::make_unique<UserSetting>(0x10040004, 0));
  // XPROFILE_GAMERCARD_REGION
  AddSetting(std::make_unique<UserSetting>(0x10040005, 0));
  // XPROFILE_GAMERCARD_CRED
  AddSetting(std::make_unique<UserSetting>(0x10040006, 0xFA));
  // XPROFILE_OPTION_VOICE_MUTED
  AddSetting(std::make_unique<UserSetting>(0x1004000C, 3));
  // XPROFILE_OPTION_VOICE_THRU_SPEAKERS
  AddSetting(std::make_unique<UserSetting>(0x1004000D, 3));
  // XPROFILE_OPTION_VOICE_VOLUME
  AddSetting(std::make_unique<UserSetting>(0x1004000E, 0x64));
  // XPROFILE_GAMERCARD_TITLES_PLAYED
  AddSetting(std::make_unique<UserSetting>(0x10040012, 1));
  // XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED
  AddSetting(std::make_unique<UserSetting>(0x10040013, 0));
  // XPROFILE_GAMER_DIFFICULTY
  AddSetting(std::make_unique<UserSetting>(0x10040015, 0));
  // XPROFILE_GAMER_CONTROL_SENSITIVITY
  AddSetting(std::make_unique<UserSetting>(0x10040018, 0));
  // Preferred color 1
  AddSetting(std::make_unique<UserSetting>(0x1004001D, PREFERRED_COLOR_NONE));
  // Preferred color 2
  AddSetting(std::make_unique<UserSetting>(0x1004001E, PREFERRED_COLOR_NONE));
  // XPROFILE_GAMER_ACTION_AUTO_AIM
  AddSetting(std::make_unique<UserSetting>(0x10040022, 1));
  // XPROFILE_GAMER_ACTION_AUTO_CENTER
  AddSetting(std::make_unique<UserSetting>(0x10040023, 0));
  // XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL
  AddSetting(std::make_unique<UserSetting>(0x10040024, 0));
  // XPROFILE_GAMER_RACE_TRANSMISSION
  AddSetting(std::make_unique<UserSetting>(0x10040026, 0));
  // XPROFILE_GAMER_RACE_CAMERA_LOCATION
  AddSetting(std::make_unique<UserSetting>(0x10040027, 0));
  // XPROFILE_GAMER_RACE_BRAKE_CONTROL
  AddSetting(std::make_unique<UserSetting>(0x10040028, 0));
  // XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL
  AddSetting(std::make_unique<UserSetting>(0x10040029, 0));
  // XPROFILE_GAMERCARD_TITLE_CRED_EARNED
  AddSetting(std::make_unique<UserSetting>(0x10040038, 0));
  // XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED
  AddSetting(std::make_unique<UserSetting>(0x10040039, 0));

  // XPROFILE_GAMERCARD_MOTTO
  AddSetting(std::make_unique<UserSetting>(0x402C0011, u""));
  // XPROFILE_GAMERCARD_PICTURE_KEY
  AddSetting(
      std::make_unique<UserSetting>(0x4064000F, u"gamercard_picture_key"));
  // XPROFILE_GAMERCARD_REP
  AddSetting(std::make_unique<UserSetting>(0x5004000B, 0.0f));

  // XPROFILE_TITLE_SPECIFIC1
  AddSetting(std::make_unique<UserSetting>(0x63E83FFF, std::vector<uint8_t>()));
  // XPROFILE_TITLE_SPECIFIC2
  AddSetting(std::make_unique<UserSetting>(0x63E83FFE, std::vector<uint8_t>()));
  // XPROFILE_TITLE_SPECIFIC3
  AddSetting(std::make_unique<UserSetting>(0x63E83FFD, std::vector<uint8_t>()));
}

void UserProfile::AddSetting(std::unique_ptr<UserSetting> setting) {
  UserSetting* previous_setting = setting.get();

  std::swap(settings_[setting->GetSettingId()], previous_setting);

  if (setting->is_title_specific()) {
    SaveSetting(setting.get());
  }

  if (previous_setting) {
    // replace: swap out the old setting from the owning list
    for (auto vec_it = setting_list_.begin(); vec_it != setting_list_.end();
         ++vec_it) {
      if (vec_it->get() == previous_setting) {
        vec_it->swap(setting);
        break;
      }
    }
  } else {
    // new setting: add to the owning list
    setting_list_.push_back(std::move(setting));
  }
}

UserSetting* UserProfile::GetSetting(uint32_t setting_id) {
  const auto& it = settings_.find(setting_id);
  if (it == settings_.end()) {
    return nullptr;
  }

  UserSetting* setting = it->second;
  if (setting->is_title_specific()) {
    // If what we have loaded in memory isn't for the title that is running
    // right now, then load it from disk.
    LoadSetting(setting);
  }
  return setting;
}

void UserProfile::LoadSetting(UserSetting* setting) {
  if (setting->is_title_specific()) {
    const std::filesystem::path content_dir =
        kernel_state()->content_manager()->ResolveGameUserContentPath(xuid_);
    const std::string setting_id_str =
        fmt::format("{:08X}", setting->GetSettingId());
    const std::filesystem::path file_path = content_dir / setting_id_str;
    FILE* file = xe::filesystem::OpenFile(file_path, "rb");
    if (!file) {
      return;
    }

    const uint32_t input_file_size =
        static_cast<uint32_t>(std::filesystem::file_size(file_path));

    if (input_file_size < sizeof(X_USER_PROFILE_SETTING_HEADER)) {
      fclose(file);
      // Setting seems to be invalid, remove it.
      std::filesystem::remove(file_path);
      return;
    }

    X_USER_PROFILE_SETTING_HEADER header;
    fread(&header, sizeof(X_USER_PROFILE_SETTING_HEADER), 1, file);
    if (header.setting_id != setting->GetSettingId()) {
      // It's setting with different ID? Corrupted perhaps.
      fclose(file);
      std::filesystem::remove(file_path);
      return;
    }

    // TODO(Gliniak): Right now we only care about CONTENT, WSTRING, BINARY
    setting->SetNewSettingHeader(&header);
    setting->SetNewSettingSource(X_USER_PROFILE_SETTING_SOURCE::TITLE);
    std::vector<uint8_t> serialized_data(setting->GetSettingHeader()->size);
    fread(serialized_data.data(), 1, serialized_data.size(), file);
    fclose(file);
    setting->GetSettingData()->Deserialize(serialized_data);
  } else {
    // Unsupported for now.  Other settings aren't per-game and need to be
    // stored some other way.
    XELOGW("Attempting to load unsupported profile setting 0x{:08X} from disk",
           setting->GetSettingId());
  }
}

void UserProfile::SaveSetting(UserSetting* setting) {
  if (setting->is_title_specific() &&
      setting->GetSettingSource() == X_USER_PROFILE_SETTING_SOURCE::TITLE) {
    const std::filesystem::path content_dir =
        kernel_state()->content_manager()->ResolveGameUserContentPath(xuid_);

    std::filesystem::create_directories(content_dir);

    const std::string setting_id_str =
        fmt::format("{:08X}", setting->GetSettingId());
    std::filesystem::path file_path = content_dir / setting_id_str;
    FILE* file = xe::filesystem::OpenFile(file_path, "wb");

    if (!file) {
      return;
    }

    const std::vector<uint8_t> serialized_setting =
        setting->GetSettingData()->Serialize();
    const uint32_t serialized_setting_length = std::min(
        kMaxSettingSize, static_cast<uint32_t>(serialized_setting.size()));

    fwrite(setting->GetSettingHeader(), sizeof(X_USER_PROFILE_SETTING_HEADER),
           1, file);
    // Writing data
    fwrite(serialized_setting.data(), 1, serialized_setting_length, file);
    fclose(file);
  } else {
    // Unsupported for now.  Other settings aren't per-game and need to be
    // stored some other way.
    XELOGW("Attempting to save unsupported profile setting 0x{:08X} from disk",
           setting->GetSettingId());
  }
}

bool UserProfile::AddProperty(const Property* property) {
  // Find if property already exits
  Property* entry = GetProperty(property->GetPropertyId());
  if (entry) {
    *entry = *property;
    return true;
  }

  properties_.push_back(*property);
  return true;
}

Property* UserProfile::GetProperty(const AttributeKey id) {
  for (auto& entry : properties_) {
    if (entry.GetPropertyId().value != id.value) {
      continue;
    }

    return &entry;
  }

  return nullptr;
}

AchievementGpdStructure* UserProfile::GetAchievement(const uint32_t title_id,
                                                     const uint32_t id) {
  auto title_achievements = achievements_.find(title_id);
  if (title_achievements == achievements_.end()) {
    return nullptr;
  }

  for (auto& entry : title_achievements->second) {
    if (entry.achievement_id == id) {
      return &entry;
    }
  }
  return nullptr;
}

std::vector<AchievementGpdStructure>* UserProfile::GetTitleAchievements(
    const uint32_t title_id) {
  auto title_achievements = achievements_.find(title_id);
  if (title_achievements == achievements_.end()) {
    return nullptr;
  }

  return &title_achievements->second;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

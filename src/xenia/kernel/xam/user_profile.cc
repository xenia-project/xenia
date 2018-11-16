/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <sstream>

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/base/clock.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/mapped_memory.h"

namespace xe {
namespace kernel {
namespace xam {

constexpr uint32_t kDashboardID = 0xFFFE07D1;

UserProfile::UserProfile() {
  xuid_ = 0xBABEBABEBABEBABE;
  name_ = "User";

  // https://cs.rin.ru/forum/viewtopic.php?f=38&t=60668&hilit=gfwl+live&start=195
  // https://github.com/arkem/py360/blob/master/py360/constants.py
  // XPROFILE_GAMER_YAXIS_INVERSION
  AddSetting(std::make_unique<Int32Setting>(0x10040002, 0));
  // XPROFILE_OPTION_CONTROLLER_VIBRATION
  AddSetting(std::make_unique<Int32Setting>(0x10040003, 3));
  // XPROFILE_GAMERCARD_ZONE
  AddSetting(std::make_unique<Int32Setting>(0x10040004, 0));
  // XPROFILE_GAMERCARD_REGION
  AddSetting(std::make_unique<Int32Setting>(0x10040005, 0));
  // XPROFILE_GAMERCARD_CRED
  AddSetting(std::make_unique<Int32Setting>(0x10040006, 0xFA));
  // XPROFILE_GAMERCARD_REP
  AddSetting(std::make_unique<FloatSetting>(0x5004000B, 0.0f));
  // XPROFILE_OPTION_VOICE_MUTED
  AddSetting(std::make_unique<Int32Setting>(0x1004000C, 0));
  // XPROFILE_OPTION_VOICE_THRU_SPEAKERS
  AddSetting(std::make_unique<Int32Setting>(0x1004000D, 0));
  // XPROFILE_OPTION_VOICE_VOLUME
  AddSetting(std::make_unique<Int32Setting>(0x1004000E, 0x64));
  // XPROFILE_GAMERCARD_MOTTO
  AddSetting(std::make_unique<UnicodeSetting>(0x402C0011, L""));
  // XPROFILE_GAMERCARD_TITLES_PLAYED
  AddSetting(std::make_unique<Int32Setting>(0x10040012, 1));
  // XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED
  AddSetting(std::make_unique<Int32Setting>(0x10040013, 0));
  // XPROFILE_GAMER_DIFFICULTY
  AddSetting(std::make_unique<Int32Setting>(0x10040015, 0));
  // XPROFILE_GAMER_CONTROL_SENSITIVITY
  AddSetting(std::make_unique<Int32Setting>(0x10040018, 0));
  // Preferred color 1
  AddSetting(std::make_unique<Int32Setting>(0x1004001D, 0xFFFF0000u));
  // Preferred color 2
  AddSetting(std::make_unique<Int32Setting>(0x1004001E, 0xFF00FF00u));
  // XPROFILE_GAMER_ACTION_AUTO_AIM
  AddSetting(std::make_unique<Int32Setting>(0x10040022, 1));
  // XPROFILE_GAMER_ACTION_AUTO_CENTER
  AddSetting(std::make_unique<Int32Setting>(0x10040023, 0));
  // XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL
  AddSetting(std::make_unique<Int32Setting>(0x10040024, 0));
  // XPROFILE_GAMER_RACE_TRANSMISSION
  AddSetting(std::make_unique<Int32Setting>(0x10040026, 0));
  // XPROFILE_GAMER_RACE_CAMERA_LOCATION
  AddSetting(std::make_unique<Int32Setting>(0x10040027, 0));
  // XPROFILE_GAMER_RACE_BRAKE_CONTROL
  AddSetting(std::make_unique<Int32Setting>(0x10040028, 0));
  // XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL
  AddSetting(std::make_unique<Int32Setting>(0x10040029, 0));
  // XPROFILE_GAMERCARD_TITLE_CRED_EARNED
  AddSetting(std::make_unique<Int32Setting>(0x10040038, 0));
  // XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED
  AddSetting(std::make_unique<Int32Setting>(0x10040039, 0));

  // If we set this, games will try to get it.
  // XPROFILE_GAMERCARD_PICTURE_KEY
  AddSetting(
      std::make_unique<UnicodeSetting>(0x4064000F, L"gamercard_picture_key"));

  // XPROFILE_TITLE_SPECIFIC1
  AddSetting(std::make_unique<BinarySetting>(0x63E83FFF));
  // XPROFILE_TITLE_SPECIFIC2
  AddSetting(std::make_unique<BinarySetting>(0x63E83FFE));
  // XPROFILE_TITLE_SPECIFIC3
  AddSetting(std::make_unique<BinarySetting>(0x63E83FFD));

  // Try loading profile GPD files...
  LoadGpdFiles();
}

void UserProfile::LoadGpdFiles() {
  auto mmap_ =
      MappedMemory::Open(L"profile\\FFFE07D1.gpd", MappedMemory::Mode::kRead);
  if (!mmap_) {
    XELOGW(
        "Failed to open dash GPD (FFFE07D1.gpd) for reading, using blank one");
    return;
  }

  dash_gpd_.Read(mmap_->data(), mmap_->size());
  mmap_->Close();

  std::vector<util::XdbfTitlePlayed> titles;
  dash_gpd_.GetTitles(&titles);

  for (auto title : titles) {
    wchar_t fname[256];
    _swprintf(fname, L"profile\\%X.gpd", title.title_id);
    mmap_ = MappedMemory::Open(fname, MappedMemory::Mode::kRead);
    if (!mmap_) {
      XELOGE("Failed to open GPD for title %X (%ws)!", title.title_id,
             title.title_name.c_str());
      continue;
    }

    util::GpdFile title_gpd;
    title_gpd.Read(mmap_->data(), mmap_->size());
    mmap_->Close();

    title_gpds_[title.title_id] = title_gpd;
  }
}

util::GpdFile* UserProfile::SetTitleSpaData(const util::SpaFile& spa_data) {
  uint32_t spa_title = spa_data.GetTitleId();

  std::vector<util::XdbfAchievement> spa_achievements;
  // TODO: let user choose locale?
  spa_data.GetAchievements(spa_data.GetDefaultLocale(), &spa_achievements);

  util::XdbfTitlePlayed title_info;

  auto gpd = title_gpds_.find(spa_title);
  if (gpd != title_gpds_.end()) {
    auto& title_gpd = (*gpd).second;

    bool always_update_title = false;
    if (!dash_gpd_.GetTitle(spa_title, &title_info)) {
      assert_always();
      XELOGE(
          "GPD exists but is missing XbdfTitlePlayed entry? (this shouldn't be "
          "happening!)");
      // Try to work around it...
      title_info.title_name = xe::to_wstring(spa_data.GetTitleName());
      title_info.title_id = spa_title;
      title_info.achievements_possible = 0;
      title_info.achievements_earned = 0;
      title_info.gamerscore_total = 0;
      title_info.gamerscore_earned = 0;
      always_update_title = true;
    }
    title_info.last_played = Clock::QueryHostSystemTime();

    // Check SPA for any achievements current GPD might be missing
    // (maybe added in TUs etc?)
    bool ach_updated = false;
    for (auto ach : spa_achievements) {
      bool ach_exists = title_gpd.GetAchievement(ach.id, nullptr);
      if (ach_exists && !always_update_title) {
        continue;
      }

      // Achievement doesn't exist in current title info, lets add it
      title_info.achievements_possible++;
      title_info.gamerscore_total += ach.gamerscore;

      // If it doesn't exist in GPD, add it to that too
      if (!ach_exists) {
        XELOGD(
            "Adding new achievement %d (%ws) from SPA (wasn't inside existing "
            "GPD)",
            ach.id, ach.label.c_str());

        ach_updated = true;
        title_gpd.UpdateAchievement(ach);
      }
    }

    // Update dash with new title_info
    dash_gpd_.UpdateTitle(title_info);

    // Only write game GPD if achievements were updated
    if (ach_updated) {
      UpdateGpd(spa_title, title_gpd);
    }
    UpdateGpd(kDashboardID, dash_gpd_);
  } else {
    // GPD not found... have to create it!
    XELOGD("Creating new GPD for title %X", spa_title);

    title_info.title_name = xe::to_wstring(spa_data.GetTitleName());
    title_info.title_id = spa_title;
    title_info.last_played = Clock::QueryHostSystemTime();

    // Copy cheevos from SPA -> GPD
    util::GpdFile title_gpd;
    for (auto ach : spa_achievements) {
      title_gpd.UpdateAchievement(ach);

      title_info.achievements_possible++;
      title_info.gamerscore_total += ach.gamerscore;
    }

    // Try copying achievement images if we can...
    for (auto ach : spa_achievements) {
      auto* image_entry = spa_data.GetEntry(
          static_cast<uint16_t>(util::XdbfSpaSection::kImage), ach.image_id);
      if (image_entry) {
        title_gpd.UpdateEntry(*image_entry);
      }
    }

    // Try adding title image & name
    auto* title_image =
        spa_data.GetEntry(static_cast<uint16_t>(util::XdbfSpaSection::kImage),
                          static_cast<uint64_t>(util::XdbfSpaID::Title));
    if (title_image) {
      title_gpd.UpdateEntry(*title_image);
    }

    auto title_name = xe::to_wstring(spa_data.GetTitleName());
    if (title_name.length()) {
      util::XdbfEntry title_name_ent;
      title_name_ent.info.section =
          static_cast<uint16_t>(util::XdbfGpdSection::kString);
      title_name_ent.info.id = static_cast<uint64_t>(util::XdbfSpaID::Title);
      title_name_ent.data.resize((title_name.length() + 1) * 2);
      xe::copy_and_swap((wchar_t*)title_name_ent.data.data(),
                        title_name.c_str(), title_name.length());
      title_gpd.UpdateEntry(title_name_ent);
    }

    title_gpds_[spa_title] = title_gpd;

    // Update dash GPD with title and write updated GPDs
    dash_gpd_.UpdateTitle(title_info);

    UpdateGpd(spa_title, title_gpd);
    UpdateGpd(kDashboardID, dash_gpd_);
  }

  curr_gpd_ = &title_gpds_[spa_title];
  curr_title_id_ = spa_title;
  return curr_gpd_;
}
util::GpdFile* UserProfile::GetTitleGpd(uint32_t title_id) {
  if (!title_id) {
    return curr_gpd_;
  }

  auto gpd = title_gpds_.find(title_id);
  if (gpd == title_gpds_.end()) {
    return nullptr;
  }

  return &(*gpd).second;
}

bool UserProfile::UpdateTitleGpd() {
  if (!curr_gpd_ || curr_title_id_ == -1) {
    return false;
  }

  bool result = UpdateGpd(curr_title_id_, *curr_gpd_);
  if (!result) {
    XELOGE("UpdateTitleGpd failed on title %X!", curr_title_id_);
  } else {
    XELOGD("Updated title %X GPD successfully!", curr_title_id_);
  }
  return result;
}

bool UserProfile::UpdateAllGpds() {
  // TODO: optimize so we only have to update the current title?
  for (const auto& pair : title_gpds_) {
    auto gpd = pair.second;
    bool result = UpdateGpd(pair.first, gpd);
    if (!result) {
      XELOGE("UpdateGpdFiles failed on title %X!", pair.first);
      continue;
    }
  }

  // No need to update dash GPD here, the UpdateGpd func should take care of it
  // when needed
  return true;
}

bool UserProfile::UpdateGpd(uint32_t title_id, util::GpdFile& gpd_data) {
  size_t gpd_length = 0;
  if (!gpd_data.Write(nullptr, &gpd_length)) {
    XELOGE("Failed to get GPD size for title %X!", title_id);
    return false;
  }

  if (!filesystem::PathExists(L"profile\\")) {
    filesystem::CreateFolder(L"profile\\");
  }

  wchar_t fname[256];
  _swprintf(fname, L"profile\\%X.gpd", title_id);

  filesystem::CreateFile(fname);
  auto mmap_ =
      MappedMemory::Open(fname, MappedMemory::Mode::kReadWrite, 0, gpd_length);
  if (!mmap_) {
    XELOGE("Failed to open %X.gpd for writing!", title_id);
    return false;
  }

  bool ret_val = true;

  if (!gpd_data.Write(mmap_->data(), &gpd_length)) {
    XELOGE("Failed to write GPD data for %X!", title_id);
    ret_val = false;
  } else {
    // Check if we need to update dashboard data...
    if (title_id != kDashboardID) {
      util::XdbfTitlePlayed title_info;
      if (dash_gpd_.GetTitle(title_id, &title_info)) {
        std::vector<util::XdbfAchievement> gpd_achievements;
        // TODO: let user choose locale?
        gpd_data.GetAchievements(&gpd_achievements);
        uint32_t num_ach_total = 0;
        uint32_t num_ach_earned = 0;
        uint32_t gamerscore_total = 0;
        uint32_t gamerscore_earned = 0;
        for (auto ach : gpd_achievements) {
          num_ach_total++;
          gamerscore_total += ach.gamerscore;
          if (ach.IsUnlocked()) {
            num_ach_earned++;
            gamerscore_earned += ach.gamerscore;
          }
        }

        if (num_ach_total != title_info.achievements_possible ||
            num_ach_earned != title_info.achievements_earned ||
            gamerscore_total != title_info.gamerscore_total ||
            gamerscore_earned != title_info.gamerscore_earned) {
          title_info.achievements_possible = num_ach_total;
          title_info.achievements_earned = num_ach_earned;
          title_info.gamerscore_total = gamerscore_total;
          title_info.gamerscore_earned = gamerscore_earned;

          dash_gpd_.UpdateTitle(title_info);
          UpdateGpd(kDashboardID, dash_gpd_);
        }
      }
    }
  }

  mmap_->Close(gpd_length);
  return ret_val;
}

void UserProfile::AddSetting(std::unique_ptr<Setting> setting) {
  Setting* previous_setting = setting.get();
  std::swap(settings_[setting->setting_id], previous_setting);

  if (setting->is_set && setting->is_title_specific()) {
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

UserProfile::Setting* UserProfile::GetSetting(uint32_t setting_id) {
  const auto& it = settings_.find(setting_id);
  if (it == settings_.end()) {
    return nullptr;
  }
  UserProfile::Setting* setting = it->second;
  if (setting->is_title_specific()) {
    // If what we have loaded in memory isn't for the title that is running
    // right now, then load it from disk.
    if (kernel_state()->title_id() != setting->loaded_title_id) {
      LoadSetting(setting);
    }
  }
  return setting;
}

void UserProfile::LoadSetting(UserProfile::Setting* setting) {
  if (setting->is_title_specific()) {
    auto content_dir =
        kernel_state()->content_manager()->ResolveGameUserContentPath();
    auto setting_id = xe::format_string(L"%.8X", setting->setting_id);
    auto file_path = xe::join_paths(content_dir, setting_id);
    auto file = xe::filesystem::OpenFile(file_path, "rb");
    if (file) {
      fseek(file, 0, SEEK_END);
      uint32_t input_file_size = static_cast<uint32_t>(ftell(file));
      fseek(file, 0, SEEK_SET);

      std::vector<uint8_t> serialized_data(input_file_size);
      fread(serialized_data.data(), 1, serialized_data.size(), file);
      fclose(file);
      setting->Deserialize(serialized_data);
      setting->loaded_title_id = kernel_state()->title_id();
    }
  } else {
    // Unsupported for now.  Other settings aren't per-game and need to be
    // stored some other way.
    XELOGW("Attempting to load unsupported profile setting from disk");
  }
}

void UserProfile::SaveSetting(UserProfile::Setting* setting) {
  if (setting->is_title_specific()) {
    auto serialized_setting = setting->Serialize();
    auto content_dir =
        kernel_state()->content_manager()->ResolveGameUserContentPath();
    xe::filesystem::CreateFolder(content_dir);
    auto setting_id = xe::format_string(L"%.8X", setting->setting_id);
    auto file_path = xe::join_paths(content_dir, setting_id);
    auto file = xe::filesystem::OpenFile(file_path, "wb");
    fwrite(serialized_setting.data(), 1, serialized_setting.size(), file);
    fclose(file);
  } else {
    // Unsupported for now.  Other settings aren't per-game and need to be
    // stored some other way.
    XELOGW("Attempting to save unsupported profile setting to disk");
  }
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

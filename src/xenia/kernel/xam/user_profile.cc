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
#include "xenia/base/cvar.h"
#include "xenia/base/clock.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/kernel/util/crypto_utils.h"
#include "xenia/kernel/xam/user_profile.h"

namespace xe {
namespace kernel {
namespace xam {

DEFINE_string(profile_directory, "Content\\Profile\\",
              "The directory to store profile data inside", "Kernel");

constexpr uint32_t kDashboardID = 0xFFFE07D1;

std::string X_XAMACCOUNTINFO::GetGamertagString() const {
  return xe::to_string(std::wstring(gamertag));
}

bool UserProfile::DecryptAccountFile(const uint8_t* data,
                                     X_XAMACCOUNTINFO* output, bool devkit) {
  const uint8_t* key = util::GetXeKey(0x19, devkit);
  if (!key) {
    return false;  // this shouldn't happen...
  }

  // Generate RC4 key from data hash
  uint8_t rc4_key[0x14];
  util::HmacSha(key, 0x10, data, 0x10, 0, 0, 0, 0, rc4_key, 0x14);

  uint8_t dec_data[sizeof(X_XAMACCOUNTINFO) + 8];

  // Decrypt data
  util::RC4(rc4_key, 0x10, data + 0x10, sizeof(dec_data), dec_data,
            sizeof(dec_data));

  // Verify decrypted data against hash
  uint8_t data_hash[0x14];
  util::HmacSha(key, 0x10, dec_data, sizeof(dec_data), 0, 0, 0, 0, data_hash,
                0x14);

  if (std::memcmp(data, data_hash, 0x10) == 0) {
    // Copy account data to output
    std::memcpy(output, dec_data + 8, sizeof(X_XAMACCOUNTINFO));

    // Swap gamertag endian
    xe::copy_and_swap<wchar_t>(output->gamertag, output->gamertag, 0x10);
    return true;
  }

  return false;
}

void UserProfile::EncryptAccountFile(const X_XAMACCOUNTINFO* input,
                                     uint8_t* output, bool devkit) {
  const uint8_t* key = util::GetXeKey(0x19, devkit);
  if (!key) {
    return;  // this shouldn't happen...
  }

  X_XAMACCOUNTINFO* output_acct = (X_XAMACCOUNTINFO*)(output + 0x18);
  std::memcpy(output_acct, input, sizeof(X_XAMACCOUNTINFO));

  // Swap gamertag endian
  xe::copy_and_swap<wchar_t>(output_acct->gamertag, output_acct->gamertag,
                             0x10);

  // Set confounder, should be random but meh
  std::memset(output + 0x10, 0xFD, 8);

  // Encrypted data = xam account info + 8 byte confounder
  uint32_t enc_data_size = sizeof(X_XAMACCOUNTINFO) + 8;

  // Set data hash
  uint8_t data_hash[0x14];
  util::HmacSha(key, 0x10, output + 0x10, enc_data_size, 0, 0, 0, 0, data_hash,
                0x14);

  std::memcpy(output, data_hash, 0x10);

  // Generate RC4 key from data hash
  uint8_t rc4_key[0x14];
  util::HmacSha(key, 0x10, data_hash, 0x10, 0, 0, 0, 0, rc4_key, 0x14);

  // Encrypt data
  util::RC4(rc4_key, 0x10, output + 0x10, enc_data_size, output + 0x10,
            enc_data_size);
}

UserProfile::UserProfile() : dash_gpd_(kDashboardID) {
  account_.xuid_online = 0xE000BABEBABEBABE;
  wcscpy(account_.gamertag, L"XeniaUser");

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
  LoadProfile();
}

void UserProfile::LoadProfile() {
  auto mmap_ =
      MappedMemory::Open(xe::to_wstring(cvars::profile_directory) + L"Account",
                         MappedMemory::Mode::kRead);
  if (mmap_) {
    XELOGI("Loading Account file from path %SAccount",
           xe::to_wstring(cvars::profile_directory).c_str());

    X_XAMACCOUNTINFO tmp_acct;
    bool success = DecryptAccountFile(mmap_->data(), &tmp_acct);
    if (!success) {
      success = DecryptAccountFile(mmap_->data(), &tmp_acct, true);
    }

    if (!success) {
      XELOGW("Failed to decrypt Account file data");
    } else {
      std::memcpy(&account_, &tmp_acct, sizeof(X_XAMACCOUNTINFO));
      XELOGI("Loaded Account \"%s\" successfully!", name().c_str());
    }

    mmap_->Close();
  }

  XELOGI("Loading profile GPDs from path %S", xe::to_wstring(cvars::profile_directory).c_str());

  mmap_ = MappedMemory::Open(
      xe::to_wstring(cvars::profile_directory) + L"FFFE07D1.gpd",
      MappedMemory::Mode::kRead);
  if (!mmap_) {
    XELOGW(
        "Failed to open dash GPD (FFFE07D1.gpd) for reading, using blank one");
    return;
  }

  dash_gpd_.Read(mmap_->data(), mmap_->size());
  mmap_->Close();

  std::vector<xdbf::TitlePlayed> titles;
  dash_gpd_.GetTitles(&titles);

  for (auto title : titles) {
    wchar_t fname[256];
    swprintf(fname, 256, L"%X.gpd", title.title_id);
    mmap_ = MappedMemory::Open(xe::to_wstring(cvars::profile_directory) + fname,
                               MappedMemory::Mode::kRead);
    if (!mmap_) {
      XELOGE("Failed to open GPD for title %X (%s)!", title.title_id,
             xe::to_string(title.title_name).c_str());
      continue;
    }

    xdbf::GpdFile title_gpd(title.title_id);
    bool result = title_gpd.Read(mmap_->data(), mmap_->size());
    mmap_->Close();

    if (!result) {
      XELOGE("Failed to read GPD for title %X (%s)!", title.title_id,
             xe::to_string(title.title_name).c_str());
      continue;
    }

    title_gpds_[title.title_id] = title_gpd;
  }

  XELOGI("Loaded %d profile GPDs", title_gpds_.size() + 1);
}

xdbf::GpdFile* UserProfile::SetTitleSpaData(const xdbf::SpaFile& spa_data) {
  uint32_t spa_title = spa_data.GetTitleId();

  std::vector<xdbf::Achievement> spa_achievements;
  // TODO: let user choose locale?
  spa_data.GetAchievements(spa_data.GetDefaultLocale(), &spa_achievements);

  xdbf::TitlePlayed title_info;

  auto gpd = title_gpds_.find(spa_title);
  if (gpd != title_gpds_.end()) {
    auto& title_gpd = (*gpd).second;

    XELOGI("Loaded existing GPD for title %X", spa_title);

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
            "Adding new achievement %d (%s) from SPA (wasn't inside existing "
            "GPD)",
            ach.id, xe::to_string(ach.label).c_str());

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
    XELOGI("Creating new GPD for title %X", spa_title);

    title_info.title_name = xe::to_wstring(spa_data.GetTitleName());
    title_info.title_id = spa_title;
    title_info.last_played = Clock::QueryHostSystemTime();

    // Copy cheevos from SPA -> GPD
    xdbf::GpdFile title_gpd(spa_title);
    for (auto ach : spa_achievements) {
      title_gpd.UpdateAchievement(ach);

      title_info.achievements_possible++;
      title_info.gamerscore_total += ach.gamerscore;
    }

    // Try copying achievement images if we can...
    for (auto ach : spa_achievements) {
      auto* image_entry = spa_data.GetEntry(
          static_cast<uint16_t>(xdbf::SpaSection::kImage), ach.image_id);
      if (image_entry) {
        title_gpd.UpdateEntry(*image_entry);
      }
    }

    // Try adding title image & name
    auto* title_image =
        spa_data.GetEntry(static_cast<uint16_t>(xdbf::SpaSection::kImage),
                          static_cast<uint64_t>(xdbf::SpaID::Title));
    if (title_image) {
      title_gpd.UpdateEntry(*title_image);
    }

    auto title_name = xe::to_wstring(spa_data.GetTitleName());
    if (title_name.length()) {
      xdbf::Entry title_name_ent;
      title_name_ent.info.section =
          static_cast<uint16_t>(xdbf::GpdSection::kString);
      title_name_ent.info.id = static_cast<uint64_t>(xdbf::SpaID::Title);
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

  // Print achievement list to log, ATM there's no other way for users to see
  // achievement status...
  std::vector<xdbf::Achievement> achievements;
  if (curr_gpd_->GetAchievements(&achievements)) {
    XELOGI("Achievement list:");

    for (auto ach : achievements) {
      // TODO: use ach.unachieved_desc for locked achievements?
      // depends on XdbfAchievementFlags::kShowUnachieved afaik
      XELOGI("%d - %s - %s - %d GS - %s", ach.id,
             xe::to_string(ach.label).c_str(),
             xe::to_string(ach.description).c_str(), ach.gamerscore,
             ach.IsUnlocked() ? "unlocked" : "locked");
    }

    XELOGI("Unlocked achievements: %d/%d, gamerscore: %d/%d\r\n",
           title_info.achievements_earned, title_info.achievements_possible,
           title_info.gamerscore_earned, title_info.gamerscore_total);
  }

  return curr_gpd_;
}

xdbf::GpdFile* UserProfile::GetTitleGpd(uint32_t title_id) {
  if (title_id == -1) {
    return curr_gpd_;
  }

  auto gpd = title_gpds_.find(title_id);
  if (gpd == title_gpds_.end()) {
    return nullptr;
  }

  return &(*gpd).second;
}

void UserProfile::GetTitles(std::vector<xdbf::GpdFile*>& titles) {
  for (auto title : title_gpds_) {
    titles.push_back(&title.second);
  }
}

bool UserProfile::UpdateTitleGpd(uint32_t title_id) {
  if (title_id == -1) {
    if (!curr_gpd_ || curr_title_id_ == -1) {
      return false;
    }
    title_id = curr_title_id_;
  }

  bool result = UpdateGpd(title_id, *curr_gpd_);
  if (!result) {
    XELOGE("UpdateTitleGpd failed on title %X!", title_id);
  } else {
    XELOGD("Updated title %X GPD successfully!", title_id);
  }
  return result;
}

bool UserProfile::UpdateAllGpds() {
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

bool UserProfile::UpdateGpd(uint32_t title_id, xdbf::GpdFile& gpd_data) {
  size_t gpd_length = 0;
  if (!gpd_data.Write(nullptr, &gpd_length)) {
    XELOGE("Failed to get GPD size for title %X!", title_id);
    return false;
  }

  if (!filesystem::PathExists(xe::to_wstring(cvars::profile_directory))) {
    filesystem::CreateFolder(xe::to_wstring(cvars::profile_directory));
  }

  wchar_t fname[256];
  swprintf(fname, 256, L"%X.gpd", title_id);

  filesystem::CreateFile(xe::to_wstring(cvars::profile_directory) + fname);
  auto mmap_ =
      MappedMemory::Open(xe::to_wstring(cvars::profile_directory) + fname,
                         MappedMemory::Mode::kReadWrite, 0, gpd_length);
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
      xdbf::TitlePlayed title_info;
      if (dash_gpd_.GetTitle(title_id, &title_info)) {
        std::vector<xdbf::Achievement> gpd_achievements;
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

        // Only update dash GPD if something has changed
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

          // TODO: update gamerscore/achievements earned/titles played settings
          // in dashboard GPD
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

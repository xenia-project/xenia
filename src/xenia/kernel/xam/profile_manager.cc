/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/profile_manager.h"

#include <filesystem>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/crypto_utils.h"
#include "xenia/vfs/devices/host_path_device.h"

DEFINE_string(logged_profile_slot_0_xuid, "",
              "XUID of the profile to load on boot in slot 0", "Profiles");
DEFINE_string(logged_profile_slot_1_xuid, "",
              "XUID of the profile to load on boot in slot 1", "Profiles");
DEFINE_string(logged_profile_slot_2_xuid, "",
              "XUID of the profile to load on boot in slot 2", "Profiles");
DEFINE_string(logged_profile_slot_3_xuid, "",
              "XUID of the profile to load on boot in slot 3", "Profiles");

namespace xe {
namespace kernel {
namespace xam {

bool ProfileManager::DecryptAccountFile(const uint8_t* data,
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
    xe::copy_and_swap<char16_t>(output->gamertag, output->gamertag, 0x10);
    return true;
  }

  return false;
}

void ProfileManager::EncryptAccountFile(const X_XAMACCOUNTINFO* input,
                                        uint8_t* output, bool devkit) {
  const uint8_t* key = util::GetXeKey(0x19, devkit);
  if (!key) {
    return;  // this shouldn't happen...
  }

  X_XAMACCOUNTINFO* output_acct =
      reinterpret_cast<X_XAMACCOUNTINFO*>(output + 0x18);
  std::memcpy(output_acct, input, sizeof(X_XAMACCOUNTINFO));

  // Swap gamertag endian
  xe::copy_and_swap<char16_t>(output_acct->gamertag, output_acct->gamertag,
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

ProfileManager::ProfileManager(KernelState* kernel_state)
    : kernel_state_(kernel_state) {
  logged_profiles_.clear();
  accounts_.clear();

  LoadAccounts(FindProfiles());

  if (!cvars::logged_profile_slot_0_xuid.empty()) {
    Login(xe::string_util::from_string<uint64_t>(
              cvars::logged_profile_slot_0_xuid, true),
          0);
  }

  if (!cvars::logged_profile_slot_1_xuid.empty()) {
    Login(xe::string_util::from_string<uint64_t>(
              cvars::logged_profile_slot_1_xuid, true),
          1);
  }

  if (!cvars::logged_profile_slot_2_xuid.empty()) {
    Login(xe::string_util::from_string<uint64_t>(
              cvars::logged_profile_slot_2_xuid, true),
          2);
  }

  if (!cvars::logged_profile_slot_3_xuid.empty()) {
    Login(xe::string_util::from_string<uint64_t>(
              cvars::logged_profile_slot_3_xuid, true),
          3);
  }
}

ProfileManager::~ProfileManager() {}

void ProfileManager::ReloadProfiles() { LoadAccounts(FindProfiles()); }

UserProfile* ProfileManager::GetProfile(const uint64_t xuid) const {
  const uint8_t user_index = GetUserIndexAssignedToProfile(xuid);
  if (user_index >= XUserMaxUserCount) {
    return nullptr;
  }

  return GetProfile(user_index);
}

UserProfile* ProfileManager::GetProfile(uint8_t user_index) const {
  if (user_index == XUserIndexLatest) {
    user_index = kernel_state_->emulator()->input_system()->GetLastUsedSlot();
  }

  if (user_index == XUserIndexNone) {
    return nullptr;
  }

  if (user_index == XUserIndexAny) {
    for (uint8_t i = 0; i < XUserMaxUserCount; i++) {
      if (!logged_profiles_.count(i)) {
        continue;
      }

      return logged_profiles_.at(i).get();
    }
  }

  if (!logged_profiles_.count(user_index)) {
    return nullptr;
  }

  return logged_profiles_.at(user_index).get();
}

bool ProfileManager::LoadAccount(const uint64_t xuid) {
  const std::string xuid_as_string = fmt::format("{:016X}", xuid);

  XELOGI("{}: Loading Account: {}", __func__, xuid_as_string);

  MountProfile(xuid);

  const std::string guest_path = xuid_as_string + ":\\Account";

  xe::vfs::File* output_file;
  xe::vfs::FileAction action = {};
  auto status = kernel_state_->file_system()->OpenFile(
      nullptr, guest_path, xe::vfs::FileDisposition::kOpen,
      xe::vfs::FileAccess::kFileReadData, false, true, &output_file, &action);

  if (XFAILED(status) || !output_file || !output_file->entry()) {
    XELOGI("{}: Failed to open Account file: {:08X}", __func__, status);
    DismountProfile(xuid);
    return false;
  }

  std::vector<uint8_t> file_data;
  file_data.resize(output_file->entry()->size());

  size_t bytes_read = 0;
  output_file->ReadSync(file_data.data(), output_file->entry()->size(), 0,
                        &bytes_read);
  output_file->Destroy();

  if (bytes_read < sizeof(X_XAMACCOUNTINFO)) {
    DismountProfile(xuid);
    return false;
  }

  X_XAMACCOUNTINFO tmp_acct;
  if (!ProfileManager::DecryptAccountFile(file_data.data(), &tmp_acct)) {
    if (!ProfileManager::DecryptAccountFile(file_data.data(), &tmp_acct,
                                            true)) {
      XELOGW("Failed to decrypt account data file for XUID: {}",
             xuid_as_string);
      return false;
    }
  }

  // We don't need profile to be mounted anymore, so for now let's close it.
  // We need it only when we want to login into this account!
  DismountProfile(xuid);

  accounts_.insert({xuid, tmp_acct});
  return true;
}

void ProfileManager::LoadAccounts(const std::vector<uint64_t> profiles_xuids) {
  for (const auto& path : profiles_xuids) {
    LoadAccount(path);
  }
}

void ProfileManager::ModifyGamertag(const uint64_t xuid, std::string gamertag) {
  if (!accounts_.count(xuid)) {
    return;
  }

  xe::X_XAMACCOUNTINFO* account = &accounts_[xuid];

  std::u16string gamertag_u16 = xe::to_utf16(gamertag);

  string_util::copy_truncating(account->gamertag, gamertag_u16,
                               sizeof(account->gamertag));

  if (!MountProfile(xuid)) {
    return;
  }

  UpdateAccount(xuid, account);
  DismountProfile(xuid);
}

bool ProfileManager::MountProfile(const uint64_t xuid, std::string mount_path) {
  std::filesystem::path profile_path = GetProfilePath(xuid);
  if (mount_path.empty()) {
    mount_path = fmt::format("{:016X}", xuid);
  }
  mount_path += ':';

  auto device =
      std::make_unique<vfs::HostPathDevice>(mount_path, profile_path, false);
  if (!device->Initialize()) {
    XELOGE(
        "MountProfile: Unable to mount {} profile; file not found or "
        "corrupted.",
        profile_path);
    return false;
  }
  return kernel_state_->file_system()->RegisterDevice(std::move(device));
}

bool ProfileManager::DismountProfile(const uint64_t xuid) {
  return kernel_state_->file_system()->UnregisterDevice(
      fmt::format("{:016X}", xuid) + ':');
}

void ProfileManager::Login(const uint64_t xuid, const uint8_t user_index,
                           bool notify) {
  if (logged_profiles_.size() >= 4 && user_index >= XUserMaxUserCount) {
    XELOGE(
        "Cannot login account with XUID: {:016X} due to lack of free slots "
        "(Max 4 accounts at once)",
        xuid);
    return;
  }

  if (user_index < XUserMaxUserCount) {
    const auto& profile = logged_profiles_.find(user_index);
    if (profile != logged_profiles_.cend()) {
      if (profile->second && profile->second->xuid() == xuid) {
        // Do nothing! User is already signed in to that slot.
        return;
      }
    }
  }

  // Find if xuid is already logged in. We might want to logout.
  for (auto& logged_profile : logged_profiles_) {
    if (logged_profile.second->xuid() == xuid) {
      Logout(logged_profile.first);
    }
  }

  if (!accounts_.count(xuid)) {
    return;
  }

  auto& profile = accounts_[xuid];
  const uint8_t assigned_user_slot =
      user_index < XUserMaxUserCount ? user_index : FindFirstFreeProfileSlot();

  XELOGI("Loaded {} (GUID: {:016X}) to slot {}", profile.GetGamertagString(),
         xuid, assigned_user_slot);

  logged_profiles_[assigned_user_slot] =
      std::make_unique<UserProfile>(xuid, &profile);

  if (kernel_state_->emulator()->is_title_open()) {
    const kernel::util::XdbfGameData db = kernel_state_->title_xdbf();
    if (db.is_valid()) {
      kernel_state_->xam_state()->achievement_manager()->LoadTitleAchievements(
          xuid, db);
    }
  }

  if (notify) {
    kernel_state_->BroadcastNotification(kXNotificationSystemSignInChanged,
                                         GetUsedUserSlots().to_ulong());
  }
  UpdateConfig(xuid, assigned_user_slot);
}

void ProfileManager::Logout(const uint8_t user_index, bool notify) {
  auto profile = logged_profiles_.find(user_index);
  if (profile == logged_profiles_.cend()) {
    return;
  }
  DismountProfile(profile->second->xuid());
  logged_profiles_.erase(profile);
  if (notify) {
    kernel_state_->BroadcastNotification(kXNotificationSystemSignInChanged,
                                         GetUsedUserSlots().to_ulong());
  }
  UpdateConfig(0, user_index);
}

void ProfileManager::LoginMultiple(
    const std::map<uint8_t, uint64_t>& profiles) {
  int slots_mask = 0;
  for (auto [slot, xuid] : profiles) {
    Login(xuid, slot, false);
    slots_mask |= (1 << slot);
  }

  kernel_state_->BroadcastNotification(kXNotificationSystemSignInChanged,
                                       slots_mask);
}

std::vector<uint64_t> ProfileManager::FindProfiles() const {
  // Info: Profile directory name is also it's offline xuid
  std::vector<uint64_t> profiles_xuids;

  auto profiles_directory = xe::filesystem::FilterByName(
      xe::filesystem::ListDirectories(
          kernel_state_->emulator()->content_root()),
      std::regex("[0-9A-F]{16}"));

  for (const auto& profile : profiles_directory) {
    const std::string profile_xuid = xe::path_to_utf8(profile.name);
    if (profile_xuid == fmt::format("{:016X}", 0)) {
      continue;
    }

    if (!std::filesystem::exists(
            profile.path / profile.name / kDashboardStringID /
            fmt::format("{:08X}",
                        static_cast<uint32_t>(XContentType::kProfile)) /
            profile.name)) {
      XELOGE("Profile {} doesn't have profile package!", profile_xuid);
      continue;
    }

    XELOGE("{}: Adding profile {} to profile list", __func__, profile_xuid);
    profiles_xuids.push_back(
        xe::string_util::from_string<uint64_t>(profile_xuid, true));
  }

  XELOGE("ProfileManager: Found {} Profiles", profiles_xuids.size());
  return profiles_xuids;
}

uint8_t ProfileManager::FindFirstFreeProfileSlot() const {
  if (!IsAnyProfileSignedIn()) {
    return 0;
  }

  std::bitset<XUserMaxUserCount> used_slots = {};
  for (const auto& [index, entry] : logged_profiles_) {
    used_slots.set(index);
  }

  for (uint8_t i = 0; i < used_slots.size(); ++i) {
    if (!used_slots[i]) {
      return i;
    }
  }
  return XUserIndexAny;
}

std::bitset<XUserMaxUserCount> ProfileManager::GetUsedUserSlots() const {
  std::bitset<XUserMaxUserCount> used_slots = {};
  for (const auto& [index, entry] : logged_profiles_) {
    if (!entry) {
      continue;
    }

    used_slots.set(index);
  }

  return used_slots;
}

uint8_t ProfileManager::GetUserIndexAssignedToProfile(
    const uint64_t xuid) const {
  for (const auto& [index, entry] : logged_profiles_) {
    if (!entry) {
      continue;
    }

    if (entry->xuid() != xuid) {
      continue;
    }

    return index;
  }
  return XUserIndexAny;
}

std::filesystem::path ProfileManager::GetProfileContentPath(
    const uint64_t xuid, const uint32_t title_id) const {
  std::filesystem::path profile_content_path =
      kernel_state_->emulator()->content_root() / fmt::format("{:016X}", xuid);
  if (title_id != -1 && title_id != 0) {
    profile_content_path =
        profile_content_path / fmt::format("{:08X}", title_id);
  }
  return profile_content_path;
}

std::filesystem::path ProfileManager::GetProfilePath(
    const uint64_t xuid) const {
  return GetProfilePath(fmt::format("{:016X}", xuid));
}

std::filesystem::path ProfileManager::GetProfilePath(
    const std::string xuid) const {
  return kernel_state_->emulator()->content_root() / xuid / kDashboardStringID /
         fmt::format("{:08X}", static_cast<uint32_t>(XContentType::kProfile)) /
         xuid;
}

bool ProfileManager::CreateProfile(const std::string gamertag, bool autologin,
                                   bool default_xuid) {
  const auto xuid = !default_xuid ? GenerateXuid() : 0xB13EBABEBABEBABE;

  if (!std::filesystem::create_directories(GetProfilePath(xuid))) {
    return false;
  }

  if (!MountProfile(xuid)) {
    return false;
  }

  const bool is_account_created = CreateAccount(xuid, gamertag);
  if (is_account_created && autologin) {
    Login(xuid);
  }
  return is_account_created;
}

const X_XAMACCOUNTINFO* ProfileManager::GetAccount(const uint64_t xuid) {
  if (!accounts_.count(xuid)) {
    return nullptr;
  }

  return &accounts_[xuid];
}

bool ProfileManager::CreateAccount(const uint64_t xuid,
                                   const std::string gamertag) {
  X_XAMACCOUNTINFO account = {};
  std::u16string gamertag_u16 = xe::to_utf16(gamertag);

  string_util::copy_truncating(account.gamertag, gamertag_u16,
                               sizeof(account.gamertag));

  UpdateAccount(xuid, &account);
  DismountProfile(xuid);

  accounts_.insert({xuid, account});
  return true;
}

bool ProfileManager::UpdateAccount(const uint64_t xuid,
                                   X_XAMACCOUNTINFO* account) {
  const std::string guest_path =
      xe::string_util::to_hex_string(xuid) + ":\\Account";

  xe::vfs::File* output_file;
  xe::vfs::FileAction action = {};
  auto status = kernel_state_->file_system()->OpenFile(
      nullptr, guest_path, xe::vfs::FileDisposition::kOpenIf,
      xe::vfs::FileAccess::kFileWriteData, false, true, &output_file, &action);

  if (XFAILED(status) || !output_file || !output_file->entry()) {
    XELOGI("{}: Failed to open Account file for creation: {:08X}", __func__,
           status);
    return false;
  }

  std::vector<uint8_t> encrypted_data;
  encrypted_data.resize(sizeof(X_XAMACCOUNTINFO) + 0x18);
  EncryptAccountFile(account, encrypted_data.data());

  size_t written_bytes = 0;
  output_file->WriteSync(encrypted_data.data(), encrypted_data.size(), 0,
                         &written_bytes);
  output_file->Destroy();
  return true;
}

void ProfileManager::UpdateConfig(const uint64_t xuid, const uint8_t slot) {
  if (slot >= XUserMaxUserCount) {
    return;
  }

  const std::string hex_xuid = xe::string_util::to_hex_string(xuid);
  switch (slot) {
    case 0:
      OVERRIDE_string(logged_profile_slot_0_xuid, hex_xuid);
      break;
    case 1:
      OVERRIDE_string(logged_profile_slot_1_xuid, hex_xuid);
      break;
    case 2:
      OVERRIDE_string(logged_profile_slot_2_xuid, hex_xuid);
      break;
    case 3:
      OVERRIDE_string(logged_profile_slot_3_xuid, hex_xuid);
      break;
    default:
      break;
  }
  return;
}

bool ProfileManager::DeleteProfile(const uint64_t xuid) {
  const uint8_t user_index = GetUserIndexAssignedToProfile(xuid);

  if (user_index < XUserMaxUserCount) {
    Logout(user_index);
  }

  DismountProfile(xuid);

  if (accounts_.count(xuid)) {
    accounts_.erase(xuid);
  }

  std::error_code ec;
  std::filesystem::remove_all(GetProfileContentPath(xuid), ec);
  if (ec) {
    XELOGE("Cannot remove profile: {}", ec.message());
    return false;
  }
  return true;
}

bool ProfileManager::IsGamertagValid(const std::string gamertag) {
  if (gamertag.empty()) {
    return false;
  }

  if (gamertag.length() > 15) {
    return false;
  }

  // Gamertag cannot start with a number.
  if (std::isdigit(gamertag.at(0))) {
    return false;
  }

  return std::find_if(gamertag.cbegin(), gamertag.cend(), [](char c) {
           return !(std::isalnum(c) || (c == ' '));
         }) == gamertag.cend();
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
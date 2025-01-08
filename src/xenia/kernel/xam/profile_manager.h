/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_PROFILE_MANAGER_H_
#define XENIA_KERNEL_XAM_PROFILE_MANAGER_H_

#include <bitset>
#include <random>
#include <string>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/string.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

constexpr uint32_t kDashboardID = 0xFFFE07D1;
const static std::string kDashboardStringID =
    fmt::format("{:08X}", kDashboardID);

enum class XTileType {
  kAchievement,
  kGameIcon,
  kGamerTile,
  kGamerTileSmall,
  kLocalGamerTile,
  kLocalGamerTileSmall,
  kBkgnd,
  kAwardedGamerTile,
  kAwardedGamerTileSmall,
  kGamerTileByImageId,
  kPersonalGamerTile,
  kPersonalGamerTileSmall,
  kGamerTileByKey,
  kAvatarGamerTile,
  kAvatarGamerTileSmall,
  kAvatarFullBody
};

// TODO: find filenames of other tile types that are stored in profile
static const std::map<XTileType, std::string> kTileFileNames = {
    {XTileType::kPersonalGamerTile, "tile_64.png"},
    {XTileType::kPersonalGamerTileSmall, "tile_32.png"},
    {XTileType::kAvatarGamerTile, "avtr_64.png"},
    {XTileType::kAvatarGamerTileSmall, "avtr_32.png"},
};

class ProfileManager {
 public:
  static bool DecryptAccountFile(const uint8_t* data, X_XAMACCOUNTINFO* output,
                                 bool devkit = false);

  static void EncryptAccountFile(const X_XAMACCOUNTINFO* input, uint8_t* output,
                                 bool devkit = false);

  // Profile:
  //  - Account
  //  - GPDs (Dashboard, titles)

  // Loading Profile means load everything
  // Loading Account means load basic data
  ProfileManager(KernelState* kernel_state);

  ~ProfileManager();

  bool CreateProfile(const std::string gamertag, bool autologin,
                     bool default_xuid = false);
  // bool CreateProfile(const X_XAMACCOUNTINFO* account_info);
  bool DeleteProfile(const uint64_t xuid);

  void ModifyGamertag(const uint64_t xuid, std::string gamertag);

  bool MountProfile(const uint64_t xuid, std::string mount_path = "");
  bool DismountProfile(const uint64_t xuid);

  void Login(const uint64_t xuid, const uint8_t user_index = XUserIndexAny,
             bool notify = true);
  void Logout(const uint8_t user_index, bool notify = true);
  void LoginMultiple(const std::map<uint8_t, uint64_t>& profiles);

  bool LoadAccount(const uint64_t xuid);
  void LoadAccounts(const std::vector<uint64_t> profiles_xuids);

  void ReloadProfiles();

  UserProfile* GetProfile(const uint64_t xuid) const;
  UserProfile* GetProfile(const uint8_t user_index) const;
  uint8_t GetUserIndexAssignedToProfile(const uint64_t xuid) const;

  const std::map<uint64_t, X_XAMACCOUNTINFO>* GetAccounts() {
    return &accounts_;
  }
  const X_XAMACCOUNTINFO* GetAccount(const uint64_t xuid);

  uint32_t GetAccountCount() const {
    return static_cast<uint32_t>(accounts_.size());
  }
  bool IsAnyProfileSignedIn() const { return !logged_profiles_.empty(); }

  std::filesystem::path GetProfileContentPath(
      const uint64_t xuid, const uint32_t title_id = -1) const;

  static bool IsGamertagValid(const std::string gamertag);

 private:
  void UpdateConfig(const uint64_t xuid, const uint8_t slot);
  bool CreateAccount(const uint64_t xuid, const std::string gamertag);
  bool UpdateAccount(const uint64_t xuid, X_XAMACCOUNTINFO* account);

  std::filesystem::path GetProfilePath(const uint64_t xuid) const;
  std::filesystem::path GetProfilePath(const std::string xuid) const;

  std::vector<uint64_t> FindProfiles() const;

  uint8_t FindFirstFreeProfileSlot() const;
  std::bitset<XUserMaxUserCount> GetUsedUserSlots() const;

  uint64_t GenerateXuid() const {
    std::random_device rd;
    std::mt19937 gen(rd());

    return ((uint64_t)0xE03 << 52) + (gen() % (1 << 31));
  }

  std::map<uint64_t, X_XAMACCOUNTINFO> accounts_;
  std::map<uint8_t, std::unique_ptr<UserProfile>> logged_profiles_;

  KernelState* kernel_state_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_PROFILE_MANAGER_H_

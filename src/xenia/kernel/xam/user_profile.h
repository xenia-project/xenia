/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_USER_PROFILE_H_
#define XENIA_KERNEL_XAM_USER_PROFILE_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/kernel/xam/user_property.h"
#include "xenia/kernel/xam/xdbf/gpd_info_profile.h"
#include "xenia/kernel/xam/xdbf/gpd_info_title.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

enum class X_USER_PROFILE_SETTING_SOURCE : uint32_t {
  NOT_SET = 0,
  DEFAULT = 1,  // Default value taken from default OS values.
  TITLE = 2,    // Value written by title or OS.
  UNKNOWN = 3,
};

struct X_USER_PROFILE_SETTING {
  xe::be<X_USER_PROFILE_SETTING_SOURCE> source;
  union {
    xe::be<uint32_t> user_index;
    xe::be<uint64_t> xuid;
  };
  xe::be<uint32_t> setting_id;
  union {
    uint8_t data_bytes[sizeof(X_USER_DATA)];
    X_USER_DATA data;
  };
};
static_assert_size(X_USER_PROFILE_SETTING, 40);

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
    {XTileType::kGamerTile, "tile_64.png"},
    {XTileType::kGamerTileSmall, "tile_32.png"},
    {XTileType::kPersonalGamerTile, "tile_64.png"},
    {XTileType::kPersonalGamerTileSmall, "tile_32.png"},
    {XTileType::kAvatarGamerTile, "avtr_64.png"},
    {XTileType::kAvatarGamerTileSmall, "avtr_32.png"},
};

class UserProfile {
 public:
  UserProfile(uint64_t xuid, X_XAMACCOUNTINFO* account_info);

  uint64_t xuid() const { return xuid_; }
  std::string name() const { return account_info_.GetGamertagString(); }
  uint32_t signin_state() const { return 1; }
  uint32_t type() const { return 1 | 2; /* local | online profile? */ }

  uint32_t GetCachedFlags() const { return account_info_.GetCachedFlags(); };
  uint32_t GetSubscriptionTier() const {
    return account_info_.GetSubscriptionTier();
  }

  std::span<const uint8_t> GetProfileIcon(XTileType icon_type) {
    // Overwrite same types?
    if (icon_type == XTileType::kPersonalGamerTile) {
      icon_type = XTileType::kGamerTile;
    }

    if (icon_type == XTileType::kPersonalGamerTileSmall) {
      icon_type = XTileType::kGamerTileSmall;
    }

    if (profile_images_.find(icon_type) == profile_images_.cend()) {
      return {};
    }

    return {profile_images_[icon_type].data(),
            profile_images_[icon_type].size()};
  }

  void GetPasscode(uint16_t* passcode) const {
    std::memcpy(passcode, account_info_.passcode,
                sizeof(account_info_.passcode));
  };

  friend class UserTracker;
  friend class GpdAchievementBackend;

 private:
  uint64_t xuid_;
  X_XAMACCOUNTINFO account_info_;

  GpdInfoProfile dashboard_gpd_;
  std::map<uint32_t, GpdInfoTitle> games_gpd_;
  std::vector<Property> properties_;  // Includes contexts!

  std::map<XTileType, std::vector<uint8_t>> profile_images_;

  GpdInfo* GetGpd(const uint32_t title_id);
  const GpdInfo* GetGpd(const uint32_t title_id) const;

  void LoadProfileGpds();
  void LoadProfileIcon(XTileType tile_type);
  std::vector<uint8_t> LoadGpd(const uint32_t title_id);
  bool WriteGpd(const uint32_t title_id);
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_USER_PROFILE_H_

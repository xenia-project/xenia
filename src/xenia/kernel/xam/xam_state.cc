/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_state.h"
#include "xenia/emulator.h"

DEFINE_uint32(max_signed_profiles, 4,
              "Limits how many profiles can be assigned. Possible values: 1-4",
              "Kernel");

namespace xe {
namespace kernel {
namespace xam {

XamState::XamState(Emulator* emulator, KernelState* kernel_state)
    : kernel_state_(kernel_state) {
  app_manager_ = std::make_unique<AppManager>();

  auto content_root = emulator->content_root();
  if (!content_root.empty()) {
    content_root = std::filesystem::absolute(content_root);
  }
  content_manager_ =
      std::make_unique<ContentManager>(kernel_state, content_root);

  user_profiles_.emplace(0, std::make_unique<xam::UserProfile>(0));

  achievement_manager_ = std::make_unique<AchievementManager>();

  AppManager::RegisterApps(kernel_state, app_manager_.get());
}

XamState::~XamState() {
  app_manager_.reset();
  achievement_manager_.reset();
  content_manager_.reset();
}

UserProfile* XamState::GetUserProfile(uint32_t user_index) const {
  if (!IsUserSignedIn(user_index)) {
    return nullptr;
  }
  return user_profiles_.at(user_index).get();
}

UserProfile* XamState::GetUserProfile(uint64_t xuid) const {
  for (const auto& [key, value] : user_profiles_) {
    if (value->xuid() == xuid) {
      return user_profiles_.at(key).get();
    }
  }
  return nullptr;
}

void XamState::UpdateUsedUserProfiles() {
  const std::bitset<4> used_slots = kernel_state_->GetConnectedUsers();

  const uint32_t signed_profile_count =
      std::max(static_cast<uint32_t>(1),
               std::min(static_cast<uint32_t>(4), cvars::max_signed_profiles));

  for (uint32_t i = 1; i < signed_profile_count; i++) {
    bool is_used = used_slots.test(i);

    if (IsUserSignedIn(i) && !is_used) {
      user_profiles_.erase(i);
      kernel_state_->BroadcastNotification(
          kXNotificationIDSystemInputDevicesChanged, 0);
    }

    if (!IsUserSignedIn(i) && is_used) {
      user_profiles_.emplace(i, std::make_unique<xam::UserProfile>(i));
      kernel_state_->BroadcastNotification(
          kXNotificationIDSystemInputDevicesChanged, 0);
    }
  }
}

bool XamState::IsUserSignedIn(uint32_t user_index) const {
  return user_profiles_.find(user_index) != user_profiles_.cend();
}

bool XamState::IsUserSignedIn(uint64_t xuid) const {
  return GetUserProfile(xuid) != nullptr;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

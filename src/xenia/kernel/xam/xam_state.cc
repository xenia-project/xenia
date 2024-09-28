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

  profile_manager_ = std::make_unique<ProfileManager>(kernel_state);
  achievement_manager_ = std::make_unique<AchievementManager>();

  AppManager::RegisterApps(kernel_state, app_manager_.get());
}

XamState::~XamState() {
  app_manager_.reset();
  achievement_manager_.reset();
  content_manager_.reset();
}

UserProfile* XamState::GetUserProfile(uint32_t user_index) const {
  if (user_index >= XUserMaxUserCount && user_index < XUserIndexLatest) {
    return nullptr;
  }

  return profile_manager_->GetProfile(static_cast<uint8_t>(user_index));
}

UserProfile* XamState::GetUserProfile(uint64_t xuid) const {
  return profile_manager_->GetProfile(xuid);
}

bool XamState::IsUserSignedIn(uint32_t user_index) const {
  return profile_manager_->GetProfile(static_cast<uint8_t>(user_index)) !=
         nullptr;
}

bool XamState::IsUserSignedIn(uint64_t xuid) const {
  return GetUserProfile(xuid) != nullptr;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

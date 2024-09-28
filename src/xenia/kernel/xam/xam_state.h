/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_STATE_H_
#define XENIA_KERNEL_XAM_XAM_STATE_H_

#include <memory>
#include "xenia/kernel/xam/achievement_manager.h"
#include "xenia/kernel/xam/app_manager.h"
#include "xenia/kernel/xam/content_manager.h"
#include "xenia/kernel/xam/profile_manager.h"

namespace xe {
class Emulator;
};

namespace xe {
namespace kernel {
class KernelState;
}
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

class XamState {
 public:
  XamState(Emulator* emulator, KernelState* kernel_state);
  ~XamState();

  AppManager* app_manager() const { return app_manager_.get(); }
  ContentManager* content_manager() const { return content_manager_.get(); }
  AchievementManager* achievement_manager() const {
    return achievement_manager_.get();
  }
  ProfileManager* profile_manager() const { return profile_manager_.get(); }

  UserProfile* GetUserProfile(uint32_t user_index) const;
  UserProfile* GetUserProfile(uint64_t xuid) const;

  bool IsUserSignedIn(uint32_t user_index) const;
  bool IsUserSignedIn(uint64_t xuid) const;

 private:
  KernelState* kernel_state_;

  std::unique_ptr<AppManager> app_manager_;
  std::unique_ptr<ContentManager> content_manager_;
  std::unique_ptr<AchievementManager> achievement_manager_;
  std::unique_ptr<ProfileManager> profile_manager_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
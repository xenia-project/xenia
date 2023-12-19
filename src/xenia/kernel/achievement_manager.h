/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_ACHIEVEMENT_MANAGER_H_
#define XENIA_KERNEL_ACHIEVEMENT_MANAGER_H_

#include <map>
#include <string>
#include <vector>

#include "xenia/xbox.h"

namespace xe {
namespace kernel {

// TODO(gibbed): probably a FILETIME/LARGE_INTEGER, unknown currently
struct X_ACHIEVEMENT_UNLOCK_TIME {
  xe::be<uint32_t> unk_0;
  xe::be<uint32_t> unk_4;
};

struct X_ACHIEVEMENT_DETAILS {
  xe::be<uint32_t> id;
  xe::be<uint32_t> label_ptr;
  xe::be<uint32_t> description_ptr;
  xe::be<uint32_t> unachieved_ptr;
  xe::be<uint32_t> image_id;
  xe::be<uint32_t> gamerscore;
  X_ACHIEVEMENT_UNLOCK_TIME unlock_time;
  xe::be<uint32_t> flags;

  static const size_t kStringBufferSize = 464;
};
static_assert_size(X_ACHIEVEMENT_DETAILS, 36);

class AchievementManager {
 public:
  AchievementManager();

  void EarnAchievement(uint64_t xuid, uint32_t title_id,
                       uint32_t achievement_id);

  bool IsAchievementUnlocked(uint32_t achievement_id);
  uint64_t GetAchievementUnlockTime(uint32_t achievement_id);

 private:
  std::map<uint32_t, uint64_t> unlocked_achievements;
  // void Load();
  // void Save();
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_ACHIEVEMENT_MANAGER_H_

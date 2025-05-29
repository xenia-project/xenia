/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XDBF_GPD_INFO_TITLE_H_
#define XENIA_KERNEL_XAM_XDBF_GPD_INFO_TITLE_H_

#include "xenia/kernel/xam/achievement_manager.h"
#include "xenia/kernel/xam/xdbf/gpd_info.h"
#include "xenia/kernel/xam/xdbf/xdbf_io.h"

#include <string>
#include <vector>

#include "xenia/base/memory.h"
#include "xenia/base/string_util.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

class GpdInfoTitle : public GpdInfo {
 public:
  GpdInfoTitle() : GpdInfo(-1) {};
  GpdInfoTitle(const uint32_t title_id) : GpdInfo(title_id) {};
  GpdInfoTitle(const uint32_t title_id, const std::vector<uint8_t> buffer)
      : GpdInfo(title_id, buffer) {};

  ~GpdInfoTitle() = default;

  std::vector<uint32_t> GetAchievementsIds() const;

  void AddAchievement(const AchievementDetails* header);

  X_XDBF_GPD_ACHIEVEMENT* GetAchievementEntry(const uint32_t id);
  std::u16string GetAchievementTitle(const uint32_t id);
  std::u16string GetAchievementDescription(const uint32_t id);
  std::u16string GetAchievementUnachievedDescription(const uint32_t id);

  uint32_t GetTotalGamerscore();
  uint32_t GetGamerscore();
  uint32_t GetAchievementCount();
  uint32_t GetUnlockedAchievementCount();

 private:
  const char16_t* GetAchievementTitlePtr(const uint32_t id);
  const char16_t* GetAchievementDescriptionPtr(const uint32_t id);
  const char16_t* GetAchievementUnachievedDescriptionPtr(const uint32_t id);
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe
#endif  // XENIA_KERNEL_XAM_XDBF_GPD_INFO_TITLE_H_
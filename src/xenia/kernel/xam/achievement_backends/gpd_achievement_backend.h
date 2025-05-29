/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_ACHIEVEMENT_BACKENDS_GPD_ACHIEVEMENT_BACKEND_H_
#define XENIA_KERNEL_XAM_ACHIEVEMENT_BACKENDS_GPD_ACHIEVEMENT_BACKEND_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "xenia/kernel/xam/achievement_manager.h"
#include "xenia/kernel/xam/xdbf/gpd_info.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

class GpdAchievementBackend : public AchievementBackendInterface {
 public:
  GpdAchievementBackend() = default;
  ~GpdAchievementBackend() = default;

  void EarnAchievement(const uint64_t xuid, const uint32_t title_id,
                       const uint32_t achievement_id) override;
  bool IsAchievementUnlocked(const uint64_t xuid, const uint32_t title_id,
                             const uint32_t achievement_id) const override;
  const std::optional<Achievement> GetAchievementInfo(
      const uint64_t xuid, const uint32_t title_id,
      const uint32_t achievement_id) const override;
  const std::vector<Achievement> GetTitleAchievements(
      const uint64_t xuid, const uint32_t title_id) const override;
  const std::span<const uint8_t> GetAchievementIcon(
      const uint64_t xuid, const uint32_t title_id,
      const uint32_t achievement_id) const override;
  bool LoadAchievementsData(const uint64_t xuid) override;

 private:
  bool SaveAchievementsData(const uint64_t xuid,
                            const uint32_t title_id) override {
    return 0;
  };
  bool SaveAchievementData(const uint64_t xuid, const uint32_t title_id,
                           const Achievement* achievement) override;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_ACHIEVEMENT_BACKENDS_GPD_ACHIEVEMENT_BACKEND_H_

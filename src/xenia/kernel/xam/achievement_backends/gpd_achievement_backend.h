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

#include "xenia/kernel/util/xdbf_utils.h"
#include "xenia/kernel/xam/achievement_manager.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

class GpdAchievementBackend : public AchievementBackendInterface {
 public:
  GpdAchievementBackend();
  ~GpdAchievementBackend();

  void EarnAchievement(const uint64_t xuid, const uint32_t title_id,
                       const uint32_t achievement_id) override;
  bool IsAchievementUnlocked(const uint64_t xuid, const uint32_t title_id,
                             const uint32_t achievement_id) const override;
  const AchievementGpdStructure* GetAchievementInfo(
      const uint64_t xuid, const uint32_t title_id,
      const uint32_t achievement_id) const override;
  const std::vector<AchievementGpdStructure>* GetTitleAchievements(
      const uint64_t xuid, const uint32_t title_id) const override;
  bool LoadAchievementsData(const uint64_t xuid,
                            const util::XdbfGameData title_data) override;

 private:
  AchievementGpdStructure* GetAchievementInfoInternal(
      const uint64_t xuid, const uint32_t title_id,
      const uint32_t achievement_id) const;

  bool SaveAchievementsData(const uint64_t xuid,
                            const uint32_t title_id) override {
    return 0;
  };
  bool SaveAchievementData(const uint64_t xuid, const uint32_t title_id,
                           const uint32_t achievement_id) override;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_ACHIEVEMENT_BACKENDS_GPD_ACHIEVEMENT_BACKEND_H_

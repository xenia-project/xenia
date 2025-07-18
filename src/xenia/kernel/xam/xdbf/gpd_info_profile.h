/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XDBF_GPD_INFO_PROFILE_H_
#define XENIA_KERNEL_XAM_XDBF_GPD_INFO_PROFILE_H_

#include "xenia/kernel/title_id_utils.h"
#include "xenia/kernel/xam/xdbf/gpd_info.h"
#include "xenia/kernel/xam/xdbf/spa_info.h"

#include <string>
#include <vector>

#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

class GpdInfoProfile : public GpdInfo {
 public:
  GpdInfoProfile() : GpdInfo(kDashboardID) {};
  GpdInfoProfile(const std::vector<uint8_t> buffer)
      : GpdInfo(kDashboardID, buffer) {};

  ~GpdInfoProfile() = default;

  void AddNewTitle(const SpaInfo* title_data);
  void UpdateTitleInfo(const uint32_t title_id,
                       X_XDBF_GPD_TITLE_PLAYED* title_data);

  const std::vector<const X_XDBF_GPD_TITLE_PLAYED*> GetTitlesInfo() const;
  X_XDBF_GPD_TITLE_PLAYED* GetTitleInfo(const uint32_t title_id);

  std::u16string GetTitleName(const uint32_t title_id) const;

 private:
  X_XDBF_GPD_TITLE_PLAYED FillTitlePlayedData(const SpaInfo* title_data) const;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XDBF_GPD_INFO_PROFILE_H_

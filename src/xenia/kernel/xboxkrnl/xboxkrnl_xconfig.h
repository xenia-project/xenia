/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XBOXKRNL_XCONFIG_H_
#define XENIA_KERNEL_XBOXKRNL_XBOXKRNL_XCONFIG_H_

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/xbox.h"

enum class XCConsoleRegion {
  kUS,
  kJapan,
  kAsia,
  kEurope,
  kRegionFree,
};

enum class XCRegionSettingType {
  kUserLanguage,
  kUserCountry,
  kConsoleRegion,
};

namespace xe {
namespace kernel {
namespace xboxkrnl {

void xcSaveRegionSetting(XCRegionSettingType type, int32_t setting_value);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XBOXKRNL_XCONFIG_H_

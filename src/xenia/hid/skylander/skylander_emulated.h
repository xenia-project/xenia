/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_SKYLANDER_SKYLANDER_PORTAL_EMULATED_H_
#define XENIA_HID_SKYLANDER_SKYLANDER_PORTAL_EMULATED_H_

#include "xenia/hid/skylander/skylander_portal.h"

namespace xe {
namespace hid {

class SkylanderPortalEmulated final : public SkylanderPortal {
 public:
  SkylanderPortalEmulated();
  ~SkylanderPortalEmulated() override;

  X_STATUS read(std::vector<uint8_t>& data) override;
  X_STATUS write(std::vector<uint8_t>& data) override;

 private:
  bool init_device() override;
  void destroy_device() override;
};

}  // namespace hid
}  // namespace xe

#endif
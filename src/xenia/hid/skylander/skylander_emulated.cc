/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/skylander/skylander_emulated.h"

namespace xe {
namespace hid {

SkylanderPortalEmulated::SkylanderPortalEmulated() : SkylanderPortal() {}

SkylanderPortalEmulated::~SkylanderPortalEmulated() {}

bool SkylanderPortalEmulated::init_device() { return false; }

void SkylanderPortalEmulated::destroy_device() {}

X_STATUS SkylanderPortalEmulated::read(std::vector<uint8_t>& data) {
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

X_STATUS SkylanderPortalEmulated::write(std::vector<uint8_t>& data) {
  return X_ERROR_DEVICE_NOT_CONNECTED;
};

}  // namespace hid
}  // namespace xe

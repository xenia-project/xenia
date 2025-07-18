/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_SKYLANDER_SKYLANDER_PORTAL_H_
#define XENIA_HID_SKYLANDER_SKYLANDER_PORTAL_H_

#include <vector>

#include "xenia/xbox.h"

namespace xe {
namespace hid {

constexpr std::pair<uint16_t, uint16_t> portal_vendor_product_id = {0x1430,
                                                                    0x1F17};
constexpr uint8_t skylander_buffer_size = 0x20;

class SkylanderPortal {
 public:
  SkylanderPortal();
  virtual ~SkylanderPortal();

  virtual X_STATUS read(std::vector<uint8_t>& data) = 0;
  virtual X_STATUS write(std::vector<uint8_t>& data) = 0;

 private:
  virtual bool init_device() = 0;
  virtual void destroy_device() = 0;
};

}  // namespace hid
}  // namespace xe

#endif

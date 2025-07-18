/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_SKYLANDER_SKYLANDER_PORTAL_LIBUSB_H_
#define XENIA_HID_SKYLANDER_SKYLANDER_PORTAL_LIBUSB_H_

// Include order is important here. Including skylander_portal.h after libusb
// will cause conflicts.
#include "xenia/hid/skylander/skylander_portal.h"

#include "third_party/libusb/libusb/libusb.h"

namespace xe {
namespace hid {

class SkylanderPortalLibusb final : public SkylanderPortal {
 public:
  SkylanderPortalLibusb();
  ~SkylanderPortalLibusb() override;

  X_STATUS read(std::vector<uint8_t>& data) override;
  X_STATUS write(std::vector<uint8_t>& data) override;

 private:
  bool init_device() override;
  void destroy_device() override;

  bool is_initialized() const { return handle_ != nullptr; }

  const uint8_t read_endpoint = 0x81;
  const uint8_t write_endpoint = 0x02;
  const uint16_t timeout = 300;

  libusb_context* context_ = nullptr;
  uintptr_t* handle_ = nullptr;
};

}  // namespace hid
}  // namespace xe

#endif

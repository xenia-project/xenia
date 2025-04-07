/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/skylander/skylander_hardware.h"

namespace xe {
namespace hid {

SkylanderPortalLibusb::SkylanderPortalLibusb() : SkylanderPortal() {
  libusb_init(&context_);
}

SkylanderPortalLibusb::~SkylanderPortalLibusb() {
  if (handle_) {
    destroy_device();
  }

  libusb_exit(context_);
}

bool SkylanderPortalLibusb::init_device() {
  if (!context_) {
    return false;
  }

  libusb_device** devs;
  ssize_t cnt = libusb_get_device_list(context_, &devs);
  if (cnt < 0) {
    // No device available... It might appear later.
    return false;
  }

  bool device_found = false;
  for (ssize_t i = 0; i < cnt; ++i) {
    libusb_device* dev = devs[i];

    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev, &desc) == 0) {
      // Check if this device matches the target Vendor ID and Product ID
      if (desc.idVendor == portal_vendor_product_id.first &&
          desc.idProduct == portal_vendor_product_id.second) {
        libusb_device_handle* handle;
        if (libusb_open(dev, &handle) == 0) {
          libusb_claim_interface(handle, 0);
          handle_ = reinterpret_cast<uintptr_t*>(handle);
          device_found = true;
        }
      }
    }
  }
  libusb_free_device_list(devs, 0);

  return device_found;
}

void SkylanderPortalLibusb::destroy_device() {
  libusb_release_interface(reinterpret_cast<libusb_device_handle*>(handle_), 0);
  libusb_close(reinterpret_cast<libusb_device_handle*>(handle_));
  handle_ = nullptr;
}

X_STATUS SkylanderPortalLibusb::read(std::vector<uint8_t>& data) {
  if (!is_initialized()) {
    if (!init_device()) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
  }

  if (data.size() > skylander_buffer_size) {
    return X_ERROR_INVALID_PARAMETER;
  }

  const int result = libusb_interrupt_transfer(
      reinterpret_cast<libusb_device_handle*>(handle_), read_endpoint,
      data.data(), static_cast<int>(data.size()), nullptr, timeout);

  if (result == LIBUSB_ERROR_NO_DEVICE) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (result < 0) {
    destroy_device();
  }

  return X_ERROR_SUCCESS;
}

X_STATUS SkylanderPortalLibusb::write(std::vector<uint8_t>& data) {
  if (!is_initialized()) {
    if (!init_device()) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
  }

  if (data.size() > skylander_buffer_size) {
    return X_ERROR_INVALID_PARAMETER;
  }

  const int result = libusb_interrupt_transfer(
      reinterpret_cast<libusb_device_handle*>(handle_), write_endpoint,
      data.data(), static_cast<int>(data.size()), nullptr, timeout);

  if (result == LIBUSB_ERROR_NO_DEVICE) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (result < 0) {
    destroy_device();
  }

  return X_ERROR_SUCCESS;
};

}  // namespace hid
}  // namespace xe
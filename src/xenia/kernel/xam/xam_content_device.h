/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_CONTENT_DEVICE_H_
#define XENIA_KERNEL_XAM_XAM_CONTENT_DEVICE_H_

#include <cstdint>
#include <string_view>
#include <vector>

namespace xe {
namespace kernel {
namespace xam {

enum class DeviceType : uint32_t {
  Invalid = 0,
  HDD = 1,
  MU = 2,  // support removed in 9199
  ODD = 4,
  USBMASS = 16,  // Added in 8955
};

enum class DummyDeviceId : uint32_t {
  HDD = 1,
  ODD = 2,
};

struct DummyDeviceInfo {
  DummyDeviceId device_id;
  DeviceType device_type;
  uint64_t total_bytes;
  uint64_t free_bytes;
  const std::u16string_view name;
};

const DummyDeviceInfo* GetDummyDeviceInfo(uint32_t device_id);
std::vector<const DummyDeviceInfo*> ListStorageDevices(
    bool include_readonly = false);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XAM_CONTENT_DEVICE_H_

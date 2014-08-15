/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_DEVICES_HOST_PATH_DEVICE_H_
#define XENIA_KERNEL_FS_DEVICES_HOST_PATH_DEVICE_H_

#include <string>

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/fs/device.h>

namespace xe {
namespace kernel {
namespace fs {

class HostPathDevice : public Device {
 public:
  HostPathDevice(const std::string& path, const std::wstring& local_path);
  ~HostPathDevice() override;

  Entry* ResolvePath(const char* path) override;

  X_STATUS QueryVolume(XVolumeInfo* out_info, size_t length) override;
  X_STATUS QueryFileSystemAttributes(XFileSystemAttributeInfo* out_info,
                                     size_t length) override;

 private:
  std::wstring local_path_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_DEVICES_HOST_PATH_DEVICE_H_

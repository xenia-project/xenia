/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_FS_DEVICES_HOST_PATH_DEVICE_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_FS_DEVICES_HOST_PATH_DEVICE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/modules/xboxkrnl/fs/device.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {
namespace fs {


class HostPathDevice : public Device {
public:
  HostPathDevice(const char* path, const xechar_t* local_path);
  virtual ~HostPathDevice();

  virtual Entry* ResolvePath(const char* path);

private:
  xechar_t*   local_path_;
};


}  // namespace fs
}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_FS_DEVICES_HOST_PATH_DEVICE_H_

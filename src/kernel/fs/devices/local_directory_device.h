/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_DEVICES_LOCAL_DIRECTORY_DEVICE_H_
#define XENIA_KERNEL_FS_DEVICES_LOCAL_DIRECTORY_DEVICE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/fs/device.h>


namespace xe {
namespace kernel {
namespace fs {


class LocalDirectoryDevice : public Device {
public:
  LocalDirectoryDevice(xe_pal_ref pal, const char* path,
                       const xechar_t* local_path);
  virtual ~LocalDirectoryDevice();

  virtual Entry* ResolvePath(const char* path);

private:
  xechar_t*   local_path_;
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_DEVICES_LOCAL_DIRECTORY_DEVICE_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_DEVICE_H_
#define XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_DEVICE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/fs/device.h>


namespace xe {
namespace kernel {
namespace fs {


class STFS;


class STFSContainerDevice : public Device {
public:
  STFSContainerDevice(const char* path, const xechar_t* local_path);
  virtual ~STFSContainerDevice();

  int Init();

  virtual Entry* ResolvePath(const char* path);

  virtual X_STATUS QueryVolume(XVolumeInfo* out_info, size_t length);
  virtual X_STATUS QueryFileSystemAttributes(XFileSystemAttributeInfo* out_info, size_t length);

private:
  xechar_t*   local_path_;
  xe_mmap_ref mmap_;
  STFS*       stfs_;
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_DEVICE_H_

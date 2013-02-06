/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_DEVICE_H_
#define XENIA_KERNEL_FS_DEVICE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/fs/entry.h>


namespace xe {
namespace kernel {
namespace fs {


class Device {
public:
  Device(xe_pal_ref pal, const char* path);
  virtual ~Device();

  xe_pal_ref pal();
  const char* path();

  virtual Entry* ResolvePath(const char* path) = 0;

protected:
  xe_pal_ref  pal_;
  char*       path_;
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_DEVICE_H_

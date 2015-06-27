/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICE_H_
#define XENIA_VFS_DEVICE_H_

#include <memory>
#include <string>

#include "xenia/vfs/entry.h"

namespace xe {
namespace vfs {

class Device {
 public:
  Device(const std::string& path);
  virtual ~Device();

  const std::string& mount_path() const { return mount_path_; }

  virtual bool is_read_only() const { return true; }

  virtual std::unique_ptr<Entry> ResolvePath(const char* path) = 0;

  virtual X_STATUS QueryVolumeInfo(X_FILE_FS_VOLUME_INFORMATION* out_info,
                                   size_t length);
  virtual X_STATUS QuerySizeInfo(X_FILE_FS_SIZE_INFORMATION* out_info,
                                 size_t length);
  virtual X_STATUS QueryAttributeInfo(X_FILE_FS_ATTRIBUTE_INFORMATION* out_info,
                                      size_t length);

 protected:
  std::string mount_path_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICE_H_

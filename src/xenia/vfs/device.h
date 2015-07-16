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

#include "xenia/base/mutex.h"
#include "xenia/base/string_buffer.h"
#include "xenia/vfs/entry.h"

namespace xe {
namespace vfs {

class Device {
 public:
  Device(const std::string& path);
  virtual ~Device();

  virtual bool Initialize() = 0;
  void Dump(StringBuffer* string_buffer);

  xe::recursive_mutex& mutex() { return mutex_; }
  const std::string& mount_path() const { return mount_path_; }

  virtual bool is_read_only() const { return true; }

  Entry* ResolvePath(std::string path);

  virtual uint32_t total_allocation_units() const = 0;
  virtual uint32_t available_allocation_units() const = 0;
  virtual uint32_t sectors_per_allocation_unit() const = 0;
  virtual uint32_t bytes_per_sector() const = 0;

 protected:
  xe::recursive_mutex mutex_;
  std::string mount_path_;
  std::unique_ptr<Entry> root_entry_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICE_H_

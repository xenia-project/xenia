/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_HOST_PATH_DEVICE_H_
#define XENIA_VFS_DEVICES_HOST_PATH_DEVICE_H_

#include <string>

#include "xenia/vfs/device.h"

namespace xe {
namespace vfs {

class HostPathEntry;

class HostPathDevice : public Device {
 public:
  HostPathDevice(const std::string& mount_path, const std::wstring& local_path,
                 bool read_only);
  ~HostPathDevice() override;

  bool Initialize() override;
  void Dump(StringBuffer* string_buffer) override;
  Entry* ResolvePath(const std::string& path) override;

  bool is_read_only() const override { return read_only_; }

  uint32_t total_allocation_units() const override { return 128 * 1024; }
  uint32_t available_allocation_units() const override { return 128 * 1024; }
  uint32_t sectors_per_allocation_unit() const override { return 1; }
  uint32_t bytes_per_sector() const override { return 2 * 1024; }

 private:
  void PopulateEntry(HostPathEntry* parent_entry);

  std::wstring local_path_;
  std::unique_ptr<Entry> root_entry_;
  bool read_only_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_HOST_PATH_DEVICE_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_DISC_ZARCHIVE_DEVICE_H_
#define XENIA_VFS_DEVICES_DISC_ZARCHIVE_DEVICE_H_

#include <memory>
#include <string>

#include "xenia/base/mapped_memory.h"
#include "xenia/vfs/device.h"

namespace xe {
namespace vfs {

class DiscZarchiveEntry;

class DiscZarchiveDevice : public Device {
 public:
  DiscZarchiveDevice(const std::string_view mount_path,
                     const std::filesystem::path& host_path);
  ~DiscZarchiveDevice() override;

  bool Initialize() override;
  void Dump(StringBuffer* string_buffer) override;
  Entry* ResolvePath(const std::string_view path) override;

  const std::string& name() const override { return name_; }
  uint32_t attributes() const override { return 0; }
  uint32_t component_name_max_length() const override { return 255; }

  uint32_t total_allocation_units() const override { return 128 * 1024; }
  uint32_t available_allocation_units() const override { return 0; }
  uint32_t sectors_per_allocation_unit() const override { return 1; }
  uint32_t bytes_per_sector() const override { return 0x200; }

 private:
  bool ReadAllEntries(void* opaque, const std::string& path,
                      DiscZarchiveEntry* node, DiscZarchiveEntry* parent);

  std::string name_;
  std::filesystem::path host_path_;
  std::unique_ptr<Entry> root_entry_;
  void* opaque_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_DISC_ZARCHIVE_DEVICE_H_

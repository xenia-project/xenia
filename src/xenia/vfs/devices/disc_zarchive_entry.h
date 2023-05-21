/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_DISC_ZARCHIVE_ENTRY_H_
#define XENIA_VFS_DEVICES_DISC_ZARCHIVE_ENTRY_H_

#include <string>
#include <vector>

#include "xenia/base/mapped_memory.h"
#include "xenia/vfs/entry.h"

namespace xe {
namespace vfs {

class DiscZarchiveDevice;

class DiscZarchiveEntry : public Entry {
 public:
  DiscZarchiveEntry(Device* device, Entry* parent, const std::string_view path,
                    void* opaque);
  ~DiscZarchiveEntry() override;

  static std::unique_ptr<DiscZarchiveEntry> Create(Device* device,
                                                   Entry* parent,
                                                   const std::string_view name,
                                                   void* opaque);

  MappedMemory* mmap() const { return nullptr; }
  size_t data_offset() const { return data_offset_; }
  size_t data_size() const { return data_size_; }

  X_STATUS Open(uint32_t desired_access, File** out_file) override;

  bool can_map() const override { return false; }
  std::unique_ptr<MappedMemory> OpenMapped(MappedMemory::Mode mode,
                                           size_t offset,
                                           size_t length) override;

 private:
  friend class DiscZarchiveDevice;
  friend class DiscZarchiveFile;

  void* opaque_;
  uint32_t handle_;
  size_t data_offset_;
  size_t data_size_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_DISC_ZARCHIVE_ENTRY_H_

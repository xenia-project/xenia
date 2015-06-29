/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_STFS_CONTAINER_ENTRY_H_
#define XENIA_VFS_DEVICES_STFS_CONTAINER_ENTRY_H_

#include <vector>

#include "xenia/base/filesystem.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/vfs/entry.h"

namespace xe {
namespace vfs {

class StfsContainerDevice;

class StfsContainerEntry : public Entry {
 public:
  StfsContainerEntry(Device* device, Entry* parent, std::string path,
                     MappedMemory* mmap);
  ~StfsContainerEntry() override;

  MappedMemory* mmap() const { return mmap_; }
  size_t data_offset() const { return data_offset_; }
  size_t data_size() const { return data_size_; }

  X_STATUS Open(KernelState* kernel_state, uint32_t desired_access,
                object_ref<XFile>* out_file) override;

  struct BlockRecord {
    size_t offset;
    size_t length;
  };
  const std::vector<BlockRecord>& block_list() const { return block_list_; }

 private:
  friend class StfsContainerDevice;

  MappedMemory* mmap_;
  size_t data_offset_;
  size_t data_size_;
  std::vector<BlockRecord> block_list_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_STFS_CONTAINER_ENTRY_H_

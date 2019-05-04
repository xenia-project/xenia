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

#include <map>
#include <string>
#include <vector>

#include "xenia/base/filesystem.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/vfs/entry.h"
#include "xenia/vfs/file.h"

namespace xe {
namespace vfs {
typedef std::map<size_t, std::unique_ptr<MappedMemory>> MultifileMemoryMap;

class StfsContainerDevice;

class StfsContainerEntry : public Entry {
 public:
  StfsContainerEntry(Device* device, Entry* parent, std::string path,
                     MultifileMemoryMap* mmap);
  ~StfsContainerEntry() override;

  static std::unique_ptr<StfsContainerEntry> Create(Device* device,
                                                    Entry* parent,
                                                    std::string name,
                                                    MultifileMemoryMap* mmap);

  MultifileMemoryMap* mmap() const { return mmap_; }
  size_t data_offset() const { return data_offset_; }
  size_t data_size() const { return data_size_; }
  size_t block() const { return block_; }

  X_STATUS Open(uint32_t desired_access, File** out_file) override;

  struct BlockRecord {
    size_t file;
    size_t offset;
    size_t length;
  };
  const std::vector<BlockRecord>& block_list() const { return block_list_; }

 private:
  friend class StfsContainerDevice;

  MultifileMemoryMap* mmap_;
  size_t data_offset_;
  size_t data_size_;
  size_t block_;
  std::vector<BlockRecord> block_list_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_STFS_CONTAINER_ENTRY_H_
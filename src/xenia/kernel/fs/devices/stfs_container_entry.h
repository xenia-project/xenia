/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_ENTRY_H_
#define XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_ENTRY_H_

#include <vector>

#include <poly/mapped_memory.h>
#include <xenia/common.h>
#include <xenia/kernel/fs/entry.h>

namespace xe {
namespace kernel {
namespace fs {

class STFSEntry;

class STFSContainerEntry : public Entry {
 public:
  STFSContainerEntry(Type type, Device* device, const char* path,
                     poly::MappedMemory* mmap, STFSEntry* stfs_entry);
  ~STFSContainerEntry() override;

  poly::MappedMemory* mmap() const { return mmap_; }
  STFSEntry* stfs_entry() const { return stfs_entry_; }

  X_STATUS QueryInfo(XFileInfo* out_info) override;
  X_STATUS QueryDirectory(XDirectoryInfo* out_info, size_t length,
                          const char* file_name, bool restart) override;

  X_STATUS Open(KernelState* kernel_state, Mode desired_access, bool async,
                XFile** out_file) override;

 private:
  poly::MappedMemory* mmap_;
  STFSEntry* stfs_entry_;
  std::vector<std::unique_ptr<STFSEntry>>::iterator stfs_entry_iterator_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_ENTRY_H_

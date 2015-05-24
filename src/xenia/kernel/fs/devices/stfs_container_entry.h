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
#include <iterator>

#include "xenia/base/fs.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/kernel/fs/entry.h"
#include "xenia/kernel/fs/stfs.h"

namespace xe {
namespace kernel {
namespace fs {

class STFSContainerEntry : public Entry {
 public:
  STFSContainerEntry(Device* device, const char* path, MappedMemory* mmap,
                     STFSEntry* stfs_entry);
  ~STFSContainerEntry() override;

  MappedMemory* mmap() const { return mmap_; }
  STFSEntry* stfs_entry() const { return stfs_entry_; }

  X_STATUS QueryInfo(X_FILE_NETWORK_OPEN_INFORMATION* out_info) override;
  X_STATUS QueryDirectory(X_FILE_DIRECTORY_INFORMATION* out_info, size_t length,
                          const char* file_name, bool restart) override;

  X_STATUS Open(KernelState* kernel_state, Mode desired_access, bool async,
                XFile** out_file) override;

 private:
  MappedMemory* mmap_;
  STFSEntry* stfs_entry_;

  xe::fs::WildcardEngine find_engine_;
  STFSEntry::child_it_t it_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_ENTRY_H_

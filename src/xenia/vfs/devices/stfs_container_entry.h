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
#include <iterator>

#include "xenia/base/filesystem.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/vfs/entry.h"
#include "xenia/vfs/stfs.h"

namespace xe {
namespace vfs {

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

  xe::filesystem::WildcardEngine find_engine_;
  STFSEntry::child_it_t it_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_STFS_CONTAINER_ENTRY_H_

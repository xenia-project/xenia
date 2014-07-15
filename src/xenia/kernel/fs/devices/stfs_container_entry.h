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

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/fs/entry.h>


namespace xe {
namespace kernel {
namespace fs {

class STFSEntry;


class STFSContainerEntry : public Entry {
public:
  STFSContainerEntry(Type type, Device* device, const char* path,
                     xe_mmap_ref mmap, STFSEntry* stfs_entry);
  virtual ~STFSContainerEntry();

  xe_mmap_ref mmap() const { return mmap_; }
  STFSEntry* stfs_entry() const { return stfs_entry_; }

  virtual X_STATUS QueryInfo(XFileInfo* out_info);
  virtual X_STATUS QueryDirectory(XDirectoryInfo* out_info,
                                  size_t length, const char* file_name, bool restart);

  virtual X_STATUS Open(
      KernelState* kernel_state,
      uint32_t desired_access, bool async,
      XFile** out_file);

private:
  xe_mmap_ref mmap_;
  STFSEntry*  stfs_entry_;
  std::vector<STFSEntry*>::iterator stfs_entry_iterator_;
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_ENTRY_H_

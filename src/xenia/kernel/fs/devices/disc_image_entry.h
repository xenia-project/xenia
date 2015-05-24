/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_DEVICES_DISC_IMAGE_ENTRY_H_
#define XENIA_KERNEL_FS_DEVICES_DISC_IMAGE_ENTRY_H_

#include <vector>

#include "xenia/base/fs.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/kernel/fs/entry.h"
#include "xenia/kernel/fs/gdfx.h"

namespace xe {
namespace kernel {
namespace fs {

class DiscImageEntry : public Entry {
 public:
  DiscImageEntry(Device* device, const char* path, MappedMemory* mmap,
                 GDFXEntry* gdfx_entry);
  ~DiscImageEntry() override;

  MappedMemory* mmap() const { return mmap_; }
  GDFXEntry* gdfx_entry() const { return gdfx_entry_; }

  X_STATUS QueryInfo(X_FILE_NETWORK_OPEN_INFORMATION* out_info) override;
  X_STATUS QueryDirectory(XDirectoryInfo* out_info, size_t length,
                          const char* file_name, bool restart) override;

  bool can_map() override { return true; }
  std::unique_ptr<MemoryMapping> CreateMemoryMapping(
      Mode map_mode, const size_t offset, const size_t length) override;

  X_STATUS Open(KernelState* kernel_state, Mode mode, bool async,
                XFile** out_file) override;

 private:
  MappedMemory* mmap_;
  GDFXEntry* gdfx_entry_;

  xe::fs::WildcardEngine find_engine_;
  GDFXEntry::child_it_t it_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_DEVICES_DISC_IMAGE_ENTRY_H_

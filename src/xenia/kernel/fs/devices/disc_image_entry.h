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

#include "poly/mapped_memory.h"
#include "xenia/common.h"
#include "xenia/kernel/fs/entry.h"

namespace xe {
namespace kernel {
namespace fs {

class GDFXEntry;

class DiscImageEntry : public Entry {
 public:
  DiscImageEntry(Type type, Device* device, const char* path,
                 poly::MappedMemory* mmap, GDFXEntry* gdfx_entry);
  ~DiscImageEntry() override;

  poly::MappedMemory* mmap() const { return mmap_; }
  GDFXEntry* gdfx_entry() const { return gdfx_entry_; }

  X_STATUS QueryInfo(XFileInfo* out_info) override;
  X_STATUS QueryDirectory(XDirectoryInfo* out_info, size_t length,
                          const char* file_name, bool restart) override;

  bool can_map() override { return true; }
  std::unique_ptr<MemoryMapping> CreateMemoryMapping(
      Mode map_mode, const size_t offset, const size_t length) override;

  X_STATUS Open(KernelState* kernel_state, Mode mode, bool async,
                XFile** out_file) override;

 private:
  poly::MappedMemory* mmap_;
  GDFXEntry* gdfx_entry_;
  std::vector<GDFXEntry*>::iterator gdfx_entry_iterator_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_DEVICES_DISC_IMAGE_ENTRY_H_

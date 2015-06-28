/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_DISC_IMAGE_ENTRY_H_
#define XENIA_VFS_DEVICES_DISC_IMAGE_ENTRY_H_

#include <vector>

#include "xenia/base/filesystem.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/vfs/entry.h"
#include "xenia/vfs/gdfx.h"

namespace xe {
namespace vfs {

class DiscImageEntry : public Entry {
 public:
  DiscImageEntry(Device* device, const char* path, MappedMemory* mmap,
                 GDFXEntry* gdfx_entry);
  ~DiscImageEntry() override;

  MappedMemory* mmap() const { return mmap_; }
  GDFXEntry* gdfx_entry() const { return gdfx_entry_; }

  X_STATUS QueryInfo(X_FILE_NETWORK_OPEN_INFORMATION* out_info) override;
  X_STATUS QueryDirectory(X_FILE_DIRECTORY_INFORMATION* out_info, size_t length,
                          const char* file_name, bool restart) override;

  X_STATUS Open(KernelState* kernel_state, Mode mode, bool async,
                XFile** out_file) override;

  bool can_map() const override { return true; }
  std::unique_ptr<MappedMemory> OpenMapped(MappedMemory::Mode mode,
                                           size_t offset,
                                           size_t length) override;

 private:
  MappedMemory* mmap_;
  GDFXEntry* gdfx_entry_;

  xe::filesystem::WildcardEngine find_engine_;
  GDFXEntry::child_it_t it_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_DISC_IMAGE_ENTRY_H_

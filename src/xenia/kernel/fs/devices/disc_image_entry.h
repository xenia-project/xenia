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

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/fs/entry.h>


namespace xe {
namespace kernel {
namespace fs {

class GDFXEntry;


class DiscImageEntry : public Entry {
public:
  DiscImageEntry(Type type, Device* device, const char* path,
                 xe_mmap_ref mmap, GDFXEntry* gdfx_entry);
  virtual ~DiscImageEntry();

  xe_mmap_ref mmap() const { return mmap_; }
  GDFXEntry* gdfx_entry() const { return gdfx_entry_; }

  virtual X_STATUS QueryInfo(XFileInfo* out_info);
  virtual X_STATUS QueryDirectory(XDirectoryInfo* out_info,
                                  size_t length, const char* file_name, bool restart);

  virtual bool can_map() { return true; }
  virtual MemoryMapping* CreateMemoryMapping(
      xe_file_mode file_mode, const size_t offset, const size_t length);

  virtual X_STATUS Open(
      KernelState* kernel_state,
      uint32_t desired_access, bool async,
      XFile** out_file);

private:
  xe_mmap_ref mmap_;
  GDFXEntry*  gdfx_entry_;
  std::vector<GDFXEntry*>::iterator gdfx_entry_iterator_;
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_DEVICES_DISC_IMAGE_ENTRY_H_

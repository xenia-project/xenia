/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/fs/devices/disc_image_entry.h>

#include <xenia/kernel/xboxkrnl/fs/gdfx.h>
#include <xenia/kernel/xboxkrnl/fs/devices/disc_image_file.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;
using namespace xe::kernel::xboxkrnl::fs;


namespace {


class DiscImageMemoryMapping : public MemoryMapping {
public:
  DiscImageMemoryMapping(uint8_t* address, size_t length, xe_mmap_ref mmap) :
      MemoryMapping(address, length) {
    mmap_ = xe_mmap_retain(mmap);
  }

  virtual ~DiscImageMemoryMapping() {
    xe_mmap_release(mmap_);
  }

private:
  xe_mmap_ref mmap_;
};


}


DiscImageEntry::DiscImageEntry(Type type, Device* device, const char* path,
                               xe_mmap_ref mmap, GDFXEntry* gdfx_entry) :
    gdfx_entry_(gdfx_entry),
    Entry(type, device, path) {
  mmap_ = xe_mmap_retain(mmap);
}

DiscImageEntry::~DiscImageEntry() {
  xe_mmap_release(mmap_);
}

X_STATUS DiscImageEntry::QueryInfo(XFileInfo* out_info) {
  XEASSERTNOTNULL(out_info);
  out_info->creation_time     = 0;
  out_info->last_access_time  = 0;
  out_info->last_write_time   = 0;
  out_info->change_time       = 0;
  out_info->allocation_size   = 2048;
  out_info->file_length       = gdfx_entry_->size;
  out_info->attributes        = gdfx_entry_->attributes;
  return X_STATUS_SUCCESS;
}

MemoryMapping* DiscImageEntry::CreateMemoryMapping(
    xe_file_mode file_mode, const size_t offset, const size_t length) {
  if (file_mode & kXEFileModeWrite) {
    // Only allow reads.
    return NULL;
  }

  size_t real_offset = gdfx_entry_->offset + offset;
  size_t real_length = length ?
      MIN(length, gdfx_entry_->size) : gdfx_entry_->size;
  return new DiscImageMemoryMapping(
      xe_mmap_get_addr(mmap_) + real_offset,
      real_length,
      mmap_);
}

X_STATUS DiscImageEntry::Open(
    KernelState* kernel_state,
    uint32_t desired_access, bool async,
    XFile** out_file) {
  *out_file = new DiscImageFile(kernel_state, desired_access, this);
  return X_STATUS_SUCCESS;
}

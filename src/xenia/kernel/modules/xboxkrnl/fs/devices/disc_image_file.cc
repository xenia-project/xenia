/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/fs/devices/disc_image_file.h>

#include <xenia/kernel/modules/xboxkrnl/fs/gdfx.h>
#include <xenia/kernel/modules/xboxkrnl/fs/devices/disc_image_entry.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;
using namespace xe::kernel::xboxkrnl::fs;


DiscImageFile::DiscImageFile(
    KernelState* kernel_state, uint32_t desired_access,
    DiscImageEntry* entry) :
    entry_(entry),
    XFile(kernel_state, desired_access) {
}

DiscImageFile::~DiscImageFile() {
}

X_STATUS DiscImageFile::QueryInfo(FileInfo* out_info) {
  XEASSERTNOTNULL(out_info);
  GDFXEntry* gdfx_entry = entry_->gdfx_entry();
  out_info->creation_time     = 0;
  out_info->last_access_time  = 0;
  out_info->last_write_time   = 0;
  out_info->change_time       = 0;
  out_info->allocation_size   = 2048;
  out_info->file_length       = gdfx_entry->size;
  out_info->attributes        = gdfx_entry->attributes;
  return X_STATUS_SUCCESS;
}

X_STATUS DiscImageFile::ReadSync(
    void* buffer, size_t buffer_length, size_t byte_offset,
    size_t* out_bytes_read) {
  GDFXEntry* gdfx_entry = entry_->gdfx_entry();
  xe_mmap_ref mmap = entry_->mmap();
  size_t real_offset = gdfx_entry->offset + byte_offset;
  size_t real_length = MIN(buffer_length, gdfx_entry->size - byte_offset);
  xe_copy_memory(
      buffer, buffer_length,
      xe_mmap_get_addr(mmap) + real_offset, real_length);
  *out_bytes_read = real_length;
  return X_STATUS_SUCCESS;
}

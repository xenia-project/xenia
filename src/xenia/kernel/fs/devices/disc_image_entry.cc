/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/devices/disc_image_entry.h>

#include <xenia/kernel/fs/gdfx.h>
#include <xenia/kernel/fs/devices/disc_image_file.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


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
    gdfx_entry_iterator_(gdfx_entry->children.end()),
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

X_STATUS DiscImageEntry::QueryDirectory(
    XDirectoryInfo* out_info, size_t length, bool restart) {
  XEASSERTNOTNULL(out_info);
  xe_zero_struct(out_info, length);

  if (restart == true && gdfx_entry_iterator_ != gdfx_entry_->children.end()) {
    gdfx_entry_iterator_ = gdfx_entry_->children.end();
  }

  if (gdfx_entry_iterator_ == gdfx_entry_->children.end()) {
    gdfx_entry_iterator_ = gdfx_entry_->children.begin();
    if (gdfx_entry_iterator_ == gdfx_entry_->children.end()) {
      return X_STATUS_UNSUCCESSFUL;
    }
  } else {
    ++gdfx_entry_iterator_;
    if (gdfx_entry_iterator_ == gdfx_entry_->children.end()) {
      return X_STATUS_UNSUCCESSFUL;
    }
  }

  auto current_buf = (uint8_t*)out_info;
  auto end = current_buf + length;

  XDirectoryInfo* current = (XDirectoryInfo*)current_buf;
  if (((uint8_t*)&current->file_name[0]) + xestrlena((*gdfx_entry_iterator_)->name.c_str()) > end) {
    gdfx_entry_iterator_ = gdfx_entry_->children.end();
    return X_STATUS_UNSUCCESSFUL;
  }

  do {
    auto entry = *gdfx_entry_iterator_;

    auto file_name = entry->name.c_str();
    size_t file_name_length = xestrlena(file_name);
    if (((uint8_t*)&((XDirectoryInfo*)current_buf)->file_name[0]) + file_name_length > end) {
      break;
    }

    current = (XDirectoryInfo*)current_buf;
    current->file_index = 0xCDCDCDCD;
    current->creation_time = 0;
    current->last_access_time = 0;
    current->last_write_time = 0;
    current->change_time = 0;
    current->end_of_file = entry->size;
    current->allocation_size = 2048;
    current->attributes = (X_FILE_ATTRIBUTES)entry->attributes;

    current->file_name_length = (uint32_t)file_name_length;
    memcpy(current->file_name, file_name, file_name_length);

    auto next_buf = (((uint8_t*)&current->file_name[0]) + file_name_length);
    next_buf += 8 - ((uint8_t)next_buf % 8);

    current->next_entry_offset = (uint32_t)(next_buf - current_buf);
    current_buf = next_buf;
  } while (current_buf < end &&
           (++gdfx_entry_iterator_) != gdfx_entry_->children.end());
  current->next_entry_offset = 0;
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

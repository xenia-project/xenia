/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/devices/stfs_container_entry.h>

#include <xenia/kernel/fs/stfs.h>
#include <xenia/kernel/fs/devices/stfs_container_file.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


STFSContainerEntry::STFSContainerEntry(
    Type type, Device* device, const char* path,
    xe_mmap_ref mmap, STFSEntry* stfs_entry) :
    stfs_entry_(stfs_entry),
    stfs_entry_iterator_(stfs_entry->children.end()),
    Entry(type, device, path) {
  mmap_ = xe_mmap_retain(mmap);
}

STFSContainerEntry::~STFSContainerEntry() {
  xe_mmap_release(mmap_);
}

X_STATUS STFSContainerEntry::QueryInfo(XFileInfo* out_info) {
  XEASSERTNOTNULL(out_info);
  out_info->creation_time     = stfs_entry_->update_timestamp;
  out_info->last_access_time  = stfs_entry_->access_timestamp;
  out_info->last_write_time   = stfs_entry_->update_timestamp;
  out_info->change_time       = stfs_entry_->update_timestamp;
  out_info->allocation_size   = 4096;
  out_info->file_length       = stfs_entry_->size;
  out_info->attributes        = stfs_entry_->attributes;
  return X_STATUS_SUCCESS;
}

X_STATUS STFSContainerEntry::QueryDirectory(
    XDirectoryInfo* out_info, size_t length, bool restart) {
  XEASSERTNOTNULL(out_info);

  if (restart && stfs_entry_iterator_ != stfs_entry_->children.end()) {
    stfs_entry_iterator_ = stfs_entry_->children.end();
  }

  if (stfs_entry_iterator_ == stfs_entry_->children.end()) {
    stfs_entry_iterator_ = stfs_entry_->children.begin();
    if (stfs_entry_iterator_ == stfs_entry_->children.end()) {
      return X_STATUS_UNSUCCESSFUL;
    }
  } else {
    ++stfs_entry_iterator_;
    if (stfs_entry_iterator_ == stfs_entry_->children.end()) {
      return X_STATUS_UNSUCCESSFUL;
    }
  }

  auto current_buf = (uint8_t*)out_info;
  auto end = current_buf + length;

  XDirectoryInfo* current = (XDirectoryInfo*)current_buf;
  if (((uint8_t*)&current->file_name[0]) + xestrlena((*stfs_entry_iterator_)->name.c_str()) > end) {
    stfs_entry_iterator_ = stfs_entry_->children.end();
    return X_STATUS_UNSUCCESSFUL;
  }

  do {
    auto entry = *stfs_entry_iterator_;

    auto file_name = entry->name.c_str();
    size_t file_name_length = xestrlena(file_name);
    if (((uint8_t*)&((XDirectoryInfo*)current_buf)->file_name[0]) + file_name_length > end) {
      break;
    }

    current = (XDirectoryInfo*)current_buf;
    current->file_index       = 0xCDCDCDCD;
    current->creation_time    = entry->update_timestamp;
    current->last_access_time = entry->access_timestamp;
    current->last_write_time  = entry->update_timestamp;
    current->change_time      = entry->update_timestamp;
    current->end_of_file      = entry->size;
    current->allocation_size  = 4096;
    current->attributes       = entry->attributes;

    current->file_name_length = (uint32_t)file_name_length;
    memcpy(current->file_name, file_name, file_name_length);

    auto next_buf = (((uint8_t*)&current->file_name[0]) + file_name_length);
    next_buf += 8 - ((uint8_t)next_buf % 8);

    current->next_entry_offset = (uint32_t)(next_buf - current_buf);
    current_buf = next_buf;
  } while (current_buf < end &&
           (++stfs_entry_iterator_) != stfs_entry_->children.end());
  current->next_entry_offset = 0;
  return X_STATUS_SUCCESS;
}

X_STATUS STFSContainerEntry::Open(
    KernelState* kernel_state,
    uint32_t desired_access, bool async,
    XFile** out_file) {
  *out_file = new STFSContainerFile(kernel_state, desired_access, this);
  return X_STATUS_SUCCESS;
}

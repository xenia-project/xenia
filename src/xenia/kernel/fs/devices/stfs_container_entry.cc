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
  assert_not_null(out_info);
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
    XDirectoryInfo* out_info, size_t length, const char* file_name, bool restart) {
  assert_not_null(out_info);

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

  auto end = (uint8_t*)out_info + length;

  auto entry = *stfs_entry_iterator_;
  auto entry_name = entry->name.c_str();
  size_t entry_name_length = xestrlena(entry_name);

  if (((uint8_t*)&out_info->file_name[0]) + entry_name_length > end) {
    stfs_entry_iterator_ = stfs_entry_->children.end();
    return X_STATUS_UNSUCCESSFUL;
  }

  out_info->file_index       = 0xCDCDCDCD;
  out_info->creation_time    = entry->update_timestamp;
  out_info->last_access_time = entry->access_timestamp;
  out_info->last_write_time  = entry->update_timestamp;
  out_info->change_time      = entry->update_timestamp;
  out_info->end_of_file      = entry->size;
  out_info->allocation_size  = 4096;
  out_info->attributes       = entry->attributes;
  out_info->file_name_length = (uint32_t)entry_name_length;
  memcpy(out_info->file_name, entry_name, entry_name_length);

  return X_STATUS_SUCCESS;
}

X_STATUS STFSContainerEntry::Open(
    KernelState* kernel_state,
    uint32_t desired_access, bool async,
    XFile** out_file) {
  *out_file = new STFSContainerFile(kernel_state, desired_access, this);
  return X_STATUS_SUCCESS;
}

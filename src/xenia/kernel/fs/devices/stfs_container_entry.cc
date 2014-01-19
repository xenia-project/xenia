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

X_STATUS STFSContainerEntry::Open(
    KernelState* kernel_state,
    uint32_t desired_access, bool async,
    XFile** out_file) {
  *out_file = new STFSContainerFile(kernel_state, desired_access, this);
  return X_STATUS_SUCCESS;
}

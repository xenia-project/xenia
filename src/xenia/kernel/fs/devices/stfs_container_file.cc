/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/devices/stfs_container_file.h>

#include <xenia/kernel/fs/stfs.h>
#include <xenia/kernel/fs/devices/stfs_container_entry.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


STFSContainerFile::STFSContainerFile(
    KernelState* kernel_state, uint32_t desired_access,
    STFSContainerEntry* entry) :
    entry_(entry),
    XFile(kernel_state, desired_access) {
}

STFSContainerFile::~STFSContainerFile() {
}

const char* STFSContainerFile::name(void) {
  return entry_->name();
}

const char* STFSContainerFile::path(void) {
  return entry_->path();
}

X_STATUS STFSContainerFile::QueryInfo(XFileInfo* out_info) {
  return entry_->QueryInfo(out_info);
}

X_STATUS STFSContainerFile::QueryDirectory(
    XDirectoryInfo* out_info, size_t length, bool restart) {
  return entry_->QueryDirectory(out_info, length, restart);
}

X_STATUS STFSContainerFile::ReadSync(
    void* buffer, size_t buffer_length, size_t byte_offset,
    size_t* out_bytes_read) {
  STFSEntry* stfs_entry = entry_->stfs_entry();
  xe_mmap_ref mmap = entry_->mmap();
  uint8_t* map_ptr = xe_mmap_get_addr(mmap);

  // Each block is 4096.
  // Blocks may not be sequential, so we need to read by blocks and handle the
  // offsets.
  size_t real_length = MIN(buffer_length, stfs_entry->size - byte_offset);
  size_t start_block = byte_offset / 4096;
  size_t end_block = MIN(
      stfs_entry->block_list.size(),
      (size_t)ceil((byte_offset + real_length) / 4096.0));
  uint8_t* dest_ptr = (uint8_t*)buffer;
  size_t remaining_length = real_length;
  for (size_t n = start_block; n < end_block; n++) {
    auto& record = stfs_entry->block_list[n];
    size_t offset = record.offset;
    size_t read_length = MIN(remaining_length, record.length);
    if (n == start_block) {
      offset += byte_offset % 4096;
      read_length = MIN(read_length, record.length - (byte_offset % 4096));
    }
    xe_copy_struct(dest_ptr, map_ptr + offset, read_length);
    dest_ptr += read_length;
    remaining_length -= read_length;
  }
  *out_bytes_read = real_length;
  return X_STATUS_SUCCESS;
}

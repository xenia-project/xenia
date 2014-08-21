/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/devices/stfs_container_file.h>

#include <algorithm>

#include <xenia/kernel/fs/device.h>
#include <xenia/kernel/fs/devices/stfs_container_entry.h>
#include <xenia/kernel/fs/stfs.h>

namespace xe {
namespace kernel {
namespace fs {

STFSContainerFile::STFSContainerFile(KernelState* kernel_state, Mode mode,
                                     STFSContainerEntry* entry)
    : entry_(entry), XFile(kernel_state, mode) {}

STFSContainerFile::~STFSContainerFile() {}

const std::string& STFSContainerFile::path() const { return entry_->path(); }

const std::string& STFSContainerFile::absolute_path() const {
  return entry_->absolute_path();
}

const std::string& STFSContainerFile::name() const { return entry_->name(); }

X_STATUS STFSContainerFile::QueryInfo(XFileInfo* out_info) {
  return entry_->QueryInfo(out_info);
}

X_STATUS STFSContainerFile::QueryDirectory(XDirectoryInfo* out_info,
                                           size_t length, const char* file_name,
                                           bool restart) {
  return entry_->QueryDirectory(out_info, length, file_name, restart);
}

X_STATUS STFSContainerFile::QueryVolume(XVolumeInfo* out_info, size_t length) {
  return entry_->device()->QueryVolume(out_info, length);
}

X_STATUS STFSContainerFile::QueryFileSystemAttributes(
    XFileSystemAttributeInfo* out_info, size_t length) {
  return entry_->device()->QueryFileSystemAttributes(out_info, length);
}

X_STATUS STFSContainerFile::ReadSync(void* buffer, size_t buffer_length,
                                     size_t byte_offset,
                                     size_t* out_bytes_read) {
  STFSEntry* stfs_entry = entry_->stfs_entry();
  if (byte_offset >= stfs_entry->size) {
    return X_STATUS_END_OF_FILE;
  }

  // Each block is 4096.
  // Blocks may not be sequential, so we need to read by blocks and handle the
  // offsets.
  size_t real_length = std::min(buffer_length, stfs_entry->size - byte_offset);
  size_t start_block = byte_offset / 4096;
  size_t end_block =
      std::min(stfs_entry->block_list.size(),
               (size_t)ceil((byte_offset + real_length) / 4096.0));
  uint8_t* dest_ptr = (uint8_t*)buffer;
  size_t remaining_length = real_length;
  for (size_t n = start_block; n < end_block; n++) {
    auto& record = stfs_entry->block_list[n];
    size_t offset = record.offset;
    size_t read_length = std::min(remaining_length, record.length);
    if (n == start_block) {
      offset += byte_offset % 4096;
      read_length = std::min(read_length, record.length - (byte_offset % 4096));
    }
    memcpy(dest_ptr, entry_->mmap()->data() + offset, read_length);
    dest_ptr += read_length;
    remaining_length -= read_length;
  }
  *out_bytes_read = real_length;
  return X_STATUS_SUCCESS;
}

}  // namespace fs
}  // namespace kernel
}  // namespace xe

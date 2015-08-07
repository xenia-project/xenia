/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/stfs_container_file.h"

#include <algorithm>

#include "xenia/vfs/devices/stfs_container_entry.h"

namespace xe {
namespace vfs {

StfsContainerFile::StfsContainerFile(kernel::KernelState* kernel_state,
                                     uint32_t file_access,
                                     StfsContainerEntry* entry)
    : XFile(kernel_state, file_access, entry), entry_(entry) {}

StfsContainerFile::~StfsContainerFile() = default;

X_STATUS StfsContainerFile::ReadSync(void* buffer, size_t buffer_length,
                                     size_t byte_offset,
                                     size_t* out_bytes_read) {
  if (byte_offset >= entry_->size()) {
    return X_STATUS_END_OF_FILE;
  }

  // Each block is 4096.
  // Blocks may not be sequential, so we need to read by blocks and handle the
  // offsets.
  size_t real_length = std::min(buffer_length, entry_->size() - byte_offset);
  size_t start_block = byte_offset / 4096;
  size_t end_block =
      std::min(entry_->block_list().size(),
               (size_t)ceil((byte_offset + real_length) / 4096.0));
  uint8_t* dest_ptr = reinterpret_cast<uint8_t*>(buffer);
  size_t remaining_length = real_length;
  for (size_t n = start_block; n < end_block; n++) {
    auto& record = entry_->block_list()[n];
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

}  // namespace vfs
}  // namespace xe

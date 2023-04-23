/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_image_file.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/vfs/devices/disc_image_entry.h"
namespace xe {
namespace vfs {

DiscImageFile::DiscImageFile(uint32_t file_access, DiscImageEntry* entry)
    : File(file_access, entry), entry_(entry) {}

DiscImageFile::~DiscImageFile() = default;

void DiscImageFile::Destroy() { delete this; }

X_STATUS DiscImageFile::ReadSync(void* buffer, size_t buffer_length,
                                 size_t byte_offset, size_t* out_bytes_read) {
  if (byte_offset >= entry_->size()) {
    return X_STATUS_END_OF_FILE;
  }

  if (entry_->data_offset() >= entry_->mmap()->size()) {
    xe::FatalError("This ISO image is corrupted and cannot be played.");
    return X_STATUS_END_OF_FILE;
  }

  size_t real_offset = entry_->data_offset() + byte_offset;
  size_t real_length =
      std::min(buffer_length, entry_->data_size() - byte_offset);
  std::memcpy(buffer, entry_->mmap()->data() + real_offset, real_length);
  *out_bytes_read = real_length;
  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe

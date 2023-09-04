/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_zarchive_file.h"

#include <algorithm>

#include "xenia/vfs/devices/disc_zarchive_device.h"
#include "xenia/vfs/devices/disc_zarchive_entry.h"

namespace xe {
namespace vfs {

DiscZarchiveFile::DiscZarchiveFile(uint32_t file_access,
                                   DiscZarchiveEntry* entry)
    : File(file_access, entry), entry_(entry) {}

DiscZarchiveFile::~DiscZarchiveFile() = default;

void DiscZarchiveFile::Destroy() { delete this; }

X_STATUS DiscZarchiveFile::ReadSync(void* buffer, size_t buffer_length,
                                    size_t byte_offset,
                                    size_t* out_bytes_read) {
  if (byte_offset >= entry_->size()) {
    return X_STATUS_END_OF_FILE;
  }
  const uint64_t bytes_read =
      ((DiscZarchiveDevice*)entry_->device_)
          ->reader()
          ->ReadFromFile(entry_->handle_, byte_offset, buffer_length, buffer);
  const size_t real_length =
      std::min(buffer_length, entry_->data_size() - byte_offset);
  *out_bytes_read = real_length;
  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe

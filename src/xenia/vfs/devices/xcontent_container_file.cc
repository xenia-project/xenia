/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <cmath>

#include "xenia/base/math.h"
#include "xenia/vfs/devices/xcontent_container_entry.h"
#include "xenia/vfs/devices/xcontent_container_file.h"

namespace xe {
namespace vfs {

XContentContainerFile::XContentContainerFile(uint32_t file_access,
                                     XContentContainerEntry* entry)
    : File(file_access, entry), entry_(entry) {}

XContentContainerFile::~XContentContainerFile() = default;

void XContentContainerFile::Destroy() { delete this; }

X_STATUS XContentContainerFile::ReadSync(void* buffer, size_t buffer_length,
                                     size_t byte_offset,
                                     size_t* out_bytes_read) {
  if (byte_offset >= entry_->size()) {
    return X_STATUS_END_OF_FILE;
  }

  size_t src_offset = 0;
  uint8_t* p = reinterpret_cast<uint8_t*>(buffer);
  size_t remaining_length =
      std::min(buffer_length, entry_->size() - byte_offset);

  *out_bytes_read = 0;
  for (size_t i = 0; i < entry_->block_list().size(); i++) {
    auto& record = entry_->block_list()[i];
    if (src_offset + record.length <= byte_offset) {
      // Doesn't begin in this region. Skip it.
      src_offset += record.length;
      continue;
    }

    size_t read_offset =
        (byte_offset > src_offset) ? byte_offset - src_offset : 0;
    size_t read_length =
        std::min(record.length - read_offset, remaining_length);

    auto& file = entry_->files()->at(record.file);
    xe::filesystem::Seek(file, record.offset + read_offset, SEEK_SET);
    auto num_read = fread(p, 1, read_length, file);

    *out_bytes_read += num_read;
    p += num_read;
    src_offset += record.length;
    remaining_length -= read_length;
    if (remaining_length == 0) {
      break;
    }
  }

  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe
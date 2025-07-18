/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_devices/stfs_container_file.h"
#include "xenia/vfs/devices/xcontent_devices/stfs_container_entry.h"

namespace xe {
namespace vfs {

StfsContainerFile::StfsContainerFile(uint32_t file_access,
                                     StfsContainerEntry* entry)
    : XContentContainerFile(file_access, entry), entry_(entry) {}

StfsContainerFile::~StfsContainerFile() = default;

void StfsContainerFile::Destroy() { delete this; }

size_t StfsContainerFile::Read(std::span<uint8_t> buffer, size_t offset,
                               size_t record_file) {
  std::memcpy(buffer.data(), entry_->data()->data() + offset, buffer.size());
  return buffer.size();
}

}  // namespace vfs
}  // namespace xe

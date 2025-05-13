/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_devices/svod_container_entry.h"
#include "xenia/vfs/devices/xcontent_devices/svod_container_file.h"

namespace xe {
namespace vfs {

SvodContainerEntry::SvodContainerEntry(Device* device, Entry* parent,
                                       const std::string_view path,
                                       MultiFileHandles* files)
    : XContentContainerEntry(device, parent, path), files_(files) {}

SvodContainerEntry::~SvodContainerEntry() = default;

std::unique_ptr<SvodContainerEntry> SvodContainerEntry::Create(
    Device* device, Entry* parent, const std::string_view name,
    MultiFileHandles* files) {
  auto path = xe::utf8::join_guest_paths(parent->path(), name);
  auto entry =
      std::make_unique<SvodContainerEntry>(device, parent, path, files);

  return std::move(entry);
}

X_STATUS SvodContainerEntry::Open(uint32_t desired_access, File** out_file) {
  *out_file = new SvodContainerFile(desired_access, this);
  return X_STATUS_SUCCESS;
}

bool SvodContainerEntry::DeleteEntryInternal(Entry* entry) { return false; }

}  // namespace vfs
}  // namespace xe
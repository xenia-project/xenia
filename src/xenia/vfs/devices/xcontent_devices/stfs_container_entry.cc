/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_devices/stfs_container_entry.h"
#include "xenia/vfs/devices/xcontent_devices/stfs_container_file.h"

namespace xe {
namespace vfs {

StfsContainerEntry::StfsContainerEntry(Device* device, Entry* parent,
                                       const std::string_view path,
                                       MappedMemory* data)
    : XContentContainerEntry(device, parent, path), data_(data) {}

StfsContainerEntry::~StfsContainerEntry() = default;

std::unique_ptr<StfsContainerEntry> StfsContainerEntry::Create(
    Device* device, Entry* parent, const std::string_view name,
    MappedMemory* data) {
  auto path = xe::utf8::join_guest_paths(parent->path(), name);
  auto entry = std::make_unique<StfsContainerEntry>(device, parent, path, data);

  return std::move(entry);
}

X_STATUS StfsContainerEntry::Open(uint32_t desired_access, File** out_file) {
  *out_file = new StfsContainerFile(desired_access, this);
  return X_STATUS_SUCCESS;
}

bool StfsContainerEntry::DeleteEntryInternal(Entry* entry) { return false; }

}  // namespace vfs
}  // namespace xe
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_container_entry.h"
#include "xenia/vfs/devices/xcontent_container_file.h"

#include <map>

namespace xe {
namespace vfs {

XContentContainerEntry::XContentContainerEntry(Device* device, Entry* parent,
                                               const std::string_view path,
                                               MultiFileHandles* files)
    : Entry(device, parent, path),
      files_(files),
      data_offset_(0),
      data_size_(0),
      block_(0) {}

XContentContainerEntry::~XContentContainerEntry() = default;

std::unique_ptr<XContentContainerEntry> XContentContainerEntry::Create(
    Device* device, Entry* parent, const std::string_view name,
    MultiFileHandles* files) {
  auto path = xe::utf8::join_guest_paths(parent->path(), name);
  auto entry =
      std::make_unique<XContentContainerEntry>(device, parent, path, files);

  return std::move(entry);
}

X_STATUS XContentContainerEntry::Open(uint32_t desired_access,
                                      File** out_file) {
  *out_file = new XContentContainerFile(desired_access, this);
  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe
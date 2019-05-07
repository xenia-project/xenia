/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/stfs_container_entry.h"
#include "xenia/base/math.h"
#include "xenia/vfs/devices/stfs_container_file.h"

#include <map>

namespace xe {
namespace vfs {

StfsContainerEntry::StfsContainerEntry(Device* device, Entry* parent,
                                       std::string path,
                                       MultifileMemoryMap* mmap)
    : Entry(device, parent, path),
      mmap_(mmap),
      data_offset_(0),
      data_size_(0) {}

StfsContainerEntry::~StfsContainerEntry() = default;

std::unique_ptr<StfsContainerEntry> StfsContainerEntry::Create(
    Device* device, Entry* parent, std::string name, MultifileMemoryMap* mmap) {
  auto path = xe::join_paths(parent->path(), name);
  auto entry = std::make_unique<StfsContainerEntry>(device, parent, path, mmap);

  return std::move(entry);
}

X_STATUS StfsContainerEntry::Open(uint32_t desired_access, File** out_file) {
  *out_file = new StfsContainerFile(desired_access, this);
  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe
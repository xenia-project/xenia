/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_zarchive_entry.h"

#include <algorithm>

#include "xenia/base/math.h"
#include "xenia/vfs/devices/disc_zarchive_file.h"

#include "third_party/zarchive/include/zarchive/zarchivereader.h"

namespace xe {
namespace vfs {

DiscZarchiveEntry::DiscZarchiveEntry(Device* device, Entry* parent,
                                     const std::string_view path)
    : Entry(device, parent, path),
      data_offset_(0),
      data_size_(0),
      handle_(ZARCHIVE_INVALID_NODE) {}

DiscZarchiveEntry::~DiscZarchiveEntry() = default;

std::unique_ptr<DiscZarchiveEntry> DiscZarchiveEntry::Create(
    Device* device, Entry* parent, const std::string_view name) {
  auto path = name;  // xe::utf8::join_guest_paths(parent->path(), name);
  auto entry = std::make_unique<DiscZarchiveEntry>(device, parent, path);
  return std::move(entry);
}

X_STATUS DiscZarchiveEntry::Open(uint32_t desired_access, File** out_file) {
  *out_file = new DiscZarchiveFile(desired_access, this);
  return X_STATUS_SUCCESS;
}

std::unique_ptr<MappedMemory> DiscZarchiveEntry::OpenMapped(
    MappedMemory::Mode mode, size_t offset, size_t length) {
  return nullptr;
}

}  // namespace vfs
}  // namespace xe

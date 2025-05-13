/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_container_entry.h"

namespace xe {
namespace vfs {

XContentContainerEntry::XContentContainerEntry(Device* device, Entry* parent,
                                               const std::string_view path)
    : Entry(device, parent, path), data_offset_(0), data_size_(0), block_(0) {}

XContentContainerEntry::~XContentContainerEntry() = default;

bool XContentContainerEntry::DeleteEntryInternal(Entry* entry) { return false; }

}  // namespace vfs
}  // namespace xe
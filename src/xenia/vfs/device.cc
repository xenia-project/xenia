/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/device.h"

#include "xenia/base/logging.h"

namespace xe {
namespace vfs {

Device::Device(const std::string& mount_path) : mount_path_(mount_path) {}

Device::~Device() = default;

void Device::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

Entry* Device::ResolvePath(std::string path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("Device::ResolvePath(%s)", path.c_str());

  // Walk the path, one separator at a time.
  auto entry = root_entry_.get();
  auto path_parts = xe::split_path(path);
  for (auto& part : path_parts) {
    entry = entry->GetChild(part);
    if (!entry) {
      // Not found.
      return nullptr;
    }
  }
  return entry;
}

}  // namespace vfs
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/fs/entry.h"

#include "xenia/base/string.h"
#include "xenia/kernel/fs/device.h"

namespace xe {
namespace kernel {
namespace fs {

MemoryMapping::MemoryMapping(uint8_t* address, size_t length)
    : address_(address), length_(length) {}

MemoryMapping::~MemoryMapping() {}

Entry::Entry(Device* device, const std::string& path)
    : device_(device), path_(path) {
  assert_not_null(device);
  absolute_path_ = device->path() + path;
  name_ = xe::find_name_from_path(path);
}

Entry::~Entry() = default;

bool Entry::is_read_only() const {
  return device_->is_read_only();
}

}  // namespace fs
}  // namespace kernel
}  // namespace xe

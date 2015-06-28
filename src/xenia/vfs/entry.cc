/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/entry.h"

#include "xenia/base/string.h"
#include "xenia/vfs/device.h"

namespace xe {
namespace vfs {

Entry::Entry(Device* device, const std::string& path)
    : device_(device), path_(path) {
  assert_not_null(device);
  absolute_path_ = xe::join_paths(device->mount_path(), path);
  name_ = xe::find_name_from_path(path);
}

Entry::~Entry() = default;

bool Entry::is_read_only() const { return device_->is_read_only(); }

}  // namespace vfs
}  // namespace xe

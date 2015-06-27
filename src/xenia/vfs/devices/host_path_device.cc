/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/host_path_device.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/vfs/devices/host_path_entry.h"
#include "xenia/kernel/objects/xfile.h"

namespace xe {
namespace vfs {

HostPathDevice::HostPathDevice(const std::string& mount_path,
                               const std::wstring& local_path, bool read_only)
    : Device(mount_path), local_path_(local_path), read_only_(read_only) {}

HostPathDevice::~HostPathDevice() {}

std::unique_ptr<Entry> HostPathDevice::ResolvePath(const char* path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("HostPathDevice::ResolvePath(%s)", path);

  auto rel_path = xe::to_wstring(path);
  auto full_path = xe::join_paths(local_path_, rel_path);
  full_path = xe::fix_path_separators(full_path);

  // Only check the file exists when the device is read-only
  if (read_only_) {
    if (!xe::filesystem::PathExists(full_path)) {
      return nullptr;
    }
  }

  // TODO(benvanik): get file info
  // TODO(benvanik): switch based on type

  return std::make_unique<HostPathEntry>(this, path, full_path);
}

}  // namespace vfs
}  // namespace xe

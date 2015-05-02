/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/fs/devices/host_path_device.h"

#include "poly/fs.h"
#include "xenia/kernel/fs/devices/host_path_entry.h"
#include "xenia/kernel/objects/xfile.h"
#include "xenia/logging.h"

namespace xe {
namespace kernel {
namespace fs {

HostPathDevice::HostPathDevice(const std::string& path,
                               const std::wstring& local_path, bool read_only)
    : Device(path), local_path_(local_path), read_only_(read_only) {}

HostPathDevice::~HostPathDevice() {}

std::unique_ptr<Entry> HostPathDevice::ResolvePath(const char* path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("HostPathDevice::ResolvePath(%s)", path);

  auto rel_path = poly::to_wstring(path);
  auto full_path = poly::join_paths(local_path_, rel_path);
  full_path = poly::fix_path_separators(full_path);

  // Only check the file exists when the device is read-only
  if (read_only_) {
    if (!poly::fs::PathExists(full_path)) {
      return nullptr;
    }
  }

  // TODO(benvanik): get file info
  // TODO(benvanik): switch based on type

  return std::make_unique<HostPathEntry>(this, path, full_path);
}

}  // namespace fs
}  // namespace kernel
}  // namespace xe

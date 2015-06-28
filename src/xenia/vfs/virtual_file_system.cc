/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/virtual_file_system.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

namespace xe {
namespace vfs {

VirtualFileSystem::VirtualFileSystem() {}

VirtualFileSystem::~VirtualFileSystem() {
  // Delete all devices.
  // This will explode if anyone is still using data from them.
  devices_.clear();
  symlinks_.clear();
}

bool VirtualFileSystem::RegisterDevice(std::unique_ptr<Device> device) {
  devices_.emplace_back(std::move(device));
  return true;
}

bool VirtualFileSystem::RegisterSymbolicLink(std::string path,
                                             std::string target) {
  symlinks_.insert({path, target});
  return true;
}

bool VirtualFileSystem::UnregisterSymbolicLink(std::string path) {
  auto& it = symlinks_.find(path);
  if (it == symlinks_.end()) {
    return false;
  }
  symlinks_.erase(it);
  return true;
}

Entry* VirtualFileSystem::ResolvePath(std::string path) {
  // Resolve relative paths
  std::string normalized_path(xe::filesystem::CanonicalizePath(path));

  // Resolve symlinks.
  std::string device_path;
  std::string relative_path;
  for (const auto& it : symlinks_) {
    if (xe::find_first_of_case(normalized_path, it.first) == 0) {
      // Found symlink!
      device_path = it.second;
      relative_path = normalized_path.substr(it.first.size());
      break;
    }
  }

  // Not to fret, check to see if the path is fully qualified.
  if (device_path.empty()) {
    for (auto& device : devices_) {
      if (xe::find_first_of_case(normalized_path, device->mount_path()) == 0) {
        device_path = device->mount_path();
        relative_path = normalized_path.substr(device_path.size());
      }
    }
  }

  if (device_path.empty()) {
    XELOGE("ResolvePath(%s) failed - no root found", path.c_str());
    return nullptr;
  }

  // Scan all devices.
  for (auto& device : devices_) {
    if (strcasecmp(device_path.c_str(), device->mount_path().c_str()) == 0) {
      return device->ResolvePath(relative_path.c_str());
    }
  }

  XELOGE("ResolvePath(%s) failed - device not found (%s)", path.c_str(),
         device_path.c_str());
  return nullptr;
}

}  // namespace vfs
}  // namespace xe

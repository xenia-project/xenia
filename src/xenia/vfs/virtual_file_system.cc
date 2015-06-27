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
#include "xenia/vfs/devices/disc_image_device.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/vfs/devices/stfs_container_device.h"

namespace xe {
namespace vfs {

VirtualFileSystem::VirtualFileSystem() {}

VirtualFileSystem::~VirtualFileSystem() {
  // Delete all devices.
  // This will explode if anyone is still using data from them.
  for (std::vector<Device*>::iterator it = devices_.begin();
       it != devices_.end(); ++it) {
    delete *it;
  }
  devices_.clear();
  symlinks_.clear();
}

FileSystemType VirtualFileSystem::InferType(const std::wstring& local_path) {
  auto last_slash = local_path.find_last_of(xe::path_separator);
  auto last_dot = local_path.find_last_of('.');
  if (last_dot < last_slash) {
    last_dot = std::wstring::npos;
  }
  if (last_dot == std::wstring::npos) {
    // Likely an STFS container.
    return FileSystemType::STFS_TITLE;
  } else if (local_path.substr(last_dot) == L".xex") {
    // Treat as a naked xex file.
    return FileSystemType::XEX_FILE;
  } else {
    // Assume a disc image.
    return FileSystemType::DISC_IMAGE;
  }
}

int VirtualFileSystem::InitializeFromPath(FileSystemType type,
                                          const std::wstring& local_path) {
  switch (type) {
    case FileSystemType::STFS_TITLE: {
      // Register the container in the virtual filesystem.
      int result_code =
          RegisterSTFSContainerDevice("\\Device\\Cdrom0", local_path);
      if (result_code) {
        XELOGE("Unable to mount STFS container");
        return result_code;
      }

      // TODO(benvanik): figure out paths.
      // Create symlinks to the device.
      CreateSymbolicLink("game:", "\\Device\\Cdrom0");
      CreateSymbolicLink("d:", "\\Device\\Cdrom0");
      break;
    }
    case FileSystemType::XEX_FILE: {
      // Get the parent path of the file.
      auto last_slash = local_path.find_last_of(xe::path_separator);
      std::wstring parent_path = local_path.substr(0, last_slash);

      // Register the local directory in the virtual filesystem.
      int result_code = RegisterHostPathDevice(
          "\\Device\\Harddisk0\\Partition0", parent_path, true);
      if (result_code) {
        XELOGE("Unable to mount local directory");
        return result_code;
      }

      // Create symlinks to the device.
      CreateSymbolicLink("game:", "\\Device\\Harddisk0\\Partition0");
      CreateSymbolicLink("d:", "\\Device\\Harddisk0\\Partition0");
      break;
    }
    case FileSystemType::DISC_IMAGE: {
      // Register the disc image in the virtual filesystem.
      int result_code = RegisterDiscImageDevice("\\Device\\Cdrom0", local_path);
      if (result_code) {
        XELOGE("Unable to mount disc image");
        return result_code;
      }

      // Create symlinks to the device.
      CreateSymbolicLink("game:", "\\Device\\Cdrom0");
      CreateSymbolicLink("d:", "\\Device\\Cdrom0");
      break;
    }
  }
  return 0;
}

int VirtualFileSystem::RegisterDevice(const std::string& path, Device* device) {
  devices_.push_back(device);
  return 0;
}

int VirtualFileSystem::RegisterHostPathDevice(const std::string& path,
                                              const std::wstring& local_path,
                                              bool read_only) {
  Device* device = new HostPathDevice(path, local_path, read_only);
  return RegisterDevice(path, device);
}

int VirtualFileSystem::RegisterDiscImageDevice(const std::string& path,
                                               const std::wstring& local_path) {
  DiscImageDevice* device = new DiscImageDevice(path, local_path);
  if (device->Init()) {
    return 1;
  }
  return RegisterDevice(path, device);
}

int VirtualFileSystem::RegisterSTFSContainerDevice(
    const std::string& path, const std::wstring& local_path) {
  STFSContainerDevice* device = new STFSContainerDevice(path, local_path);
  if (device->Init()) {
    return 1;
  }
  return RegisterDevice(path, device);
}

int VirtualFileSystem::CreateSymbolicLink(const std::string& path,
                                          const std::string& target) {
  symlinks_.insert({path, target});
  return 0;
}

int VirtualFileSystem::DeleteSymbolicLink(const std::string& path) {
  auto& it = symlinks_.find(path);
  if (it == symlinks_.end()) {
    return 1;
  }
  symlinks_.erase(it);
  return 0;
}

std::unique_ptr<Entry> VirtualFileSystem::ResolvePath(const std::string& path) {
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
      if (xe::find_first_of_case(normalized_path, device->path()) == 0) {
        device_path = device->path();
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
    if (strcasecmp(device_path.c_str(), device->path().c_str()) == 0) {
      return device->ResolvePath(relative_path.c_str());
    }
  }

  XELOGE("ResolvePath(%s) failed - device not found (%s)", path.c_str(),
         device_path.c_str());
  return nullptr;
}

X_STATUS VirtualFileSystem::Open(std::unique_ptr<Entry> entry,
                                 KernelState* kernel_state, Mode mode,
                                 bool async, XFile** out_file) {
  auto result = entry->Open(kernel_state, mode, async, out_file);
  if (XSUCCEEDED(result)) {
    entry.release();
  }
  return result;
}

}  // namespace vfs
}  // namespace xe

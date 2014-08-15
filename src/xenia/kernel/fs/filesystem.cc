/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/filesystem.h>

#include <xenia/kernel/fs/devices/disc_image_device.h>
#include <xenia/kernel/fs/devices/host_path_device.h>
#include <xenia/kernel/fs/devices/stfs_container_device.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


FileSystem::FileSystem() {
}

FileSystem::~FileSystem() {
  // Delete all devices.
  // This will explode if anyone is still using data from them.
  for (std::vector<Device*>::iterator it = devices_.begin();
       it != devices_.end(); ++it) {
    delete *it;
  }
  devices_.clear();
  symlinks_.clear();
}

fs::FileSystemType FileSystem::InferType(const std::wstring& local_path) {
  auto last_dot = local_path.find_last_of('.');
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

int FileSystem::InitializeFromPath(fs::FileSystemType type,
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
      auto last_slash = local_path.find_last_of(poly::path_separator);
      std::wstring parent_path = local_path.substr(0, last_slash);

      // Register the local directory in the virtual filesystem.
      int result_code = RegisterHostPathDevice(
          "\\Device\\Harddisk1\\Partition0", parent_path);
      if (result_code) {
        XELOGE("Unable to mount local directory");
        return result_code;
      }

      // Create symlinks to the device.
      CreateSymbolicLink("game:", "\\Device\\Harddisk1\\Partition0");
      CreateSymbolicLink("d:", "\\Device\\Harddisk1\\Partition0");
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

int FileSystem::RegisterDevice(const std::string& path, Device* device) {
  devices_.push_back(device);
  return 0;
}

int FileSystem::RegisterHostPathDevice(
    const std::string& path, const std::wstring& local_path) {
  Device* device = new HostPathDevice(path, local_path);
  return RegisterDevice(path, device);
}

int FileSystem::RegisterDiscImageDevice(
    const std::string& path, const std::wstring& local_path) {
  DiscImageDevice* device = new DiscImageDevice(path, local_path);
  if (device->Init()) {
    return 1;
  }
  return RegisterDevice(path, device);
}

int FileSystem::RegisterSTFSContainerDevice(
    const std::string& path, const std::wstring& local_path) {
  STFSContainerDevice* device = new STFSContainerDevice(path, local_path);
  if (device->Init()) {
    return 1;
  }
  return RegisterDevice(path, device);
}

int FileSystem::CreateSymbolicLink(const char* path, const char* target) {
  symlinks_.insert(std::pair<const char*, const char*>(
      xestrdupa(path),
      xestrdupa(target)));
  return 0;
}

int FileSystem::DeleteSymbolicLink(const char* path) {
  std::unordered_map<std::string, std::string>::iterator it =
      symlinks_.find(std::string(path));
  if (it != symlinks_.end()) {
    symlinks_.erase(it);
    return 0;
  }
  return 1;
}

Entry* FileSystem::ResolvePath(const char* path) {
  // Strip off prefix and pass to device.
  // e.g., d:\some\PATH.foo -> some\PATH.foo
  // Support both symlinks and device specifiers, like:
  // \\Device\Foo\some\PATH.foo, d:\some\PATH.foo, etc.

  // TODO(benvanik): normalize path/etc
  // e.g., remove ..'s and such

  // Resolve symlinks.
  // TODO(benvanik): more robust symlink handling - right now we assume simple
  //     drive path -> device mappings with nothing nested.
  char full_path[poly::max_path];
  XEIGNORE(xestrcpya(full_path, XECOUNT(full_path), path));
  for (std::unordered_map<std::string, std::string>::iterator it =
       symlinks_.begin(); it != symlinks_.end(); ++it) {
    if (xestrcasestra(path, it->first.c_str()) == path) {
      // Found symlink, fixup.
      const char* after_path = path + it->first.size();
      XEIGNORE(xesnprintfa(full_path, XECOUNT(full_path), "%s%s",
                           it->second.c_str(), after_path));
      break;
    }
  }

  // Scan all devices.
  for (std::vector<Device*>::iterator it = devices_.begin();
       it != devices_.end(); ++it) {
    Device* device = *it;
    if (xestrcasestra(full_path, device->path()) == full_path) {
      // Found!
      // Trim the device prefix off and pass down.
      char device_path[poly::max_path];
      XEIGNORE(xestrcpya(device_path, XECOUNT(device_path),
                         full_path + xestrlena(device->path())));
      return device->ResolvePath(device_path);
    }
  }

  XELOGE("ResolvePath(%s) failed - no root found", path);
  return NULL;
}

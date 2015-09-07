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
#include "xenia/kernel/xfile.h"

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
  auto global_lock = global_critical_region_.Acquire();
  devices_.emplace_back(std::move(device));
  return true;
}

bool VirtualFileSystem::RegisterSymbolicLink(std::string path,
                                             std::string target) {
  auto global_lock = global_critical_region_.Acquire();
  symlinks_.insert({path, target});
  return true;
}

bool VirtualFileSystem::UnregisterSymbolicLink(std::string path) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = symlinks_.find(path);
  if (it == symlinks_.end()) {
    return false;
  }
  symlinks_.erase(it);
  return true;
}

Entry* VirtualFileSystem::ResolvePath(std::string path) {
  auto global_lock = global_critical_region_.Acquire();

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
      return device->ResolvePath(relative_path);
    }
  }

  XELOGE("ResolvePath(%s) failed - device not found (%s)", path.c_str(),
         device_path.c_str());
  return nullptr;
}

Entry* VirtualFileSystem::ResolveBasePath(std::string path) {
  auto base_path = xe::find_base_path(path);
  return ResolvePath(base_path);
}

Entry* VirtualFileSystem::CreatePath(std::string path, uint32_t attributes) {
  // Create all required directories recursively.
  auto path_parts = xe::split_path(path);
  if (path_parts.empty()) {
    return nullptr;
  }
  auto partial_path = path_parts[0];
  auto partial_entry = ResolvePath(partial_path);
  if (!partial_entry) {
    return nullptr;
  }
  auto parent_entry = partial_entry;
  for (size_t i = 1; i < path_parts.size() - 1; ++i) {
    partial_path = xe::join_paths(partial_path, path_parts[i]);
    auto child_entry = ResolvePath(partial_path);
    if (!child_entry) {
      child_entry =
          parent_entry->CreateEntry(path_parts[i], kFileAttributeDirectory);
    }
    if (!child_entry) {
      return nullptr;
    }
    parent_entry = child_entry;
  }
  return parent_entry->CreateEntry(path_parts[path_parts.size() - 1],
                                   attributes);
}

bool VirtualFileSystem::DeletePath(std::string path) {
  auto entry = ResolvePath(path);
  if (!entry) {
    return false;
  }
  auto parent = entry->parent();
  if (!parent) {
    // Can't delete root.
    return false;
  }
  return parent->Delete(entry);
}

X_STATUS VirtualFileSystem::OpenFile(
    kernel::KernelState* kernel_state, std::string path,
    FileDisposition creation_disposition, uint32_t desired_access,
    kernel::object_ref<kernel::XFile>* out_file, FileAction* out_action) {
  // Cleanup access.
  if (desired_access & FileAccess::kGenericRead) {
    desired_access |= FileAccess::kFileReadData;
  }
  if (desired_access & FileAccess::kGenericWrite) {
    desired_access |= FileAccess::kFileWriteData;
  }
  if (desired_access & FileAccess::kGenericAll) {
    desired_access |= FileAccess::kFileReadData | FileAccess::kFileWriteData;
  }

  // Lookup host device/parent path.
  // If no device or parent, fail.
  Entry* parent_entry = nullptr;
  Entry* entry = nullptr;
  if (!xe::find_base_path(path).empty()) {
    parent_entry = ResolveBasePath(path);
    if (!parent_entry) {
      *out_action = FileAction::kDoesNotExist;
      return X_STATUS_NO_SUCH_FILE;
    }

    auto file_name = xe::find_name_from_path(path);
    entry = parent_entry->GetChild(file_name);
  } else {
    entry = ResolvePath(path);
  }

  // Check if exists (if we need it to), or that it doesn't (if it shouldn't).
  switch (creation_disposition) {
    case FileDisposition::kOpen:
    case FileDisposition::kOverwrite:
      // Must exist.
      if (!entry) {
        *out_action = FileAction::kDoesNotExist;
        return X_STATUS_NO_SUCH_FILE;
      }
      break;
    case FileDisposition::kCreate:
      // Must not exist.
      if (entry) {
        *out_action = FileAction::kExists;
        return X_STATUS_OBJECT_NAME_COLLISION;
      }
      break;
    default:
      // Either way, ok.
      break;
  }

  // Verify permissions.
  bool wants_write = desired_access & FileAccess::kFileWriteData ||
                     desired_access & FileAccess::kFileAppendData;
  if (wants_write && ((parent_entry && parent_entry->is_read_only()) ||
                      (entry && entry->is_read_only()))) {
    // Fail if read only device and wants write.
    // return X_STATUS_ACCESS_DENIED;
    // TODO(benvanik): figure out why games are opening read-only files with
    // write modes.
    assert_always();
    XELOGW("Attempted to open the file/dir for create/write");
    desired_access = FileAccess::kGenericRead | FileAccess::kFileReadData;
  }

  bool created = false;
  if (!entry) {
    // Remember that we are creating this new, instead of replacing.
    created = true;
    *out_action = FileAction::kCreated;
  } else {
    // May need to delete, if it exists.
    switch (creation_disposition) {
      case FileDisposition::kCreate:
        // Shouldn't be possible to hit this.
        assert_always();
        return X_STATUS_ACCESS_DENIED;
      case FileDisposition::kSuperscede:
        // Replace (by delete + recreate).
        if (!entry->Delete()) {
          return X_STATUS_ACCESS_DENIED;
        }
        entry = nullptr;
        *out_action = FileAction::kSuperseded;
        break;
      case FileDisposition::kOpen:
      case FileDisposition::kOpenIf:
        // Normal open.
        *out_action = FileAction::kOpened;
        break;
      case FileDisposition::kOverwrite:
      case FileDisposition::kOverwriteIf:
        // Overwrite (we do by delete + recreate).
        if (!entry->Delete()) {
          return X_STATUS_ACCESS_DENIED;
        }
        entry = nullptr;
        *out_action = FileAction::kOverwritten;
        break;
    }
  }
  if (!entry) {
    // Create if needed (either new or as a replacement).
    entry = CreatePath(path, kFileAttributeNormal);
    if (!entry) {
      return X_STATUS_ACCESS_DENIED;
    }
  }

  // Open.
  auto result = entry->Open(kernel_state, desired_access, out_file);
  if (XFAILED(result)) {
    *out_action = FileAction::kDoesNotExist;
  }
  return result;
}

}  // namespace vfs
}  // namespace xe

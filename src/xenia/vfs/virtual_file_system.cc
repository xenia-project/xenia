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
Entry* VirtualFileSystem::ResolveDevice(const std::string& devicepath) {
  auto global_lock = global_critical_region_.Acquire();

  // Resolve relative paths
  std::string normalized_path(xe::filesystem::CanonicalizePath(devicepath));

  // Find the device.
  auto it =
      std::find_if(devices_.cbegin(), devices_.cend(), [&](const auto& d) {
        return xe::find_first_of_case(normalized_path, d->mount_path()) == 0;
      });
  if (it == devices_.cend()) {
    XELOGE("ResolveDevice(%s) device not initialized", devicepath.c_str());
    return nullptr;
  }

  const auto& device = *it;
  auto relative_path = normalized_path.substr(device->mount_path().size());
  return device->ResolvePath(relative_path);
}

bool VirtualFileSystem::RegisterDevice(std::unique_ptr<Device> device) {
  auto global_lock = global_critical_region_.Acquire();
  devices_.emplace_back(std::move(device));
  return true;
}

bool VirtualFileSystem::UnregisterDevice(const std::string& path) {
  auto global_lock = global_critical_region_.Acquire();
  for (auto it = devices_.begin(); it != devices_.end(); ++it) {
    if ((*it)->mount_path() == path) {
      XELOGD("Unregistered device: %s", (*it)->mount_path().c_str());
      devices_.erase(it);
      return true;
    }
  }
  return false;
}

bool VirtualFileSystem::RegisterSymbolicLink(const std::string& path,
                                             const std::string& target) {
  auto global_lock = global_critical_region_.Acquire();
  symlinks_.insert({path, target});
  XELOGD("Registered symbolic link: %s => %s", path.c_str(), target.c_str());

  return true;
}

bool VirtualFileSystem::UnregisterSymbolicLink(const std::string& path) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = symlinks_.find(path);
  if (it == symlinks_.end()) {
    return false;
  }
  XELOGD("Unregistered symbolic link: %s => %s", it->first.c_str(),
         it->second.c_str());

  symlinks_.erase(it);
  return true;
}
bool VirtualFileSystem::IsSymbolicLink(const std::string& path) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = symlinks_.find(path);
  if (it == symlinks_.end()) {
    return false;
  }
  return true;
}

bool VirtualFileSystem::FindSymbolicLink(const std::string& path,
                                         std::string& target) {
  auto it =
      std::find_if(symlinks_.cbegin(), symlinks_.cend(), [&](const auto& s) {
        return xe::find_first_of_case(path, s.first) == 0;
      });
  if (it == symlinks_.cend()) {
    return false;
  }
  target = (*it).second;
  return true;
}

bool VirtualFileSystem::ResolveSymbolicLink(const std::string& path,
                                            std::string& result) {
  result = path;
  bool was_resolved = false;
  while (true) {
    auto it =
        std::find_if(symlinks_.cbegin(), symlinks_.cend(), [&](const auto& s) {
          return xe::find_first_of_case(result, s.first) == 0;
        });
    if (it == symlinks_.cend()) {
      break;
    }
    // Found symlink!
    auto target_path = (*it).second;
    auto relative_path = result.substr((*it).first.size());
    result = target_path + relative_path;
    was_resolved = true;
  }
  return was_resolved;
}

Entry* VirtualFileSystem::ResolvePath(const std::string& path) {
  auto global_lock = global_critical_region_.Acquire();

  // Resolve relative paths
  std::string normalized_path(xe::filesystem::CanonicalizePath(path));

  // Resolve symlinks.
  std::string resolved_path;
  if (ResolveSymbolicLink(normalized_path, resolved_path)) {
    normalized_path = resolved_path;
  }

  // Find the device.
  auto it =
      std::find_if(devices_.cbegin(), devices_.cend(), [&](const auto& d) {
        return xe::find_first_of_case(normalized_path, d->mount_path()) == 0;
      });
  if (it == devices_.cend()) {
    XELOGE("ResolvePath(%s) failed - device not found", path.c_str());
    return nullptr;
  }

  const auto& device = *it;
  auto relative_path = normalized_path.substr(device->mount_path().size());
  return device->ResolvePath(relative_path);
}

Entry* VirtualFileSystem::ResolveBasePath(const std::string& path) {
  auto base_path = xe::find_base_path(path);
  return ResolvePath(base_path);
}

Entry* VirtualFileSystem::CreatePath(const std::string& path,
                                     uint32_t attributes) {
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

bool VirtualFileSystem::DeletePath(const std::string& path) {
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

X_STATUS VirtualFileSystem::OpenFile(const std::string& path,
                                     FileDisposition creation_disposition,
                                     uint32_t desired_access, bool is_directory,
                                     File** out_file, FileAction* out_action) {
  // TODO(gibbed): should 'is_directory' remain as a bool or should it be
  // flipped to a generic FileAttributeFlags?

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
    entry = CreatePath(
        path, !is_directory ? kFileAttributeNormal : kFileAttributeDirectory);
    if (!entry) {
      return X_STATUS_ACCESS_DENIED;
    }
  }

  // Open.
  auto result = entry->Open(desired_access, out_file);
  if (XFAILED(result)) {
    *out_action = FileAction::kDoesNotExist;
  }
  return result;
}

}  // namespace vfs
}  // namespace xe

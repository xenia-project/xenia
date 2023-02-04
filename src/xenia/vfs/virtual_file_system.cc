/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/virtual_file_system.h"
#include "xenia/kernel/xam/content_manager.h"
#include "xenia/vfs/devices/stfs_container_device.h"

#include "devices/host_path_entry.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/kernel/xfile.h"

namespace xe {
namespace vfs {

using namespace xe::literals;

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

bool VirtualFileSystem::UnregisterDevice(const std::string_view path) {
  auto global_lock = global_critical_region_.Acquire();
  for (auto it = devices_.begin(); it != devices_.end(); ++it) {
    if ((*it)->mount_path() == path) {
      XELOGD("Unregistered device: {}", (*it)->mount_path());
      devices_.erase(it);
      return true;
    }
  }
  return false;
}

bool VirtualFileSystem::RegisterSymbolicLink(const std::string_view path,
                                             const std::string_view target) {
  auto global_lock = global_critical_region_.Acquire();
  symlinks_.insert({std::string(path), std::string(target)});
  XELOGD("Registered symbolic link: {} => {}", path, target);

  return true;
}

bool VirtualFileSystem::UnregisterSymbolicLink(const std::string_view path) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = std::find_if(
      symlinks_.cbegin(), symlinks_.cend(),
      [&](const auto& s) { return xe::utf8::equal_case(path, s.first); });
  if (it == symlinks_.end()) {
    return false;
  }
  XELOGD("Unregistered symbolic link: {} => {}", it->first, it->second);

  symlinks_.erase(it);
  return true;
}

bool VirtualFileSystem::FindSymbolicLink(const std::string_view path,
                                         std::string& target) {
  auto it = std::find_if(
      symlinks_.cbegin(), symlinks_.cend(),
      [&](const auto& s) { return xe::utf8::starts_with_case(path, s.first); });
  if (it == symlinks_.cend()) {
    return false;
  }
  target = (*it).second;
  return true;
}

bool VirtualFileSystem::ResolveSymbolicLink(const std::string_view path,
                                            std::string& result) {
  result = path;
  bool was_resolved = false;
  while (true) {
    auto it =
        std::find_if(symlinks_.cbegin(), symlinks_.cend(), [&](const auto& s) {
          return xe::utf8::starts_with_case(result, s.first);
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

Entry* VirtualFileSystem::ResolvePath(const std::string_view path) {
  auto global_lock = global_critical_region_.Acquire();

  // Resolve relative paths
  auto normalized_path(xe::utf8::canonicalize_guest_path(path));

  // Resolve symlinks.
  std::string resolved_path;
  if (ResolveSymbolicLink(normalized_path, resolved_path)) {
    normalized_path = resolved_path;
  }

  // Find the device.
  auto it =
      std::find_if(devices_.cbegin(), devices_.cend(), [&](const auto& d) {
        return xe::utf8::starts_with(normalized_path, d->mount_path());
      });
  if (it == devices_.cend()) {
    // Supress logging the error for ShaderDumpxe:\CompareBackEnds as this is
    // not an actual problem nor something we care about.
    if (path != "ShaderDumpxe:\\CompareBackEnds") {
      XELOGE("ResolvePath({}) failed - device not found", path);
    }
    return nullptr;
  }

  const auto& device = *it;
  auto relative_path = normalized_path.substr(device->mount_path().size());
  return device->ResolvePath(relative_path);
}

Entry* VirtualFileSystem::CreatePath(const std::string_view path,
                                     uint32_t attributes) {
  // Create all required directories recursively.
  auto path_parts = xe::utf8::split_path(path);
  if (path_parts.empty()) {
    return nullptr;
  }
  auto partial_path = std::string(path_parts[0]);
  auto partial_entry = ResolvePath(partial_path);
  if (!partial_entry) {
    return nullptr;
  }
  auto parent_entry = partial_entry;
  for (size_t i = 1; i < path_parts.size() - 1; ++i) {
    partial_path = xe::utf8::join_guest_paths(partial_path, path_parts[i]);
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

bool VirtualFileSystem::DeletePath(const std::string_view path) {
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

X_STATUS VirtualFileSystem::OpenFile(Entry* root_entry,
                                     const std::string_view path,
                                     FileDisposition creation_disposition,
                                     uint32_t desired_access, bool is_directory,
                                     bool is_non_directory, File** out_file,
                                     FileAction* out_action) {
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

  auto base_path = xe::utf8::find_base_guest_path(path);
  if (!base_path.empty()) {
    parent_entry = !root_entry ? ResolvePath(base_path)
                               : root_entry->ResolvePath(base_path);
    if (!parent_entry) {
      *out_action = FileAction::kDoesNotExist;
      return X_STATUS_NO_SUCH_FILE;
    }

    auto file_name = xe::utf8::find_name_from_guest_path(path);
    entry = parent_entry->GetChild(file_name);
  } else {
    entry = !root_entry ? ResolvePath(path) : root_entry->GetChild(path);
  }

  if (entry) {
    if (entry->attributes() & kFileAttributeDirectory && is_non_directory) {
      return X_STATUS_FILE_IS_A_DIRECTORY;
    }

    // If the entry does not exist on the host then remove the cached entry
    if (parent_entry) {
      const xe::vfs::HostPathEntry* host_Path =
          dynamic_cast<const xe::vfs::HostPathEntry*>(parent_entry);

      if (host_Path) {
        auto const file_path = host_Path->host_path() / entry->name();

        if (!std::filesystem::exists(file_path)) {
          // Remove cached entry
          entry->Delete();
          entry = nullptr;
        }
      }
    }
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

X_STATUS VirtualFileSystem::ExtractContentFiles(
    Device* device, std::filesystem::path base_path) {
  // Run through all the files, breadth-first style.
  std::queue<vfs::Entry*> queue;
  auto root = device->ResolvePath("/");
  queue.push(root);

  // Allocate a buffer when needed.
  size_t buffer_size = 0;
  uint8_t* buffer = nullptr;

  while (!queue.empty()) {
    auto entry = queue.front();
    queue.pop();
    for (auto& entry : entry->children()) {
      queue.push(entry.get());
    }

    XELOGI("Extracting file: {}", entry->path());
    auto dest_name = base_path / xe::to_path(entry->path());
    if (entry->attributes() & kFileAttributeDirectory) {
      std::error_code error_code;
      std::filesystem::create_directories(dest_name, error_code);
      if (error_code) {
        return error_code.value();
      }
      continue;
    }

    vfs::File* in_file = nullptr;
    if (entry->Open(FileAccess::kFileReadData, &in_file) != X_STATUS_SUCCESS) {
      continue;
    }

    auto file = xe::filesystem::OpenFile(dest_name, "wb");
    if (!file) {
      in_file->Destroy();
      continue;
    }

    if (entry->can_map()) {
      auto map = entry->OpenMapped(xe::MappedMemory::Mode::kRead);
      fwrite(map->data(), map->size(), 1, file);
      map->Close();
    } else {
      // Can't map the file into memory. Read it into a temporary buffer.
      if (!buffer || entry->size() > buffer_size) {
        // Resize the buffer.
        if (buffer) {
          delete[] buffer;
        }

        // Allocate a buffer rounded up to the nearest 512MB.
        buffer_size = xe::round_up(entry->size(), 512_MiB);
        buffer = new uint8_t[buffer_size];
      }

      size_t bytes_read = 0;
      in_file->ReadSync(buffer, entry->size(), 0, &bytes_read);
      fwrite(buffer, bytes_read, 1, file);
    }

    fclose(file);
    in_file->Destroy();
  }

  if (buffer) {
    delete[] buffer;
  }

  return X_STATUS_SUCCESS;
}

void VirtualFileSystem::ExtractContentHeader(Device* device,
                                             std::filesystem::path base_path) {
  auto stfs_device = ((StfsContainerDevice*)device);

  if (!std::filesystem::exists(base_path.parent_path())) {
    if (!std::filesystem::create_directories(base_path.parent_path())) {
      return;
    }
  }
  auto header_filename = base_path.filename().string() + ".header";
  auto header_path = base_path.parent_path() / header_filename;
  xe::filesystem::CreateEmptyFile(header_path);

  if (std::filesystem::exists(header_path)) {
    auto file = xe::filesystem::OpenFile(header_path, "wb");
    kernel::xam::XCONTENT_AGGREGATE_DATA data = stfs_device->content_header();
    data.set_file_name(base_path.filename().string());
    fwrite(&data, 1, sizeof(kernel::xam::XCONTENT_AGGREGATE_DATA), file);
    fclose(file);
  }
  return;
}
}  // namespace vfs
}  // namespace xe

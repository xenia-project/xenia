/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_zarchive_device.h"

#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/vfs/devices/disc_zarchive_entry.h"

#include "third_party/zarchive/include/zarchive/zarchivereader.h"

namespace xe {
namespace vfs {

DiscZarchiveDevice::DiscZarchiveDevice(const std::string_view mount_path,
                                       const std::filesystem::path& host_path)
    : Device(mount_path), name_("GDFX"), host_path_(host_path), reader_() {}

DiscZarchiveDevice::~DiscZarchiveDevice(){};

bool DiscZarchiveDevice::Initialize() {
  reader_ =
      std::unique_ptr<ZArchiveReader>(ZArchiveReader::OpenFromFile(host_path_));

  if (!reader_) {
    XELOGE("Disc ZArchive could not be opened");
    return false;
  }

  const std::string root_path = std::string("/");
  const ZArchiveNodeHandle handle = reader_->LookUp(root_path);
  auto root_entry = new DiscZarchiveEntry(this, nullptr, root_path);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry->handle_ = static_cast<uint32_t>(handle);
  root_entry->name_ = root_path;
  root_entry->absolute_path_ = root_path;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  return ReadAllEntries("", root_entry, nullptr);
}

void DiscZarchiveDevice::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

Entry* DiscZarchiveDevice::ResolvePath(const std::string_view path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo
  XELOGFS("DiscZarchiveDevice::ResolvePath({})", path);

  if (!reader_) {
    return nullptr;
  }

  const ZArchiveNodeHandle handle = reader_->LookUp(path);
  if (handle == ZARCHIVE_INVALID_NODE) {
    return nullptr;
  }

  return root_entry_->ResolvePath(path);
}

bool DiscZarchiveDevice::ReadAllEntries(const std::string& path,
                                        DiscZarchiveEntry* node,
                                        DiscZarchiveEntry* parent) {
  const ZArchiveNodeHandle handle = node->handle_;

  if (handle == ZARCHIVE_INVALID_NODE) {
    return false;
  }

  if (reader_->IsFile(handle)) {
    auto entry = new DiscZarchiveEntry(this, parent, path);
    entry->attributes_ = kFileAttributeReadOnly;
    entry->handle_ = static_cast<uint32_t>(handle);
    entry->parent_ = parent;
    entry->children_.push_back(std::unique_ptr<Entry>(entry));
    return true;
  }

  if (reader_->IsDirectory(handle)) {
    const uint32_t count = reader_->GetDirEntryCount(handle);

    for (uint32_t i = 0; i < count; i++) {
      ZArchiveReader::DirEntry dirEntry;
      if (!reader_->GetDirEntry(handle, i, dirEntry)) {
        XELOGE("Invalid ZArchive directory! Skipping loading");
        return false;
      }

      const std::string full_path = path + std::string(dirEntry.name);
      const ZArchiveNodeHandle fileHandle = reader_->LookUp(full_path);
      if (fileHandle == ZARCHIVE_INVALID_NODE) {
        return false;
      }

      auto entry = new DiscZarchiveEntry(this, parent, full_path);
      entry->handle_ = static_cast<uint32_t>(fileHandle);
      entry->data_offset_ = 0;

      // Set to January 1, 1970 (UTC) in 100-nanosecond intervals
      entry->create_timestamp_ = 10000 * 11644473600000LL;
      entry->access_timestamp_ = 10000 * 11644473600000LL;
      entry->write_timestamp_ = 10000 * 11644473600000LL;
      entry->parent_ = node;

      if (dirEntry.isDirectory) {
        entry->data_size_ = 0;
        entry->size_ = dirEntry.size;
        entry->attributes_ = kFileAttributeDirectory | kFileAttributeReadOnly;
        node->children_.push_back(std::unique_ptr<Entry>(entry));
        if (!ReadAllEntries(full_path + "\\", entry, node)) {
          return false;
        }
      } else if (dirEntry.isFile) {
        entry->data_size_ = entry->size_ = reader_->GetFileSize(fileHandle);
        entry->attributes_ = kFileAttributeReadOnly;
        entry->allocation_size_ =
            xe::round_up(entry->size_, bytes_per_sector());
        node->children_.push_back(std::unique_ptr<Entry>(entry));
      }
    }
    return true;
  }

  return false;
}

}  // namespace vfs
}  // namespace xe

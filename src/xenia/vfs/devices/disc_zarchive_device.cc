/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
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

using namespace xe::literals;

const size_t kXESectorSize = 2_KiB;

DiscZarchiveDevice::DiscZarchiveDevice(const std::string_view mount_path,
                                       const std::filesystem::path& host_path)
    : Device(mount_path),
      name_("GDFX"),
      host_path_(host_path),
      opaque_(nullptr) {}

DiscZarchiveDevice::~DiscZarchiveDevice() {
  ZArchiveReader* reader = static_cast<ZArchiveReader*>(opaque_);
  if (reader != nullptr) delete reader;
};

bool DiscZarchiveDevice::Initialize() {
  ZArchiveReader* reader = nullptr;
  reader = ZArchiveReader::OpenFromFile(host_path_);

  if (!reader) {
    XELOGE("Disc ZArchive could not be opened");
    return false;
  }

  opaque_ = static_cast<void*>(reader);
  bool result = false;

  result = reader->IsFile(reader->LookUp("default.xex", true, false));
  if (!result) XELOGE("Failed to verify disc ZArchive (no default.xex)");

  const std::string root_path = std::string("/");
  ZArchiveNodeHandle handle = reader->LookUp(root_path);
  auto root_entry = new DiscZarchiveEntry(this, nullptr, root_path, reader);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry->handle_ = static_cast<uint32_t>(handle);
  root_entry->name_ = root_path;
  root_entry->absolute_path_ = root_path;
  root_entry_ = std::unique_ptr<Entry>(root_entry);
  result = ReadAllEntries(reader, "", root_entry, nullptr);

  return result;
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

  ZArchiveReader* reader = static_cast<ZArchiveReader*>(opaque_);
  if (!reader) return nullptr;

  ZArchiveNodeHandle handle = reader->LookUp(path);
  bool result = (handle != ZARCHIVE_INVALID_NODE);

  if (!result) return nullptr;

  return root_entry_->ResolvePath(path);
}

bool DiscZarchiveDevice::ReadAllEntries(void* opaque, const std::string& path,
                                        DiscZarchiveEntry* node,
                                        DiscZarchiveEntry* parent) {
  ZArchiveReader* reader = static_cast<ZArchiveReader*>(opaque);
  ZArchiveNodeHandle handle = node->handle_;
  if (handle == ZARCHIVE_INVALID_NODE) return false;
  if (reader->IsDirectory(handle)) {
    uint32_t count = reader->GetDirEntryCount(handle);
    for (uint32_t i = 0; i < count; i++) {
      ZArchiveReader::DirEntry dirEntry;
      if (!reader->GetDirEntry(handle, i, dirEntry)) return false;
      std::string full_path = path + std::string(dirEntry.name);
      ZArchiveNodeHandle fileHandle = reader->LookUp(full_path);
      if (handle == ZARCHIVE_INVALID_NODE) return false;
      auto entry = new DiscZarchiveEntry(this, parent, full_path, opaque);
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
        if (!ReadAllEntries(reader, full_path + "\\", entry, node))
          return false;
      } else if (dirEntry.isFile) {
        entry->data_size_ = entry->size_ = reader->GetFileSize(fileHandle);
        entry->attributes_ = kFileAttributeReadOnly;
        entry->allocation_size_ =
            xe::round_up(entry->size_, bytes_per_sector());
        node->children_.push_back(std::unique_ptr<Entry>(entry));
      }
    }
    return true;
  } else if (reader->IsFile(handle)) {
    auto entry = new DiscZarchiveEntry(this, parent, path, opaque);
    entry->attributes_ = kFileAttributeReadOnly;
    entry->handle_ = static_cast<uint32_t>(handle);
    entry->parent_ = parent;
    entry->children_.push_back(std::unique_ptr<Entry>(entry));
    return true;
  }

  return false;
}

}  // namespace vfs
}  // namespace xe

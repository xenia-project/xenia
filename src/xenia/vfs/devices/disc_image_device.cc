/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_image_device.h"

#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/vfs/devices/disc_image_entry.h"

namespace xe {
namespace vfs {

using namespace xe::literals;

const size_t kXESectorSize = 2_KiB;

DiscImageDevice::DiscImageDevice(const std::string_view mount_path,
                                 const std::filesystem::path& host_path)
    : Device(mount_path), name_("GDFX"), host_path_(host_path) {}

DiscImageDevice::~DiscImageDevice() = default;

bool DiscImageDevice::Initialize() {
  mmap_ = MappedMemory::Open(host_path_, MappedMemory::Mode::kRead);
  if (!mmap_) {
    XELOGE("Disc image could not be mapped");
    return false;
  } else {
    XELOGFS("DiscImageDevice::Initialize");
  }

  ParseState state = {0};
  state.ptr = mmap_->data();
  state.size = mmap_->size();
  auto result = Verify(&state);
  if (result != Error::kSuccess) {
    XELOGE("Failed to verify disc image header: {}",
           static_cast<int32_t>(result));
    return false;
  }

  result = ReadAllEntries(&state, state.ptr + state.root_offset);
  if (result != Error::kSuccess) {
    XELOGE("Failed to read all GDFX entries: {}", static_cast<int32_t>(result));
    return false;
  }

  return true;
}

void DiscImageDevice::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

Entry* DiscImageDevice::ResolvePath(const std::string_view path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo
  XELOGFS("DiscImageDevice::ResolvePath({})", path);
  return root_entry_->ResolvePath(path);
}

DiscImageDevice::Error DiscImageDevice::Verify(ParseState* state) {
  // Find sector 32 of the game partition - try at a few points.
  static const size_t likely_offsets[] = {
      0x00000000, 0x0000FB20, 0x00020600, 0x02080000, 0x0FD90000,
  };
  bool magic_found = false;
  for (size_t n = 0; n < xe::countof(likely_offsets); n++) {
    state->game_offset = likely_offsets[n];
    if (VerifyMagic(state, state->game_offset + (32 * kXESectorSize))) {
      magic_found = true;
      break;
    }
  }
  if (!magic_found) {
    // File doesn't have the magic values - likely not a real GDFX source.
    return Error::kErrorFileMismatch;
  }

  // Read sector 32 to get FS state.
  if (state->size < state->game_offset + (32 * kXESectorSize)) {
    return Error::kErrorReadError;
  }
  uint8_t* fs_ptr = state->ptr + state->game_offset + (32 * kXESectorSize);
  state->root_sector = xe::load<uint32_t>(fs_ptr + 20);
  state->root_size = xe::load<uint32_t>(fs_ptr + 24);
  state->root_offset =
      state->game_offset + (state->root_sector * kXESectorSize);
  if (state->root_size < 13 || state->root_size > 32_MiB) {
    return Error::kErrorDamagedFile;
  }

  return Error::kSuccess;
}

bool DiscImageDevice::VerifyMagic(ParseState* state, size_t offset) {
  if (offset >= state->size) {
    return false;
  }

  // Simple check to see if the given offset contains the magic value.
  return std::memcmp(state->ptr + offset, "MICROSOFT*XBOX*MEDIA", 20) == 0;
}

DiscImageDevice::Error DiscImageDevice::ReadAllEntries(
    ParseState* state, const uint8_t* root_buffer) {
  auto root_entry = new DiscImageEntry(this, nullptr, "", mmap_.get());
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  if (!ReadEntry(state, root_buffer, 0, root_entry)) {
    return Error::kErrorOutOfMemory;
  }

  return Error::kSuccess;
}

bool DiscImageDevice::ReadEntry(ParseState* state, const uint8_t* buffer,
                                uint16_t entry_ordinal,
                                DiscImageEntry* parent) {
  const uint8_t* p = buffer + (entry_ordinal * 4);

  uint16_t node_l = xe::load<uint16_t>(p + 0);
  uint16_t node_r = xe::load<uint16_t>(p + 2);
  size_t sector = xe::load<uint32_t>(p + 4);
  size_t length = xe::load<uint32_t>(p + 8);
  uint8_t attributes = xe::load<uint8_t>(p + 12);
  uint8_t name_length = xe::load<uint8_t>(p + 13);
  auto name_buffer = reinterpret_cast<const char*>(p + 14);

  if (node_l && !ReadEntry(state, buffer, node_l, parent)) {
    return false;
  }

  // Filename is stored as Windows-1252, convert it to UTF-8.
  auto ansi_name = std::string(name_buffer, name_length);
  auto name = xe::win1252_to_utf8(ansi_name);

  auto entry = DiscImageEntry::Create(this, parent, name, mmap_.get());
  entry->attributes_ = attributes | kFileAttributeReadOnly;
  entry->size_ = length;
  entry->allocation_size_ = xe::round_up(length, bytes_per_sector());

  // Set to January 1, 1970 (UTC) in 100-nanosecond intervals
  entry->create_timestamp_ = 10000 * 11644473600000LL;
  entry->access_timestamp_ = 10000 * 11644473600000LL;
  entry->write_timestamp_ = 10000 * 11644473600000LL;

  if (attributes & kFileAttributeDirectory) {
    // Folder.
    entry->data_offset_ = 0;
    entry->data_size_ = 0;
    if (length) {
      // Not a leaf - read in children.
      if (state->size < state->game_offset + (sector * kXESectorSize)) {
        // Out of bounds read.
        return false;
      }
      // Read child list.
      uint8_t* folder_ptr =
          state->ptr + state->game_offset + (sector * kXESectorSize);
      if (!ReadEntry(state, folder_ptr, 0, entry.get())) {
        return false;
      }
    }
  } else {
    // File.
    entry->data_offset_ = state->game_offset + (sector * kXESectorSize);
    entry->data_size_ = length;
  }

  // Add to parent.
  parent->children_.emplace_back(std::move(entry));

  // Read next file in the list.
  if (node_r && !ReadEntry(state, buffer, node_r, parent)) {
    return false;
  }

  return true;
}

}  // namespace vfs
}  // namespace xe

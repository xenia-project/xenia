/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/stfs_container_device.h"

#include <algorithm>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/vfs/devices/stfs_container_entry.h"

namespace xe {
namespace vfs {

uint32_t load_uint24_be(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 16) |
         (static_cast<uint32_t>(p[1]) << 8) | static_cast<uint32_t>(p[2]);
}
uint32_t load_uint24_le(const uint8_t* p) {
  return (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[1]) << 8) | static_cast<uint32_t>(p[0]);
}

StfsContainerDevice::StfsContainerDevice(const std::string& mount_path,
                                         const std::wstring& local_path)
    : Device(mount_path), local_path_(local_path) {}

StfsContainerDevice::~StfsContainerDevice() = default;

bool StfsContainerDevice::Initialize() {
  if (filesystem::IsFolder(local_path_)) {
    // Was given a folder. Try to find the file in
    // local_path\TITLE_ID\000D0000\HASH_OF_42_CHARS
    // We take care to not die if there are additional files around.
    bool found_alternative = false;
    auto files = filesystem::ListFiles(local_path_);
    for (auto& file : files) {
      if (file.type != filesystem::FileInfo::Type::kDirectory ||
          file.name.size() != 8) {
        continue;
      }
      auto child_path = xe::join_paths(local_path_, file.name);
      auto child_files = filesystem::ListFiles(child_path);
      for (auto& child_file : child_files) {
        if (child_file.type != filesystem::FileInfo::Type::kDirectory ||
            child_file.name != L"000D0000") {
          continue;
        }
        auto stfs_path = xe::join_paths(child_path, child_file.name);
        auto stfs_files = filesystem::ListFiles(stfs_path);
        for (auto& stfs_file : stfs_files) {
          if (stfs_file.type != filesystem::FileInfo::Type::kFile ||
              stfs_file.name.size() != 42) {
            continue;
          }
          // Probably it!
          local_path_ = xe::join_paths(stfs_path, stfs_file.name);
          found_alternative = true;
          break;
        }
        if (found_alternative) {
          break;
        }
      }
      if (found_alternative) {
        break;
      }
    }
  }
  if (!filesystem::PathExists(local_path_)) {
    XELOGE("STFS container does not exist");
    return false;
  }

  mmap_ = MappedMemory::Open(local_path_, MappedMemory::Mode::kRead);
  if (!mmap_) {
    XELOGE("STFS container could not be mapped");
    return false;
  }

  uint8_t* map_ptr = mmap_->data();

  auto result = ReadHeaderAndVerify(map_ptr);
  if (result != Error::kSuccess) {
    XELOGE("STFS header read/verification failed: %d", result);
    return false;
  }

  result = ReadAllEntries(map_ptr);
  if (result != Error::kSuccess) {
    XELOGE("STFS entry reading failed: %d", result);
    return false;
  }

  return true;
}

StfsContainerDevice::Error StfsContainerDevice::ReadHeaderAndVerify(
    const uint8_t* map_ptr) {
  // Check signature.
  if (memcmp(map_ptr, "LIVE", 4) == 0) {
    package_type_ = StfsPackageType::kLive;
  } else if (memcmp(map_ptr, "PIRS", 4) == 0) {
    package_type_ = StfsPackageType::kPirs;
  } else if (memcmp(map_ptr, "CON", 3) == 0) {
    package_type_ = StfsPackageType::kCon;
  } else {
    // Unexpected format.
    return Error::kErrorFileMismatch;
  }

  // Read header.
  if (!header_.Read(map_ptr)) {
    return Error::kErrorDamagedFile;
  }

  if (((header_.header_size + 0x0FFF) & 0xB000) == 0xB000) {
    table_size_shift_ = 0;
  } else {
    table_size_shift_ = 1;
  }

  return Error::kSuccess;
}

StfsContainerDevice::Error StfsContainerDevice::ReadAllEntries(
    const uint8_t* map_ptr) {
  auto root_entry = new StfsContainerEntry(this, nullptr, "", mmap_.get());
  root_entry->attributes_ = kFileAttributeDirectory;

  root_entry_ = std::unique_ptr<Entry>(root_entry);

  std::vector<StfsContainerEntry*> all_entries;

  // Load all listings.
  auto& volume_descriptor = header_.volume_descriptor;
  uint32_t table_block_index = volume_descriptor.file_table_block_number;
  for (size_t n = 0; n < volume_descriptor.file_table_block_count; n++) {
    const uint8_t* p =
        map_ptr + BlockToOffset(ComputeBlockNumber(table_block_index));
    for (size_t m = 0; m < 0x1000 / 0x40; m++) {
      const uint8_t* filename = p;  // 0x28b
      if (filename[0] == 0) {
        // Done.
        break;
      }
      uint8_t filename_length_flags = xe::load_and_swap<uint8_t>(p + 0x28);
      // TODO(benvanik): use for allocation_size_?
      // uint32_t allocated_block_count = load_uint24_le(p + 0x29);
      uint32_t start_block_index = load_uint24_le(p + 0x2F);
      uint16_t path_indicator = xe::load_and_swap<uint16_t>(p + 0x32);
      uint32_t file_size = xe::load_and_swap<uint32_t>(p + 0x34);
      uint32_t update_timestamp = xe::load_and_swap<uint32_t>(p + 0x38);
      uint32_t access_timestamp = xe::load_and_swap<uint32_t>(p + 0x3C);
      p += 0x40;

      StfsContainerEntry* parent_entry = nullptr;
      if (path_indicator == 0xFFFF) {
        parent_entry = root_entry;
      } else {
        parent_entry = all_entries[path_indicator];
      }

      auto entry = new StfsContainerEntry(
          this, parent_entry,
          std::string(reinterpret_cast<const char*>(filename),
                      filename_length_flags & 0x3F),
          mmap_.get());
      // bit 0x40 = consecutive blocks (not fragmented?)
      if (filename_length_flags & 0x80) {
        entry->attributes_ = kFileAttributeDirectory;
      } else {
        entry->attributes_ = kFileAttributeNormal | kFileAttributeReadOnly;
        entry->data_offset_ =
            BlockToOffset(ComputeBlockNumber(start_block_index));
        entry->data_size_ = file_size;
      }
      entry->size_ = file_size;
      entry->allocation_size_ = xe::round_up(file_size, bytes_per_sector());
      entry->create_timestamp_ = update_timestamp;
      entry->access_timestamp_ = access_timestamp;
      entry->write_timestamp_ = update_timestamp;
      all_entries.push_back(entry);

      // Fill in all block records.
      // It's easier to do this now and just look them up later, at the cost
      // of some memory. Nasty chain walk.
      // TODO(benvanik): optimize if flag 0x40 (consecutive) is set.
      if (entry->attributes() & X_FILE_ATTRIBUTE_NORMAL) {
        uint32_t block_index = start_block_index;
        size_t remaining_size = file_size;
        uint32_t info = 0x80;
        while (remaining_size && block_index && info >= 0x80) {
          size_t block_size =
              std::min(static_cast<size_t>(0x1000), remaining_size);
          size_t offset = BlockToOffset(ComputeBlockNumber(block_index));
          entry->block_list_.push_back({offset, block_size});
          remaining_size -= block_size;
          auto block_hash = GetBlockHash(map_ptr, block_index, 0);
          if (table_size_shift_ && block_hash.info < 0x80) {
            block_hash = GetBlockHash(map_ptr, block_index, 1);
          }
          block_index = block_hash.next_block_index;
          info = block_hash.info;
        }
      }

      parent_entry->children_.emplace_back(std::unique_ptr<Entry>(entry));
    }

    auto block_hash = GetBlockHash(map_ptr, table_block_index, 0);
    if (table_size_shift_ && block_hash.info < 0x80) {
      block_hash = GetBlockHash(map_ptr, table_block_index, 1);
    }
    table_block_index = block_hash.next_block_index;
    if (table_block_index == 0xFFFFFF) {
      break;
    }
  }

  return Error::kSuccess;
}

size_t StfsContainerDevice::BlockToOffset(uint32_t block) {
  if (block >= 0xFFFFFF) {
    return ~0ull;
  } else {
    return ((header_.header_size + 0x0FFF) & 0xF000) + (block << 12);
  }
}

uint32_t StfsContainerDevice::ComputeBlockNumber(uint32_t block_index) {
  uint32_t block_shift = 0;
  if (((header_.header_size + 0x0FFF) & 0xB000) == 0xB000) {
    block_shift = 1;
  } else {
    if ((header_.volume_descriptor.block_separation & 0x1) == 0x1) {
      block_shift = 0;
    } else {
      block_shift = 1;
    }
  }

  uint32_t base = (block_index + 0xAA) / 0xAA;
  if (package_type_ == StfsPackageType::kCon) {
    base <<= block_shift;
  }
  uint32_t block = base + block_index;
  if (block_index >= 0xAA) {
    base = (block_index + 0x70E4) / 0x70E4;
    if (package_type_ == StfsPackageType::kCon) {
      base <<= block_shift;
    }
    block += base;
    if (block_index >= 0x70E4) {
      base = (block_index + 0x4AF768) / 0x4AF768;
      if (package_type_ == StfsPackageType::kCon) {
        base <<= block_shift;
      }
      block += base;
    }
  }
  return block;
}

StfsContainerDevice::BlockHash StfsContainerDevice::GetBlockHash(
    const uint8_t* map_ptr, uint32_t block_index, uint32_t table_offset) {
  static const uint32_t table_spacing[] = {
      0xAB,    0x718F,
      0xFE7DA,  // The distance in blocks between tables
      0xAC,    0x723A,
      0xFD00B,  // For when tables are 1 block and when they are 2 blocks
  };
  uint32_t record = block_index % 0xAA;
  uint32_t table_index =
      (block_index / 0xAA) * table_spacing[table_size_shift_ * 3 + 0];
  if (block_index >= 0xAA) {
    table_index += ((block_index / 0x70E4) + 1) << table_size_shift_;
    if (block_index >= 0x70E4) {
      table_index += 1 << table_size_shift_;
    }
  }
  // table_index += table_offset - (1 << table_size_shift_);
  const uint8_t* hash_data = map_ptr + BlockToOffset(table_index);
  const uint8_t* record_data = hash_data + record * 0x18;
  uint32_t info = xe::load_and_swap<uint8_t>(record_data + 0x14);
  uint32_t next_block_index = load_uint24_be(record_data + 0x15);
  return {next_block_index, info};
}

bool StfsVolumeDescriptor::Read(const uint8_t* p) {
  descriptor_size = xe::load_and_swap<uint8_t>(p + 0x00);
  if (descriptor_size != 0x24) {
    XELOGE("STFS volume descriptor size mismatch, expected 0x24 but got 0x%X",
           descriptor_size);
    return false;
  }
  reserved = xe::load_and_swap<uint8_t>(p + 0x01);
  block_separation = xe::load_and_swap<uint8_t>(p + 0x02);
  file_table_block_count = xe::load_and_swap<uint16_t>(p + 0x03);
  file_table_block_number = load_uint24_be(p + 0x05);
  std::memcpy(top_hash_table_hash, p + 0x08, 0x14);
  total_allocated_block_count = xe::load_and_swap<uint32_t>(p + 0x1C);
  total_unallocated_block_count = xe::load_and_swap<uint32_t>(p + 0x20);
  return true;
}

bool StfsHeader::Read(const uint8_t* p) {
  std::memcpy(license_entries, p + 0x22C, 0x100);
  std::memcpy(header_hash, p + 0x32C, 0x14);
  header_size = xe::load_and_swap<uint32_t>(p + 0x340);
  content_type = (StfsContentType)xe::load_and_swap<uint32_t>(p + 0x344);
  metadata_version = xe::load_and_swap<uint32_t>(p + 0x348);
  if (metadata_version > 1) {
    // This is a variant of thumbnail data/etc.
    // Can just ignore it for now (until we parse thumbnails).
    XELOGW("STFSContainer doesn't support version %d yet", metadata_version);
  }
  content_size = xe::load_and_swap<uint32_t>(p + 0x34C);
  media_id = xe::load_and_swap<uint32_t>(p + 0x354);
  version = xe::load_and_swap<uint32_t>(p + 0x358);
  base_version = xe::load_and_swap<uint32_t>(p + 0x35C);
  title_id = xe::load_and_swap<uint32_t>(p + 0x360);
  platform = (StfsPlatform)xe::load_and_swap<uint8_t>(p + 0x364);
  executable_type = xe::load_and_swap<uint8_t>(p + 0x365);
  disc_number = xe::load_and_swap<uint8_t>(p + 0x366);
  disc_in_set = xe::load_and_swap<uint8_t>(p + 0x367);
  save_game_id = xe::load_and_swap<uint32_t>(p + 0x368);
  std::memcpy(console_id, p + 0x36C, 0x5);
  std::memcpy(profile_id, p + 0x371, 0x8);
  data_file_count = xe::load_and_swap<uint32_t>(p + 0x39D);
  data_file_combined_size = xe::load_and_swap<uint64_t>(p + 0x3A1);
  descriptor_type = (StfsDescriptorType)xe::load_and_swap<uint8_t>(p + 0x3A9);
  if (descriptor_type != StfsDescriptorType::kStfs) {
    XELOGE("STFS descriptor format not supported: %d", descriptor_type);
    return false;
  }
  if (!volume_descriptor.Read(p + 0x379)) {
    return false;
  }
  memcpy(device_id, p + 0x3FD, 0x14);
  for (size_t n = 0; n < 0x900 / 2; n++) {
    display_names[n] = xe::load_and_swap<uint16_t>(p + 0x411 + n * 2);
    display_descs[n] = xe::load_and_swap<uint16_t>(p + 0xD11 + n * 2);
  }
  for (size_t n = 0; n < 0x80 / 2; n++) {
    publisher_name[n] = xe::load_and_swap<uint16_t>(p + 0x1611 + n * 2);
    title_name[n] = xe::load_and_swap<uint16_t>(p + 0x1691 + n * 2);
  }
  transfer_flags = xe::load_and_swap<uint8_t>(p + 0x1711);
  thumbnail_image_size = xe::load_and_swap<uint32_t>(p + 0x1712);
  title_thumbnail_image_size = xe::load_and_swap<uint32_t>(p + 0x1716);
  std::memcpy(thumbnail_image, p + 0x171A, 0x4000);
  std::memcpy(title_thumbnail_image, p + 0x571A, 0x4000);
  return true;
}

}  // namespace vfs
}  // namespace xe

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
#include <queue>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/vfs/devices/stfs_container_entry.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#define timegm _mkgmtime
#endif

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

// Convert FAT timestamp to 100-nanosecond intervals since January 1, 1601 (UTC)
uint64_t decode_fat_timestamp(uint32_t date, uint32_t time) {
  struct tm tm = {0};
  // 80 is the difference between 1980 (FAT) and 1900 (tm);
  tm.tm_year = ((0xFE00 & date) >> 9) + 80;
  tm.tm_mon = (0x01E0 & date) >> 5;
  tm.tm_mday = (0x001F & date) >> 0;
  tm.tm_hour = (0xF800 & time) >> 11;
  tm.tm_min = (0x07E0 & time) >> 5;
  tm.tm_sec = (0x001F & time) << 1;  // the value stored in 2-seconds intervals
  tm.tm_isdst = 0;
  time_t timet = timegm(&tm);
  if (timet == -1) {
    return 0;
  }
  // 11644473600LL is a difference between 1970 and 1601
  return (timet + 11644473600LL) * 10000000;
}

StfsContainerDevice::StfsContainerDevice(const std::string& mount_path,
                                         const std::wstring& local_path)
    : Device(mount_path), local_path_(local_path) {}

StfsContainerDevice::~StfsContainerDevice() = default;

bool StfsContainerDevice::Initialize() {
  // Resolve a valid STFS file if a directory is given.
  if (filesystem::IsFolder(local_path_) && !ResolveFromFolder(local_path_)) {
    XELOGE("Could not resolve an STFS container given path %s",
           xe::to_string(local_path_).c_str());
    return false;
  }

  if (!filesystem::PathExists(local_path_)) {
    XELOGE("Path to STFS container does not exist: %s",
           xe::to_string(local_path_).c_str());
    return false;
  }

  // Map the data file(s)
  auto map_result = MapFiles();
  if (map_result != Error::kSuccess) {
    XELOGE("Failed to map STFS container: %d", map_result);
    return false;
  }

  switch (header_.descriptor_type) {
    case StfsDescriptorType::kStfs:
      return ReadSTFS() == Error::kSuccess;
      break;
    case StfsDescriptorType::kSvod:
      return ReadSVOD() == Error::kSuccess;
    default:
      XELOGE("Unknown STFS Descriptor Type: %d", header_.descriptor_type);
      return false;
  }
}

StfsContainerDevice::Error StfsContainerDevice::MapFiles() {
  // Map the file containing the STFS Header and read it.
  XELOGI("Mapping STFS Header File: %s", xe::to_string(local_path_).c_str());
  auto header_map = MappedMemory::Open(local_path_, MappedMemory::Mode::kRead);

  auto header_result = ReadHeaderAndVerify(header_map->data());
  if (header_result != Error::kSuccess) {
    XELOGE("Error reading STFS Header: %d", header_result);
    return header_result;
  }

  // If the STFS package is a single file, the header is self contained and
  // we don't need to map any extra files.
  // NOTE: data_file_count is 0 for STFS and 1 for SVOD
  if (header_.data_file_count <= 1) {
    XELOGI("STFS container is a single file.");
    mmap_.emplace(std::make_pair(0, std::move(header_map)));
    return Error::kSuccess;
  }

  // If the STFS package is multi-file, it is an SVOD system. We need to map
  // the files in the .data folder and can discard the header.
  auto data_fragment_path = local_path_ + L".data";
  if (!filesystem::PathExists(data_fragment_path)) {
    XELOGE("STFS container is multi-file, but path %s does not exist.",
           xe::to_string(data_fragment_path).c_str());
    return Error::kErrorFileMismatch;
  }

  // Ensure data fragment files are sorted
  auto fragment_files = filesystem::ListFiles(data_fragment_path);
  std::sort(fragment_files.begin(), fragment_files.end(),
            [](filesystem::FileInfo& left, filesystem::FileInfo& right) {
              return left.name < right.name;
            });

  if (fragment_files.size() != header_.data_file_count) {
    XELOGE("SVOD expecting %d data fragments, but %d are present.",
           header_.data_file_count, fragment_files.size());
    return Error::kErrorFileMismatch;
  }

  for (size_t i = 0; i < fragment_files.size(); i++) {
    auto file = fragment_files.at(i);
    auto path = xe::join_paths(file.path, file.name);
    auto data = MappedMemory::Open(path, MappedMemory::Mode::kRead);
    mmap_.emplace(std::make_pair(i, std::move(data)));
  }
  XELOGI("SVOD successfully mapped %d files.", fragment_files.size());
  return Error::kSuccess;
}

void StfsContainerDevice::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

Entry* StfsContainerDevice::ResolvePath(std::string path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("StfsContainerDevice::ResolvePath(%s)", path.c_str());

  // Walk the path, one separator at a time.
  auto entry = root_entry_.get();
  auto path_parts = xe::split_path(path);
  for (auto& part : path_parts) {
    entry = entry->GetChild(part);
    if (!entry) {
      // Not found.
      return nullptr;
    }
  }

  return entry;
}

StfsContainerDevice::Error StfsContainerDevice::ReadHeaderAndVerify(
    const uint8_t* map_ptr) {
  // Check signature.
  if (memcmp(map_ptr, "LIVE", 4) == 0) {
    package_type_ = StfsPackageType::kLive;
  } else if (memcmp(map_ptr, "PIRS", 4) == 0) {
    package_type_ = StfsPackageType::kPirs;
  } else if (memcmp(map_ptr, "CON ", 4) == 0) {
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

StfsContainerDevice::Error StfsContainerDevice::ReadSVOD() {
  // SVOD Systems can have different layouts. The root block is
  // denoted by the magic "MICROSOFT*XBOX*MEDIA" and is always in
  // the first "actual" data fragment of the system.
  auto data = mmap_.at(0)->data();
  const char* MEDIA_MAGIC = "MICROSOFT*XBOX*MEDIA";

  // Check for EDGF layout
  auto layout = &header_.svod_volume_descriptor.layout_type;
  auto features = header_.svod_volume_descriptor.device_features;
  bool has_egdf_layout = features & kFeatureHasEnhancedGDFLayout;

  if (has_egdf_layout) {
    // The STFS header has specified that this SVOD system uses the EGDF layout.
    // We can expect the magic block to be located immediately after the hash
    // blocks. We also offset block address calculation by 0x1000 by shifting
    // block indices by +0x2.
    if (memcmp(data + 0x2000, MEDIA_MAGIC, 20) == 0) {
      base_offset_ = 0x0000;
      magic_offset_ = 0x2000;
      *layout = kEnhancedGDFLayout;
      XELOGI("SVOD uses an EGDF layout. Magic block present at 0x2000.");
    } else {
      XELOGE("SVOD uses an EGDF layout, but the magic block was not found.");
      return Error::kErrorFileMismatch;
    }
  } else if (memcmp(data + 0x12000, MEDIA_MAGIC, 20) == 0) {
    // If the SVOD's magic block is at 0x12000, it is likely using an XSF
    // layout. This is usually due to converting the game using a third-party
    // tool, as most of them use a nulled XSF as a template.

    base_offset_ = 0x10000;
    magic_offset_ = 0x12000;

    // Check for XSF Header
    const char* XSF_MAGIC = "XSF";
    if (memcmp(data + 0x2000, XSF_MAGIC, 3) == 0) {
      *layout = kXSFLayout;
      XELOGI("SVOD uses an XSF layout. Magic block present at 0x12000.");
      XELOGI("Game was likely converted using a third-party tool.");
    } else {
      *layout = kUnknownLayout;
      XELOGI("SVOD appears to use an XSF layout, but no header is present.");
      XELOGI("SVOD magic block found at 0x12000");
    }
  } else if (memcmp(data + 0xD000, MEDIA_MAGIC, 20) == 0) {
    // If the SVOD's magic block is at 0xD000, it most likely means that it is
    // a single-file system. The STFS Header is 0xB000 bytes , and the remaining
    // 0x2000 is from hash tables. In most cases, these will be STFS, not SVOD.

    base_offset_ = 0xB000;
    magic_offset_ = 0xD000;

    // Check for single file system
    if (header_.data_file_count == 1) {
      *layout = kSingleFileLayout;
      XELOGI("SVOD is a single file. Magic block present at 0xD000.");
    } else {
      *layout = kUnknownLayout;
      XELOGE(
          "SVOD is not a single file, but the magic block was found at "
          "0xD000.");
    }
  } else {
    XELOGE("Could not locate SVOD magic block.");
    return Error::kErrorReadError;
  }

  // Parse the root directory
  uint8_t* magic_block = data + magic_offset_;
  uint32_t root_block = xe::load<uint32_t>(magic_block + 0x14);
  uint32_t root_size = xe::load<uint32_t>(magic_block + 0x18);
  uint32_t root_creation_date = xe::load<uint32_t>(magic_block + 0x1C);
  uint32_t root_creation_time = xe::load<uint32_t>(magic_block + 0x20);
  uint64_t root_creation_timestamp =
      decode_fat_timestamp(root_creation_date, root_creation_time);

  auto root_entry = new StfsContainerEntry(this, nullptr, "", &mmap_);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry->access_timestamp_ = root_creation_timestamp;
  root_entry->create_timestamp_ = root_creation_timestamp;
  root_entry->write_timestamp_ = root_creation_timestamp;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  // Traverse all child entries
  return ReadEntrySVOD(root_block, 0, root_entry);
}

StfsContainerDevice::Error StfsContainerDevice::ReadEntrySVOD(
    uint32_t block, uint32_t ordinal, StfsContainerEntry* parent) {
  // For games with a large amount of files, the ordinal offset can overrun
  // the current block and potentially hit a hash block.
  size_t ordinal_offset = ordinal * 0x4;
  size_t block_offset = ordinal_offset / 0x800;
  size_t true_ordinal_offset = ordinal_offset % 0x800;

  // Calculate the file & address of the block
  size_t entry_address, entry_file;
  BlockToOffsetSVOD(block + block_offset, &entry_address, &entry_file);
  entry_address += true_ordinal_offset;

  // Read block's descriptor
  auto data = mmap_.at(entry_file)->data() + entry_address;

  uint16_t node_l = xe::load<uint16_t>(data + 0x00);
  uint16_t node_r = xe::load<uint16_t>(data + 0x02);
  uint32_t data_block = xe::load<uint32_t>(data + 0x04);
  uint32_t length = xe::load<uint32_t>(data + 0x08);
  uint8_t attributes = xe::load<uint8_t>(data + 0x0C);
  uint8_t name_length = xe::load<uint8_t>(data + 0x0D);
  auto name = reinterpret_cast<const char*>(data + 0x0E);
  auto name_str = std::string(name, name_length);

  // Read the left node
  if (node_l) {
    auto node_result = ReadEntrySVOD(block, node_l, parent);
    if (node_result != Error::kSuccess) {
      return node_result;
    }
  }

  // Read file & address of block's data
  size_t data_address, data_file;
  BlockToOffsetSVOD(data_block, &data_address, &data_file);

  // Create the entry
  // NOTE: SVOD entries don't have timestamps for individual files, which can
  //       cause issues when decrypting games. Using the root entry's timestamp
  //       solves this issues.
  auto entry = StfsContainerEntry::Create(this, parent, name_str, &mmap_);
  if (attributes & kFileAttributeDirectory) {
    // Entry is a directory
    entry->attributes_ = kFileAttributeDirectory | kFileAttributeReadOnly;
    entry->data_offset_ = 0;
    entry->data_size_ = 0;
    entry->block_ = block;
    entry->access_timestamp_ = root_entry_->create_timestamp();
    entry->create_timestamp_ = root_entry_->create_timestamp();
    entry->write_timestamp_ = root_entry_->create_timestamp();

    if (length) {
      // If length is greater than 0, traverse the directory's children
      auto directory_result = ReadEntrySVOD(data_block, 0, entry.get());
      if (directory_result != Error::kSuccess) {
        return directory_result;
      }
    }
  } else {
    // Entry is a file
    entry->attributes_ = kFileAttributeNormal | kFileAttributeReadOnly;
    entry->size_ = length;
    entry->allocation_size_ = xe::round_up(length, bytes_per_sector());
    entry->data_offset_ = data_address;
    entry->data_size_ = length;
    entry->block_ = data_block;
    entry->access_timestamp_ = root_entry_->create_timestamp();
    entry->create_timestamp_ = root_entry_->create_timestamp();
    entry->write_timestamp_ = root_entry_->create_timestamp();

    // Fill in all block records, sector by sector.
    if (entry->attributes() & X_FILE_ATTRIBUTE_NORMAL) {
      uint32_t block_index = data_block;
      size_t remaining_size = xe::round_up(length, 0x800);

      size_t last_record = -1;
      size_t last_offset = -1;
      while (remaining_size) {
        const size_t BLOCK_SIZE = 0x800;

        size_t offset, file_index;
        BlockToOffsetSVOD(block_index, &offset, &file_index);

        block_index++;
        remaining_size -= BLOCK_SIZE;

        if (offset - last_offset == 0x800) {
          // Consecutive, so append to last entry.
          entry->block_list_[last_record].length += BLOCK_SIZE;
          last_offset = offset;
          continue;
        }

        entry->block_list_.push_back({file_index, offset, BLOCK_SIZE});
        last_record = entry->block_list_.size() - 1;
        last_offset = offset;
      }
    }
  }

  parent->children_.emplace_back(std::move(entry));

  // Read the right node.
  if (node_r) {
    auto node_result = ReadEntrySVOD(block, node_r, parent);
    if (node_result != Error::kSuccess) {
      return node_result;
    }
  }

  return Error::kSuccess;
}

void StfsContainerDevice::BlockToOffsetSVOD(size_t block, size_t* out_address,
                                            size_t* out_file_index) {
  // SVOD Systems use hash blocks for integrity checks. These hash blocks
  // cause blocks to be discontinuous in memory, and must be accounted for.
  //  - Each data block is 0x800 bytes in length
  //  - Every group of 0x198 data blocks is preceded a Level0 hash table.
  //    Level0 tables contain 0xCC hashes, each representing two data blocks.
  //    The total size of each Level0 hash table is 0x1000 bytes in length.
  //  - Every 0xA1C4 Level0 hash tables is preceded by a Level1 hash table.
  //    Level1 tables contain 0xCB hashes, each representing two Level0 hashes.
  //    The total size of each Level1 hash table is 0x1000 bytes in length.
  //  - Files are split into fragments of 0xA290000 bytes in length,
  //    consisting of 0x14388 data blocks, 0xCB Level0 hash tables, and 0x1
  //    Level1 hash table.

  const size_t BLOCK_SIZE = 0x800;
  const size_t HASH_BLOCK_SIZE = 0x1000;
  const size_t BLOCKS_PER_L0_HASH = 0x198;
  const size_t HASHES_PER_L1_HASH = 0xA1C4;
  const size_t BLOCKS_PER_FILE = 0x14388;
  const size_t MAX_FILE_SIZE = 0xA290000;
  const size_t BLOCK_OFFSET = header_.svod_volume_descriptor.data_block_offset;
  const SvodLayoutType LAYOUT = header_.svod_volume_descriptor.layout_type;

  // Resolve the true block address and file index
  size_t true_block = block - (BLOCK_OFFSET * 2);
  if (LAYOUT == kEnhancedGDFLayout) {
    // EGDF has an 0x1000 byte offset, which is two blocks
    true_block += 0x2;
  }

  size_t file_block = true_block % BLOCKS_PER_FILE;
  size_t file_index = true_block / BLOCKS_PER_FILE;
  size_t offset = 0;

  // Calculate offset caused by Level0 Hash Tables
  size_t level0_table_count = (file_block / BLOCKS_PER_L0_HASH) + 1;
  offset += level0_table_count * HASH_BLOCK_SIZE;

  // Calculate offset caused by Level1 Hash Tables
  size_t level1_table_count = (level0_table_count / HASHES_PER_L1_HASH) + 1;
  offset += level1_table_count * HASH_BLOCK_SIZE;

  // For single-file SVOD layouts, include the size of the header in the offset.
  if (LAYOUT == kSingleFileLayout) {
    offset += base_offset_;
  }

  size_t block_address = (file_block * BLOCK_SIZE) + offset;

  // If the offset causes the block address to overrun the file, round it.
  if (block_address >= MAX_FILE_SIZE) {
    file_index += 1;
    block_address %= MAX_FILE_SIZE;
    block_address += 0x2000;
  }

  *out_address = block_address;
  *out_file_index = file_index;
}

StfsContainerDevice::Error StfsContainerDevice::ReadSTFS() {
  auto data = mmap_.at(0)->data();

  auto root_entry = new StfsContainerEntry(this, nullptr, "", &mmap_);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  std::vector<StfsContainerEntry*> all_entries;

  // Load all listings.
  auto& volume_descriptor = header_.stfs_volume_descriptor;
  uint32_t table_block_index = volume_descriptor.file_table_block_number;
  for (size_t n = 0; n < volume_descriptor.file_table_block_count; n++) {
    const uint8_t* p = data + BlockToOffsetSTFS(table_block_index);
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

      // both date and time parts of the timestamp are big endian
      uint16_t update_date = xe::load_and_swap<uint16_t>(p + 0x38);
      uint16_t update_time = xe::load_and_swap<uint16_t>(p + 0x3A);
      uint32_t access_date = xe::load_and_swap<uint16_t>(p + 0x3C);
      uint32_t access_time = xe::load_and_swap<uint16_t>(p + 0x3E);
      p += 0x40;

      StfsContainerEntry* parent_entry = nullptr;
      if (path_indicator == 0xFFFF) {
        parent_entry = root_entry;
      } else {
        parent_entry = all_entries[path_indicator];
      }

      std::string name_str(reinterpret_cast<const char*>(filename),
                           filename_length_flags & 0x3F);
      auto entry =
          StfsContainerEntry::Create(this, parent_entry, name_str, &mmap_);

      // bit 0x40 = consecutive blocks (not fragmented?)
      if (filename_length_flags & 0x80) {
        entry->attributes_ = kFileAttributeDirectory;
      } else {
        entry->attributes_ = kFileAttributeNormal | kFileAttributeReadOnly;
        entry->data_offset_ = BlockToOffsetSTFS(start_block_index);
        entry->data_size_ = file_size;
      }
      entry->size_ = file_size;
      entry->allocation_size_ = xe::round_up(file_size, bytes_per_sector());

      entry->create_timestamp_ = decode_fat_timestamp(update_date, update_time);
      entry->access_timestamp_ = decode_fat_timestamp(access_date, access_time);
      entry->write_timestamp_ = entry->create_timestamp_;

      all_entries.push_back(entry.get());

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
          size_t offset = BlockToOffsetSTFS(block_index);
          entry->block_list_.push_back({0, offset, block_size});
          remaining_size -= block_size;
          auto block_hash = GetBlockHash(data, block_index, 0);
          if (table_size_shift_ && block_hash.info < 0x80) {
            block_hash = GetBlockHash(data, block_index, 1);
          }
          block_index = block_hash.next_block_index;
          info = block_hash.info;
        }
      }

      parent_entry->children_.emplace_back(std::move(entry));
    }

    auto block_hash = GetBlockHash(data, table_block_index, 0);
    if (table_size_shift_ && block_hash.info < 0x80) {
      block_hash = GetBlockHash(data, table_block_index, 1);
    }
    table_block_index = block_hash.next_block_index;
    if (table_block_index == 0xFFFFFF) {
      break;
    }
  }

  return Error::kSuccess;
}

size_t StfsContainerDevice::BlockToOffsetSTFS(uint64_t block_index) {
  uint64_t block;
  uint32_t block_shift = 0;
  if (((header_.header_size + 0x0FFF) & 0xB000) == 0xB000 ||
      (header_.stfs_volume_descriptor.flags & 0x1) == 0x0) {
    block_shift = package_type_ == StfsPackageType::kCon ? 1 : 0;
  }

  // For every level there is a hash table
  // Level 0: hash table of next 170 blocks
  // Level 1: hash table of next 170 hash tables
  // Level 2: hash table of next 170 level 1 hash tables
  // And so on...
  uint64_t base = kSTFSHashSpacing;
  block = block_index;
  for (uint32_t i = 0; i < 3; i++) {
    block += (block_index + (base << block_shift)) / (base << block_shift);
    if (block_index < base) {
      break;
    }

    base *= kSTFSHashSpacing;
  }

  return xe::round_up(header_.header_size, 0x1000) + (block << 12);
}

StfsContainerDevice::BlockHash StfsContainerDevice::GetBlockHash(
    const uint8_t* map_ptr, uint32_t block_index, uint32_t table_offset) {
  uint32_t record = block_index % 0xAA;

  // This is a bit hacky, but we'll get a pointer to the first block after the
  // table and then subtract one sector to land on the table itself.
  size_t hash_offset = BlockToOffsetSTFS(
      xe::round_up(block_index + 1, kSTFSHashSpacing) - kSTFSHashSpacing);
  hash_offset -= bytes_per_sector();
  const uint8_t* hash_data = map_ptr + hash_offset;

  // table_index += table_offset - (1 << table_size_shift_);
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
  version = xe::load_and_swap<uint8_t>(p + 0x01);
  flags = xe::load_and_swap<uint8_t>(p + 0x02);
  file_table_block_count = xe::load_and_swap<uint16_t>(p + 0x03);
  file_table_block_number = load_uint24_be(p + 0x05);
  std::memcpy(top_hash_table_hash, p + 0x08, 0x14);
  total_allocated_block_count = xe::load_and_swap<uint32_t>(p + 0x1C);
  total_unallocated_block_count = xe::load_and_swap<uint32_t>(p + 0x20);
  return true;
}

bool SvodVolumeDescriptor::Read(const uint8_t* p) {
  descriptor_size = xe::load<uint8_t>(p + 0x00);
  if (descriptor_size != 0x24) {
    XELOGE("SVOD volume descriptor size mismatch, expected 0x24 but got 0x%X",
           descriptor_size);
    return false;
  }

  block_cache_element_count = xe::load<uint8_t>(p + 0x01);
  worker_thread_processor = xe::load<uint8_t>(p + 0x02);
  worker_thread_priority = xe::load<uint8_t>(p + 0x03);
  std::memcpy(hash, p + 0x04, 0x14);
  device_features = xe::load<uint8_t>(p + 0x18);
  data_block_count = load_uint24_be(p + 0x19);
  data_block_offset = load_uint24_le(p + 0x1C);
  return true;
}

bool StfsHeader::Read(const uint8_t* p) {
  std::memcpy(license_entries, p + 0x22C, 0x100);
  std::memcpy(header_hash, p + 0x32C, 0x14);
  header_size = xe::load_and_swap<uint32_t>(p + 0x340);
  content_type = (StfsContentType)xe::load_and_swap<uint32_t>(p + 0x344);
  metadata_version = xe::load_and_swap<uint32_t>(p + 0x348);
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
  descriptor_type = (StfsDescriptorType)xe::load_and_swap<uint32_t>(p + 0x3A9);
  switch (descriptor_type) {
    case StfsDescriptorType::kStfs:
      stfs_volume_descriptor.Read(p + 0x379);
      break;
    case StfsDescriptorType::kSvod:
      svod_volume_descriptor.Read(p + 0x379);
      break;
    default:
      XELOGE("STFS descriptor format not supported: %d", descriptor_type);
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

  // Metadata v2 Fields
  if (metadata_version == 2) {
    std::memcpy(series_id, p + 0x3B1, 0x10);
    std::memcpy(season_id, p + 0x3C1, 0x10);
    season_number = xe::load_and_swap<uint16_t>(p + 0x3D1);
    episode_number = xe::load_and_swap<uint16_t>(p + 0x3D5);

    for (size_t n = 0; n < 0x300 / 2; n++) {
      additonal_display_names[n] =
          xe::load_and_swap<uint16_t>(p + 0x541A + n * 2);
      additional_display_descriptions[n] =
          xe::load_and_swap<uint16_t>(p + 0x941A + n * 2);
    }
  }

  return true;
}

const char* StfsContainerDevice::ReadMagic(const std::wstring& path) {
  auto map = MappedMemory::Open(path, MappedMemory::Mode::kRead, 0, 4);
  auto magic_data = xe::load<uint32_t>(map->data());
  auto magic_bytes = static_cast<char*>(static_cast<void*>(&magic_data));
  return std::move(magic_bytes);
}

bool StfsContainerDevice::ResolveFromFolder(const std::wstring& path) {
  // Scan through folders until a file with magic is found
  std::queue<filesystem::FileInfo> queue;

  filesystem::FileInfo folder;
  filesystem::GetInfo(local_path_, &folder);
  queue.push(folder);

  while (!queue.empty()) {
    auto current_file = queue.front();
    queue.pop();

    if (current_file.type == filesystem::FileInfo::Type::kDirectory) {
      auto path = xe::join_paths(current_file.path, current_file.name);
      auto child_files = filesystem::ListFiles(path);
      for (auto file : child_files) {
        queue.push(file);
      }
    } else {
      // Try to read the file's magic
      auto path = xe::join_paths(current_file.path, current_file.name);
      auto magic = ReadMagic(path);

      if (memcmp(magic, "LIVE", 4) == 0 || memcmp(magic, "PIRS", 4) == 0 ||
          memcmp(magic, "CON ", 4) == 0) {
        local_path_ = xe::join_paths(current_file.path, current_file.name);
        XELOGI("STFS Package found: %s", xe::to_string(local_path_).c_str());
        return true;
      }
    }
  }

  if (local_path_ == path) {
    // Could not find a suitable container file
    return false;
  }
  return true;
}

}  // namespace vfs
}  // namespace xe
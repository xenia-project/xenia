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
  if (filesystem::IsFolder(local_path_) && !ResolveFromFolder(local_path_)) {
    XELOGE("Could not resolve an STFS container given path %s",
           local_path_.c_str());
    return false;
  }

  if (!filesystem::PathExists(local_path_)) {
    XELOGE("STFS container does not exist");
    return false;
  }

  // Map the appropriate file(s)
  if (filesystem::PathExists(local_path_ + L".data")) {
    // Container is multi-file (GoD)
    // Read all datafiles to mapped memory
    XELOGI("STFS Container is mutli-file");

    // List datafiles and sort by name
    auto data_files = filesystem::ListFiles(local_path_ + L".data");
    std::sort(data_files.begin(), data_files.end(),
              [](filesystem::FileInfo& left, filesystem::FileInfo& right) {
                return left.name < right.name;
              });

    mmap_.clear();
    mmap_total_size_ = 0;
    for (size_t i = 0; i < data_files.size(); i++) {
      auto file = data_files.at(i);
      auto path = xe::join_paths(file.path, file.name);
      auto map = MappedMemory::Open(path, MappedMemory::Mode::kRead);
      mmap_total_size_ += map->size();
      mmap_.emplace(std::make_pair(i, std::move(map)));
    }
    XELOGI("Mapped %d STFS datafiles", mmap_.size());
  } else {
    // Container is single-file (XBLA)
    XELOGI("STFS Container is single-file");
    auto map = MappedMemory::Open(local_path_, MappedMemory::Mode::kRead);
    mmap_.emplace(std::make_pair(0, std::move(map)));
  }

  // Verify successful file mapping
  auto map = mmap_.at(0).get();
  if (!map) {
    XELOGI("STFS container could not be mapped");
    return false;
  }

  // In single-file containers, the header is self-contained.
  // In multi-file containers, the header is in the manifest.
  auto header_file = MappedMemory::Open(local_path_, MappedMemory::Mode::kRead);
  uint8_t* header_data = (header_file)->data();

  auto result = ReadHeaderAndVerify(header_data);
  if (result != Error::kSuccess) {
    XELOGI("STFS header read/verification failed: %d", result);
    return false;
  }

  switch (header_.descriptor_type) {
    case StfsDescriptorType::kStfs:
      result = ReadAllEntriesSTFS(header_data);
      break;
    case StfsDescriptorType::kSvod: {
      bool is_gdf = header_.svod_volume_descriptor.device_features &
                    kFeatureHasEnhancedGDFLayout;

      if (is_gdf) {
        XELOGI("SVOD uses EGDF Layout.");
        const size_t HEADER_SIZE = 0x2000;
        base_address_ = HEADER_SIZE;
      } else {
        XELOGI("SVOD does not use EGDF Layout.");
        // If the datafile contains the header, we base after it.
        const size_t HEADER_SIZE = 0xB000;
        base_address_ = mmap_.size() > 1 ? 0x0 : HEADER_SIZE;
      }

      result = ReadAllEntriesSVOD();
    } break;
    default:
      // Shouldn't reach here.
      return false;
  }

  if (result != Error::kSuccess) {
    XELOGE("STFS entry reading failed: %d", result);
    return false;
  }

  return true;
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

StfsContainerDevice::Error StfsContainerDevice::ReadAllEntriesSVOD() {
  // Verify SVOD Magic
  const size_t MAGIC_BLOCK = 0x20;
  size_t magic_address, magic_file;
  BlockToOffsetSVOD(MAGIC_BLOCK, &magic_address, &magic_file);

  auto data = mmap_.at(0)->data();
  const uint8_t* p = data + magic_address;
  if (std::memcmp(p, "MICROSOFT*XBOX*MEDIA", 20) != 0) {
    return Error::kErrorDamagedFile;
  }

  // Read Root Entry
  uint32_t root_block = xe::load<uint32_t>(p + 0x14);
  uint32_t root_size = xe::load<uint32_t>(p + 0x18);

  size_t root_address, root_file;
  BlockToOffsetSVOD(root_block, &root_address, &root_file);
  p = mmap_.at(root_file)->data() + root_address;

  auto root_entry = new StfsContainerEntry(this, nullptr, "", &mmap_);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  // Traverse all children
  return ReadEntrySVOD(root_block, 0, root_entry) ? Error::kSuccess
                                                  : Error::kErrorDamagedFile;
}

bool StfsContainerDevice::ReadEntrySVOD(uint32_t block, uint32_t ordinal,
                                        StfsContainerEntry* parent) {
  // Calculate the file & address of the block
  size_t entry_address, entry_file;
  BlockToOffsetSVOD(block, &entry_address, &entry_file);
  entry_address += ordinal * 0x04;

  // Read block's descriptor
  auto data = mmap_.at(entry_file)->data() + entry_address;

  uint16_t node_l = xe::load<uint16_t>(data + 0);
  uint16_t node_r = xe::load<uint16_t>(data + 2);
  uint32_t data_block = xe::load<uint32_t>(data + 4);
  uint32_t length = xe::load<uint32_t>(data + 8);
  uint8_t attributes = xe::load<uint8_t>(data + 12);
  uint8_t name_length = xe::load<uint8_t>(data + 13);
  auto name = reinterpret_cast<const char*>(data + 14);

  // Read the left node
  if (node_l && !ReadEntrySVOD(block, node_l, parent)) {
    return false;
  }

  // Read file & address of block's data
  size_t data_address, data_file;
  BlockToOffsetSVOD(data_block, &data_address, &data_file);

  // Create the entry
  auto name_str = std::string(name, name_length);
  auto entry = StfsContainerEntry::Create(this, parent, name_str, &mmap_);

  if (attributes & kFileAttributeDirectory) {
    // Entry is a folder
    entry->attributes_ = kFileAttributeDirectory | kFileAttributeReadOnly;
    entry->data_offset_ = 0;
    entry->data_size_ = 0;
    entry->block_ = block;

    if (length) {
      // Folder contains children
      if (!ReadEntrySVOD(data_block, 0, entry.get())) {
        return false;
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

  // Read next file in the list.
  if (node_r && !ReadEntrySVOD(block, node_r, parent)) {
    return false;
  }

  return true;
}

StfsContainerDevice::Error StfsContainerDevice::ReadAllEntriesSTFS(
    const uint8_t* map_ptr) {
  auto root_entry = new StfsContainerEntry(this, nullptr, "", &mmap_);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  std::vector<StfsContainerEntry*> all_entries;

  // Load all listings.
  auto& volume_descriptor = header_.stfs_volume_descriptor;
  uint32_t table_block_index = volume_descriptor.file_table_block_number;
  for (size_t n = 0; n < volume_descriptor.file_table_block_count; n++) {
    const uint8_t* p = map_ptr + BlockToOffsetSTFS(table_block_index);
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
          auto block_hash = GetBlockHash(map_ptr, block_index, 0);
          if (table_size_shift_ && block_hash.info < 0x80) {
            block_hash = GetBlockHash(map_ptr, block_index, 1);
          }
          block_index = block_hash.next_block_index;
          info = block_hash.info;
        }
      }

      parent_entry->children_.emplace_back(std::move(entry));
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

size_t StfsContainerDevice::BlockToOffsetSTFS(uint64_t block_index) {
  uint64_t block;
  uint32_t block_shift = 0;
  if (((header_.header_size + 0x0FFF) & 0xB000) == 0xB000 ||
      (header_.stfs_volume_descriptor.flags & 0x1) == 0x0) {
    block_shift = package_type_ == StfsPackageType::kCon ? 1 : 0;
  }

  if (header_.descriptor_type == StfsDescriptorType::kStfs) {
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
  } else if (header_.descriptor_type == StfsDescriptorType::kSvod) {
    // Level 0: Hashes for the next 204 blocks
    // Level 1: Hashes for the next 203 hash blocks + 1 for the next level 0
    // 10......[204 blocks].....0.....[204 blocks].....0
    // There are 0xA1C4 (41412) blocks for every level 1 hash table.
    block = block_index;
    block += (block_index + 204) / 204;                // Level 0
    block += (block_index + 204 * 203) / (204 * 203);  // Level 1
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

void StfsContainerDevice::BlockToOffsetSVOD(size_t block, size_t* out_address,
                                            size_t* out_file_index) {
  /* Blocks are 0x800 bytes each */
  /* Every 0x198 blocks there is a Level 0 hash table of size 0x1000,
     which contains the hashes of the next 0x198 blocks. Hashes are 0x14 bytes
     each, and there is 0x10 bytes of padding at the end. */
  /* Every 0xA1C4 blocks there is a Level 1 hash table of size 0x1000,
     which contains the hashes of the next 0xCB Level 0 hash blocks.
     Hashes are 0x14 bytes each and there is 0x10 bytes of padding at
     the end. */
  /* Files are split up into chunks of 0xA290000 bytes. */

  const size_t BLOCK_SIZE = 0x800;
  const size_t HASH_BLOCK_SIZE = 0x1000;
  const size_t BLOCKS_PER_L0_HASH = 0x198;
  const size_t HASHES_PER_L1_HASH = 0xA1C4;
  const size_t BLOCKS_PER_FILE = 0x14388;
  const size_t MAX_FILE_SIZE = 0xA290000;
  const size_t BLOCK_OFFSET = header_.svod_volume_descriptor.data_block_offset;

  // Resolve the true block address and file index
  size_t true_block = block - (BLOCK_OFFSET * 2);
  size_t file_block = true_block % BLOCKS_PER_FILE;
  size_t file_index = true_block / BLOCKS_PER_FILE;
  size_t offset = 0;

  // Calculate offset caused by Level0 Hash Tables
  size_t level0_table_count = (file_block / BLOCKS_PER_L0_HASH) + 1;
  offset += level0_table_count * HASH_BLOCK_SIZE;

  // Calculate offset caused by Level1 Hash Tables
  size_t level1_table_count = (level0_table_count / HASHES_PER_L1_HASH) + 1;
  offset += level1_table_count * HASH_BLOCK_SIZE;

  size_t block_address = (file_block * BLOCK_SIZE) + base_address_ + offset;

  // If the offset causes the block address to overrun the file, round it
  if (block_address >= MAX_FILE_SIZE) {
    file_index += 1;
    block_address %= block_address;
  }

  *out_address = block_address;
  *out_file_index = file_index;
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
        XELOGI("STFS Package found: %s", local_path_.c_str());
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
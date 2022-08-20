/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
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

StfsContainerDevice::StfsContainerDevice(const std::string_view mount_path,
                                         const std::filesystem::path& host_path)
    : Device(mount_path),
      name_("STFS"),
      host_path_(host_path),
      files_total_size_(),
      component_name_max_length_(40),
      svod_base_offset_(),
      header_(),
      svod_layout_(),
      blocks_per_hash_table_(1),
      block_step{0, 0} {}

StfsContainerDevice::~StfsContainerDevice() { CloseFiles(); }

bool StfsContainerDevice::Initialize() {
  // Resolve a valid STFS file if a directory is given.
  if (std::filesystem::is_directory(host_path_) &&
      !ResolveFromFolder(host_path_)) {
    XELOGE("Could not resolve an STFS container given path {}",
           xe::path_to_utf8(host_path_));
    return false;
  }

  if (!std::filesystem::exists(host_path_)) {
    XELOGE("Path to STFS container does not exist: {}",
           xe::path_to_utf8(host_path_));
    return false;
  }

  // Open the data file(s)
  auto open_result = OpenFiles();
  if (open_result != Error::kSuccess) {
    XELOGE("Failed to open STFS container: {}", open_result);
    return false;
  }

  switch (header_.metadata.volume_type) {
    case XContentVolumeType::kStfs:
      return ReadSTFS() == Error::kSuccess;
      break;
    case XContentVolumeType::kSvod:
      component_name_max_length_ = 255;
      return ReadSVOD() == Error::kSuccess;
    default:
      XELOGE("Unknown XContent volume type: {}",
             xe::byte_swap(uint32_t(header_.metadata.volume_type.value)));
      return false;
  }
}

StfsContainerDevice::Error StfsContainerDevice::OpenFiles() {
  // Map the file containing the STFS Header and read it.
  XELOGI("Loading STFS header file: {}", xe::path_to_utf8(host_path_));

  auto header_file = xe::filesystem::OpenFile(host_path_, "rb");
  if (!header_file) {
    XELOGE("Error opening STFS header file.");
    return Error::kErrorReadError;
  }

  auto header_result = ReadHeaderAndVerify(header_file);
  if (header_result != Error::kSuccess) {
    XELOGE("Error reading STFS header: {}", header_result);
    fclose(header_file);
    files_total_size_ = 0;
    return header_result;
  }

  // If the STFS package is a single file, the header is self contained and
  // we don't need to map any extra files.
  // NOTE: data_file_count is 0 for STFS and 1 for SVOD
  if (header_.metadata.data_file_count <= 1) {
    XELOGI("STFS container is a single file.");
    files_.emplace(std::make_pair(0, header_file));
    return Error::kSuccess;
  }

  // If the STFS package is multi-file, it is an SVOD system. We need to map
  // the files in the .data folder and can discard the header.
  auto data_fragment_path = host_path_;
  data_fragment_path += ".data";
  if (!std::filesystem::exists(data_fragment_path)) {
    XELOGE("STFS container is multi-file, but path {} does not exist.",
           xe::path_to_utf8(data_fragment_path));
    return Error::kErrorFileMismatch;
  }

  // Ensure data fragment files are sorted
  auto fragment_files = filesystem::ListFiles(data_fragment_path);
  std::sort(fragment_files.begin(), fragment_files.end(),
            [](filesystem::FileInfo& left, filesystem::FileInfo& right) {
              return left.name < right.name;
            });

  if (fragment_files.size() != header_.metadata.data_file_count) {
    XELOGE("SVOD expecting {} data fragments, but {} are present.",
           header_.metadata.data_file_count, fragment_files.size());
    return Error::kErrorFileMismatch;
  }

  for (size_t i = 0; i < fragment_files.size(); i++) {
    auto& fragment = fragment_files.at(i);
    auto path = fragment.path / fragment.name;
    auto file = xe::filesystem::OpenFile(path, "rb");
    if (!file) {
      XELOGI("Failed to map SVOD file {}.", xe::path_to_utf8(path));
      CloseFiles();
      return Error::kErrorReadError;
    }

    xe::filesystem::Seek(file, 0L, SEEK_END);
    files_total_size_ += xe::filesystem::Tell(file);
    // no need to seek back, any reads from this file will seek first anyway
    files_.emplace(std::make_pair(i, file));
  }
  XELOGI("SVOD successfully mapped {} files.", fragment_files.size());
  return Error::kSuccess;
}

void StfsContainerDevice::CloseFiles() {
  for (auto& file : files_) {
    fclose(file.second);
  }
  files_.clear();
  files_total_size_ = 0;
}

void StfsContainerDevice::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

Entry* StfsContainerDevice::ResolvePath(const std::string_view path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo
  XELOGFS("StfsContainerDevice::ResolvePath({})", path);
  return root_entry_->ResolvePath(path);
}

StfsContainerDevice::Error StfsContainerDevice::ReadHeaderAndVerify(
    FILE* header_file) {
  // Check size of the file is enough to store an STFS header
  xe::filesystem::Seek(header_file, 0L, SEEK_END);
  files_total_size_ = xe::filesystem::Tell(header_file);
  xe::filesystem::Seek(header_file, 0L, SEEK_SET);

  if (sizeof(StfsHeader) > files_total_size_) {
    return Error::kErrorTooSmall;
  }

  // Read header & check signature
  if (fread(&header_, sizeof(StfsHeader), 1, header_file) != 1) {
    return Error::kErrorReadError;
  }

  if (!header_.header.is_magic_valid()) {
    // Unexpected format.
    return Error::kErrorFileMismatch;
  }

  // Pre-calculate some values used in block number calculations
  if (header_.metadata.volume_type == XContentVolumeType::kStfs) {
    blocks_per_hash_table_ =
        header_.metadata.volume_descriptor.stfs.flags.bits.read_only_format ? 1
                                                                            : 2;

    block_step[0] = kBlocksPerHashLevel[0] + blocks_per_hash_table_;
    block_step[1] = kBlocksPerHashLevel[1] +
                    ((kBlocksPerHashLevel[0] + 1) * blocks_per_hash_table_);
  }

  return Error::kSuccess;
}

StfsContainerDevice::Error StfsContainerDevice::ReadSVOD() {
  // SVOD Systems can have different layouts. The root block is
  // denoted by the magic "MICROSOFT*XBOX*MEDIA" and is always in
  // the first "actual" data fragment of the system.
  auto& svod_header = files_.at(0);
  const char* MEDIA_MAGIC = "MICROSOFT*XBOX*MEDIA";

  uint8_t magic_buf[20];
  size_t magic_offset;

  // Check for EDGF layout
  if (header_.metadata.volume_descriptor.svod.features.bits
          .enhanced_gdf_layout) {
    // The STFS header has specified that this SVOD system uses the EGDF layout.
    // We can expect the magic block to be located immediately after the hash
    // blocks. We also offset block address calculation by 0x1000 by shifting
    // block indices by +0x2.
    xe::filesystem::Seek(svod_header, 0x2000, SEEK_SET);
    if (fread(magic_buf, 1, countof(magic_buf), svod_header) !=
        countof(magic_buf)) {
      XELOGE("ReadSVOD failed to read SVOD magic at 0x2000");
      return Error::kErrorReadError;
    }

    if (std::memcmp(magic_buf, MEDIA_MAGIC, 20) == 0) {
      svod_base_offset_ = 0x0000;
      magic_offset = 0x2000;
      svod_layout_ = SvodLayoutType::kEnhancedGDF;
      XELOGI("SVOD uses an EGDF layout. Magic block present at 0x2000.");
    } else {
      XELOGE("SVOD uses an EGDF layout, but the magic block was not found.");
      return Error::kErrorFileMismatch;
    }
  } else {
    xe::filesystem::Seek(svod_header, 0x12000, SEEK_SET);
    if (fread(magic_buf, 1, countof(magic_buf), svod_header) !=
        countof(magic_buf)) {
      XELOGE("ReadSVOD failed to read SVOD magic at 0x12000");
      return Error::kErrorReadError;
    }
    if (std::memcmp(magic_buf, MEDIA_MAGIC, 20) == 0) {
      // If the SVOD's magic block is at 0x12000, it is likely using an XSF
      // layout. This is usually due to converting the game using a third-party
      // tool, as most of them use a nulled XSF as a template.

      svod_base_offset_ = 0x10000;
      magic_offset = 0x12000;

      // Check for XSF Header
      const char* XSF_MAGIC = "XSF";
      xe::filesystem::Seek(svod_header, 0x2000, SEEK_SET);
      if (fread(magic_buf, 1, 3, svod_header) != 3) {
        XELOGE("ReadSVOD failed to read SVOD XSF magic at 0x2000");
        return Error::kErrorReadError;
      }
      if (std::memcmp(magic_buf, XSF_MAGIC, 3) == 0) {
        svod_layout_ = SvodLayoutType::kXSF;
        XELOGI("SVOD uses an XSF layout. Magic block present at 0x12000.");
        XELOGI("Game was likely converted using a third-party tool.");
      } else {
        svod_layout_ = SvodLayoutType::kUnknown;
        XELOGI("SVOD appears to use an XSF layout, but no header is present.");
        XELOGI("SVOD magic block found at 0x12000");
      }
    } else {
      xe::filesystem::Seek(svod_header, 0xD000, SEEK_SET);
      if (fread(magic_buf, 1, countof(magic_buf), svod_header) !=
          countof(magic_buf)) {
        XELOGE("ReadSVOD failed to read SVOD magic at 0xD000");
        return Error::kErrorReadError;
      }
      if (std::memcmp(magic_buf, MEDIA_MAGIC, 20) == 0) {
        // If the SVOD's magic block is at 0xD000, it most likely means that it
        // is a single-file system. The STFS Header is 0xB000 bytes , and the
        // remaining 0x2000 is from hash tables. In most cases, these will be
        // STFS, not SVOD.

        svod_base_offset_ = 0xB000;
        magic_offset = 0xD000;

        // Check for single file system
        if (header_.metadata.data_file_count == 1) {
          svod_layout_ = SvodLayoutType::kSingleFile;
          XELOGI("SVOD is a single file. Magic block present at 0xD000.");
        } else {
          svod_layout_ = SvodLayoutType::kUnknown;
          XELOGE(
              "SVOD is not a single file, but the magic block was found at "
              "0xD000.");
        }
      } else {
        XELOGE("Could not locate SVOD magic block.");
        return Error::kErrorReadError;
      }
    }
  }

  // Parse the root directory
  xe::filesystem::Seek(svod_header, magic_offset + 0x14, SEEK_SET);

  struct {
    uint32_t block;
    uint32_t size;
    uint32_t creation_date;
    uint32_t creation_time;
  } root_data;
  static_assert_size(root_data, 0x10);

  if (fread(&root_data, sizeof(root_data), 1, svod_header) != 1) {
    XELOGE("ReadSVOD failed to read root block data at 0x{X}",
           magic_offset + 0x14);
    return Error::kErrorReadError;
  }

  uint64_t root_creation_timestamp =
      decode_fat_timestamp(root_data.creation_date, root_data.creation_time);

  auto root_entry = new StfsContainerEntry(this, nullptr, "", &files_);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry->access_timestamp_ = root_creation_timestamp;
  root_entry->create_timestamp_ = root_creation_timestamp;
  root_entry->write_timestamp_ = root_creation_timestamp;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  // Traverse all child entries
  return ReadEntrySVOD(root_data.block, 0, root_entry);
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

  // Read directory entry
  auto& file = files_.at(entry_file);
  xe::filesystem::Seek(file, entry_address, SEEK_SET);

#pragma pack(push, 1)
  struct {
    uint16_t node_l;
    uint16_t node_r;
    uint32_t data_block;
    uint32_t length;
    uint8_t attributes;
    uint8_t name_length;
  } dir_entry;
  static_assert_size(dir_entry, 0xE);
#pragma pack(pop)

  if (fread(&dir_entry, sizeof(dir_entry), 1, file) != 1) {
    XELOGE("ReadEntrySVOD failed to read directory entry at 0x{X}",
           entry_address);
    return Error::kErrorReadError;
  }

  auto name_buffer = std::make_unique<char[]>(dir_entry.name_length);
  if (fread(name_buffer.get(), 1, dir_entry.name_length, file) !=
      dir_entry.name_length) {
    XELOGE("ReadEntrySVOD failed to read directory entry name at 0x{X}",
           entry_address);
    return Error::kErrorReadError;
  }

  auto name = std::string(name_buffer.get(), dir_entry.name_length);

  // Read the left node
  if (dir_entry.node_l) {
    auto node_result = ReadEntrySVOD(block, dir_entry.node_l, parent);
    if (node_result != Error::kSuccess) {
      return node_result;
    }
  }

  // Read file & address of block's data
  size_t data_address, data_file;
  BlockToOffsetSVOD(dir_entry.data_block, &data_address, &data_file);

  // Create the entry
  // NOTE: SVOD entries don't have timestamps for individual files, which can
  //       cause issues when decrypting games. Using the root entry's timestamp
  //       solves this issues.
  auto entry = StfsContainerEntry::Create(this, parent, name, &files_);
  if (dir_entry.attributes & kFileAttributeDirectory) {
    // Entry is a directory
    entry->attributes_ = kFileAttributeDirectory | kFileAttributeReadOnly;
    entry->data_offset_ = 0;
    entry->data_size_ = 0;
    entry->block_ = block;
    entry->access_timestamp_ = root_entry_->create_timestamp();
    entry->create_timestamp_ = root_entry_->create_timestamp();
    entry->write_timestamp_ = root_entry_->create_timestamp();

    if (dir_entry.length) {
      // If length is greater than 0, traverse the directory's children
      auto directory_result =
          ReadEntrySVOD(dir_entry.data_block, 0, entry.get());
      if (directory_result != Error::kSuccess) {
        return directory_result;
      }
    }
  } else {
    // Entry is a file
    entry->attributes_ = kFileAttributeNormal | kFileAttributeReadOnly;
    entry->size_ = dir_entry.length;
    entry->allocation_size_ = xe::round_up(dir_entry.length, kBlockSize);
    entry->data_offset_ = data_address;
    entry->data_size_ = dir_entry.length;
    entry->block_ = dir_entry.data_block;
    entry->access_timestamp_ = root_entry_->create_timestamp();
    entry->create_timestamp_ = root_entry_->create_timestamp();
    entry->write_timestamp_ = root_entry_->create_timestamp();

    // Fill in all block records, sector by sector.
    if (entry->attributes() & X_FILE_ATTRIBUTE_NORMAL) {
      uint32_t block_index = dir_entry.data_block;
      size_t remaining_size = xe::round_up(dir_entry.length, 0x800);

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
  if (dir_entry.node_r) {
    auto node_result = ReadEntrySVOD(block, dir_entry.node_r, parent);
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
  const size_t BLOCK_OFFSET =
      header_.metadata.volume_descriptor.svod.start_data_block();

  // Resolve the true block address and file index
  size_t true_block = block - (BLOCK_OFFSET * 2);
  if (svod_layout_ == SvodLayoutType::kEnhancedGDF) {
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
  if (svod_layout_ == SvodLayoutType::kSingleFile) {
    offset += svod_base_offset_;
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
  auto& file = files_.at(0);

  auto root_entry = new StfsContainerEntry(this, nullptr, "", &files_);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  std::vector<StfsContainerEntry*> all_entries;

  // Load all listings.
  StfsDirectoryBlock directory;

  auto& descriptor = header_.metadata.volume_descriptor.stfs;
  uint32_t table_block_index = descriptor.file_table_block_number();
  size_t n = 0;
  for (n = 0; n < descriptor.file_table_block_count; n++) {
    auto offset = BlockToOffsetSTFS(table_block_index);
    xe::filesystem::Seek(file, offset, SEEK_SET);

    if (fread(&directory, sizeof(StfsDirectoryBlock), 1, file) != 1) {
      XELOGE("ReadSTFS failed to read directory block at 0x{X}", offset);
      return Error::kErrorReadError;
    }

    for (size_t m = 0; m < kEntriesPerDirectoryBlock; m++) {
      auto& dir_entry = directory.entries[m];

      if (dir_entry.name[0] == 0) {
        // Done.
        break;
      }

      StfsContainerEntry* parent_entry = nullptr;
      if (dir_entry.directory_index == 0xFFFF) {
        parent_entry = root_entry;
      } else {
        parent_entry = all_entries[dir_entry.directory_index];
      }

      std::string name(reinterpret_cast<const char*>(dir_entry.name),
                       dir_entry.flags.name_length & 0x3F);
      auto entry =
          StfsContainerEntry::Create(this, parent_entry, name, &files_);

      if (dir_entry.flags.directory) {
        entry->attributes_ = kFileAttributeDirectory;
      } else {
        entry->attributes_ = kFileAttributeNormal | kFileAttributeReadOnly;
        entry->data_offset_ = BlockToOffsetSTFS(dir_entry.start_block_number());
        entry->data_size_ = dir_entry.length;
      }
      entry->size_ = dir_entry.length;
      entry->allocation_size_ = xe::round_up(dir_entry.length, kBlockSize);

      entry->create_timestamp_ =
          decode_fat_timestamp(dir_entry.create_date, dir_entry.create_time);
      entry->write_timestamp_ = decode_fat_timestamp(dir_entry.modified_date,
                                                     dir_entry.modified_time);
      entry->access_timestamp_ = entry->write_timestamp_;

      all_entries.push_back(entry.get());

      // Fill in all block records.
      // It's easier to do this now and just look them up later, at the cost
      // of some memory. Nasty chain walk.
      // TODO(benvanik): optimize if flags.contiguous is set.
      if (entry->attributes() & X_FILE_ATTRIBUTE_NORMAL) {
        uint32_t block_index = dir_entry.start_block_number();
        size_t remaining_size = dir_entry.length;
        while (remaining_size && block_index != kEndOfChain) {
          size_t block_size =
              std::min(static_cast<size_t>(kBlockSize), remaining_size);
          size_t offset = BlockToOffsetSTFS(block_index);
          entry->block_list_.push_back({0, offset, block_size});
          remaining_size -= block_size;
          auto block_hash = GetBlockHash(block_index);
          block_index = block_hash->level0_next_block();
        }

        if (remaining_size) {
          // Loop above must have exited prematurely, bad hash tables?
          XELOGW(
              "STFS file {} only found {} bytes for file, expected {} ({} "
              "bytes missing)",
              name, dir_entry.length - remaining_size, dir_entry.length,
              remaining_size);
          assert_always();
        }

        // Check that the number of blocks retrieved from hash entries matches
        // the block count read from the file entry
        if (entry->block_list_.size() != dir_entry.allocated_data_blocks()) {
          XELOGW(
              "STFS failed to read correct block-chain for entry {}, read {} "
              "blocks, expected {}",
              entry->name_, entry->block_list_.size(),
              dir_entry.allocated_data_blocks());
          assert_always();
        }
      }

      parent_entry->children_.emplace_back(std::move(entry));
    }

    auto block_hash = GetBlockHash(table_block_index);
    table_block_index = block_hash->level0_next_block();
    if (table_block_index == kEndOfChain) {
      break;
    }
  }

  if (n + 1 != descriptor.file_table_block_count) {
    XELOGW("STFS read {} file table blocks, but STFS headers expected {}!",
           n + 1, descriptor.file_table_block_count);
    assert_always();
  }

  return Error::kSuccess;
}

size_t StfsContainerDevice::BlockToOffsetSTFS(uint64_t block_index) const {
  // For every level there is a hash table
  // Level 0: hash table of next 170 blocks
  // Level 1: hash table of next 170 hash tables
  // Level 2: hash table of next 170 level 1 hash tables
  // And so on...
  uint64_t base = kBlocksPerHashLevel[0];
  uint64_t block = block_index;
  for (uint32_t i = 0; i < 3; i++) {
    block += ((block_index + base) / base) * blocks_per_hash_table_;
    if (block_index < base) {
      break;
    }

    base *= kBlocksPerHashLevel[0];
  }

  return xe::round_up(header_.header.header_size, kBlockSize) + (block << 12);
}

uint32_t StfsContainerDevice::BlockToHashBlockNumberSTFS(
    uint32_t block_index, uint32_t hash_level) const {
  uint32_t block = 0;
  if (hash_level == 0) {
    if (block_index < kBlocksPerHashLevel[0]) {
      return 0;
    }

    block = (block_index / kBlocksPerHashLevel[0]) * block_step[0];
    block +=
        ((block_index / kBlocksPerHashLevel[1]) + 1) * blocks_per_hash_table_;

    if (block_index < kBlocksPerHashLevel[1]) {
      return block;
    }

    return block + blocks_per_hash_table_;
  }

  if (hash_level == 1) {
    if (block_index < kBlocksPerHashLevel[1]) {
      return block_step[0];
    }

    block = (block_index / kBlocksPerHashLevel[1]) * block_step[1];
    return block + blocks_per_hash_table_;
  }

  // Level 2 is always at blockStep1
  return block_step[1];
}

size_t StfsContainerDevice::BlockToHashBlockOffsetSTFS(
    uint32_t block_index, uint32_t hash_level) const {
  uint64_t block = BlockToHashBlockNumberSTFS(block_index, hash_level);
  return xe::round_up(header_.header.header_size, kBlockSize) + (block << 12);
}

const StfsHashEntry* StfsContainerDevice::GetBlockHash(uint32_t block_index) {
  auto& file = files_.at(0);

  auto& descriptor = header_.metadata.volume_descriptor.stfs;

  // Offset for selecting the secondary hash block, in packages that have them
  uint32_t secondary_table_offset =
      descriptor.flags.bits.root_active_index ? kBlockSize : 0;

  auto hash_offset_lv0 = BlockToHashBlockOffsetSTFS(block_index, 0);
  if (!cached_hash_tables_.count(hash_offset_lv0)) {
    // If this is read_only_format then it doesn't contain secondary blocks, no
    // need to check upper hash levels
    if (descriptor.flags.bits.read_only_format) {
      secondary_table_offset = 0;
    } else {
      // Not a read-only package, need to check each levels active index flag to
      // see if we need to use secondary block or not

      // Check level1 table if package has it
      if (descriptor.total_block_count > kBlocksPerHashLevel[0]) {
        auto hash_offset_lv1 = BlockToHashBlockOffsetSTFS(block_index, 1);

        if (!cached_hash_tables_.count(hash_offset_lv1)) {
          // Check level2 table if package has it
          if (descriptor.total_block_count > kBlocksPerHashLevel[1]) {
            auto hash_offset_lv2 = BlockToHashBlockOffsetSTFS(block_index, 2);

            if (!cached_hash_tables_.count(hash_offset_lv2)) {
              xe::filesystem::Seek(
                  file, hash_offset_lv2 + secondary_table_offset, SEEK_SET);

              StfsHashTable table_lv2;
              if (fread(&table_lv2, sizeof(StfsHashTable), 1, file) != 1) {
                XELOGE("GetBlockHash failed to read level2 hash table at 0x{X}",
                       hash_offset_lv2 + secondary_table_offset);
                return nullptr;
              }
              cached_hash_tables_[hash_offset_lv2] = table_lv2;
            }

            auto record =
                (block_index / kBlocksPerHashLevel[1]) % kBlocksPerHashLevel[0];
            auto record_data =
                &cached_hash_tables_[hash_offset_lv2].entries[record];
            secondary_table_offset =
                record_data->levelN_active_index() ? kBlockSize : 0;
          }

          xe::filesystem::Seek(file, hash_offset_lv1 + secondary_table_offset,
                               SEEK_SET);

          StfsHashTable table_lv1;
          if (fread(&table_lv1, sizeof(StfsHashTable), 1, file) != 1) {
            XELOGE("GetBlockHash failed to read level1 hash table at 0x{X}",
                   hash_offset_lv1 + secondary_table_offset);
            return nullptr;
          }
          cached_hash_tables_[hash_offset_lv1] = table_lv1;
        }

        auto record =
            (block_index / kBlocksPerHashLevel[0]) % kBlocksPerHashLevel[0];
        auto record_data =
            &cached_hash_tables_[hash_offset_lv1].entries[record];
        secondary_table_offset =
            record_data->levelN_active_index() ? kBlockSize : 0;
      }
    }

    xe::filesystem::Seek(file, hash_offset_lv0 + secondary_table_offset,
                         SEEK_SET);

    StfsHashTable table_lv0;
    if (fread(&table_lv0, sizeof(StfsHashTable), 1, file) != 1) {
      XELOGE("GetBlockHash failed to read level0 hash table at 0x{X}",
             hash_offset_lv0 + secondary_table_offset);
      return nullptr;
    }
    cached_hash_tables_[hash_offset_lv0] = table_lv0;
  }

  auto record = block_index % kBlocksPerHashLevel[0];
  auto record_data = &cached_hash_tables_[hash_offset_lv0].entries[record];

  return record_data;
}

XContentPackageType StfsContainerDevice::ReadMagic(
    const std::filesystem::path& path) {
  auto map = MappedMemory::Open(path, MappedMemory::Mode::kRead, 0, 4);
  return XContentPackageType(xe::load_and_swap<uint32_t>(map->data()));
}

bool StfsContainerDevice::ResolveFromFolder(const std::filesystem::path& path) {
  // Scan through folders until a file with magic is found
  std::queue<filesystem::FileInfo> queue;

  filesystem::FileInfo folder;
  filesystem::GetInfo(host_path_, &folder);
  queue.push(folder);

  while (!queue.empty()) {
    auto current_file = queue.front();
    queue.pop();

    if (current_file.type == filesystem::FileInfo::Type::kDirectory) {
      auto path = current_file.path / current_file.name;
      auto child_files = filesystem::ListFiles(path);
      for (auto file : child_files) {
        queue.push(file);
      }
    } else {
      // Try to read the file's magic
      auto path = current_file.path / current_file.name;
      auto magic = ReadMagic(path);

      if (magic == XContentPackageType::kCon ||
          magic == XContentPackageType::kLive ||
          magic == XContentPackageType::kPirs) {
        host_path_ = current_file.path / current_file.name;
        XELOGI("STFS Package found: {}", xe::path_to_utf8(host_path_));
        return true;
      }
    }
  }

  if (host_path_ == path) {
    // Could not find a suitable container file
    return false;
  }
  return true;
}

kernel::xam::XCONTENT_AGGREGATE_DATA StfsContainerDevice::content_header() const {
  kernel::xam::XCONTENT_AGGREGATE_DATA data;

  std::memset(&data, 0, sizeof(kernel::xam::XCONTENT_AGGREGATE_DATA));

  data.device_id = 1;
  data.title_id = header_.metadata.execution_info.title_id;
  data.content_type = header_.metadata.content_type;

  auto name = header_.metadata.display_name(XLanguage::kEnglish);
  if (name.empty()) {
    // Find first filled language and use it. It might be incorrect, but meh
    // until stfs support is done.
    for (uint8_t i = 0; i < header_.metadata.kNumLanguagesV2; i++) {
      name = header_.metadata.display_name((XLanguage)i);
      if (!name.empty()) {
        break;
      }
    }
  }

  data.set_display_name(name);

  return data;
}

}  // namespace vfs
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/kernel/xam/content_manager.h"
#include "xenia/vfs/devices/xcontent_container_entry.h"
#include "xenia/vfs/devices/xcontent_devices/stfs_container_device.h"

namespace xe {
namespace vfs {

StfsContainerDevice::StfsContainerDevice(const std::string_view mount_path,
                                         const std::filesystem::path& host_path)
    : XContentContainerDevice(mount_path, host_path),
      blocks_per_hash_table_(1),
      block_step_{0, 0} {
  SetName("STFS");
}

StfsContainerDevice::~StfsContainerDevice() { CloseFiles(); }

void StfsContainerDevice::SetupContainer() {
  // Additional part specific to STFS container.
  const XContentContainerHeader* header = GetContainerHeader();
  blocks_per_hash_table_ = header->is_package_readonly() ? 1 : 2;

  block_step_[0] = kBlocksPerHashLevel[0] + blocks_per_hash_table_;
  block_step_[1] = kBlocksPerHashLevel[1] +
                   ((kBlocksPerHashLevel[0] + 1) * blocks_per_hash_table_);
}

XContentContainerDevice::Result StfsContainerDevice::LoadHostFiles(
    FILE* header_file) {
  const XContentContainerHeader* header = GetContainerHeader();

  if (header->content_metadata.data_file_count > 0) {
    XELOGW("STFS container is not a single file. Loading might fail!");
  }

  files_.emplace(std::make_pair(0, header_file));
  return Result::kSuccess;
}

StfsContainerDevice::Result StfsContainerDevice::Read() {
  auto& file = files_.at(0);

  auto root_entry = new XContentContainerEntry(this, nullptr, "", &files_);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  std::vector<XContentContainerEntry*> all_entries;

  // Load all listings.
  StfsDirectoryBlock directory;

  auto& descriptor =
      GetContainerHeader()->content_metadata.volume_descriptor.stfs;
  uint32_t table_block_index = descriptor.file_table_block_number();
  size_t n = 0;
  for (n = 0; n < descriptor.file_table_block_count; n++) {
    const size_t offset = BlockToOffset(table_block_index);
    xe::filesystem::Seek(file, offset, SEEK_SET);

    if (fread(&directory, sizeof(StfsDirectoryBlock), 1, file) != 1) {
      XELOGE("ReadSTFS failed to read directory block at 0x{X}", offset);
      return Result::kReadError;
    }

    for (size_t m = 0; m < kEntriesPerDirectoryBlock; m++) {
      const StfsDirectoryEntry& dir_entry = directory.entries[m];

      if (dir_entry.name[0] == 0) {
        // Done.
        break;
      }

      XContentContainerEntry* parent_entry =
          dir_entry.directory_index == 0xFFFF
              ? root_entry
              : all_entries[dir_entry.directory_index];

      std::unique_ptr<XContentContainerEntry> entry =
          ReadEntry(parent_entry, &files_, &dir_entry);
      all_entries.push_back(entry.get());
      parent_entry->children_.emplace_back(std::move(entry));
    }

    const StfsHashEntry* block_hash = GetBlockHash(table_block_index);
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

  return Result::kSuccess;
}

std::unique_ptr<XContentContainerEntry> StfsContainerDevice::ReadEntry(
    Entry* parent, MultiFileHandles* files,
    const StfsDirectoryEntry* dir_entry) {
  // Filename is stored as Windows-1252, convert it to UTF-8.
  std::string ansi_name(reinterpret_cast<const char*>(dir_entry->name),
                        dir_entry->flags.name_length & 0x3F);
  std::string name = xe::win1252_to_utf8(ansi_name);

  auto entry = XContentContainerEntry::Create(this, parent, name, &files_);

  if (dir_entry->flags.directory) {
    entry->attributes_ = kFileAttributeDirectory;
  } else {
    entry->attributes_ = kFileAttributeNormal | kFileAttributeReadOnly;
    entry->data_offset_ = BlockToOffset(dir_entry->start_block_number());
    entry->data_size_ = dir_entry->length;
  }
  entry->size_ = dir_entry->length;
  entry->allocation_size_ = xe::round_up(dir_entry->length, kBlockSize);

  entry->create_timestamp_ =
      decode_fat_timestamp(dir_entry->create_date, dir_entry->create_time);
  entry->write_timestamp_ =
      decode_fat_timestamp(dir_entry->modified_date, dir_entry->modified_time);
  entry->access_timestamp_ = entry->write_timestamp_;

  // Fill in all block records.
  // It's easier to do this now and just look them up later, at the cost
  // of some memory. Nasty chain walk.
  // TODO(benvanik): optimize if flags.contiguous is set.
  if (entry->attributes() & X_FILE_ATTRIBUTE_NORMAL) {
    uint32_t block_index = dir_entry->start_block_number();
    size_t remaining_size = dir_entry->length;
    while (remaining_size && block_index != kEndOfChain) {
      size_t block_size =
          std::min(static_cast<size_t>(kBlockSize), remaining_size);
      size_t offset = BlockToOffset(block_index);
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
          name, dir_entry->length.get() - remaining_size,
          dir_entry->length.get(), remaining_size);
      assert_always();
    }

    // Check that the number of blocks retrieved from hash entries matches
    // the block count read from the file entry
    if (entry->block_list_.size() != dir_entry->allocated_data_blocks()) {
      XELOGW(
          "STFS failed to read correct block-chain for entry {}, read {} "
          "blocks, expected {}",
          entry->name_, entry->block_list_.size(),
          dir_entry->allocated_data_blocks());
      assert_always();
    }
  }

  return entry;
}

size_t StfsContainerDevice::BlockToOffset(uint64_t block_index) const {
  // For every level there is a hash table
  // Level 0: hash table of next 170 blocks
  // Level 1: hash table of next 170 hash tables
  // Level 2: hash table of next 170 level 1 hash tables
  // And so on...
  uint64_t block = block_index;
  for (uint32_t i = 0; i < kBlocksHashLevelAmount; i++) {
    const uint32_t level_base = kBlocksPerHashLevel[i];
    block += ((block_index + level_base) / level_base) * blocks_per_hash_table_;
    if (block_index < level_base) {
      break;
    }
  }

  return xe::round_up(GetContainerHeader()->content_header.header_size,
                      kBlockSize) +
         (block << 12);
}

uint32_t StfsContainerDevice::BlockToHashBlockNumber(
    uint32_t block_index, uint32_t hash_level) const {
  if (hash_level == 2) {
    return block_step_[1];
  }

  if (block_index < kBlocksPerHashLevel[hash_level]) {
    return hash_level == 0 ? 0 : block_step_[hash_level - 1];
  }

  uint32_t block =
      (block_index / kBlocksPerHashLevel[hash_level]) * block_step_[hash_level];

  if (hash_level == 0) {
    block +=
        ((block_index / kBlocksPerHashLevel[1]) + 1) * blocks_per_hash_table_;

    if (block_index < kBlocksPerHashLevel[1]) {
      return block;
    }
  }

  return block + blocks_per_hash_table_;
}

size_t StfsContainerDevice::BlockToHashBlockOffset(uint32_t block_index,
                                                   uint32_t hash_level) const {
  const uint64_t block = BlockToHashBlockNumber(block_index, hash_level);
  return xe::round_up(header_->content_header.header_size, kBlockSize) +
         (block << 12);
}

const uint8_t StfsContainerDevice::GetAmountOfHashLevelsToCheck(
    uint32_t total_block_count) const {
  for (uint8_t level = 0; level < kBlocksHashLevelAmount; level++) {
    if (total_block_count < kBlocksPerHashLevel[level]) {
      return level;
    }
  }
  XELOGE("GetAmountOfHashLevelsToCheck - Invalid total_block_count: {}",
         total_block_count);
  return 0;
}

void StfsContainerDevice::UpdateCachedHashTable(
    uint32_t block_index, uint8_t hash_level,
    uint32_t& secondary_table_offset) {
  const size_t hash_offset = BlockToHashBlockOffset(block_index, hash_level);
  // Do nothing. It's already there.
  if (!cached_hash_tables_.count(hash_offset)) {
    auto& file = files_.at(0);
    xe::filesystem::Seek(file, hash_offset + secondary_table_offset, SEEK_SET);
    StfsHashTable table;
    if (fread(&table, sizeof(StfsHashTable), 1, file) != 1) {
      XELOGE("GetBlockHash failed to read level{} hash table at 0x{:08X}",
             hash_level, hash_offset + secondary_table_offset);
      return;
    }
    cached_hash_tables_[hash_offset] = table;
  }

  uint32_t record = block_index % kBlocksPerHashLevel[0];
  if (hash_level >= 1) {
    record = (block_index / kBlocksPerHashLevel[hash_level - 1]) %
             kBlocksPerHashLevel[0];
  }
  const StfsHashEntry* record_data =
      &cached_hash_tables_[hash_offset].entries[record];
  secondary_table_offset = record_data->levelN_active_index() ? kBlockSize : 0;
}

void StfsContainerDevice::UpdateCachedHashTables(
    uint32_t block_index, uint8_t highest_hash_level_to_update,
    uint32_t& secondary_table_offset) {
  for (int8_t level = highest_hash_level_to_update; level >= 0; level--) {
    UpdateCachedHashTable(block_index, level, secondary_table_offset);
  }
}

const StfsHashEntry* StfsContainerDevice::GetBlockHash(uint32_t block_index) {
  auto& file = files_.at(0);

  const StfsVolumeDescriptor& descriptor =
      header_->content_metadata.volume_descriptor.stfs;

  // Offset for selecting the secondary hash block, in packages that have them
  uint32_t secondary_table_offset =
      descriptor.flags.bits.root_active_index ? kBlockSize : 0;

  uint8_t hash_levels_to_process =
      GetAmountOfHashLevelsToCheck(descriptor.total_block_count);

  if (header_->is_package_readonly()) {
    secondary_table_offset = 0;
    // Because we have read only package we only need to check first hash level.
    hash_levels_to_process = 0;
  }

  UpdateCachedHashTables(block_index, hash_levels_to_process,
                         secondary_table_offset);

  const size_t hash_offset = BlockToHashBlockOffset(block_index, 0);
  const uint32_t record = block_index % kBlocksPerHashLevel[0];
  return &cached_hash_tables_[hash_offset].entries[record];
}

const uint8_t StfsContainerDevice::GetBlocksPerHashTableFromContainerHeader()
    const {
  const XContentContainerHeader* header = GetContainerHeader();
  if (!header) {
    XELOGE(
        "VFS: SetBlocksPerHashTableBasedOnContainerHeader - Missing "
        "Container "
        "Header!");
    return 0;
  }

  if (header->is_package_readonly()) {
    return 1;
  }

  return 2;
}

}  // namespace vfs
}  // namespace xe

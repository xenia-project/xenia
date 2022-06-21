/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_XCONTENT_DEVICES_STFS_CONTAINER_DEVICE_H_
#define XENIA_VFS_DEVICES_XCONTENT_DEVICES_STFS_CONTAINER_DEVICE_H_

#include <string>
#include <unordered_map>

#include "xenia/base/string_util.h"
#include "xenia/kernel/util/xex2_info.h"
#include "xenia/vfs/device.h"
#include "xenia/vfs/devices/stfs_xbox.h"
#include "xenia/vfs/devices/xcontent_container_device.h"
#include "xenia/vfs/devices/xcontent_container_entry.h"

namespace xe {
namespace vfs {

// https://free60project.github.io/wiki/STFS.html

class StfsContainerDevice : public XContentContainerDevice {
 public:
  StfsContainerDevice(const std::string_view mount_path,
                      const std::filesystem::path& host_path);
  ~StfsContainerDevice() override;

  bool is_read_only() const override {
    return GetContainerHeader()
        ->content_metadata.volume_descriptor.stfs.flags.bits.read_only_format;
  }

  uint32_t component_name_max_length() const override { return 40; }

  uint32_t total_allocation_units() const override {
    return GetContainerHeader()
        ->content_metadata.volume_descriptor.stfs.total_block_count;
  }
  uint32_t available_allocation_units() const override {
    if (!is_read_only()) {
      auto& descriptor =
          GetContainerHeader()->content_metadata.volume_descriptor.stfs;
      return kBlocksPerHashLevel[2] -
             (descriptor.total_block_count - descriptor.free_block_count);
    }
    return 0;
  }

 private:
  static const uint8_t kBlocksHashLevelAmount = 3;
  const uint32_t kBlocksPerHashLevel[kBlocksHashLevelAmount] = {170, 28900,
                                                                4913000};
  const uint32_t kEndOfChain = 0xFFFFFF;
  const uint32_t kEntriesPerDirectoryBlock =
      kBlockSize / sizeof(StfsDirectoryEntry);

  void SetupContainer() override;
  Result LoadHostFiles(FILE* header_file) override;

  Result Read() override;
  std::unique_ptr<XContentContainerEntry> ReadEntry(
      Entry* parent, MultiFileHandles* files,
      const StfsDirectoryEntry* dir_entry);

  size_t BlockToOffset(uint64_t block_index) const;
  uint32_t BlockToHashBlockNumber(uint32_t block_index,
                                  uint32_t hash_level) const;
  size_t BlockToHashBlockOffset(uint32_t block_index,
                                uint32_t hash_level) const;
  const uint8_t GetAmountOfHashLevelsToCheck(uint32_t total_block_count) const;

  const StfsHashEntry* GetBlockHash(uint32_t block_index);
  void UpdateCachedHashTable(uint32_t block_index, uint8_t hash_level,
                             uint32_t& secondary_table_offset);
  void UpdateCachedHashTables(uint32_t block_index,
                              uint8_t highest_hash_level_to_update,
                              uint32_t& secondary_table_offset);
  const uint8_t GetBlocksPerHashTableFromContainerHeader() const;

  uint8_t blocks_per_hash_table_;
  uint32_t block_step_[2];

  std::unordered_map<size_t, StfsHashTable> cached_hash_tables_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_XCONTENT_DEVICES_STFS_CONTAINER_DEVICE_H_

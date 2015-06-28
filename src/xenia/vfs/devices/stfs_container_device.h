/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_STFS_CONTAINER_DEVICE_H_
#define XENIA_VFS_DEVICES_STFS_CONTAINER_DEVICE_H_

#include <memory>
#include <string>

#include "xenia/base/mapped_memory.h"
#include "xenia/vfs/device.h"

namespace xe {
namespace vfs {

// http://www.free60.org/STFS

enum class StfsPackageType {
  STFS_PACKAGE_CON,
  STFS_PACKAGE_PIRS,
  STFS_PACKAGE_LIVE,
};

enum STFSContentType : uint32_t {
  STFS_CONTENT_ARCADE_TITLE = 0x000D0000,
  STFS_CONTENT_AVATAR_ITEM = 0x00009000,
  STFS_CONTENT_CACHE_FILE = 0x00040000,
  STFS_CONTENT_COMMUNITY_GAME = 0x02000000,
  STFS_CONTENT_GAME_DEMO = 0x00080000,
  STFS_CONTENT_GAMER_PICTURE = 0x00020000,
  STFS_CONTENT_GAME_TITLE = 0x000A0000,
  STFS_CONTENT_GAME_TRAILER = 0x000C0000,
  STFS_CONTENT_GAME_VIDEO = 0x00400000,
  STFS_CONTENT_INSTALLED_GAME = 0x00004000,
  STFS_CONTENT_INSTALLER = 0x000B0000,
  STFS_CONTENT_IPTV_PAUSE_BUFFER = 0x00002000,
  STFS_CONTENT_LICENSE_STORE = 0x000F0000,
  STFS_CONTENT_MARKETPLACE_CONTENT = 0x00000002,
  STFS_CONTENT_MOVIE = 0x00100000,
  STFS_CONTENT_MUSIC_VIDEO = 0x00300000,
  STFS_CONTENT_PODCAST_VIDEO = 0x00500000,
  STFS_CONTENT_PROFILE = 0x00010000,
  STFS_CONTENT_PUBLISHER = 0x00000003,
  STFS_CONTENT_SAVED_GAME = 0x00000001,
  STFS_CONTENT_STORAGE_DOWNLOAD = 0x00050000,
  STFS_CONTENT_THEME = 0x00030000,
  STFS_CONTENT_TV = 0x00200000,
  STFS_CONTENT_VIDEO = 0x00090000,
  STFS_CONTENT_VIRAL_VIDEO = 0x00600000,
  STFS_CONTENT_XBOX_DOWNLOAD = 0x00070000,
  STFS_CONTENT_XBOX_ORIGINAL_GAME = 0x00005000,
  STFS_CONTENT_XBOX_SAVED_GAME = 0x00060000,
  STFS_CONTENT_XBOX_360_TITLE = 0x00001000,
  STFS_CONTENT_XBOX_TITLE = 0x00005000,
  STFS_CONTENT_XNA = 0x000E0000,
};

enum class StfsPlatform : uint8_t {
  STFS_PLATFORM_XBOX_360 = 0x02,
  STFS_PLATFORM_PC = 0x04,
};

enum class StfsDescriptorType : uint32_t {
  STFS_DESCRIPTOR_STFS = 0,
  STFS_DESCRIPTOR_SVOD = 1,
};

struct StfsVolumeDescriptor {
  bool Read(const uint8_t* p);

  uint8_t descriptor_size;
  uint8_t reserved;
  uint8_t block_separation;
  uint16_t file_table_block_count;
  uint32_t file_table_block_number;
  uint8_t top_hash_table_hash[0x14];
  uint32_t total_allocated_block_count;
  uint32_t total_unallocated_block_count;
};

class StfsHeader {
 public:
  bool Read(const uint8_t* p);

  uint8_t license_entries[0x100];
  uint8_t header_hash[0x14];
  uint32_t header_size;
  STFSContentType content_type;
  uint32_t metadata_version;
  uint64_t content_size;
  uint32_t media_id;
  uint32_t version;
  uint32_t base_version;
  uint32_t title_id;
  StfsPlatform platform;
  uint8_t executable_type;
  uint8_t disc_number;
  uint8_t disc_in_set;
  uint32_t save_game_id;
  uint8_t console_id[0x5];
  uint8_t profile_id[0x8];
  StfsVolumeDescriptor volume_descriptor;
  uint32_t data_file_count;
  uint64_t data_file_combined_size;
  StfsDescriptorType descriptor_type;
  uint8_t device_id[0x14];
  wchar_t display_names[0x900 / 2];
  wchar_t display_descs[0x900 / 2];
  wchar_t publisher_name[0x80 / 2];
  wchar_t title_name[0x80 / 2];
  uint8_t transfer_flags;
  uint32_t thumbnail_image_size;
  uint32_t title_thumbnail_image_size;
  uint8_t thumbnail_image[0x4000];
  uint8_t title_thumbnail_image[0x4000];
};

class STFSContainerDevice : public Device {
 public:
  STFSContainerDevice(const std::string& mount_path,
                      const std::wstring& local_path);
  ~STFSContainerDevice() override;

  bool Initialize() override;

  uint32_t total_allocation_units() const override {
    return uint32_t(mmap_->size() / sectors_per_allocation_unit() /
                    bytes_per_sector());
  }
  uint32_t available_allocation_units() const override { return 0; }
  uint32_t sectors_per_allocation_unit() const override { return 1; }
  uint32_t bytes_per_sector() const override { return 4 * 1024; }

 private:
  enum class Error {
    kSuccess = 0,
    kErrorOutOfMemory = -1,
    kErrorReadError = -10,
    kErrorFileMismatch = -30,
    kErrorDamagedFile = -31,
  };

  struct BlockHash {
    uint32_t next_block_index;
    uint32_t info;
  };

  Error ReadHeaderAndVerify(const uint8_t* map_ptr);
  Error ReadAllEntries(const uint8_t* map_ptr);
  size_t BlockToOffset(uint32_t block);
  uint32_t ComputeBlockNumber(uint32_t block_index);

  BlockHash GetBlockHash(const uint8_t* map_ptr, uint32_t block_index,
                         uint32_t table_offset);

  std::wstring local_path_;
  std::unique_ptr<MappedMemory> mmap_;

  StfsPackageType package_type_;
  StfsHeader header_;
  uint32_t table_size_shift_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_STFS_CONTAINER_DEVICE_H_

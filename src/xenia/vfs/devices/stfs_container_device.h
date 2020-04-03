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

#include <map>
#include <memory>
#include <string>

#include "xenia/base/mapped_memory.h"
#include "xenia/vfs/device.h"

namespace xe {
namespace vfs {

// https://free60project.github.io/wiki/STFS.html

class StfsContainerEntry;

enum class StfsPackageType {
  kCon,
  kPirs,
  kLive,
};

enum class StfsContentType : uint32_t {
  kArcadeTitle = 0x000D0000,
  kAvatarItem = 0x00009000,
  kCacheFile = 0x00040000,
  kCommunityGame = 0x02000000,
  kGamesOnDemand = 0x00007000,
  kGameDemo = 0x00080000,
  kGamerPicture = 0x00020000,
  kGameTitle = 0x000A0000,
  kGameTrailer = 0x000C0000,
  kGameVideo = 0x00400000,
  kInstalledGame = 0x00004000,
  kInstaller = 0x000B0000,
  kIptvPauseBuffer = 0x00002000,
  kLicenseStore = 0x000F0000,
  kMarketplaceContent = 0x00000002,
  kMovie = 0x00100000,
  kMusicVideo = 0x00300000,
  kPodcastVideo = 0x00500000,
  kProfile = 0x00010000,
  kPublisher = 0x00000003,
  kSavedGame = 0x00000001,
  kStorageDownload = 0x00050000,
  kTheme = 0x00030000,
  kTV = 0x00200000,
  kVideo = 0x00090000,
  kViralVideo = 0x00600000,
  kXboxDownload = 0x00070000,
  kXboxOriginalGame = 0x00005000,
  kXboxSavedGame = 0x00060000,
  kXbox360Title = 0x00001000,
  kXboxTitle = 0x00005000,
  kXNA = 0x000E0000,
};

enum class StfsPlatform : uint8_t {
  kXbox360 = 0x02,
  kPc = 0x04,
};

enum class StfsDescriptorType : uint32_t {
  kStfs = 0,
  kSvod = 1,
};

struct StfsVolumeDescriptor {
  bool Read(const uint8_t* p);

  uint8_t descriptor_size;
  uint8_t version;
  uint8_t flags;
  uint16_t file_table_block_count;
  uint32_t file_table_block_number;
  uint8_t top_hash_table_hash[0x14];
  uint32_t total_allocated_block_count;
  uint32_t total_unallocated_block_count;
};

enum SvodDeviceFeatures {
  kFeatureHasEnhancedGDFLayout = 0x40,
};

enum SvodLayoutType {
  kUnknownLayout = 0x0,
  kEnhancedGDFLayout = 0x1,
  kXSFLayout = 0x2,
  kSingleFileLayout = 0x4,
};

struct SvodVolumeDescriptor {
  bool Read(const uint8_t* p);

  uint8_t descriptor_size;
  uint8_t block_cache_element_count;
  uint8_t worker_thread_processor;
  uint8_t worker_thread_priority;
  uint8_t hash[0x14];
  uint8_t device_features;
  uint32_t data_block_count;
  uint32_t data_block_offset;
  // 0x5 padding bytes...

  SvodLayoutType layout_type;
};

class StfsHeader {
 public:
  static const uint32_t kHeaderLength = 0xA000;

  bool Read(const uint8_t* p);

  uint8_t license_entries[0x100];
  uint8_t header_hash[0x14];
  uint32_t header_size;
  StfsContentType content_type;
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
  union {
    StfsVolumeDescriptor stfs_volume_descriptor;
    SvodVolumeDescriptor svod_volume_descriptor;
  };
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

  // Metadata v2 Fields
  uint8_t series_id[0x10];
  uint8_t season_id[0x10];
  int16_t season_number;
  int16_t episode_number;
  wchar_t additonal_display_names[0x300 / 2];
  wchar_t additional_display_descriptions[0x300 / 2];
};

class StfsContainerDevice : public Device {
 public:
  StfsContainerDevice(const std::string& mount_path,
                      const std::wstring& local_path);
  ~StfsContainerDevice() override;

  bool Initialize() override;
  void Dump(StringBuffer* string_buffer) override;
  Entry* ResolvePath(const std::string& path) override;

  uint32_t total_allocation_units() const override {
    return uint32_t(mmap_total_size_ / sectors_per_allocation_unit() /
                    bytes_per_sector());
  }
  uint32_t available_allocation_units() const override { return 0; }
  uint32_t sectors_per_allocation_unit() const override { return 1; }
  uint32_t bytes_per_sector() const override { return 4 * 1024; }

  StfsHeader& header() { return header_; }

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

  const uint32_t kSTFSHashSpacing = 170;

  bool ResolveFromFolder(const std::wstring& path);

  Error MapFiles();
  static Error ReadPackageType(const uint8_t* map_ptr, size_t map_size,
                               StfsPackageType* package_type_out);
  Error ReadHeaderAndVerify(const uint8_t* map_ptr, size_t map_size);

  Error ReadSVOD();
  Error ReadEntrySVOD(uint32_t sector, uint32_t ordinal,
                      StfsContainerEntry* parent);
  void BlockToOffsetSVOD(size_t sector, size_t* address, size_t* file_index);

  Error ReadSTFS();
  size_t BlockToOffsetSTFS(uint64_t block);

  BlockHash GetBlockHash(const uint8_t* map_ptr, uint32_t block_index,
                         uint32_t table_offset);

  std::wstring local_path_;
  std::map<size_t, std::unique_ptr<MappedMemory>> mmap_;
  size_t mmap_total_size_;

  size_t base_offset_;
  size_t magic_offset_;
  std::unique_ptr<Entry> root_entry_;
  StfsPackageType package_type_;
  StfsHeader header_;
  uint32_t table_size_shift_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_STFS_CONTAINER_DEVICE_H_

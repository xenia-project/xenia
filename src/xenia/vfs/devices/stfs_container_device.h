/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_STFS_CONTAINER_DEVICE_H_
#define XENIA_VFS_DEVICES_STFS_CONTAINER_DEVICE_H_

#include <map>
#include <memory>
#include <string>

#include "xenia/base/mapped_memory.h"
#include "xenia/base/math.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/util/xex2_info.h"
#include "xenia/vfs/device.h"

namespace xe {
namespace vfs {

// https://free60project.github.io/wiki/STFS.html

class StfsContainerEntry;

enum XContentPackageType : uint32_t {
  kPackageTypeCon = 0x434F4E20,
  kPackageTypePirs = 0x50495253,
  kPackageTypeLive = 0x4C495645,
};

enum XContentType : uint32_t {
  kSavedGame = 0x00000001,
  kMarketplaceContent = 0x00000002,
  kPublisher = 0x00000003,
  kXbox360Title = 0x00001000,
  kIptvPauseBuffer = 0x00002000,
  kXNACommunity = 0x00003000,
  kInstalledGame = 0x00004000,
  kXboxTitle = 0x00005000,
  kSocialTitle = 0x00006000,
  kGamesOnDemand = 0x00007000,
  kSUStoragePack = 0x00008000,
  kAvatarItem = 0x00009000,
  kProfile = 0x00010000,
  kGamerPicture = 0x00020000,
  kTheme = 0x00030000,
  kCacheFile = 0x00040000,
  kStorageDownload = 0x00050000,
  kXboxSavedGame = 0x00060000,
  kXboxDownload = 0x00070000,
  kGameDemo = 0x00080000,
  kVideo = 0x00090000,
  kGameTitle = 0x000A0000,
  kInstaller = 0x000B0000,
  kGameTrailer = 0x000C0000,
  kArcadeTitle = 0x000D0000,
  kXNA = 0x000E0000,
  kLicenseStore = 0x000F0000,
  kMovie = 0x00100000,
  kTV = 0x00200000,
  kMusicVideo = 0x00300000,
  kGameVideo = 0x00400000,
  kPodcastVideo = 0x00500000,
  kViralVideo = 0x00600000,
  kCommunityGame = 0x02000000,
};

enum class XContentVolumeType : uint32_t {
  kStfs = 0,
  kSvod = 1,
};

/* STFS structures */
XEPACKEDSTRUCT(StfsVolumeDescriptor, {
  uint8_t descriptor_length;
  uint8_t version;
  union {
    struct {
      uint8_t read_only_format : 1;  // if set, only uses a single backing-block
                                     // per hash table (no resiliency),
                                     // otherwise uses two
      uint8_t root_active_index : 1;  // if set, uses secondary backing-block
                                      // for the highest-level hash table
      uint8_t directory_overallocated : 1;
      uint8_t directory_index_bounds_valid : 1;
    };
    uint8_t as_byte;
  } flags;
  uint16_t file_table_block_count;
  uint8_t file_table_block_number_0;
  uint8_t file_table_block_number_1;
  uint8_t file_table_block_number_2;
  uint8_t top_hash_table_hash[0x14];
  be<uint32_t> allocated_block_count;
  be<uint32_t> free_block_count;

  uint32_t file_table_block_number() {
    return uint32_t(file_table_block_number_0) |
           (uint32_t(file_table_block_number_1) << 8) |
           (uint32_t(file_table_block_number_2) << 16);
  }
});
static_assert_size(StfsVolumeDescriptor, 0x24);

struct StfsHashEntry {
  uint8_t sha1[0x14];

  uint8_t info0;  // usually contains flags

  uint8_t info1;
  uint8_t info2;
  uint8_t info3;

  // If this is a level0 entry, this points to the next block in the chain
  uint32_t level0_next_block() const {
    return uint32_t(info3) | (uint32_t(info2) << 8) | (uint32_t(info1) << 16);
  }

  void level0_next_block(uint32_t value) {
    info3 = uint8_t(value & 0xFF);
    info2 = uint8_t((value >> 8) & 0xFF);
    info1 = uint8_t((value >> 16) & 0xFF);
  }

  // If this is level 1 or 2, this says whether the hash table this entry refers
  // to is using the secondary block or not
  bool levelN_activeindex() const { return info0 & 0x40; }

  bool levelN_writeable() const { return info0 & 0x80; }
};
static_assert_size(StfsHashEntry, 0x18);

struct StfsHashTable {
  StfsHashEntry entries[170];
  xe::be<uint32_t> num_blocks;  // num L0 blocks covered by this table?
  uint8_t padding[12];
};
static_assert_size(StfsHashTable, 0x1000);

/* SVOD structures */
struct SvodDeviceDescriptor {
  uint8_t descriptor_length;
  uint8_t block_cache_element_count;
  uint8_t worker_thread_processor;
  uint8_t worker_thread_priority;
  uint8_t first_fragment_hash_entry[0x14];
  union {
    struct {
      uint8_t must_be_zero_for_future_usage : 6;
      uint8_t enhanced_gdf_layout : 1;
      uint8_t zero_for_downlevel_clients : 1;
    };
    uint8_t as_byte;
  } features;
  uint8_t num_data_blocks2;
  uint8_t num_data_blocks1;
  uint8_t num_data_blocks0;
  uint8_t start_data_block0;
  uint8_t start_data_block1;
  uint8_t start_data_block2;
  uint8_t reserved[5];

  uint32_t num_data_blocks() {
    return uint32_t(num_data_blocks0) | (uint32_t(num_data_blocks1) << 8) |
           (uint32_t(num_data_blocks2) << 16);
  }

  uint32_t start_data_block() {
    return uint32_t(start_data_block0) | (uint32_t(start_data_block1) << 8) |
           (uint32_t(start_data_block2) << 16);
  }
};
static_assert_size(SvodDeviceDescriptor, 0x24);

/* XContent structures */
struct XContentMediaData {
  uint8_t series_id[0x10];
  uint8_t season_id[0x10];
  be<uint16_t> season_number;
  be<uint16_t> episode_number;
};
static_assert_size(XContentMediaData, 0x24);

struct XContentAvatarAssetData {
  be<uint32_t> sub_category;
  be<uint32_t> colorizable;
  uint8_t asset_id[0x10];
  uint8_t skeleton_version_mask;
  uint8_t reserved[0xB];
};
static_assert_size(XContentAvatarAssetData, 0x24);

struct XContentAttributes {
  uint8_t profile_transfer : 1;
  uint8_t device_transfer : 1;
  uint8_t move_only_transfer : 1;
  uint8_t kinect_enabled : 1;
  uint8_t disable_network_storage : 1;
  uint8_t deep_link_supported : 1;
  uint8_t reserved : 2;
};
static_assert_size(XContentAttributes, 1);

XEPACKEDSTRUCT(XContentMetadata, {
  static const uint32_t kThumbLengthV1 = 0x4000;
  static const uint32_t kThumbLengthV2 = 0x3D00;

  static const uint32_t kNumLanguagesV1 = 9;
  // metadata_version 2 adds 3 languages inside thumbnail/title_thumbnail space
  static const uint32_t kNumLanguagesV2 = 12;

  be<XContentType> content_type;
  be<uint32_t> metadata_version;
  be<uint64_t> content_size;
  xex2_opt_execution_info execution_info;
  uint8_t console_id[5];
  be<uint64_t> profile_id;
  union {
    StfsVolumeDescriptor stfs_volume_descriptor;
    SvodDeviceDescriptor svod_volume_descriptor;
  };
  be<uint32_t> data_file_count;
  be<uint64_t> data_file_size;
  be<XContentVolumeType> volume_type;
  be<uint64_t> online_creator;
  be<uint32_t> category;
  uint8_t reserved2[0x20];
  union {
    XContentMediaData media_data;
    XContentAvatarAssetData avatar_asset_data;
  };
  uint8_t device_id[0x14];
  union {
    be<uint16_t> display_name_raw[kNumLanguagesV1][128];
    char16_t display_name_chars[kNumLanguagesV1][128];
  };
  union {
    be<uint16_t> description_raw[kNumLanguagesV1][128];
    char16_t description_chars[kNumLanguagesV1][128];
  };
  union {
    be<uint16_t> publisher_raw[64];
    char16_t publisher_chars[64];
  };
  union {
    be<uint16_t> title_name_raw[64];
    char16_t title_name_chars[64];
  };
  union {
    XContentAttributes bits;
    uint8_t as_byte;
  } flags;
  be<uint32_t> thumbnail_size;
  be<uint32_t> title_thumbnail_size;
  uint8_t thumbnail[kThumbLengthV2];
  union {
    be<uint16_t> display_name_ex_raw[kNumLanguagesV2 - kNumLanguagesV1][128];
    char16_t display_name_ex_chars[kNumLanguagesV2 - kNumLanguagesV1][128];
  };
  uint8_t title_thumbnail[kThumbLengthV2];
  union {
    be<uint16_t> description_ex_raw[kNumLanguagesV2 - kNumLanguagesV1][128];
    char16_t description_ex_chars[kNumLanguagesV2 - kNumLanguagesV1][128];
  };

  std::u16string display_name(XLanguage language) const {
    uint32_t lang_id = uint32_t(language) - 1;

    if (lang_id >= kNumLanguagesV2) {
      assert_always();
      // no room for this lang, read from english slot..
      lang_id = uint32_t(XLanguage::kEnglish) - 1;
    }

    const be<uint16_t>* str = 0;
    if (lang_id >= 0 && lang_id < kNumLanguagesV1) {
      str = display_name_raw[lang_id];
    } else if (lang_id >= kNumLanguagesV1 && lang_id < kNumLanguagesV2 &&
               metadata_version >= 2) {
      str = display_name_ex_raw[lang_id - kNumLanguagesV1];
    }

    if (!str) {
      // Invalid language ID?
      assert_always();
      return u"";
    }

    return load_and_swap<std::u16string>(str);
  }

  std::u16string description(XLanguage language) const {
    uint32_t lang_id = uint32_t(language) - 1;

    if (lang_id >= kNumLanguagesV2) {
      assert_always();
      // no room for this lang, read from english slot..
      lang_id = uint32_t(XLanguage::kEnglish) - 1;
    }

    const be<uint16_t>* str = 0;
    if (lang_id >= 0 && lang_id < kNumLanguagesV1) {
      str = description_raw[lang_id];
    } else if (lang_id >= kNumLanguagesV1 && lang_id < kNumLanguagesV2 &&
               metadata_version >= 2) {
      str = description_ex_raw[lang_id - kNumLanguagesV1];
    }

    if (!str) {
      // Invalid language ID?
      assert_always();
      return u"";
    }

    return load_and_swap<std::u16string>(str);
  }

  std::u16string publisher() const {
    return load_and_swap<std::u16string>(publisher_raw);
  }

  std::u16string title_name() const {
    return load_and_swap<std::u16string>(title_name_raw);
  }

  bool set_display_name(XLanguage language, const std::u16string_view value) {
    uint32_t lang_id = uint32_t(language) - 1;

    if (lang_id >= kNumLanguagesV2) {
      assert_always();
      // no room for this lang, store in english slot..
      lang_id = uint32_t(XLanguage::kEnglish) - 1;
    }

    char16_t* str = 0;
    if (lang_id >= 0 && lang_id < kNumLanguagesV1) {
      str = display_name_chars[lang_id];
    } else if (lang_id >= kNumLanguagesV1 && lang_id < kNumLanguagesV2 &&
               metadata_version >= 2) {
      str = display_name_ex_chars[lang_id - kNumLanguagesV1];
    }

    if (!str) {
      // Invalid language ID?
      assert_always();
      return false;
    }

    string_util::copy_and_swap_truncating(str, value,
                                          countof(display_name_chars[0]));
    return true;
  }

  bool set_description(XLanguage language, const std::u16string_view value) {
    uint32_t lang_id = uint32_t(language) - 1;

    if (lang_id >= kNumLanguagesV2) {
      assert_always();
      // no room for this lang, store in english slot..
      lang_id = uint32_t(XLanguage::kEnglish) - 1;
    }

    char16_t* str = 0;
    if (lang_id >= 0 && lang_id < kNumLanguagesV1) {
      str = description_chars[lang_id];
    } else if (lang_id >= kNumLanguagesV1 && lang_id < kNumLanguagesV2 &&
               metadata_version >= 2) {
      str = description_ex_chars[lang_id - kNumLanguagesV1];
    }

    if (!str) {
      // Invalid language ID?
      assert_always();
      return false;
    }

    string_util::copy_and_swap_truncating(str, value,
                                          countof(description_chars[0]));
    return true;
  }

  bool set_publisher(const std::u16string_view value) {
    string_util::copy_and_swap_truncating(publisher_chars, value,
                                          countof(publisher_chars));
    return true;
  }

  bool set_title_name(const std::u16string_view value) {
    string_util::copy_and_swap_truncating(title_name_chars, value,
                                          countof(title_name_chars));
    return true;
  }
});
static_assert_size(XContentMetadata, 0x93D6);

struct XContentLicense {
  be<uint64_t> licensee_id;
  be<uint32_t> license_bits;
  be<uint32_t> license_flags;
};
static_assert_size(XContentLicense, 0x10);

XEPACKEDSTRUCT(XContentHeader, {
  be<XContentPackageType> magic;
  uint8_t signature[0x228];
  XContentLicense licenses[0x10];
  uint8_t content_id[0x14];
  be<uint32_t> header_size;
});
static_assert_size(XContentHeader, 0x344);

struct StfsHeader {
  XContentHeader header;
  XContentMetadata metadata;
  // TODO: title/system updates contain more data after XContentMetadata, seems
  // to affect header.header_size
};
static_assert_size(StfsHeader, 0x971A);

class StfsContainerDevice : public Device {
 public:
  StfsContainerDevice(const std::string_view mount_path,
                      const std::filesystem::path& host_path);
  ~StfsContainerDevice() override;

  bool Initialize() override;
  void Dump(StringBuffer* string_buffer) override;
  Entry* ResolvePath(const std::string_view path) override;

  const std::string& name() const override { return name_; }
  uint32_t attributes() const override { return 0; }
  uint32_t component_name_max_length() const override { return 40; }

  uint32_t total_allocation_units() const override {
    return uint32_t(data_size() / sectors_per_allocation_unit() /
                    bytes_per_sector());
  }
  uint32_t available_allocation_units() const override { return 0; }
  uint32_t sectors_per_allocation_unit() const override { return 8; }
  uint32_t bytes_per_sector() const override { return 0x200; }

  // Gives rough estimate of the size of the data in this container
  // TODO: use allocated_block_count inside volume-descriptor?
  size_t data_size() const {
    if (header_.header.header_size) {
      return mmap_total_size_ -
             xe::round_up(header_.header.header_size, kSectorSize);
    }
    return mmap_total_size_ - sizeof(StfsHeader);
  }

 private:
  const uint32_t kSectorSize = 0x1000;
  const uint32_t kBlocksPerHashLevel[3] = {170, 28900, 4913000};

  enum class Error {
    kSuccess = 0,
    kErrorOutOfMemory = -1,
    kErrorReadError = -10,
    kErrorFileMismatch = -30,
    kErrorDamagedFile = -31,
  };

  enum class SvodLayoutType {
    kUnknown = 0x0,
    kEnhancedGDF = 0x1,
    kXSF = 0x2,
    kSingleFile = 0x4,
  };

  uint32_t ReadMagic(const std::filesystem::path& path);
  bool ResolveFromFolder(const std::filesystem::path& path);

  Error MapFiles();
  Error ReadHeaderAndVerify(const uint8_t* map_ptr, size_t map_size);

  Error ReadSVOD();
  Error ReadEntrySVOD(uint32_t sector, uint32_t ordinal,
                      StfsContainerEntry* parent);
  void BlockToOffsetSVOD(size_t sector, size_t* address, size_t* file_index);

  Error ReadSTFS();
  size_t BlockToOffsetSTFS(uint64_t block_index) const;
  uint32_t BlockToHashBlockNumberSTFS(uint32_t block_index,
                                      uint32_t hash_level) const;
  size_t BlockToHashBlockOffsetSTFS(uint32_t block_index,
                                    uint32_t hash_level) const;

  const StfsHashEntry* GetBlockHash(const uint8_t* map_ptr,
                                    uint32_t block_index);

  std::string name_;
  std::filesystem::path host_path_;
  std::map<size_t, std::unique_ptr<MappedMemory>> mmap_;
  size_t mmap_total_size_;

  size_t base_offset_;
  size_t magic_offset_;
  std::unique_ptr<Entry> root_entry_;
  StfsHeader header_;
  SvodLayoutType svod_layout_;
  uint32_t blocks_per_hash_table_;
  uint32_t block_step[2];

  std::unordered_map<size_t, StfsHashTable> cached_hash_tables_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_STFS_CONTAINER_DEVICE_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_STFS_H_
#define XENIA_KERNEL_FS_STFS_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <vector>

#include <xenia/xbox.h>
#include <xenia/kernel/fs/entry.h>


namespace xe {
namespace kernel {
namespace fs {


class STFS;


// http://www.free60.org/STFS

enum STFSPackageType {
  STFS_PACKAGE_CON,
  STFS_PACKAGE_PIRS,
  STFS_PACKAGE_LIVE,
};

enum STFSContentType : uint32_t {
  STFS_CONTENT_ARCADE_TITLE         = 0x000D0000,
  STFS_CONTENT_AVATAR_ITEM          = 0x00009000,
  STFS_CONTENT_CACHE_FILE           = 0x00040000,
  STFS_CONTENT_COMMUNITY_GAME       = 0x02000000,
  STFS_CONTENT_GAME_DEMO            = 0x00080000,
  STFS_CONTENT_GAMER_PICTURE        = 0x00020000,
  STFS_CONTENT_GAME_TITLE           = 0x000A0000,
  STFS_CONTENT_GAME_TRAILER         = 0x000C0000,
  STFS_CONTENT_GAME_VIDEO           = 0x00400000,
  STFS_CONTENT_INSTALLED_GAME       = 0x00004000,
  STFS_CONTENT_INSTALLER            = 0x000B0000,
  STFS_CONTENT_IPTV_PAUSE_BUFFER    = 0x00002000,
  STFS_CONTENT_LICENSE_STORE        = 0x000F0000,
  STFS_CONTENT_MARKETPLACE_CONTENT  = 0x00000002,
  STFS_CONTENT_MOVIE                = 0x00100000,
  STFS_CONTENT_MUSIC_VIDEO          = 0x00300000,
  STFS_CONTENT_PODCAST_VIDEO        = 0x00500000,
  STFS_CONTENT_PROFILE              = 0x00010000,
  STFS_CONTENT_PUBLISHER            = 0x00000003,
  STFS_CONTENT_SAVED_GAME           = 0x00000001,
  STFS_CONTENT_STORAGE_DOWNLOAD     = 0x00050000,
  STFS_CONTENT_THEME                = 0x00030000,
  STFS_CONTENT_TV                   = 0x00200000,
  STFS_CONTENT_VIDEO                = 0x00090000,
  STFS_CONTENT_VIRAL_VIDEO          = 0x00600000,
  STFS_CONTENT_XBOX_DOWNLOAD        = 0x00070000,
  STFS_CONTENT_XBOX_ORIGINAL_GAME   = 0x00005000,
  STFS_CONTENT_XBOX_SAVED_GAME      = 0x00060000,
  STFS_CONTENT_XBOX_360_TITLE       = 0x00001000,
  STFS_CONTENT_XBOX_TITLE           = 0x00005000,
  STFS_CONTENT_XNA                  = 0x000E0000,
};

enum STFSPlatform : uint8_t {
  STFS_PLATFORM_XBOX_360  = 0x02,
  STFS_PLATFORM_PC        = 0x04,
};

enum STFSDescriptorType : uint32_t {
  STFS_DESCRIPTOR_STFS    = 0,
  STFS_DESCRIPTOR_SVOD    = 1,
};


class STFSVolumeDescriptor {
public:
  bool Read(const uint8_t* p);

  uint8_t   descriptor_size;
  uint8_t   reserved;
  uint8_t   block_separation;
  uint16_t  file_table_block_count;
  uint32_t  file_table_block_number;
  uint8_t   top_hash_table_hash[0x14];
  uint32_t  total_allocated_block_count;
  uint32_t  total_unallocated_block_count;
};


class STFSHeader {
public:
  bool Read(const uint8_t* p);

  uint8_t         license_entries[0x100];
  uint8_t         header_hash[0x14];
  uint32_t        header_size;
  STFSContentType content_type;
  uint32_t        metadata_version;
  uint64_t        content_size;
  uint32_t        media_id;
  uint32_t        version;
  uint32_t        base_version;
  uint32_t        title_id;
  STFSPlatform    platform;
  uint8_t         executable_type;
  uint8_t         disc_number;
  uint8_t         disc_in_set;
  uint32_t        save_game_id;
  uint8_t         console_id[0x5];
  uint8_t         profile_id[0x8];
  STFSVolumeDescriptor volume_descriptor;
  uint32_t        data_file_count;
  uint64_t        data_file_combined_size;
  STFSDescriptorType descriptor_type;
  uint8_t         device_id[0x14];
  wchar_t         display_names[0x900 / 2];
  wchar_t         display_descs[0x900 / 2];
  wchar_t         publisher_name[0x80 / 2];
  wchar_t         title_name[0x80 / 2];
  uint8_t         transfer_flags;
  uint32_t        thumbnail_image_size;
  uint32_t        title_thumbnail_image_size;
  uint8_t         thumbnail_image[0x4000];
  uint8_t         title_thumbnail_image[0x4000];
};


class STFSEntry {
public:
  STFSEntry();
  ~STFSEntry();

  STFSEntry* GetChild(const char* name);

  void Dump(int indent);

  std::string       name;
  X_FILE_ATTRIBUTES attributes;
  size_t            offset;
  size_t            size;
  uint32_t          update_timestamp;
  uint32_t          access_timestamp;

  std::vector<STFSEntry*> children;

  typedef struct {
    size_t offset;
    size_t length;
  } BlockRecord_t;
  std::vector<BlockRecord_t> block_list;
};


class STFS {
public:
  enum Error {
    kSuccess            = 0,
    kErrorOutOfMemory   = -1,
    kErrorReadError     = -10,
    kErrorFileMismatch  = -30,
    kErrorDamagedFile   = -31,
  };

  STFS(xe_mmap_ref mmap);
  virtual ~STFS();

  const STFSHeader* header() const { return &header_; }
  STFSEntry* root_entry();

  Error Load();
  void Dump();

private:
  Error ReadHeaderAndVerify(const uint8_t* map_ptr);
  Error ReadAllEntries(const uint8_t* map_ptr);
  size_t BlockToOffset(uint32_t block);
  uint32_t ComputeBlockNumber(uint32_t block_index);

  typedef struct {
    uint32_t next_block_index;
    uint32_t info;
  } BlockHash_t;
  BlockHash_t GetBlockHash(const uint8_t* map_ptr,
                           uint32_t block_index, uint32_t table_offset);

  xe_mmap_ref mmap_;

  STFSPackageType package_type_;
  STFSHeader      header_;
  uint32_t        table_size_shift_;
  STFSEntry*      root_entry_;
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_STFS_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 * Major contributions to this file from:
 * - free60
 */

#include <xenia/kernel/fs/stfs.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;

#define XEGETUINT24BE(p) \
    (((uint32_t)poly::load_and_swap<uint8_t>((p) + 0) << 16) | \
     ((uint32_t)poly::load_and_swap<uint8_t>((p) + 1) << 8) | \
     (uint32_t)poly::load_and_swap<uint8_t>((p) + 2))
#define XEGETUINT24LE(p) \
    (((uint32_t)poly::load<uint8_t>((p) + 2) << 16) | \
     ((uint32_t)poly::load<uint8_t>((p) + 1) << 8) | \
     (uint32_t)poly::load<uint8_t>((p) + 0))

bool STFSVolumeDescriptor::Read(const uint8_t* p) {
  descriptor_size               = poly::load_and_swap<uint8_t>(p + 0x00);
  if (descriptor_size != 0x24) {
    XELOGE("STFS volume descriptor size mismatch, expected 0x24 but got 0x%X",
           descriptor_size);
    return false;
  }
  reserved                      = poly::load_and_swap<uint8_t>(p + 0x01);
  block_separation              = poly::load_and_swap<uint8_t>(p + 0x02);
  file_table_block_count        = poly::load_and_swap<uint16_t>(p + 0x03);
  file_table_block_number       = XEGETUINT24BE(p + 0x05);
  xe_copy_struct(top_hash_table_hash, p + 0x08, 0x14);
  total_allocated_block_count   = poly::load_and_swap<uint32_t>(p + 0x1C);
  total_unallocated_block_count = poly::load_and_swap<uint32_t>(p + 0x20);
  return true;
};


bool STFSHeader::Read(const uint8_t* p) {
  xe_copy_struct(license_entries, p + 0x22C, 0x100);
  xe_copy_struct(header_hash, p + 0x32C, 0x14);
  header_size       = poly::load_and_swap<uint32_t>(p + 0x340);
  content_type      = (STFSContentType)poly::load_and_swap<uint32_t>(p + 0x344);
  metadata_version  = poly::load_and_swap<uint32_t>(p + 0x348);
  if (metadata_version > 1) {
    // This is a variant of thumbnail data/etc.
    // Can just ignore it for now (until we parse thumbnails).
    XELOGW("STFSContainer doesn't support version %d yet", metadata_version);
  }
  content_size      = poly::load_and_swap<uint32_t>(p + 0x34C);
  media_id          = poly::load_and_swap<uint32_t>(p + 0x354);
  version           = poly::load_and_swap<uint32_t>(p + 0x358);
  base_version      = poly::load_and_swap<uint32_t>(p + 0x35C);
  title_id          = poly::load_and_swap<uint32_t>(p + 0x360);
  platform          = (STFSPlatform)poly::load_and_swap<uint8_t>(p + 0x364);
  executable_type   = poly::load_and_swap<uint8_t>(p + 0x365);
  disc_number       = poly::load_and_swap<uint8_t>(p + 0x366);
  disc_in_set       = poly::load_and_swap<uint8_t>(p + 0x367);
  save_game_id      = poly::load_and_swap<uint32_t>(p + 0x368);
  xe_copy_struct(console_id, p + 0x36C, 0x5);
  xe_copy_struct(profile_id, p + 0x371, 0x8);
  data_file_count             = poly::load_and_swap<uint32_t>(p + 0x39D);
  data_file_combined_size     = poly::load_and_swap<uint64_t>(p + 0x3A1);
  descriptor_type             = (STFSDescriptorType)poly::load_and_swap<uint8_t>(p + 0x3A9);
  if (descriptor_type != STFS_DESCRIPTOR_STFS) {
    XELOGE("STFS descriptor format not supported: %d", descriptor_type);
    return false;
  }
  if (!volume_descriptor.Read(p + 0x379)) {
    return false;
  }
  xe_copy_struct(device_id, p + 0x3FD, 0x14);
  for (size_t n = 0; n < 0x900 / 2; n++) {
    display_names[n] = poly::load_and_swap<uint16_t>(p + 0x411 + n * 2);
    display_descs[n] = poly::load_and_swap<uint16_t>(p + 0xD11 + n * 2);
  }
  for (size_t n = 0; n < 0x80 / 2; n++) {
    publisher_name[n] = poly::load_and_swap<uint16_t>(p + 0x1611 + n * 2);
    title_name[n] = poly::load_and_swap<uint16_t>(p + 0x1691 + n * 2);
  }
  transfer_flags              = poly::load_and_swap<uint8_t>(p + 0x1711);
  thumbnail_image_size        = poly::load_and_swap<uint32_t>(p + 0x1712);
  title_thumbnail_image_size  = poly::load_and_swap<uint32_t>(p + 0x1716);
  xe_copy_struct(thumbnail_image, p + 0x171A, 0x4000);
  xe_copy_struct(title_thumbnail_image, p + 0x571A, 0x4000);
  return true;
}

STFSEntry::STFSEntry() :
    attributes(X_FILE_ATTRIBUTE_NONE), offset(0), size(0),
    update_timestamp(0), access_timestamp(0) {
}

STFSEntry::~STFSEntry() {
  for (auto it = children.begin(); it != children.end(); ++it) {
    delete *it;
  }
}

STFSEntry* STFSEntry::GetChild(const char* name) {
  // TODO(benvanik): a faster search
  for (auto it = children.begin(); it != children.end(); ++it) {
    STFSEntry* entry = *it;
    if (xestrcasecmpa(entry->name.c_str(), name) == 0) {
      return entry;
    }
  }
  return NULL;
}

void STFSEntry::Dump(int indent) {
  printf("%s%s\n", std::string(indent, ' ').c_str(), name.c_str());
  for (auto it = children.begin(); it != children.end(); ++it) {
    STFSEntry* entry = *it;
    entry->Dump(indent + 2);
  }
}


STFS::STFS(xe_mmap_ref mmap) {
  mmap_ = xe_mmap_retain(mmap);

  root_entry_ = NULL;
}

STFS::~STFS() {
  delete root_entry_;

  xe_mmap_release(mmap_);
}

STFSEntry* STFS::root_entry() {
  return root_entry_;
}

STFS::Error STFS::Load() {
  Error result = kErrorOutOfMemory;

  uint8_t* map_ptr = (uint8_t*)xe_mmap_get_addr(mmap_);
  size_t map_size  = xe_mmap_get_length(mmap_);

  result = ReadHeaderAndVerify(map_ptr);
  XEEXPECTZERO(result);

  result = ReadAllEntries(map_ptr);
  XEEXPECTZERO(result);

  result = kSuccess;
XECLEANUP:
  return result;
}

void STFS::Dump() {
  if (root_entry_) {
    root_entry_->Dump(0);
  }
}

STFS::Error STFS::ReadHeaderAndVerify(const uint8_t* map_ptr) {
  // Check signature.
  if (memcmp(map_ptr, "LIVE", 4) == 0) {
    package_type_ = STFS_PACKAGE_LIVE;
  } else if (memcmp(map_ptr, "PIRS", 4) == 0) {
    package_type_ = STFS_PACKAGE_PIRS;
  } else if (memcmp(map_ptr, "CON", 3) == 0) {
    package_type_ = STFS_PACKAGE_CON;
  } else {
    // Unexpected format.
    return STFS::Error::kErrorFileMismatch;
  }

  // Read header.
  if (!header_.Read(map_ptr)) {
    return STFS::Error::kErrorDamagedFile;
  }

  if (((header_.header_size + 0x0FFF) & 0xB000) == 0xB000) {
    table_size_shift_ = 0;
  } else {
    table_size_shift_ = 1;
  }

  return kSuccess;
}

STFS::Error STFS::ReadAllEntries(const uint8_t* map_ptr) {
  root_entry_ = new STFSEntry();
  root_entry_->offset = 0;
  root_entry_->size   = 0;
  root_entry_->name   = "";
  root_entry_->attributes = X_FILE_ATTRIBUTE_DIRECTORY;

  std::vector<STFSEntry*> entries;

  // Load all listings.
  auto& volume_descriptor = header_.volume_descriptor;
  uint32_t table_block_index = volume_descriptor.file_table_block_number;
  for (size_t n = 0; n < volume_descriptor.file_table_block_count; n++) {
    const uint8_t* p =
        map_ptr + BlockToOffset(ComputeBlockNumber(table_block_index));
    for (size_t m = 0; m < 0x1000 / 0x40; m++) {
      const uint8_t* filename = p; // 0x28b
      if (filename[0] == 0) {
        // Done.
        break;
      }
      uint8_t filename_length_flags   = poly::load_and_swap<uint8_t>(p + 0x28);
      uint32_t allocated_block_count  = XEGETUINT24LE(p + 0x29);
      uint32_t start_block_index      = XEGETUINT24LE(p + 0x2F);
      uint16_t path_indicator         = poly::load_and_swap<uint16_t>(p + 0x32);
      uint32_t file_size              = poly::load_and_swap<uint32_t>(p + 0x34);
      uint32_t update_timestamp       = poly::load_and_swap<uint32_t>(p + 0x38);
      uint32_t access_timestamp       = poly::load_and_swap<uint32_t>(p + 0x3C);
      p += 0x40;

      STFSEntry* entry = new STFSEntry();
      entry->name = std::string((char*)filename, filename_length_flags & 0x3F);
      entry->name.append(1, '\0');
      // bit 0x40 = consecutive blocks (not fragmented?)
      if (filename_length_flags & 0x80) {
        entry->attributes = X_FILE_ATTRIBUTE_DIRECTORY;
      } else {
        entry->attributes = X_FILE_ATTRIBUTE_NORMAL;
        entry->offset     = BlockToOffset(ComputeBlockNumber(start_block_index));
        entry->size       = file_size;
      }
      entry->update_timestamp = update_timestamp;
      entry->access_timestamp = access_timestamp;
      entries.push_back(entry);

      const uint8_t* p = map_ptr + entry->offset;

      if (path_indicator == 0xFFFF) {
        // Root entry.
        root_entry_->children.push_back(entry);
      } else {
        // Lookup and add.
        auto parent = entries[path_indicator];
        parent->children.push_back(entry);
      }

      // Fill in all block records.
      // It's easier to do this now and just look them up later, at the cost
      // of some memory. Nasty chain walk.
      // TODO(benvanik): optimize if flag 0x40 (consecutive) is set.
      if (entry->attributes & X_FILE_ATTRIBUTE_NORMAL) {
        uint32_t block_index = start_block_index;
        size_t remaining_size = file_size;
        uint32_t info = 0x80;
        while (remaining_size &&
                block_index &&
                info >= 0x80) {
          size_t block_size = MIN(0x1000, remaining_size);
          size_t offset = BlockToOffset(ComputeBlockNumber(block_index));
          entry->block_list.push_back({ offset, block_size });
          remaining_size -= block_size;
          auto block_hash = GetBlockHash(map_ptr, block_index, 0);
          if (table_size_shift_ && block_hash.info < 0x80) {
            block_hash = GetBlockHash(map_ptr, block_index, 1);
          }
          block_index = block_hash.next_block_index;
          info = block_hash.info;
        }
      }
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

  return kSuccess;
}

size_t STFS::BlockToOffset(uint32_t block) {
  if (block >= 0xFFFFFF) {
    return -1;
  } else {
    return ((header_.header_size + 0x0FFF) & 0xF000) + (block << 12);
  }
}

uint32_t STFS::ComputeBlockNumber(uint32_t block_index) {
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
  if (package_type_ == STFS_PACKAGE_CON) {
    base <<= block_shift;
  }
  uint32_t block = base + block_index;
  if (block_index >= 0xAA) {
    base = (block_index + 0x70E4) / 0x70E4;
    if (package_type_ == STFS_PACKAGE_CON) {
      base <<= block_shift;
    }
    block += base;
    if (block_index >= 0x70E4) {
      base = (block_index + 0x4AF768) / 0x4AF768;
      if (package_type_ == STFS_PACKAGE_CON) {
        base <<= block_shift;
      }
      block += base;
    }
  }
  return block;
}

STFS::BlockHash_t STFS::GetBlockHash(
    const uint8_t* map_ptr,
    uint32_t block_index, uint32_t table_offset) {
  static const uint32_t table_spacing[] = {
    0xAB, 0x718F, 0xFE7DA, // The distance in blocks between tables
    0xAC, 0x723A, 0xFD00B, // For when tables are 1 block and when they are 2 blocks
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
  //table_index += table_offset - (1 << table_size_shift_);
  const uint8_t* hash_data = map_ptr + BlockToOffset(table_index);
  const uint8_t* record_data = hash_data + record * 0x18;
  uint32_t info = poly::load_and_swap<uint8_t>(record_data + 0x14);
  uint32_t next_block_index = XEGETUINT24BE(record_data + 0x15);
  return{ next_block_index, info };
}

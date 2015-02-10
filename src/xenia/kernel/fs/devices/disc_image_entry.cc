/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/fs/devices/disc_image_entry.h"

#include <algorithm>

#include "xenia/kernel/fs/gdfx.h"
#include "xenia/kernel/fs/devices/disc_image_file.h"

namespace xe {
namespace kernel {
namespace fs {

class DiscImageMemoryMapping : public MemoryMapping {
 public:
  DiscImageMemoryMapping(uint8_t* address, size_t length)
      : MemoryMapping(address, length) {}

  ~DiscImageMemoryMapping() override = default;
};

DiscImageEntry::DiscImageEntry(Device* device, const char* path,
                               poly::MappedMemory* mmap, GDFXEntry* gdfx_entry)
    : Entry(device, path),
      mmap_(mmap),
      gdfx_entry_(gdfx_entry),
      gdfx_entry_iterator_(gdfx_entry->children.end()) {}

DiscImageEntry::~DiscImageEntry() {}

X_STATUS DiscImageEntry::QueryInfo(XFileInfo* out_info) {
  assert_not_null(out_info);
  out_info->creation_time = 0;
  out_info->last_access_time = 0;
  out_info->last_write_time = 0;
  out_info->change_time = 0;
  out_info->allocation_size = 2048;
  out_info->file_length = gdfx_entry_->size;
  out_info->attributes = gdfx_entry_->attributes;
  return X_STATUS_SUCCESS;
}

X_STATUS DiscImageEntry::QueryDirectory(XDirectoryInfo* out_info, size_t length,
                                        const char* file_name, bool restart) {
  assert_not_null(out_info);

  // TODO(benvanik): move to common code.
  assert_null(file_name);
  GDFXEntry* entry = nullptr;
  if (file_name) {
    // Specified filename, return just that info.
    assert_true(std::strchr(file_name, '*') == nullptr);
    entry = gdfx_entry_->GetChild(file_name);
    if (!entry) {
      return X_STATUS_NO_SUCH_FILE;
    }
  } else {
    if (restart == true &&
        gdfx_entry_iterator_ != gdfx_entry_->children.end()) {
      gdfx_entry_iterator_ = gdfx_entry_->children.end();
    }

    if (gdfx_entry_iterator_ == gdfx_entry_->children.end()) {
      gdfx_entry_iterator_ = gdfx_entry_->children.begin();
      if (gdfx_entry_iterator_ == gdfx_entry_->children.end()) {
        return X_STATUS_UNSUCCESSFUL;
      }
    } else {
      ++gdfx_entry_iterator_;
      if (gdfx_entry_iterator_ == gdfx_entry_->children.end()) {
        return X_STATUS_UNSUCCESSFUL;
      }
    }

    auto end = (uint8_t*)out_info + length;
    entry = *gdfx_entry_iterator_;
    auto entry_name = entry->name;
    if (((uint8_t*)&out_info->file_name[0]) + entry_name.size() > end) {
      gdfx_entry_iterator_ = gdfx_entry_->children.end();
      return X_STATUS_NO_MORE_FILES;
    }
  }

  out_info->next_entry_offset = 0;
  out_info->file_index = 0xCDCDCDCD;
  out_info->creation_time = 0;
  out_info->last_access_time = 0;
  out_info->last_write_time = 0;
  out_info->change_time = 0;
  out_info->end_of_file = entry->size;
  out_info->allocation_size = 2048;
  out_info->attributes = (X_FILE_ATTRIBUTES)entry->attributes;
  out_info->file_name_length = static_cast<uint32_t>(entry->name.size());
  memcpy(out_info->file_name, entry->name.c_str(), entry->name.size());

  return X_STATUS_SUCCESS;
}

std::unique_ptr<MemoryMapping> DiscImageEntry::CreateMemoryMapping(
    Mode map_mode, const size_t offset, const size_t length) {
  if (map_mode != Mode::READ) {
    // Only allow reads.
    return nullptr;
  }

  size_t real_offset = gdfx_entry_->offset + offset;
  size_t real_length =
      length ? std::min(length, gdfx_entry_->size) : gdfx_entry_->size;
  return std::make_unique<DiscImageMemoryMapping>(mmap_->data() + real_offset,
                                                  real_length);
}

X_STATUS DiscImageEntry::Open(KernelState* kernel_state, Mode mode, bool async,
                              XFile** out_file) {
  *out_file = new DiscImageFile(kernel_state, mode, this);
  return X_STATUS_SUCCESS;
}

}  // namespace fs
}  // namespace kernel
}  // namespace xe

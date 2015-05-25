/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XFILE_H_
#define XENIA_KERNEL_XBOXKRNL_XFILE_H_

#include "xenia/kernel/fs/entry.h"
#include "xenia/kernel/xobject.h"

#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XAsyncRequest;
class XEvent;

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff545822.aspx
class X_FILE_NETWORK_OPEN_INFORMATION {
 public:
  // FILE_NETWORK_OPEN_INFORMATION
  uint64_t creation_time;
  uint64_t last_access_time;
  uint64_t last_write_time;
  uint64_t change_time;
  uint64_t allocation_size;
  uint64_t file_length;
  X_FILE_ATTRIBUTES attributes;

  void Write(uint8_t* base, uint32_t p) {
    xe::store_and_swap<uint64_t>(base + p, creation_time);
    xe::store_and_swap<uint64_t>(base + p + 8, last_access_time);
    xe::store_and_swap<uint64_t>(base + p + 16, last_write_time);
    xe::store_and_swap<uint64_t>(base + p + 24, change_time);
    xe::store_and_swap<uint64_t>(base + p + 32, allocation_size);
    xe::store_and_swap<uint64_t>(base + p + 40, file_length);
    xe::store_and_swap<uint32_t>(base + p + 48, attributes);
    xe::store_and_swap<uint32_t>(base + p + 52, 0);  // pad
  }
};

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff540248.aspx
class X_FILE_DIRECTORY_INFORMATION {
 public:
  // FILE_DIRECTORY_INFORMATION
  uint32_t next_entry_offset;
  uint32_t file_index;
  uint64_t creation_time;
  uint64_t last_access_time;
  uint64_t last_write_time;
  uint64_t change_time;
  uint64_t end_of_file;
  uint64_t allocation_size;
  X_FILE_ATTRIBUTES attributes;
  uint32_t file_name_length;
  char file_name[1];

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    uint8_t* src = (uint8_t*)this;
    X_FILE_DIRECTORY_INFORMATION* info;
    do {
      info = (X_FILE_DIRECTORY_INFORMATION*)src;
      xe::store_and_swap<uint32_t>(dst, info->next_entry_offset);
      xe::store_and_swap<uint32_t>(dst + 4, info->file_index);
      xe::store_and_swap<uint64_t>(dst + 8, info->creation_time);
      xe::store_and_swap<uint64_t>(dst + 16, info->last_access_time);
      xe::store_and_swap<uint64_t>(dst + 24, info->last_write_time);
      xe::store_and_swap<uint64_t>(dst + 32, info->change_time);
      xe::store_and_swap<uint64_t>(dst + 40, info->end_of_file);
      xe::store_and_swap<uint64_t>(dst + 48, info->allocation_size);
      xe::store_and_swap<uint32_t>(dst + 56, info->attributes);
      xe::store_and_swap<uint32_t>(dst + 60, info->file_name_length);
      memcpy(dst + 64, info->file_name, info->file_name_length);
      dst += info->next_entry_offset;
      src += info->next_entry_offset;
    } while (info->next_entry_offset != 0);
  }
};
static_assert_size(X_FILE_DIRECTORY_INFORMATION, 72);

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff540287.aspx
class X_FILE_FS_VOLUME_INFORMATION {
 public:
  // FILE_FS_VOLUME_INFORMATION
  uint64_t creation_time;
  uint32_t serial_number;
  uint32_t label_length;
  uint32_t supports_objects;
  char label[1];

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    xe::store_and_swap<uint64_t>(dst + 0, this->creation_time);
    xe::store_and_swap<uint32_t>(dst + 8, this->serial_number);
    xe::store_and_swap<uint32_t>(dst + 12, this->label_length);
    xe::store_and_swap<uint32_t>(dst + 16, this->supports_objects);
    memcpy(dst + 20, this->label, this->label_length);
  }
};
static_assert_size(X_FILE_FS_VOLUME_INFORMATION, 24);

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff540282.aspx
class X_FILE_FS_SIZE_INFORMATION {
 public:
  // FILE_FS_SIZE_INFORMATION
  uint64_t total_allocation_units;
  uint64_t available_allocation_units;
  uint32_t sectors_per_allocation_unit;
  uint32_t bytes_per_sector;

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    xe::store_and_swap<uint64_t>(dst + 0, this->total_allocation_units);
    xe::store_and_swap<uint64_t>(dst + 8, this->available_allocation_units);
    xe::store_and_swap<uint32_t>(dst + 16, this->sectors_per_allocation_unit);
    xe::store_and_swap<uint32_t>(dst + 20, this->bytes_per_sector);
  }
};
static_assert_size(X_FILE_FS_SIZE_INFORMATION, 24);

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff540251(v=vs.85).aspx
class X_FILE_FS_ATTRIBUTE_INFORMATION {
 public:
  // FILE_FS_ATTRIBUTE_INFORMATION
  uint32_t attributes;
  int32_t maximum_component_name_length;
  uint32_t fs_name_length;
  char fs_name[1];

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    xe::store_and_swap<uint32_t>(dst + 0, this->attributes);
    xe::store_and_swap<uint32_t>(dst + 4, this->maximum_component_name_length);
    xe::store_and_swap<uint32_t>(dst + 8, this->fs_name_length);
    memcpy(dst + 12, this->fs_name, this->fs_name_length);
  }
};
static_assert_size(X_FILE_FS_ATTRIBUTE_INFORMATION, 16);

class XFile : public XObject {
 public:
  virtual ~XFile();

  virtual const std::string& path() const = 0;
  virtual const std::string& name() const = 0;

  virtual fs::Device* device() const = 0;

  size_t position() const { return position_; }
  void set_position(size_t value) { position_ = value; }

  virtual X_STATUS QueryInfo(X_FILE_NETWORK_OPEN_INFORMATION* out_info) = 0;
  virtual X_STATUS QueryDirectory(X_FILE_DIRECTORY_INFORMATION* out_info, size_t length,
                                  const char* file_name, bool restart) = 0;

  X_STATUS Read(void* buffer, size_t buffer_length, size_t byte_offset,
                size_t* out_bytes_read);
  X_STATUS Read(void* buffer, size_t buffer_length, size_t byte_offset,
                XAsyncRequest* request);

  X_STATUS Write(const void* buffer, size_t buffer_length, size_t byte_offset,
                 size_t* out_bytes_written);

  virtual void* GetWaitHandle();

 protected:
  XFile(KernelState* kernel_state, fs::Mode mode);
  virtual X_STATUS ReadSync(void* buffer, size_t buffer_length,
                            size_t byte_offset, size_t* out_bytes_read) = 0;
  virtual X_STATUS WriteSync(const void* buffer, size_t buffer_length,
                             size_t byte_offset, size_t* out_bytes_written) {
    return X_STATUS_ACCESS_DENIED;
  }

 private:
  fs::Mode mode_;
  XEvent* async_event_;

  // TODO(benvanik): create flags, open state, etc.

  size_t position_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XFILE_H_

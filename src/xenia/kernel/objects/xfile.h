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

#include <xenia/kernel/xobject.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {

class XAsyncRequest;
class XEvent;

class XFileInfo {
public:
  // FILE_NETWORK_OPEN_INFORMATION
  uint64_t          creation_time;
  uint64_t          last_access_time;
  uint64_t          last_write_time;
  uint64_t          change_time;
  uint64_t          allocation_size;
  uint64_t          file_length;
  X_FILE_ATTRIBUTES attributes;

  void Write(uint8_t* base, uint32_t p) {
    poly::store_and_swap<uint64_t>(base + p, creation_time);
    poly::store_and_swap<uint64_t>(base + p + 8, last_access_time);
    poly::store_and_swap<uint64_t>(base + p + 16, last_write_time);
    poly::store_and_swap<uint64_t>(base + p + 24, change_time);
    poly::store_and_swap<uint64_t>(base + p + 32, allocation_size);
    poly::store_and_swap<uint64_t>(base + p + 40, file_length);
    poly::store_and_swap<uint32_t>(base + p + 48, attributes);
    poly::store_and_swap<uint32_t>(base + p + 52, 0); // pad
  }
};

class XDirectoryInfo {
public:
  // FILE_DIRECTORY_INFORMATION
  uint32_t          next_entry_offset;
  uint32_t          file_index;
  uint64_t          creation_time;
  uint64_t          last_access_time;
  uint64_t          last_write_time;
  uint64_t          change_time;
  uint64_t          end_of_file;
  uint64_t          allocation_size;
  X_FILE_ATTRIBUTES attributes;
  uint32_t          file_name_length;
  char              file_name[1];

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    uint8_t* src = (uint8_t*)this;
    XDirectoryInfo* info;
    do {
      info = (XDirectoryInfo*)src;
      poly::store_and_swap<uint32_t>(dst, info->next_entry_offset);
      poly::store_and_swap<uint32_t>(dst + 4, info->file_index);
      poly::store_and_swap<uint64_t>(dst + 8, info->creation_time);
      poly::store_and_swap<uint64_t>(dst + 16, info->last_access_time);
      poly::store_and_swap<uint64_t>(dst + 24, info->last_write_time);
      poly::store_and_swap<uint64_t>(dst + 32, info->change_time);
      poly::store_and_swap<uint64_t>(dst + 40, info->end_of_file);
      poly::store_and_swap<uint64_t>(dst + 48, info->allocation_size);
      poly::store_and_swap<uint32_t>(dst + 56, info->attributes);
      poly::store_and_swap<uint32_t>(dst + 60, info->file_name_length);
      xe_copy_memory(dst + 64, info->file_name_length, info->file_name, info->file_name_length);
      dst += info->next_entry_offset;
      src += info->next_entry_offset;
    } while (info->next_entry_offset != 0);
  }
};
static_assert_size(XDirectoryInfo, 72);

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff540287(v=vs.85).aspx
class XVolumeInfo {
public:
  // FILE_FS_VOLUME_INFORMATION
  uint64_t          creation_time;
  uint32_t          serial_number;
  uint32_t          label_length;
  uint32_t          supports_objects;
  char              label[1];

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    poly::store_and_swap<uint64_t>(dst + 0, this->creation_time);
    poly::store_and_swap<uint32_t>(dst + 8, this->serial_number);
    poly::store_and_swap<uint32_t>(dst + 12, this->label_length);
    poly::store_and_swap<uint32_t>(dst + 16, this->supports_objects);
    xe_copy_memory(dst + 20, this->label_length, this->label, this->label_length);
  }
};
static_assert_size(XVolumeInfo, 24);

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff540251(v=vs.85).aspx
class XFileSystemAttributeInfo {
public:
  // FILE_FS_ATTRIBUTE_INFORMATION
  uint32_t          attributes;
  int32_t           maximum_component_name_length;
  uint32_t          fs_name_length;
  char              fs_name[1];

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    poly::store_and_swap<uint32_t>(dst + 0, this->attributes);
    poly::store_and_swap<uint32_t>(dst + 4, this->maximum_component_name_length);
    poly::store_and_swap<uint32_t>(dst + 8, this->fs_name_length);
    xe_copy_memory(dst + 12, this->fs_name_length, this->fs_name, this->fs_name_length);
  }
};
static_assert_size(XFileSystemAttributeInfo, 16);

class XFile : public XObject {
public:
  virtual ~XFile();

  virtual const char* path(void) const = 0;
  virtual const char* absolute_path(void) const = 0;
  virtual const char* name(void) const = 0;

  size_t position() const { return position_; }
  void set_position(size_t value) { position_ = value; }

  virtual X_STATUS QueryInfo(XFileInfo* out_info) = 0;
  virtual X_STATUS QueryDirectory(XDirectoryInfo* out_info,
                                  size_t length, const char* file_name, bool restart) = 0;
  virtual X_STATUS QueryVolume(XVolumeInfo* out_info, size_t length) = 0;
  virtual X_STATUS QueryFileSystemAttributes(XFileSystemAttributeInfo* out_info, size_t length) = 0;

  X_STATUS Read(void* buffer, size_t buffer_length, size_t byte_offset,
                size_t* out_bytes_read);
  X_STATUS Read(void* buffer, size_t buffer_length, size_t byte_offset,
                XAsyncRequest* request);

  virtual void* GetWaitHandle();

protected:
  XFile(KernelState* kernel_state, uint32_t desired_access);
  virtual X_STATUS ReadSync(
      void* buffer, size_t buffer_length, size_t byte_offset,
      size_t* out_bytes_read) = 0;

private:
  uint32_t        desired_access_;
  XEvent*         async_event_;

  // TODO(benvanik): create flags, open state, etc.

  size_t          position_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_XFILE_H_

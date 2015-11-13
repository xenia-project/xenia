/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XFILE_H_
#define XENIA_KERNEL_XFILE_H_

#include <string>

#include "xenia/base/filesystem.h"
#include "xenia/kernel/xevent.h"
#include "xenia/kernel/xobject.h"
#include "xenia/vfs/device.h"
#include "xenia/vfs/entry.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff545822.aspx
struct X_FILE_NETWORK_OPEN_INFORMATION {
  xe::be<uint64_t> creation_time;
  xe::be<uint64_t> last_access_time;
  xe::be<uint64_t> last_write_time;
  xe::be<uint64_t> change_time;
  xe::be<uint64_t> allocation_size;
  xe::be<uint64_t> end_of_file;  // size in bytes
  xe::be<uint32_t> attributes;
  xe::be<uint32_t> pad;
};

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff540248.aspx
class X_FILE_DIRECTORY_INFORMATION {
 public:
  // FILE_DIRECTORY_INFORMATION
  xe::be<uint32_t> next_entry_offset;  // 0x0
  xe::be<uint32_t> file_index;         // 0x4
  xe::be<uint64_t> creation_time;      // 0x8
  xe::be<uint64_t> last_access_time;   // 0x10
  xe::be<uint64_t> last_write_time;    // 0x18
  xe::be<uint64_t> change_time;        // 0x20
  xe::be<uint64_t> end_of_file;        // 0x28 size in bytes
  xe::be<uint64_t> allocation_size;    // 0x30
  xe::be<uint32_t> attributes;         // 0x38 X_FILE_ATTRIBUTES
  xe::be<uint32_t> file_name_length;   // 0x3C
  char file_name[1];                   // 0x40

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    uint8_t* src = reinterpret_cast<uint8_t*>(this);
    X_FILE_DIRECTORY_INFORMATION* info;
    do {
      info = reinterpret_cast<X_FILE_DIRECTORY_INFORMATION*>(src);
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

class XFile : public XObject {
 public:
  static const Type kType = kTypeFile;

  ~XFile() override;

  vfs::Device* device() const { return entry_->device(); }
  vfs::Entry* entry() const { return entry_; }
  uint32_t file_access() const { return file_access_; }

  const std::string& path() const { return entry_->path(); }
  const std::string& name() const { return entry_->name(); }

  size_t position() const { return position_; }
  void set_position(size_t value) { position_ = value; }

  X_STATUS QueryDirectory(X_FILE_DIRECTORY_INFORMATION* out_info, size_t length,
                          const char* file_name, bool restart);

  X_STATUS Read(void* buffer, size_t buffer_length, size_t byte_offset,
                size_t* out_bytes_read);

  X_STATUS Write(const void* buffer, size_t buffer_length, size_t byte_offset,
                 size_t* out_bytes_written);

  xe::threading::WaitHandle* GetWaitHandle() override {
    return async_event_->GetWaitHandle();
  }

 protected:
  XFile(KernelState* kernel_state, uint32_t file_access, vfs::Entry* entry);
  virtual X_STATUS ReadSync(void* buffer, size_t buffer_length,
                            size_t byte_offset, size_t* out_bytes_read) = 0;
  virtual X_STATUS WriteSync(const void* buffer, size_t buffer_length,
                             size_t byte_offset, size_t* out_bytes_written) {
    return X_STATUS_ACCESS_DENIED;
  }

 private:
  vfs::Entry* entry_ = nullptr;
  uint32_t file_access_ = 0;
  XEvent* async_event_ = nullptr;

  // TODO(benvanik): create flags, open state, etc.

  size_t position_ = 0;

  xe::filesystem::WildcardEngine find_engine_;
  size_t find_index_ = 0;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XFILE_H_

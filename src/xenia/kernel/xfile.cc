/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xfile.h"

#include "xenia/base/math.h"
#include "xenia/kernel/xevent.h"

namespace xe {
namespace kernel {

XFile::XFile(KernelState* kernel_state, uint32_t file_access, vfs::Entry* entry)
    : XObject(kernel_state, kTypeFile),
      entry_(entry),
      file_access_(file_access) {
  async_event_ = new XEvent(kernel_state);
  async_event_->Initialize(false, false);
}

XFile::~XFile() {
  // TODO(benvanik): signal that the file is closing?
  async_event_->Set(0, false);
  async_event_->Delete();
}

X_STATUS XFile::QueryDirectory(X_FILE_DIRECTORY_INFORMATION* out_info,
                               size_t length, const char* file_name,
                               bool restart) {
  assert_not_null(out_info);

  vfs::Entry* entry = nullptr;

  if (file_name != nullptr) {
    // Only queries in the current directory are supported for now.
    assert_true(std::strchr(file_name, '\\') == nullptr);

    find_engine_.SetRule(file_name);

    // Always restart the search?
    find_index_ = 0;
    entry = entry_->IterateChildren(find_engine_, &find_index_);
    if (!entry) {
      return X_STATUS_NO_SUCH_FILE;
    }
  } else {
    if (restart) {
      find_index_ = 0;
    }

    entry = entry_->IterateChildren(find_engine_, &find_index_);
    if (!entry) {
      return X_STATUS_NO_SUCH_FILE;
    }
  }

  auto end = reinterpret_cast<uint8_t*>(out_info) + length;
  const auto& entry_name = entry->name();
  if (reinterpret_cast<uint8_t*>(&out_info->file_name[0]) + entry_name.size() >
      end) {
    assert_always("Buffer overflow?");
    return X_STATUS_NO_SUCH_FILE;
  }

  out_info->next_entry_offset = 0;
  out_info->file_index = static_cast<uint32_t>(find_index_);
  out_info->creation_time = entry->create_timestamp();
  out_info->last_access_time = entry->access_timestamp();
  out_info->last_write_time = entry->write_timestamp();
  out_info->change_time = entry->write_timestamp();
  out_info->end_of_file = entry->size();
  out_info->allocation_size = entry->allocation_size();
  out_info->attributes = entry->attributes();
  out_info->file_name_length = static_cast<uint32_t>(entry_name.size());
  std::memcpy(out_info->file_name, entry_name.data(), entry_name.size());

  return X_STATUS_SUCCESS;
}

X_STATUS XFile::Read(void* buffer, size_t buffer_length, size_t byte_offset,
                     size_t* out_bytes_read) {
  if (byte_offset == -1) {
    // Read from current position.
    byte_offset = position_;
  }
  X_STATUS result =
      ReadSync(buffer, buffer_length, byte_offset, out_bytes_read);
  if (XSUCCEEDED(result)) {
    position_ += *out_bytes_read;
  }
  async_event_->Set(0, false);
  return result;
}

X_STATUS XFile::Write(const void* buffer, size_t buffer_length,
                      size_t byte_offset, size_t* out_bytes_written) {
  if (byte_offset == -1) {
    // Write from current position.
    byte_offset = position_;
  }
  X_STATUS result =
      WriteSync(buffer, buffer_length, byte_offset, out_bytes_written);
  if (XSUCCEEDED(result)) {
    position_ += *out_bytes_written;
  }
  async_event_->Set(0, false);
  return result;
}

}  // namespace kernel
}  // namespace xe

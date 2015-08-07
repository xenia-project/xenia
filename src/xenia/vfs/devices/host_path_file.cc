/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/host_path_file.h"

#include "xenia/vfs/devices/host_path_entry.h"

namespace xe {
namespace vfs {

HostPathFile::HostPathFile(
    kernel::KernelState* kernel_state, uint32_t file_access,
    HostPathEntry* entry,
    std::unique_ptr<xe::filesystem::FileHandle> file_handle)
    : XFile(kernel_state, file_access, entry),
      file_handle_(std::move(file_handle)) {}

HostPathFile::~HostPathFile() = default;

X_STATUS HostPathFile::ReadSync(void* buffer, size_t buffer_length,
                                size_t byte_offset, size_t* out_bytes_read) {
  if (!(file_access() & FileAccess::kFileReadData)) {
    return X_STATUS_ACCESS_DENIED;
  }

  if (file_handle_->Read(byte_offset, buffer, buffer_length, out_bytes_read)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

X_STATUS HostPathFile::WriteSync(const void* buffer, size_t buffer_length,
                                 size_t byte_offset,
                                 size_t* out_bytes_written) {
  if (!(file_access() &
        (FileAccess::kFileWriteData | FileAccess::kFileAppendData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  if (file_handle_->Write(byte_offset, buffer, buffer_length,
                          out_bytes_written)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

}  // namespace vfs
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/fs/devices/host_path_file.h"

#include "xenia/kernel/fs/device.h"
#include "xenia/kernel/fs/devices/host_path_entry.h"

namespace xe {
namespace kernel {
namespace fs {

HostPathFile::HostPathFile(KernelState* kernel_state, Mode mode,
                           HostPathEntry* entry, HANDLE file_handle)
    : entry_(entry), file_handle_(file_handle), XFile(kernel_state, mode) {}

HostPathFile::~HostPathFile() {
  CloseHandle(file_handle_);
  delete entry_;
}

const std::string& HostPathFile::path() const { return entry_->path(); }

const std::string& HostPathFile::name() const { return entry_->name(); }

Device* HostPathFile::device() const { return entry_->device(); }

X_STATUS HostPathFile::QueryInfo(X_FILE_NETWORK_OPEN_INFORMATION* out_info) {
  return entry_->QueryInfo(out_info);
}

X_STATUS HostPathFile::QueryDirectory(X_FILE_DIRECTORY_INFORMATION* out_info, size_t length,
                                      const char* file_name, bool restart) {
  return entry_->QueryDirectory(out_info, length, file_name, restart);
}

X_STATUS HostPathFile::QueryVolume(XVolumeInfo* out_info, size_t length) {
  return entry_->device()->QueryVolume(out_info, length);
}

X_STATUS HostPathFile::QueryFileSystemAttributes(
    XFileSystemAttributeInfo* out_info, size_t length) {
  return entry_->device()->QueryFileSystemAttributes(out_info, length);
}

X_STATUS HostPathFile::ReadSync(void* buffer, size_t buffer_length,
                                size_t byte_offset, size_t* out_bytes_read) {
  OVERLAPPED overlapped;
  overlapped.Pointer = (PVOID)byte_offset;
  overlapped.hEvent = NULL;
  DWORD bytes_read = 0;
  BOOL read = ReadFile(file_handle_, buffer, (DWORD)buffer_length, &bytes_read,
                       &overlapped);
  if (read) {
    *out_bytes_read = bytes_read;
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

X_STATUS HostPathFile::WriteSync(const void* buffer, size_t buffer_length,
                                 size_t byte_offset,
                                 size_t* out_bytes_written) {
  OVERLAPPED overlapped;
  overlapped.Pointer = (PVOID)byte_offset;
  overlapped.hEvent = NULL;
  DWORD bytes_written = 0;
  BOOL wrote = WriteFile(file_handle_, buffer, (DWORD)buffer_length,
                         &bytes_written, &overlapped);
  if (wrote) {
    *out_bytes_written = bytes_written;
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

}  // namespace fs
}  // namespace kernel
}  // namespace xe

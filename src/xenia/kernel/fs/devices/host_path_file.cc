/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/devices/host_path_file.h>

#include <xenia/kernel/fs/devices/host_path_entry.h>

#include <xenia/kernel/fs/device.h>

using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


HostPathFile::HostPathFile(
    KernelState* kernel_state, uint32_t desired_access,
    HostPathEntry* entry, HANDLE file_handle) :
    entry_(entry), file_handle_(file_handle),
    XFile(kernel_state, desired_access) {
}

HostPathFile::~HostPathFile() {
  CloseHandle(file_handle_);
}

const char* HostPathFile::path(void) const {
  return entry_->path();
}

const char* HostPathFile::absolute_path(void) const {
  return entry_->absolute_path();
}

const char* HostPathFile::name(void) const {
  return entry_->name();
}

X_STATUS HostPathFile::QueryInfo(XFileInfo* out_info) {
  return entry_->QueryInfo(out_info);
}

X_STATUS HostPathFile::QueryDirectory(XDirectoryInfo* out_info,
    size_t length, const char* file_name, bool restart) {
  return entry_->QueryDirectory(out_info, length, file_name, restart);
}

X_STATUS HostPathFile::QueryVolume(XVolumeInfo* out_info, size_t length) {
  return entry_->device()->QueryVolume(out_info, length);
}

X_STATUS HostPathFile::QueryFileSystemAttributes(XFileSystemAttributeInfo* out_info, size_t length) {
  return entry_->device()->QueryFileSystemAttributes(out_info, length);
}

X_STATUS HostPathFile::ReadSync(
    void* buffer, size_t buffer_length, size_t byte_offset,
    size_t* out_bytes_read) {
  OVERLAPPED overlapped;
  overlapped.Pointer = (PVOID)byte_offset;
  overlapped.hEvent = NULL;
  DWORD bytes_read = 0;
  BOOL read = ReadFile(
      file_handle_, buffer, (DWORD)buffer_length, &bytes_read, &overlapped);
  if (read) {
    *out_bytes_read = bytes_read;
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/fs/devices/host_path_file.h>

#include <xenia/kernel/modules/xboxkrnl/fs/devices/host_path_entry.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;
using namespace xe::kernel::xboxkrnl::fs;


HostPathFile::HostPathFile(
    KernelState* kernel_state, uint32_t desired_access,
    HostPathEntry* entry, HANDLE file_handle) :
    entry_(entry), file_handle_(file_handle),
    XFile(kernel_state, desired_access) {
}

HostPathFile::~HostPathFile() {
  CloseHandle(file_handle_);
}

X_STATUS HostPathFile::QueryInfo(FileInfo* out_info) {
  XEASSERTNOTNULL(out_info);

  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesEx(
      entry_->local_path(), GetFileExInfoStandard, &data)) {
    return X_STATUS_ACCESS_DENIED;
  }

#define COMBINE_TIME(t) (((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime)
  out_info->creation_time     = COMBINE_TIME(data.ftCreationTime);
  out_info->last_access_time  = COMBINE_TIME(data.ftLastAccessTime);
  out_info->last_write_time   = COMBINE_TIME(data.ftLastWriteTime);
  out_info->change_time       = COMBINE_TIME(data.ftLastWriteTime);
  out_info->allocation_size   = 4096;
  out_info->file_length       = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
  out_info->attributes        = (X_FILE_ATTRIBUTES)data.dwFileAttributes;
  return X_STATUS_SUCCESS;
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
    return X_STATUS_UNSUCCESSFUL;
  }
}

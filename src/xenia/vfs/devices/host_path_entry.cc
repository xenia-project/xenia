/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/host_path_entry.h"

#include "xenia/base/mapped_memory.h"
#include "xenia/base/math.h"
#include "xenia/base/string.h"
#include "xenia/vfs/devices/host_path_file.h"

namespace xe {
namespace vfs {

HostPathEntry::HostPathEntry(Device* device, const char* path,
                             const std::wstring& local_path)
    : Entry(device, path),
      local_path_(local_path),
      find_file_(INVALID_HANDLE_VALUE) {}

HostPathEntry::~HostPathEntry() {
  if (find_file_ != INVALID_HANDLE_VALUE) {
    FindClose(find_file_);
  }
}

#define COMBINE_TIME(t) (((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime)

X_STATUS HostPathEntry::QueryInfo(X_FILE_NETWORK_OPEN_INFORMATION* out_info) {
  assert_not_null(out_info);

  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesEx(local_path_.c_str(), GetFileExInfoStandard, &data)) {
    return X_STATUS_ACCESS_DENIED;
  }

  uint64_t file_size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;

  out_info->creation_time = COMBINE_TIME(data.ftCreationTime);
  out_info->last_access_time = COMBINE_TIME(data.ftLastAccessTime);
  out_info->last_write_time = COMBINE_TIME(data.ftLastWriteTime);
  out_info->change_time = COMBINE_TIME(data.ftLastWriteTime);
  out_info->allocation_size = xe::round_up(file_size, 4096);
  out_info->end_of_file = file_size;
  out_info->attributes = (X_FILE_ATTRIBUTES)data.dwFileAttributes;
  return X_STATUS_SUCCESS;
}

X_STATUS HostPathEntry::QueryDirectory(X_FILE_DIRECTORY_INFORMATION* out_info,
                                       size_t length, const char* file_name,
                                       bool restart) {
  assert_not_null(out_info);

  WIN32_FIND_DATA ffd;

  HANDLE handle = find_file_;

  if (restart == true && handle != INVALID_HANDLE_VALUE) {
    FindClose(find_file_);
    handle = find_file_ = INVALID_HANDLE_VALUE;
  }

  if (handle == INVALID_HANDLE_VALUE) {
    std::wstring target_path = local_path_;
    if (!file_name) {
      target_path = xe::join_paths(target_path, L"*");
    } else {
      target_path = xe::join_paths(target_path, xe::to_wstring(file_name));
    }
    handle = find_file_ = FindFirstFile(target_path.c_str(), &ffd);
    if (handle == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_NOT_FOUND) {
        return X_STATUS_NO_SUCH_FILE;
      }
      return X_STATUS_UNSUCCESSFUL;
    }
  } else {
    if (FindNextFile(handle, &ffd) == FALSE) {
      FindClose(handle);
      find_file_ = INVALID_HANDLE_VALUE;
      return X_STATUS_NO_SUCH_FILE;
    }
  }

  auto end = (uint8_t*)out_info + length;
  size_t entry_name_length = wcslen(ffd.cFileName);
  if (((uint8_t*)&out_info->file_name[0]) + entry_name_length > end) {
    FindClose(handle);
    find_file_ = INVALID_HANDLE_VALUE;
    return X_STATUS_BUFFER_OVERFLOW;
  }

  uint64_t file_size = ((uint64_t)ffd.nFileSizeHigh << 32) | ffd.nFileSizeLow;

  out_info->next_entry_offset = 0;
  out_info->file_index = 0xCDCDCDCD;
  out_info->creation_time = COMBINE_TIME(ffd.ftCreationTime);
  out_info->last_access_time = COMBINE_TIME(ffd.ftLastAccessTime);
  out_info->last_write_time = COMBINE_TIME(ffd.ftLastWriteTime);
  out_info->change_time = COMBINE_TIME(ffd.ftLastWriteTime);
  out_info->end_of_file = file_size;
  out_info->allocation_size = xe::round_up(file_size, 4096);
  out_info->attributes = (X_FILE_ATTRIBUTES)ffd.dwFileAttributes;

  out_info->file_name_length = (uint32_t)entry_name_length;
  for (size_t i = 0; i < entry_name_length; ++i) {
    out_info->file_name[i] =
        ffd.cFileName[i] < 256 ? (char)ffd.cFileName[i] : '?';
  }

  return X_STATUS_SUCCESS;
}

X_STATUS HostPathEntry::Open(KernelState* kernel_state, Mode mode, bool async,
                             XFile** out_file) {
  // TODO(benvanik): plumb through proper disposition/access mode.
  DWORD desired_access =
      is_read_only() ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
  if (mode == Mode::READ_APPEND) {
    desired_access |= FILE_APPEND_DATA;
  }
  DWORD share_mode = FILE_SHARE_READ;
  DWORD creation_disposition;
  switch (mode) {
    case Mode::READ:
      creation_disposition = OPEN_EXISTING;
      break;
    case Mode::READ_WRITE:
      creation_disposition = OPEN_ALWAYS;
      break;
    case Mode::READ_APPEND:
      creation_disposition = OPEN_EXISTING;
      break;
    default:
      assert_unhandled_case(mode);
      break;
  }
  DWORD flags_and_attributes = async ? FILE_FLAG_OVERLAPPED : 0;
  HANDLE file =
      CreateFile(local_path_.c_str(), desired_access, share_mode, NULL,
                 creation_disposition,
                 flags_and_attributes | FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (file == INVALID_HANDLE_VALUE) {
    // TODO(benvanik): pick correct response.
    return X_STATUS_NO_SUCH_FILE;
  }

  *out_file = new HostPathFile(kernel_state, mode, this, file);
  return X_STATUS_SUCCESS;
}

std::unique_ptr<MappedMemory> HostPathEntry::OpenMapped(MappedMemory::Mode mode,
                                                        size_t offset,
                                                        size_t length) {
  return MappedMemory::Open(local_path_, mode, offset, length);
}

}  // namespace vfs
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/devices/host_path_entry.h>

#include <xenia/kernel/fs/devices/host_path_file.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


namespace {


class HostPathMemoryMapping : public MemoryMapping {
public:
  HostPathMemoryMapping(uint8_t* address, size_t length, xe_mmap_ref mmap) :
      MemoryMapping(address, length) {
    mmap_ = xe_mmap_retain(mmap);
  }
  virtual ~HostPathMemoryMapping() {
    xe_mmap_release(mmap_);
  }
private:
  xe_mmap_ref mmap_;
};


}


HostPathEntry::HostPathEntry(Type type, Device* device, const char* path,
                     const xechar_t* local_path) :
    Entry(type, device, path),
    find_file_(INVALID_HANDLE_VALUE) {
  local_path_ = xestrdup(local_path);
}

HostPathEntry::~HostPathEntry() {
  if (find_file_ != INVALID_HANDLE_VALUE) {
    FindClose(find_file_);
  }
  xe_free(local_path_);
}

#define COMBINE_TIME(t) (((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime)

X_STATUS HostPathEntry::QueryInfo(XFileInfo* out_info) {
  XEASSERTNOTNULL(out_info);

  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesEx(
      local_path_, GetFileExInfoStandard, &data)) {
    return X_STATUS_ACCESS_DENIED;
  }

  out_info->creation_time     = COMBINE_TIME(data.ftCreationTime);
  out_info->last_access_time  = COMBINE_TIME(data.ftLastAccessTime);
  out_info->last_write_time   = COMBINE_TIME(data.ftLastWriteTime);
  out_info->change_time       = COMBINE_TIME(data.ftLastWriteTime);
  out_info->allocation_size   = 4096;
  out_info->file_length       =
      ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
  out_info->attributes        = (X_FILE_ATTRIBUTES)data.dwFileAttributes;
  return X_STATUS_SUCCESS;
}

X_STATUS HostPathEntry::QueryDirectory(
    XDirectoryInfo* out_info, size_t length, bool restart) {
  XEASSERTNOTNULL(out_info);
  xe_zero_struct(out_info, length);

  WIN32_FIND_DATA ffd;

  HANDLE handle = find_file_;

  if (restart == true && handle != INVALID_HANDLE_VALUE) {
    FindClose(find_file_);
    handle = find_file_ = INVALID_HANDLE_VALUE;
  }

  if (handle == INVALID_HANDLE_VALUE) {
    handle = find_file_ = FindFirstFile(local_path_, &ffd);
    if (handle == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_NOT_FOUND) {
        return X_STATUS_NO_SUCH_FILE;
      }
      return X_STATUS_UNSUCCESSFUL;
    }
  }
  else {
    if (FindNextFile(handle, &ffd) == FALSE) {
      FindClose(handle);
      find_file_ = INVALID_HANDLE_VALUE;
      return X_STATUS_UNSUCCESSFUL;
    }
  }

  XDirectoryInfo* current;

  auto current_buf = (uint8_t*)out_info;
  auto end = current_buf + length;

  current = (XDirectoryInfo*)current_buf;
  if (((uint8_t*)&current->file_name[0]) + wcslen(ffd.cFileName) > end) {
    FindClose(handle);
    find_file_ = INVALID_HANDLE_VALUE;
    return X_STATUS_UNSUCCESSFUL;
  }

  do {
    size_t file_name_length = wcslen(ffd.cFileName);
    if (((uint8_t*)&((XDirectoryInfo*)current_buf)->file_name[0]) +
        file_name_length > end) {
      break;
    }

    current = (XDirectoryInfo*)current_buf;
    current->file_index = 0xCDCDCDCD;
    current->creation_time = COMBINE_TIME(ffd.ftCreationTime);
    current->last_access_time = COMBINE_TIME(ffd.ftLastAccessTime);
    current->last_write_time = COMBINE_TIME(ffd.ftLastWriteTime);
    current->change_time = COMBINE_TIME(ffd.ftLastWriteTime);
    current->end_of_file =
        ((uint64_t)ffd.nFileSizeHigh << 32) | ffd.nFileSizeLow;
    current->allocation_size = 4096;
    current->attributes = (X_FILE_ATTRIBUTES)ffd.dwFileAttributes;

    current->file_name_length = (uint32_t)file_name_length;
    for (size_t i = 0; i < file_name_length; ++i) {
      current->file_name[i] =
          ffd.cFileName[i] < 256 ? (char)ffd.cFileName[i] : '?';
    }

    auto next_buf = (((uint8_t*)&current->file_name[0]) + file_name_length);
    next_buf += 8 - ((uint8_t)next_buf % 8);

    current->next_entry_offset = (uint32_t)(next_buf - current_buf);
    current_buf = next_buf;
  } while (current_buf < end && FindNextFile(handle, &ffd) == TRUE);
  current->next_entry_offset = 0;
  return X_STATUS_SUCCESS;
}

MemoryMapping* HostPathEntry::CreateMemoryMapping(
    xe_file_mode file_mode, const size_t offset, const size_t length) {
  xe_mmap_ref mmap = xe_mmap_open(file_mode, local_path_, offset, length);
  if (!mmap) {
    return NULL;
  }

  HostPathMemoryMapping* lfmm = new HostPathMemoryMapping(
      (uint8_t*)xe_mmap_get_addr(mmap), xe_mmap_get_length(mmap),
      mmap);
  xe_mmap_release(mmap);

  return lfmm;
}

X_STATUS HostPathEntry::Open(
    KernelState* kernel_state,
    uint32_t desired_access, bool async,
    XFile** out_file) {
  DWORD share_mode = FILE_SHARE_READ;
  DWORD creation_disposition = OPEN_EXISTING;
  DWORD flags_and_attributes = async ? FILE_FLAG_OVERLAPPED : 0;
  HANDLE file = CreateFile(
      local_path_,
      desired_access,
      share_mode,
      NULL,
      creation_disposition,
      flags_and_attributes,
      NULL);
  if (!file) {
    // TODO(benvanik): pick correct response.
    return X_STATUS_ACCESS_DENIED;
  }

  *out_file = new HostPathFile(kernel_state, desired_access, this, file);
  return X_STATUS_SUCCESS;
}

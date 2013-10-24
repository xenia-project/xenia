/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/fs/devices/host_path_entry.h>

#include <xenia/kernel/xboxkrnl/fs/devices/host_path_file.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;
using namespace xe::kernel::xboxkrnl::fs;


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
    Entry(type, device, path) {
  local_path_ = xestrdup(local_path);
}

HostPathEntry::~HostPathEntry() {
  xe_free(local_path_);
}

X_STATUS HostPathEntry::QueryInfo(XFileInfo* out_info) {
  XEASSERTNOTNULL(out_info);

  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesEx(
      local_path_, GetFileExInfoStandard, &data)) {
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

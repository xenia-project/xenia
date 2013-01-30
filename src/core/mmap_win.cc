/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/mmap.h>


typedef struct xe_mmap {
  xe_ref_t ref;

  HANDLE file_handle;
  HANDLE mmap_handle;

  void* addr;
  size_t length;
} xe_mmap_t;


xe_mmap_ref xe_mmap_open(xe_pal_ref pal, const xe_file_mode mode,
                         const xechar_t *path,
                         const size_t offset, const size_t length) {
  xe_mmap_ref mmap = (xe_mmap_ref)xe_calloc(sizeof(xe_mmap_t));
  xe_ref_init((xe_ref)mmap);

  DWORD fileAccess      = 0;
  DWORD fileShare       = 0;
  DWORD createMode      = 0;
  DWORD mappingProtect  = 0;
  DWORD viewAccess      = 0;
  switch (mode) {
  case kXEFileModeRead:
      fileAccess      |= GENERIC_READ;
      fileShare       |= FILE_SHARE_READ;
      createMode      |= OPEN_EXISTING;
      mappingProtect  |= PAGE_READONLY;
      viewAccess      |= FILE_MAP_READ;
      break;
  case kXEFileModeWrite:
      fileAccess      |= GENERIC_WRITE;
      fileShare       |= 0;
      createMode      |= OPEN_ALWAYS;
      mappingProtect  |= PAGE_READWRITE;
      viewAccess      |= FILE_MAP_WRITE;
      break;
  case kXEFileModeRead | kXEFileModeWrite:
      fileAccess      |= GENERIC_READ | GENERIC_WRITE;
      fileShare       |= 0;
      createMode      |= OPEN_EXISTING;
      mappingProtect  |= PAGE_READWRITE;
      viewAccess      |= FILE_MAP_READ | FILE_MAP_WRITE;
      break;
  }

  SYSTEM_INFO systemInfo;
  GetSystemInfo(&systemInfo);

  const size_t aligned_offset =
      offset & (~(systemInfo.dwAllocationGranularity - 1));
  const size_t aligned_length =
      length + (offset - aligned_offset);

  HANDLE file_handle = CreateFile(
      path, fileAccess, fileShare, NULL, createMode, FILE_ATTRIBUTE_NORMAL,
      NULL);
  XEEXPECTNOTNULL(file_handle);

  HANDLE handle = CreateFileMapping(
      file_handle, NULL, mappingProtect, 0, 0, NULL);
  //(DWORD)(aligned_length >> 32), (DWORD)(aligned_length & 0xFFFFFFFF), NULL);
  XEEXPECTNOTNULL(handle);

  void* address = MapViewOfFile(handle, viewAccess,
                                (DWORD)(aligned_offset >> 32),
                                (DWORD)(aligned_offset & 0xFFFFFFFF),
                                aligned_length);
  XEEXPECTNOTNULL(address);

  mmap->file_handle = file_handle;
  mmap->mmap_handle = handle;
  mmap->addr        = address;
  if (!length) {
    mmap->length = length;
  } else {
    size_t map_length = GetFileSize(file_handle, NULL);
    mmap->length = map_length;
  }
  
  return mmap;

XECLEANUP:
  xe_mmap_release(mmap);
  return NULL;
}

void xe_mmap_dealloc(xe_mmap_ref mmap) {
  if (mmap->addr) {
    UnmapViewOfFile(mmap->addr);
    mmap->addr = NULL;
  }

  if (mmap->mmap_handle) {
    CloseHandle(mmap->mmap_handle);
    mmap->mmap_handle = NULL;
  }

  if (mmap->file_handle) {
    CloseHandle(mmap->file_handle);
    mmap->file_handle = NULL;
  }
}

xe_mmap_ref xe_mmap_retain(xe_mmap_ref mmap) {
  xe_ref_retain((xe_ref)mmap);
  return mmap;
}

void xe_mmap_release(xe_mmap_ref mmap) {
  xe_ref_release((xe_ref)mmap, (xe_ref_dealloc_t)xe_mmap_dealloc);
}

void* xe_mmap_get_addr(xe_mmap_ref mmap) {
  return mmap->addr;
}

size_t xe_mmap_get_length(xe_mmap_ref mmap) {
  return mmap->length;
}

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/mmap.h>

#include <sys/mman.h>


typedef struct xe_mmap {
  xe_ref_t ref;

  void* file_handle;
  void* mmap_handle;

  void* addr;
  size_t length;
} xe_mmap_t;


xe_mmap_ref xe_mmap_open(const xe_file_mode mode, const xechar_t *path,
                         const size_t offset, const size_t length) {
  xe_mmap_ref mmap = (xe_mmap_ref)xe_calloc(sizeof(xe_mmap_t));
  xe_ref_init((xe_ref)mmap);

  xechar_t mode_string[10];
  mode_string[0] = 0;
  if (mode & kXEFileModeRead) {
    XEIGNORE(xestrcat(mode_string, XECOUNT(mode_string), XT("r")));
  }
  if (mode & kXEFileModeWrite) {
    XEIGNORE(xestrcat(mode_string, XECOUNT(mode_string), XT("w")));
  }
  XEIGNORE(xestrcat(mode_string, XECOUNT(mode_string), XT("b")));

  int prot = 0;
  if (mode & kXEFileModeRead) {
    prot |= PROT_READ;
  }
  if (mode & kXEFileModeWrite) {
    prot |= PROT_WRITE;
  }

  FILE* file_handle = fopen(path, mode_string);
  mmap->file_handle = file_handle;
  XEEXPECTNOTNULL(mmap->file_handle);

  size_t map_length;
  map_length = length;
  if (!length) {
    fseeko(file_handle, 0, SEEK_END);
    map_length = ftello(file_handle);
  }
  mmap->length = map_length;

  mmap->addr = ::mmap(0, map_length, prot, MAP_SHARED, fileno(file_handle),
                      offset);
  XEEXPECTNOTNULL(mmap->addr);

  return mmap;

XECLEANUP:
  xe_mmap_release(mmap);
  return NULL;
}

void xe_mmap_dealloc(xe_mmap_ref mmap) {
  if (mmap->addr) {
    munmap(mmap->addr, mmap->length);
    mmap->addr = NULL;
  }

  FILE* file_handle = (FILE*)mmap->file_handle;
  if (file_handle) {
    fclose(file_handle);
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

uint8_t* xe_mmap_get_addr(xe_mmap_ref mmap) {
  return (uint8_t*)mmap->addr;
}

size_t xe_mmap_get_length(xe_mmap_ref mmap) {
  return mmap->length;
}

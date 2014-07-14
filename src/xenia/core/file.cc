/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/file.h>


typedef struct xe_file {
  xe_ref_t ref;

  void* handle;
} xe_file_t;


#if XE_LIKE_WIN32
#define fseeko                          _fseeki64
#define ftello                          _ftelli64
#endif  // WIN32


xe_file_ref xe_file_open(const xe_file_mode mode, const xechar_t *path) {
  xe_file_ref file = (xe_file_ref)xe_calloc(sizeof(xe_file_t));
  xe_ref_init((xe_ref)file);

  xechar_t mode_string[10];
  mode_string[0] = 0;
  if (mode & kXEFileModeRead) {
    XEIGNORE(xestrcat(mode_string, XECOUNT(mode_string), L"r"));
  }
  if (mode & kXEFileModeWrite) {
    XEIGNORE(xestrcat(mode_string, XECOUNT(mode_string), L"w"));
  }
  XEIGNORE(xestrcat(mode_string, XECOUNT(mode_string), L"b"));

#if XE_LIKE_WIN32 && XE_WCHAR
  XEEXPECTZERO(_wfopen_s((FILE**)&file->handle, path, mode_string));
#else
  file->handle = fopen(path, mode_string);
  XEEXPECTNOTNULL(file->handle);
#endif  // WIN32

  return file;

XECLEANUP:
  xe_file_release(file);
  return NULL;
}

void xe_file_dealloc(xe_file_ref file) {
  FILE* handle = (FILE*)file->handle;
  fclose(handle);
  file->handle = NULL;
}

xe_file_ref xe_file_retain(xe_file_ref file) {
  xe_ref_retain((xe_ref)file);
  return file;
}

void xe_file_release(xe_file_ref file) {
  xe_ref_release((xe_ref)file, (xe_ref_dealloc_t)xe_file_dealloc);
}

size_t xe_file_get_length(xe_file_ref file) {
  FILE* handle = (FILE*)file->handle;

  fseeko(handle, 0, SEEK_END);
  return ftello(handle);
}

size_t xe_file_read(xe_file_ref file, const size_t offset,
                    uint8_t *buffer, const size_t buffer_size) {
  FILE* handle = (FILE*)file->handle;
  if (fseeko(handle, offset, SEEK_SET) != 0) {
    return -1;
  }

  return fread(buffer, 1, buffer_size, handle);
}

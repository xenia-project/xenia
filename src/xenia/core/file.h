/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_FILE_H_
#define XENIA_CORE_FILE_H_

#include <xenia/common.h>
#include <xenia/core/path.h>
#include <xenia/core/ref.h>


struct xe_file;
typedef struct xe_file* xe_file_ref;


typedef enum {
  kXEFileModeRead = (1 << 0),
  kXEFileModeWrite = (1 << 1),
} xe_file_mode;


xe_file_ref xe_file_open(const xe_file_mode mode, const xechar_t *path);
xe_file_ref xe_file_retain(xe_file_ref file);
void xe_file_release(xe_file_ref file);
size_t xe_file_get_length(xe_file_ref file);
size_t xe_file_read(xe_file_ref file, const size_t offset,
                    uint8_t *buffer, const size_t buffer_size);


#endif  // XENIA_CORE_FILE_H_

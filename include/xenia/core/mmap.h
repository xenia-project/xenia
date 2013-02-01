/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_MMAP_H_
#define XENIA_CORE_MMAP_H_

#include <xenia/common.h>
#include <xenia/core/file.h>
#include <xenia/core/pal.h>
#include <xenia/core/ref.h>


struct xe_mmap;
typedef struct xe_mmap* xe_mmap_ref;


xe_mmap_ref xe_mmap_open(xe_pal_ref pal, const xe_file_mode mode,
                         const xechar_t *path,
                         const size_t offset, const size_t length);
xe_mmap_ref xe_mmap_retain(xe_mmap_ref mmap);
void xe_mmap_release(xe_mmap_ref mmap);
uint8_t* xe_mmap_get_addr(xe_mmap_ref mmap);
size_t xe_mmap_get_length(xe_mmap_ref mmap);


#endif  // XENIA_CORE_MMAP_H_

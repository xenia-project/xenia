/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XEX2_H_
#define XENIA_KERNEL_XEX2_H_

#include <xenia/core.h>

#include <xenia/kernel/xex2_info.h>

typedef struct {
  int reserved;
} xe_xex2_options_t;

struct xe_xex2;
typedef struct xe_xex2* xe_xex2_ref;

typedef struct {
  uint32_t ordinal;
  uint32_t value_address;   // address to place value
  uint32_t thunk_address;   // NULL or address of thunk
} xe_xex2_import_info_t;


xe_xex2_ref xe_xex2_load(xe_memory_ref memory,
                         const void* addr, const size_t length,
                         xe_xex2_options_t options);
xe_xex2_ref xe_xex2_retain(xe_xex2_ref xex);
void xe_xex2_release(xe_xex2_ref xex);

const xechar_t *xe_xex2_get_name(xe_xex2_ref xex);
const xe_xex2_header_t *xe_xex2_get_header(xe_xex2_ref xex);

int xe_xex2_get_import_infos(xe_xex2_ref xex,
                             const xe_xex2_import_library_t *library,
                             xe_xex2_import_info_t **out_import_infos,
                             size_t *out_import_info_count);


#endif  // XENIA_KERNEL_XEX2_H_

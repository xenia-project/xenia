/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_MEMORY_H_
#define XENIA_CORE_MEMORY_H_

#include <xenia/common.h>
#include <xenia/core/pal.h>
#include <xenia/core/ref.h>


typedef struct {
  int reserved;
} xe_memory_options_t;


struct xe_memory;
typedef struct xe_memory* xe_memory_ref;


xe_memory_ref xe_memory_create(xe_pal_ref pal, xe_memory_options_t options);
xe_memory_ref xe_memory_retain(xe_memory_ref memory);
void xe_memory_release(xe_memory_ref memory);

size_t xe_memory_get_length(xe_memory_ref memory);
uint8_t *xe_memory_addr(xe_memory_ref memory, uint32_t guest_addr);


#endif  // XENIA_CORE_MEMORY_H_

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
#include <xenia/core/ref.h>


typedef struct {
  int reserved;
} xe_memory_options_t;


struct xe_memory;
typedef struct xe_memory* xe_memory_ref;


xe_memory_ref xe_memory_create(xe_memory_options_t options);
xe_memory_ref xe_memory_retain(xe_memory_ref memory);
void xe_memory_release(xe_memory_ref memory);

uint8_t *xe_memory_addr(xe_memory_ref memory, size_t guest_addr = 0);

void xe_memory_copy(xe_memory_ref memory,
                    uint32_t dest, uint32_t src, uint32_t size);

uint32_t xe_memory_search_aligned(xe_memory_ref memory, size_t start,
                                  size_t end, const uint32_t *values,
                                  const size_t value_count);

// These methods slice off memory from the virtual address space, unless
// base_address is specified in which case they place into an address in
// memory.

enum {
  XE_MEMORY_FLAG_64KB_PAGES = (1 << 1),
};

enum {
  XE_MEMORY_ACCESS_READ     = (1 << 1),
  XE_MEMORY_ACCESS_WRITE    = (1 << 2)
};

void xe_memory_heap_dump(xe_memory_ref memory);

uint32_t xe_memory_heap_alloc(xe_memory_ref memory, uint32_t base_address,
                              uint32_t size, uint32_t flags,
                              uint32_t alignment = 0x20);
int  xe_memory_heap_free(xe_memory_ref memory, uint32_t addr, uint32_t size);

bool xe_memory_is_valid(xe_memory_ref memory, uint32_t address);
int xe_memory_protect(xe_memory_ref memory, uint32_t address, uint32_t size,
                      uint32_t access);

#endif  // XENIA_CORE_MEMORY_H_

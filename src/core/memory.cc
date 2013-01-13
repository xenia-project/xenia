/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/memory.h>

#include <sys/mman.h>


/**
 * Memory map:
 * 0x00000000 - 0x40000000 (1024mb) - virtual 4k pages
 * 0x40000000 - 0x80000000 (1024mb) - virtual 64k pages
 * 0x80000000 - 0x8C000000 ( 192mb) - xex 64k pages
 * 0x8C000000 - 0x90000000 (  64mb) - xex 64k pages (encrypted)
 * 0x90000000 - 0xA0000000 ( 256mb) - xex 4k pages
 * 0xA0000000 - 0xC0000000 ( 512mb) - physical 64k pages
 *
 * We use the host OS to create an entire addressable range for this. That way
 * we don't have to emulate a TLB. It'd be really cool to pass through page
 * sizes or use madvice to let the OS know what to expect.
 */


struct xe_memory {
  xe_ref_t ref;

  size_t    length;
  void      *ptr;
};


xe_memory_ref xe_memory_create(xe_pal_ref pal, xe_memory_options_t options) {
  xe_memory_ref memory = (xe_memory_ref)xe_calloc(sizeof(xe_memory));
  xe_ref_init((xe_ref)memory);

  memory->length = 0xC0000000;
  memory->ptr = mmap(0, memory->length, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON, -1, 0);
  XEEXPECTNOTNULL(memory->ptr);
  XEEXPECT(memory->ptr != MAP_FAILED);

  return memory;

XECLEANUP:
  xe_memory_release(memory);
  return NULL;
}

void xe_memory_dealloc(xe_memory_ref memory) {
  munmap(memory->ptr, memory->length);
}

xe_memory_ref xe_memory_retain(xe_memory_ref memory) {
  xe_ref_retain((xe_ref)memory);
  return memory;
}

void xe_memory_release(xe_memory_ref memory) {
  xe_ref_release((xe_ref)memory, (xe_ref_dealloc_t)xe_memory_dealloc);
}

size_t xe_memory_get_length(xe_memory_ref memory) {
  return memory->length;
}

void* xe_memory_addr(xe_memory_ref memory, uint32_t guest_addr) {
  return (uint8_t*)memory->ptr + guest_addr;
}

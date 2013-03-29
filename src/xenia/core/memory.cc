/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/memory.h>

#include <xenia/core/mutex.h>

#if !XE_PLATFORM(WIN32)
#include <sys/mman.h>
#endif  // WIN32

#define MSPACES                 1
#define USE_LOCKS               0
#define USE_DL_PREFIX           1
#define HAVE_MORECORE           0
#define HAVE_MMAP               0
#define HAVE_MREMAP             0
#define malloc_getpagesize      4096
#define DEFAULT_GRANULARITY     64 * 1024
#define DEFAULT_TRIM_THRESHOLD  MAX_SIZE_T
#include <third_party/dlmalloc/malloc.c.h>


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

  size_t      length;
  void*       ptr;

  xe_mutex_t* heap_mutex;
  mspace      heap;
};


xe_memory_ref xe_memory_create(xe_memory_options_t options) {
  uint32_t offset;
  uint32_t mspace_size;

  xe_memory_ref memory = (xe_memory_ref)xe_calloc(sizeof(xe_memory));
  xe_ref_init((xe_ref)memory);

  memory->length = 0xC0000000;

#if XE_PLATFORM(WIN32)
  memory->ptr = VirtualAlloc(0, memory->length,
                             MEM_COMMIT | MEM_RESERVE,
                             PAGE_READWRITE);
#else
  memory->ptr = mmap(0, memory->length, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON, -1, 0);
  XEEXPECT(memory->ptr != MAP_FAILED);
#endif  // WIN32
  XEEXPECTNOTNULL(memory->ptr);

  memory->heap_mutex = xe_mutex_alloc(0);
  XEEXPECTNOTNULL(memory->heap_mutex);

  // Allocate the mspace for our heap.
  // We skip the first page to make writes to 0 easier to find.
  offset = 64 * 1024;
  mspace_size = 512 * 1024 * 1024 - offset;
  memory->heap = create_mspace_with_base(
      (uint8_t*)memory->ptr + offset, mspace_size, 0);

  return memory;

XECLEANUP:
  xe_memory_release(memory);
  return NULL;
}

void xe_memory_dealloc(xe_memory_ref memory) {
  if (memory->heap_mutex && memory->heap) {
    xe_mutex_lock(memory->heap_mutex);
    destroy_mspace(memory->heap);
    memory->heap = NULL;
    xe_mutex_unlock(memory->heap_mutex);
  }
  if (memory->heap_mutex) {
    xe_mutex_free(memory->heap_mutex);
    memory->heap_mutex = NULL;
  }

#if XE_PLATFORM(WIN32)
  XEIGNORE(VirtualFree(memory->ptr, memory->length, MEM_RELEASE));
#else
  munmap(memory->ptr, memory->length);
#endif  // WIN32
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

uint8_t *xe_memory_addr(xe_memory_ref memory, size_t guest_addr) {
  return (uint8_t*)memory->ptr + guest_addr;
}

uint32_t xe_memory_search_aligned(xe_memory_ref memory, size_t start,
                                  size_t end, const uint32_t *values,
                                  const size_t value_count) {
  XEASSERT(start <= end);
  const uint32_t *p = (const uint32_t*)xe_memory_addr(memory, start);
  const uint32_t *pe = (const uint32_t*)xe_memory_addr(memory, end);
  while (p != pe) {
    if (*p == values[0]) {
      const uint32_t *pc = p + 1;
      size_t matched = 1;
      for (size_t n = 1; n < value_count; n++, pc++) {
        if (*pc != values[n]) {
          break;
        }
        matched++;
      }
      if (matched == value_count) {
        return (uint32_t)((uint8_t*)p - (uint8_t*)memory->ptr);
      }
    }
    p++;
  }
  return 0;
}

// TODO(benvanik): reserve/commit states/large pages/etc

uint32_t xe_memory_heap_alloc(xe_memory_ref memory, uint32_t base_addr,
                              uint32_t size, uint32_t flags) {
  if (base_addr) {
    return base_addr;
  }
  XEASSERT(base_addr == 0);
  XEASSERT(flags == 0);

  XEIGNORE(xe_mutex_lock(memory->heap_mutex));
  uint8_t* p = (uint8_t*)mspace_malloc(memory->heap, size);
  XEIGNORE(xe_mutex_unlock(memory->heap_mutex));
  if (!p) {
    return 0;
  }

  return (uint32_t)((uintptr_t)p - (uintptr_t)memory->ptr);
}

uint32_t xe_memory_heap_free(xe_memory_ref memory, uint32_t addr,
                             uint32_t flags) {
  XEASSERT(flags == 0);

  uint8_t* p = (uint8_t*)memory->ptr + addr;
  size_t real_size = mspace_usable_size(p);
  if (!real_size) {
    return 0;
  }

  XEIGNORE(xe_mutex_lock(memory->heap_mutex));
  mspace_free(memory->heap, p);
  XEIGNORE(xe_mutex_unlock(memory->heap_mutex));

  return (uint32_t)real_size;
}

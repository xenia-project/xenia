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
#define HAVE_MREMAP             0
#define malloc_getpagesize      4096
#define DEFAULT_GRANULARITY     64 * 1024
#define DEFAULT_TRIM_THRESHOLD  MAX_SIZE_T
#define MALLOC_ALIGNMENT        32
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
 *
 * We create our own heap of committed memory that lives at XE_MEMORY_HEAP_LOW
 * to XE_MEMORY_HEAP_HIGH - all normal user allocations come from there. Since
 * the Xbox has no paging, we know that the size of this heap will never need
 * to be larger than ~512MB (realistically, smaller than that). We place it far
 * away from the XEX data and keep the memory around it uncommitted so that we
 * have some warning if things go astray.
 *
 * For XEX/GPU/etc data we allow placement allocations (base_address != 0) and
 * commit the requested memory as needed. This bypasses the standard heap, but
 * XEXs should never be overwriting anything so that's fine. We can also query
 * for previous commits and assert that we really isn't committing twice.
 */

#define XE_MEMORY_HEAP_LOW    0x20000000
#define XE_MEMORY_HEAP_HIGH   0x40000000


struct xe_memory {
  xe_ref_t ref;

  size_t      system_page_size;

  size_t      length;
  void*       ptr;

  xe_mutex_t* heap_mutex;
  mspace      heap;
};


xe_memory_ref xe_memory_create(xe_memory_options_t options) {

  xe_memory_ref memory = (xe_memory_ref)xe_calloc(sizeof(xe_memory));
  xe_ref_init((xe_ref)memory);

#if XE_PLATFORM(WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  memory->system_page_size = si.dwPageSize;
#else
#error need to implement page size retrieval
#endif  // WIN32

  memory->length = 0xC0000000;

#if XE_PLATFORM(WIN32)
  // Reserve the entire usable address space.
  // We're 64-bit, so this should be no problem.
  memory->ptr = VirtualAlloc(0, memory->length,
                             MEM_RESERVE,
                             PAGE_READWRITE);
  XEEXPECTNOTNULL(memory->ptr);
#else
  memory->ptr = mmap(0, memory->length, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON, -1, 0);
  XEEXPECT(memory->ptr != MAP_FAILED);
  XEEXPECTNOTNULL(memory->ptr);
#endif  // WIN32

  // Lock used around heap allocs/frees.
  memory->heap_mutex = xe_mutex_alloc(10000);
  XEEXPECTNOTNULL(memory->heap_mutex);

  // Commit the memory where our heap will live.
  // We don't allocate at 0 to make bad writes easier to find.
  uint32_t heap_offset = XE_MEMORY_HEAP_LOW;
  uint32_t heap_size = XE_MEMORY_HEAP_HIGH - XE_MEMORY_HEAP_LOW;
  uint8_t* heap_ptr = (uint8_t*)memory->ptr + heap_offset;
#if XE_PLATFORM(WIN32)
  void* heap_result = VirtualAlloc(heap_ptr, heap_size,
                                   MEM_COMMIT, PAGE_READWRITE);
  XEEXPECTNOTNULL(heap_result);
#else
#error ?
#endif  // WIN32

  // Allocate the mspace for our heap.
  memory->heap = create_mspace_with_base(heap_ptr, heap_size, 0);

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
  // This decommits all pages and releases everything.
  XEIGNORE(VirtualFree(memory->ptr, 0, MEM_RELEASE));
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

uint32_t xe_memory_heap_alloc(
    xe_memory_ref memory, uint32_t base_address, uint32_t size,
    uint32_t flags) {
  XEASSERT(flags == 0);

  // If we were given a base address we are outside of the normal heap and
  // will place wherever asked (so long as it doesn't overlap the heap).
  if (!base_address) {
    // Normal allocation from the managed heap.
    XEIGNORE(xe_mutex_lock(memory->heap_mutex));
    uint8_t* p = (uint8_t*)mspace_malloc(memory->heap, size);
    XEIGNORE(xe_mutex_unlock(memory->heap_mutex));
    if (!p) {
      return 0;
    }
    return (uint32_t)((uintptr_t)p - (uintptr_t)memory->ptr);
  } else {
    if (base_address >= XE_MEMORY_HEAP_LOW &&
        base_address < XE_MEMORY_HEAP_HIGH) {
      // Overlapping managed heap.
      XEASSERTALWAYS();
      return 0;
    }

    uint8_t* p = (uint8_t*)memory->ptr + base_address;
#if XE_PLATFORM(WIN32)
    // TODO(benvanik): check if address range is in use with a query.

    void* pv = VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE);
    if (!pv) {
      // Failed.
      XEASSERTALWAYS();
      return 0;
    }
#else
#error ?
#endif  // WIN32

    return base_address;
  }
}

int xe_memory_heap_free(
    xe_memory_ref memory, uint32_t address, uint32_t size) {
  uint8_t* p = (uint8_t*)memory->ptr + address;
  if (address >= XE_MEMORY_HEAP_LOW && address < XE_MEMORY_HEAP_HIGH) {
    // Heap allocated address.
    size_t real_size = mspace_usable_size(p);
    if (!real_size) {
      return 0;
    }

    XEIGNORE(xe_mutex_lock(memory->heap_mutex));
    mspace_free(memory->heap, p);
    XEIGNORE(xe_mutex_unlock(memory->heap_mutex));
    return (uint32_t)real_size;
  } else {
    // A placed address. Decommit.
#if XE_PLATFORM(WIN32)
    return VirtualFree(p, size, MEM_DECOMMIT) ? 0 : 1;
#else
#error decommit
#endif  // WIN32
  }
}

bool xe_memory_is_valid(xe_memory_ref memory, uint32_t address) {
  uint8_t* p = (uint8_t*)memory->ptr + address;
  if (address >= XE_MEMORY_HEAP_LOW && address < XE_MEMORY_HEAP_HIGH) {
    // Within heap range, ask dlmalloc.
    return mspace_usable_size(p) > 0;
  } else {
    // Maybe -- could Query here (though that may be expensive).
    return true;
  }
}

int xe_memory_protect(
    xe_memory_ref memory, uint32_t address, uint32_t size, uint32_t access) {
  uint8_t* p = (uint8_t*)memory->ptr + address;

#if XE_PLATFORM(WIN32)
  DWORD new_protect = 0;
  if (access & XE_MEMORY_ACCESS_WRITE) {
    new_protect = PAGE_READWRITE;
  } else if (access & XE_MEMORY_ACCESS_READ) {
    new_protect = PAGE_READONLY;
  } else {
    new_protect = PAGE_NOACCESS;
  }
  DWORD old_protect;
  return VirtualProtect(p, size, new_protect, &old_protect) == TRUE ? 0 : 1;
#else
  int prot = 0;
  if (access & XE_MEMORY_ACCESS_READ) {
    prot = PROT_READ;
  }
  if (access & XE_MEMORY_ACCESS_WRITE) {
    prot = PROT_WRITE;
  }
  return mprotect(p, size, prot);
#endif  // WIN32
}

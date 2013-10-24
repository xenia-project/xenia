/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/memory.h>

#include <gflags/gflags.h>
#include <xenia/core/mutex.h>

// TODO(benvanik): move xbox.h out
#include <xenia/xbox.h>

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
#define MALLOC_INSPECT_ALL      1
#if XE_DEBUG
#define FOOTERS                 0
#endif  // XE_DEBUG
#include <third_party/dlmalloc/malloc.c.h>

DEFINE_bool(
    log_heap, false,
    "Log heap structure on alloc/free.");
DEFINE_uint64(
    heap_guard_pages, 0,
    "Allocate the given number of guard pages around all heap chunks.");


/**
 * Memory map:
 * 0x00000000 - 0x3FFFFFFF (1024mb) - virtual 4k pages
 * 0x40000000 - 0x7FFFFFFF (1024mb) - virtual 64k pages
 * 0x80000000 - 0x8BFFFFFF ( 192mb) - xex 64k pages
 * 0x8C000000 - 0x8FFFFFFF (  64mb) - xex 64k pages (encrypted)
 * 0x90000000 - 0x9FFFFFFF ( 256mb) - xex 4k pages
 * 0xA0000000 - 0xBFFFFFFF ( 512mb) - physical 64k pages
 * 0xC0000000 - 0xDFFFFFFF          - physical 16mb pages
 * 0xE0000000 - 0xFFFFFFFF          - physical 4k pages
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
 *
 * GPU memory is mapped onto the lower 512mb of the virtual 4k range (0).
 * So 0xA0000000 = 0x00000000. A more sophisticated allocator could handle
 * this.
 */

#define XE_MEMORY_PHYSICAL_HEAP_LOW   0x00010000
#define XE_MEMORY_PHYSICAL_HEAP_HIGH  0x20000000
#define XE_MEMORY_VIRTUAL_HEAP_LOW    0x20000000
#define XE_MEMORY_VIRTUAL_HEAP_HIGH   0x40000000


typedef struct {
  xe_memory_ref memory;
  bool          physical;
  xe_mutex_t*   mutex;
  size_t        size;
  uint8_t*      ptr;
  mspace        space;

  int Initialize(xe_memory_ref memory, uint32_t low, uint32_t high,
                 bool physical);
  void Cleanup();
  void Dump();
  uint32_t Alloc(uint32_t base_address,
                 uint32_t size, uint32_t flags,
                 uint32_t alignment);
  uint32_t Free(uint32_t address, uint32_t size);

private:
  static void DumpHandler(
      void* start, void* end, size_t used_bytes, void* context);
} xe_memory_heap_t;


struct xe_memory {
  xe_ref_t ref;

  size_t        system_page_size;

  HANDLE        mapping;
  uint8_t*      mapping_base;
  union {
    struct {
      uint8_t*  v00000000;
      uint8_t*  v40000000;
      uint8_t*  v80000000;
      uint8_t*  vA0000000;
      uint8_t*  vC0000000;
      uint8_t*  vE0000000;
    };
    uint8_t*    all_views[6];
  } views;

  xe_memory_heap_t virtual_heap;
  xe_memory_heap_t physical_heap;
};


int xe_memory_map_views(xe_memory_ref memory, uint8_t* mapping_base);
void xe_memory_unmap_views(xe_memory_ref memory);


xe_memory_ref xe_memory_create(xe_memory_options_t options) {
  xe_memory_ref memory = (xe_memory_ref)xe_calloc(sizeof(xe_memory));
  xe_ref_init((xe_ref)memory);

  SYSTEM_INFO si;
  GetSystemInfo(&si);
  memory->system_page_size = si.dwPageSize;

  // Create main page file-backed mapping. This is all reserved but
  // uncommitted (so it shouldn't expand page file).
  memory->mapping = CreateFileMapping(
      INVALID_HANDLE_VALUE,
      NULL,
      PAGE_READWRITE | SEC_RESERVE,
      1, 0, // entire 4gb space
      NULL);
  if (!memory->mapping) {
    XELOGE("Unable to reserve the 4gb guest address space.");
    XEASSERTNOTNULL(memory->mapping);
    XEFAIL();
  }

  // Attempt to create our views. This may fail at the first address
  // we pick, so try a few times.
  memory->mapping_base = 0;
  for (size_t n = 32; n < 64; n++) {
    uint8_t* mapping_base = (uint8_t*)(1ull << n);
    if (!xe_memory_map_views(memory, mapping_base)) {
      memory->mapping_base = mapping_base;
      break;
    }
  }
  if (!memory->mapping_base) {
    XELOGE("Unable to find a continuous block in the 64bit address space.");
    XEASSERTALWAYS();
    XEFAIL();
  }

  // Prepare heaps.
  memory->virtual_heap.Initialize(
      memory, XE_MEMORY_VIRTUAL_HEAP_LOW, XE_MEMORY_VIRTUAL_HEAP_HIGH,
      false);
  memory->physical_heap.Initialize(
      memory, XE_MEMORY_PHYSICAL_HEAP_LOW, XE_MEMORY_PHYSICAL_HEAP_HIGH,
      true);

  // GPU writeback.
  VirtualAlloc(
      memory->mapping_base + 0xC0000000, 0x00100000,
      MEM_COMMIT, PAGE_READWRITE);

  return memory;

XECLEANUP:
  xe_memory_release(memory);
  return NULL;
}

void xe_memory_dealloc(xe_memory_ref memory) {
  // GPU writeback.
  VirtualFree(
      memory->mapping_base + 0xC0000000, 0x00100000,
      MEM_DECOMMIT);

  // Cleanup heaps.
  memory->virtual_heap.Cleanup();
  memory->physical_heap.Cleanup();

  // Unmap all views and close mapping.
  if (memory->mapping) {
    xe_memory_unmap_views(memory);
    CloseHandle(memory->mapping);
  }
}

int xe_memory_map_views(xe_memory_ref memory, uint8_t* mapping_base) {
  static struct {
    uint32_t  virtual_address_start;
    uint32_t  virtual_address_end;
    uint32_t  target_address;
  } map_info[] = {
    0x00000000, 0x3FFFFFFF, 0x00000000, // (1024mb) - virtual 4k pages
    0x40000000, 0x7FFFFFFF, 0x40000000, // (1024mb) - virtual 64k pages
    0x80000000, 0x9FFFFFFF, 0x80000000, //  (512mb) - xex pages
    0xA0000000, 0xBFFFFFFF, 0x00000000, //  (512mb) - physical 64k pages
    0xC0000000, 0xDFFFFFFF, 0x00000000, //          - physical 16mb pages
    0xE0000000, 0xFFFFFFFF, 0x00000000, //          - physical 4k pages
  };
  XEASSERT(XECOUNT(map_info) == XECOUNT(memory->views.all_views));
  for (size_t n = 0; n < XECOUNT(map_info); n++) {
    memory->views.all_views[n] = (uint8_t*)MapViewOfFileEx(
        memory->mapping,
        FILE_MAP_ALL_ACCESS,
        0x00000000, map_info[n].target_address,
        map_info[n].virtual_address_end - map_info[n].virtual_address_start + 1,
        mapping_base + map_info[n].virtual_address_start);
    XEEXPECTNOTNULL(memory->views.all_views[n]);
  }
  return 0;

XECLEANUP:
  xe_memory_unmap_views(memory);
  return 1;
}

void xe_memory_unmap_views(xe_memory_ref memory) {
  for (size_t n = 0; n < XECOUNT(memory->views.all_views); n++) {
    if (memory->views.all_views[n]) {
      UnmapViewOfFile(memory->views.all_views[n]);
    }
  }
}

xe_memory_ref xe_memory_retain(xe_memory_ref memory) {
  xe_ref_retain((xe_ref)memory);
  return memory;
}

void xe_memory_release(xe_memory_ref memory) {
  xe_ref_release((xe_ref)memory, (xe_ref_dealloc_t)xe_memory_dealloc);
}

uint8_t *xe_memory_addr(xe_memory_ref memory, size_t guest_addr) {
  return memory->mapping_base + guest_addr;
}

void xe_memory_copy(xe_memory_ref memory,
                    uint32_t dest, uint32_t src, uint32_t size) {
  uint8_t* pdest = memory->mapping_base + dest;
  uint8_t* psrc = memory->mapping_base + src;
  XEIGNORE(xe_copy_memory(pdest, size, psrc, size));
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
        return (uint32_t)((uint8_t*)p - memory->mapping_base);
      }
    }
    p++;
  }
  return 0;
}

uint32_t xe_memory_heap_alloc(
    xe_memory_ref memory, uint32_t base_address, uint32_t size,
    uint32_t flags, uint32_t alignment) {
  // If we were given a base address we are outside of the normal heap and
  // will place wherever asked (so long as it doesn't overlap the heap).
  if (!base_address) {
    // Normal allocation from the managed heap.
    uint32_t result;
    if (flags & XE_MEMORY_FLAG_PHYSICAL) {
      result = memory->physical_heap.Alloc(
          base_address, size, flags, alignment);
    } else {
      result = memory->virtual_heap.Alloc(
          base_address, size, flags, alignment);
    }
    if (result) {
      if (flags & XE_MEMORY_FLAG_ZERO) {
        xe_zero_struct(memory->mapping_base + result, size);
      }
    }
    return result;
  } else {
    if (base_address >= XE_MEMORY_VIRTUAL_HEAP_LOW &&
        base_address < XE_MEMORY_VIRTUAL_HEAP_HIGH) {
      // Overlapping managed heap.
      XEASSERTALWAYS();
      return 0;
    }
    if (base_address >= XE_MEMORY_PHYSICAL_HEAP_LOW &&
        base_address < XE_MEMORY_PHYSICAL_HEAP_HIGH) {
      // Overlapping managed heap.
      XEASSERTALWAYS();
      return 0;
    }

    uint8_t* p = memory->mapping_base + base_address;
    // TODO(benvanik): check if address range is in use with a query.

    void* pv = VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE);
    if (!pv) {
      // Failed.
      XEASSERTALWAYS();
      return 0;
    }

    if (flags & XE_MEMORY_FLAG_ZERO) {
      xe_zero_struct(pv, size);
    }

    return base_address;
  }
}

int xe_memory_heap_free(
    xe_memory_ref memory, uint32_t address, uint32_t size) {
  if (address >= XE_MEMORY_VIRTUAL_HEAP_LOW &&
      address < XE_MEMORY_VIRTUAL_HEAP_HIGH) {
    return memory->virtual_heap.Free(address, size);
  } else if (address >= XE_MEMORY_PHYSICAL_HEAP_LOW &&
             address < XE_MEMORY_PHYSICAL_HEAP_HIGH) {
    return memory->physical_heap.Free(address, size);
  } else {
    // A placed address. Decommit.
    uint8_t* p = memory->mapping_base + address;
    return VirtualFree(p, size, MEM_DECOMMIT) ? 0 : 1;
  }
}

bool xe_memory_is_valid(xe_memory_ref memory, uint32_t address) {
  uint8_t* p = memory->mapping_base + address;
  if ((address >= XE_MEMORY_VIRTUAL_HEAP_LOW &&
       address < XE_MEMORY_VIRTUAL_HEAP_HIGH) ||
      (address >= XE_MEMORY_PHYSICAL_HEAP_LOW &&
       address < XE_MEMORY_PHYSICAL_HEAP_HIGH)) {
    // Within heap range, ask dlmalloc.
    size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
    p -= heap_guard_size;
    return mspace_usable_size(p) > 0;
  } else {
    // Maybe -- could Query here (though that may be expensive).
    return true;
  }
}

int xe_memory_protect(
    xe_memory_ref memory, uint32_t address, uint32_t size, uint32_t access) {
  uint8_t* p = memory->mapping_base + address;

  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  p += heap_guard_size;

  DWORD new_protect = access;
  new_protect = new_protect & (
      X_PAGE_NOACCESS | X_PAGE_READONLY | X_PAGE_READWRITE |
      X_PAGE_WRITECOPY | X_PAGE_GUARD | X_PAGE_NOCACHE |
      X_PAGE_WRITECOMBINE);

  DWORD old_protect;
  return VirtualProtect(p, size, new_protect, &old_protect) == TRUE ? 0 : 1;
}


int xe_memory_heap_t::Initialize(
    xe_memory_ref memory, uint32_t low, uint32_t high, bool physical) {
  this->memory = memory;
  this->physical = physical;

  // Lock used around heap allocs/frees.
  mutex = xe_mutex_alloc(10000);
  if (!mutex) {
    return 1;
  }

  // Commit the memory where our heap will live and allocate it.
  // TODO(benvanik): replace dlmalloc with an implementation that can commit
  //     as it goes.
  size = high - low;
  ptr = memory->views.v00000000 + low;
  void* heap_result = VirtualAlloc(
      ptr, size, MEM_COMMIT, PAGE_READWRITE);
  if (!heap_result) {
    return 1;
  }
  space = create_mspace_with_base(ptr, size, 0);
  return 0;
}

void xe_memory_heap_t::Cleanup() {
  if (mutex && space) {
    xe_mutex_lock(mutex);
    destroy_mspace(space);
    space = NULL;
    xe_mutex_unlock(mutex);
  }
  if (mutex) {
    xe_mutex_free(mutex);
    mutex = NULL;
  }

  XEIGNORE(VirtualFree(ptr, 0, MEM_RELEASE));
}

void xe_memory_heap_t::Dump() {
  XELOGI("xe_memory_heap::Dump - %s",
         physical ? "physical" : "virtual");
  if (FLAGS_heap_guard_pages) {
    XELOGI("  (heap guard pages enabled, stats will be wrong)");
  }
  struct mallinfo info = mspace_mallinfo(space);
  XELOGI("    arena: %lld", info.arena);
  XELOGI("  ordblks: %lld", info.ordblks);
  XELOGI("    hblks: %lld", info.hblks);
  XELOGI("   hblkhd: %lld", info.hblkhd);
  XELOGI("  usmblks: %lld", info.usmblks);
  XELOGI(" uordblks: %lld", info.uordblks);
  XELOGI(" fordblks: %lld", info.fordblks);
  XELOGI(" keepcost: %lld", info.keepcost);
  mspace_inspect_all(space, DumpHandler, this);
}

void xe_memory_heap_t::DumpHandler(
  void* start, void* end, size_t used_bytes, void* context) {
  xe_memory_heap_t* heap = (xe_memory_heap_t*)context;
  xe_memory_ref memory = heap->memory;
  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  uint64_t start_addr = (uint64_t)start + heap_guard_size;
  uint64_t end_addr = (uint64_t)end - heap_guard_size;
  uint32_t guest_start =
      (uint32_t)(start_addr - (uintptr_t)memory->mapping_base);
  uint32_t guest_end =
      (uint32_t)(end_addr - (uintptr_t)memory->mapping_base);
  if (used_bytes > 0) {
    XELOGI(" - %.8X-%.8X (%10db) %.16llX-%.16llX - %9db used",
           guest_start, guest_end, (guest_end - guest_start),
           start_addr, end_addr,
           used_bytes);
  } else {
    XELOGI(" -                                 %.16llX-%.16llX - %9db used",
           start_addr, end_addr, used_bytes);
  }
}

uint32_t xe_memory_heap_t::Alloc(
    uint32_t base_address, uint32_t size, uint32_t flags,
    uint32_t alignment) {
  XEIGNORE(xe_mutex_lock(mutex));
  size_t alloc_size = size;
  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  if (heap_guard_size) {
    alignment = (uint32_t)MAX(alignment, heap_guard_size);
    alloc_size = (uint32_t)XEROUNDUP(size, heap_guard_size);
  }
  uint8_t* p = (uint8_t*)mspace_memalign(
      space,
      alignment,
      alloc_size + heap_guard_size * 2);
  if (FLAGS_heap_guard_pages) {
    size_t real_size = mspace_usable_size(p);
    DWORD old_protect;
    VirtualProtect(
        p, heap_guard_size,
        PAGE_NOACCESS, &old_protect);
    p += heap_guard_size;
    VirtualProtect(
        p + alloc_size, heap_guard_size,
        PAGE_NOACCESS, &old_protect);
  }
  if (FLAGS_log_heap) {
    Dump();
  }
  XEIGNORE(xe_mutex_unlock(mutex));
  if (!p) {
    return 0;
  }

  if (physical) {
    // If physical, we need to commit the memory in the physical address ranges
    // so that it can be accessed.
    VirtualAlloc(
        memory->views.vA0000000 + (p - memory->views.v00000000),
        size,
        MEM_COMMIT,
        PAGE_READWRITE);
    VirtualAlloc(
        memory->views.vC0000000 + (p - memory->views.v00000000),
        size,
        MEM_COMMIT,
        PAGE_READWRITE);
    VirtualAlloc(
        memory->views.vE0000000 + (p - memory->views.v00000000),
        size,
        MEM_COMMIT,
        PAGE_READWRITE);
  }

  return (uint32_t)((uintptr_t)p - (uintptr_t)memory->mapping_base);
}

uint32_t xe_memory_heap_t::Free(uint32_t address, uint32_t size) {
  uint8_t* p = memory->mapping_base + address;

  // Heap allocated address.
  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  p -= heap_guard_size;
  size_t real_size = mspace_usable_size(p);
  real_size -= heap_guard_size * 2;
  if (!real_size) {
    return 0;
  }

  XEIGNORE(xe_mutex_lock(mutex));
  if (FLAGS_heap_guard_pages) {
    DWORD old_protect;
    VirtualProtect(
        p, heap_guard_size,
        PAGE_READWRITE, &old_protect);
    VirtualProtect(
        p + heap_guard_size + real_size, heap_guard_size,
        PAGE_READWRITE, &old_protect);
  }
  mspace_free(space, p);
  if (FLAGS_log_heap) {
    Dump();
  }
  XEIGNORE(xe_mutex_unlock(mutex));

  if (physical) {
    // If physical, decommit from physical ranges too.
    VirtualFree(
        memory->views.vA0000000 + (p - memory->views.v00000000),
        size,
        MEM_DECOMMIT);
    VirtualFree(
        memory->views.vC0000000 + (p - memory->views.v00000000),
        size,
        MEM_DECOMMIT);
    VirtualFree(
        memory->views.vE0000000 + (p - memory->views.v00000000),
        size,
        MEM_DECOMMIT);
  }

  return (uint32_t)real_size;
}

uint32_t xe_memory_query_protect(xe_memory_ref memory, uint32_t address) {
  uint8_t* p = memory->mapping_base + address;
  MEMORY_BASIC_INFORMATION info;
  size_t info_size = VirtualQuery((void*)p, &info, sizeof(info));
  if (!info_size) {
    return 0;
  }
  return info.Protect;
}

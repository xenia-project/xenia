/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/xenon_memory.h>

#include <alloy/runtime/tracing.h>

#include <gflags/gflags.h>

using namespace alloy;
using namespace alloy::runtime;
using namespace xe::cpu;

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
DEFINE_bool(
    scribble_heap, false,
    "Scribble 0xCD into all allocated heap memory.");


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
 * We create our own heap of committed memory that lives at
 * XENON_MEMORY_HEAP_LOW to XENON_MEMORY_HEAP_HIGH - all normal user allocations
 * come from there. Since the Xbox has no paging, we know that the size of this
 * heap will never need to be larger than ~512MB (realistically, smaller than
 * that). We place it far away from the XEX data and keep the memory around it
 * uncommitted so that we have some warning if things go astray.
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

#define XENON_MEMORY_PHYSICAL_HEAP_LOW    0x00010000
#define XENON_MEMORY_PHYSICAL_HEAP_HIGH   0x20000000
#define XENON_MEMORY_VIRTUAL_HEAP_LOW     0x20000000
#define XENON_MEMORY_VIRTUAL_HEAP_HIGH    0x40000000


class xe::cpu::XenonMemoryHeap {
public:
  XenonMemoryHeap(XenonMemory* memory, bool is_physical);
  ~XenonMemoryHeap();

  int Initialize(uint64_t low, uint64_t high);

  uint64_t Alloc(uint64_t base_address, size_t size,
                 uint32_t flags, uint32_t alignment);
  uint64_t Free(uint64_t address, size_t size);

  void Dump();

private:
  static uint32_t next_heap_id_;
  static void DumpHandler(
      void* start, void* end, size_t used_bytes, void* context);

private:
  XenonMemory*  memory_;
  uint32_t      heap_id_;
  bool          is_physical_;
  Mutex*        lock_;
  size_t        size_;
  uint8_t*      ptr_;
  mspace        space_;
};
uint32_t XenonMemoryHeap::next_heap_id_ = 1;


XenonMemory::XenonMemory() :
    mapping_(0), mapping_base_(0),
    Memory() {
  virtual_heap_ = new XenonMemoryHeap(this, false);
  physical_heap_ = new XenonMemoryHeap(this, true);
}

XenonMemory::~XenonMemory() {
  if (mapping_base_) {
    // GPU writeback.
    VirtualFree(
        Translate(0xC0000000), 0x00100000,
        MEM_DECOMMIT);
  }

  delete physical_heap_;
  delete virtual_heap_;

  // Unmap all views and close mapping.
  if (mapping_) {
    UnmapViews();
    CloseHandle(mapping_);
    mapping_base_ = 0;
    mapping_ = 0;

    alloy::tracing::WriteEvent(EventType::MemoryDeinit({
    }));
  }
}

int XenonMemory::Initialize() {
  int result = Memory::Initialize();
  if (result) {
    return result;
  }
  result = 1;

  // Create main page file-backed mapping. This is all reserved but
  // uncommitted (so it shouldn't expand page file).
  mapping_ = CreateFileMapping(
      INVALID_HANDLE_VALUE,
      NULL,
      PAGE_READWRITE | SEC_RESERVE,
      1, 0, // entire 4gb space
      NULL);
  if (!mapping_) {
    XELOGE("Unable to reserve the 4gb guest address space.");
    XEASSERTNOTNULL(mapping_);
    XEFAIL();
  }

  // Attempt to create our views. This may fail at the first address
  // we pick, so try a few times.
  mapping_base_ = 0;
  for (size_t n = 32; n < 64; n++) {
    uint8_t* mapping_base = (uint8_t*)(1ull << n);
    if (!MapViews(mapping_base)) {
      mapping_base_ = mapping_base;
      break;
    }
  }
  if (!mapping_base_) {
    XELOGE("Unable to find a continuous block in the 64bit address space.");
    XEASSERTALWAYS();
    XEFAIL();
  }
  membase_ = mapping_base_;

  alloy::tracing::WriteEvent(EventType::MemoryInit({
  }));

  // Prepare heaps.
  virtual_heap_->Initialize(
      XENON_MEMORY_VIRTUAL_HEAP_LOW, XENON_MEMORY_VIRTUAL_HEAP_HIGH);
  physical_heap_->Initialize(
      XENON_MEMORY_PHYSICAL_HEAP_LOW, XENON_MEMORY_PHYSICAL_HEAP_HIGH);

  // GPU writeback.
  VirtualAlloc(
      Translate(0xC0000000 + system_page_size_),
      0x00100000 - system_page_size_,
      MEM_COMMIT, PAGE_READWRITE);

  return 0;

XECLEANUP:
  return result;
}

int XenonMemory::MapViews(uint8_t* mapping_base) {
  static struct {
    uint64_t  virtual_address_start;
    uint64_t  virtual_address_end;
    uint64_t  target_address;
  } map_info[] = {
    0x00000000, 0x3FFFFFFF, 0x00000000, // (1024mb) - virtual 4k pages
    0x40000000, 0x7FFFFFFF, 0x40000000, // (1024mb) - virtual 64k pages
    0x80000000, 0x9FFFFFFF, 0x80000000, //  (512mb) - xex pages
    0xA0000000, 0xBFFFFFFF, 0x00000000, //  (512mb) - physical 64k pages
    0xC0000000, 0xDFFFFFFF, 0x00000000, //          - physical 16mb pages
    0xE0000000, 0xFFFFFFFF, 0x00000000, //          - physical 4k pages
  };
  XEASSERT(XECOUNT(map_info) == XECOUNT(views_.all_views));
  for (size_t n = 0; n < XECOUNT(map_info); n++) {
    views_.all_views[n] = (uint8_t*)MapViewOfFileEx(
        mapping_,
        FILE_MAP_ALL_ACCESS,
        0x00000000, (DWORD)map_info[n].target_address,
        map_info[n].virtual_address_end - map_info[n].virtual_address_start + 1,
        mapping_base + map_info[n].virtual_address_start);
    XEEXPECTNOTNULL(views_.all_views[n]);
  }
  return 0;

XECLEANUP:
  UnmapViews();
  return 1;
}

void XenonMemory::UnmapViews() {
  for (size_t n = 0; n < XECOUNT(views_.all_views); n++) {
    if (views_.all_views[n]) {
      UnmapViewOfFile(views_.all_views[n]);
    }
  }
}

uint64_t XenonMemory::HeapAlloc(
    uint64_t base_address, size_t size, uint32_t flags,
    uint32_t alignment) {
  // If we were given a base address we are outside of the normal heap and
  // will place wherever asked (so long as it doesn't overlap the heap).
  if (!base_address) {
    // Normal allocation from the managed heap.
    uint64_t result;
    if (flags & MEMORY_FLAG_PHYSICAL) {
      result = physical_heap_->Alloc(
          base_address, size, flags, alignment);
    } else {
      result = virtual_heap_->Alloc(
          base_address, size, flags, alignment);
    }
    if (result) {
      if (flags & MEMORY_FLAG_ZERO) {
        xe_zero_struct(Translate(result), size);
      }
    }
    return result;
  } else {
    if (base_address >= XENON_MEMORY_VIRTUAL_HEAP_LOW &&
        base_address < XENON_MEMORY_VIRTUAL_HEAP_HIGH) {
      // Overlapping managed heap.
      XEASSERTALWAYS();
      return 0;
    }
    if (base_address >= XENON_MEMORY_PHYSICAL_HEAP_LOW &&
        base_address < XENON_MEMORY_PHYSICAL_HEAP_HIGH) {
      // Overlapping managed heap.
      XEASSERTALWAYS();
      return 0;
    }

    uint8_t* p = Translate(base_address);
    // TODO(benvanik): check if address range is in use with a query.

    void* pv = VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE);
    if (!pv) {
      // Failed.
      XEASSERTALWAYS();
      return 0;
    }

    if (flags & MEMORY_FLAG_ZERO) {
      xe_zero_struct(pv, size);
    }

    alloy::tracing::WriteEvent(EventType::MemoryHeapAlloc({
      0, flags, base_address, size,
    }));

    return base_address;
  }
}

int XenonMemory::HeapFree(uint64_t address, size_t size) {
  if (address >= XENON_MEMORY_VIRTUAL_HEAP_LOW &&
      address < XENON_MEMORY_VIRTUAL_HEAP_HIGH) {
    return virtual_heap_->Free(address, size) ? 0 : 1;
  } else if (address >= XENON_MEMORY_PHYSICAL_HEAP_LOW &&
             address < XENON_MEMORY_PHYSICAL_HEAP_HIGH) {
    return physical_heap_->Free(address, size) ? 0 : 1;
  } else {
    // A placed address. Decommit.
    alloy::tracing::WriteEvent(EventType::MemoryHeapFree({
      0, address,
    }));
    uint8_t* p = Translate(address);
    return VirtualFree(p, size, MEM_DECOMMIT) ? 0 : 1;
  }
}

int XenonMemory::Protect(uint64_t address, size_t size, uint32_t access) {
  uint8_t* p = Translate(address);

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

uint32_t XenonMemory::QueryProtect(uint64_t address) {
  uint8_t* p = Translate(address);
  MEMORY_BASIC_INFORMATION info;
  size_t info_size = VirtualQuery((void*)p, &info, sizeof(info));
  if (!info_size) {
    return 0;
  }
  return info.Protect;
}


XenonMemoryHeap::XenonMemoryHeap(XenonMemory* memory, bool is_physical) :
    memory_(memory), is_physical_(is_physical) {
  heap_id_ = next_heap_id_++;
  lock_ = AllocMutex(10000);
}

XenonMemoryHeap::~XenonMemoryHeap() {
  if (lock_ && space_) {
    LockMutex(lock_);
    destroy_mspace(space_);
    space_ = NULL;
    UnlockMutex(lock_);
  }
  if (lock_) {
    FreeMutex(lock_);
    lock_ = NULL;
  }

  if (ptr_) {
    XEIGNORE(VirtualFree(ptr_, 0, MEM_RELEASE));

    alloy::tracing::WriteEvent(EventType::MemoryHeapDeinit({
      heap_id_,
    }));
  }
}

int XenonMemoryHeap::Initialize(uint64_t low, uint64_t high) {
  // Commit the memory where our heap will live and allocate it.
  // TODO(benvanik): replace dlmalloc with an implementation that can commit
  //     as it goes.
  size_ = high - low;
  ptr_ = memory_->views_.v00000000 + low;
  void* heap_result = VirtualAlloc(
      ptr_, size_, MEM_COMMIT, PAGE_READWRITE);
  if (!heap_result) {
    return 1;
  }
  space_ = create_mspace_with_base(ptr_, size_, 0);

  alloy::tracing::WriteEvent(EventType::MemoryHeapInit({
    heap_id_, low, high, is_physical_,
  }));

  return 0;
}

uint64_t XenonMemoryHeap::Alloc(
    uint64_t base_address, size_t size, uint32_t flags, uint32_t alignment) {
  XEIGNORE(LockMutex(lock_));
  size_t alloc_size = size;
  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  if (heap_guard_size) {
    alignment = (uint32_t)MAX(alignment, heap_guard_size);
    alloc_size = (uint32_t)XEROUNDUP(size, heap_guard_size);
  }
  uint8_t* p = (uint8_t*)mspace_memalign(
      space_,
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
  XEIGNORE(UnlockMutex(lock_));
  if (!p) {
    return 0;
  }

  if (is_physical_) {
    // If physical, we need to commit the memory in the physical address ranges
    // so that it can be accessed.
    VirtualAlloc(
        memory_->views_.vA0000000 + (p - memory_->views_.v00000000),
        size,
        MEM_COMMIT,
        PAGE_READWRITE);
    VirtualAlloc(
        memory_->views_.vC0000000 + (p - memory_->views_.v00000000),
        size,
        MEM_COMMIT,
        PAGE_READWRITE);
    VirtualAlloc(
        memory_->views_.vE0000000 + (p - memory_->views_.v00000000),
        size,
        MEM_COMMIT,
        PAGE_READWRITE);
  }

  if ((flags & X_MEM_NOZERO) &&
      FLAGS_scribble_heap) {
    // Trash the memory so that we can see bad read-before-write bugs easier.
    memset(p, 0xCD, alloc_size);
  } else {
    // Implicit clear.
    memset(p, 0, alloc_size);
  }

  uint64_t address =
      (uint64_t)((uintptr_t)p - (uintptr_t)memory_->mapping_base_);

  alloy::tracing::WriteEvent(EventType::MemoryHeapAlloc({
    heap_id_, flags, address, size,
  }));

  return address;
}

uint64_t XenonMemoryHeap::Free(uint64_t address, size_t size) {
  uint8_t* p = memory_->Translate(address);

  // Heap allocated address.
  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  p -= heap_guard_size;
  size_t real_size = mspace_usable_size(p);
  real_size -= heap_guard_size * 2;
  if (!real_size) {
    return 0;
  }

  if (FLAGS_scribble_heap) {
    // Trash the memory so that we can see bad read-before-write bugs easier.
    memset(p + heap_guard_size, 0xDC, size);
  }

  XEIGNORE(LockMutex(lock_));
  if (FLAGS_heap_guard_pages) {
    DWORD old_protect;
    VirtualProtect(
        p, heap_guard_size,
        PAGE_READWRITE, &old_protect);
    VirtualProtect(
        p + heap_guard_size + real_size, heap_guard_size,
        PAGE_READWRITE, &old_protect);
  }
  mspace_free(space_, p);
  if (FLAGS_log_heap) {
    Dump();
  }
  XEIGNORE(UnlockMutex(lock_));

  if (is_physical_) {
    // If physical, decommit from physical ranges too.
    VirtualFree(
        memory_->views_.vA0000000 + (p - memory_->views_.v00000000),
        size,
        MEM_DECOMMIT);
    VirtualFree(
        memory_->views_.vC0000000 + (p - memory_->views_.v00000000),
        size,
        MEM_DECOMMIT);
    VirtualFree(
        memory_->views_.vE0000000 + (p - memory_->views_.v00000000),
        size,
        MEM_DECOMMIT);
  }

  alloy::tracing::WriteEvent(EventType::MemoryHeapFree({
    heap_id_, address,
  }));

  return (uint64_t)real_size;
}

void XenonMemoryHeap::Dump() {
  XELOGI("XenonMemoryHeap::Dump - %s",
         is_physical_ ? "physical" : "virtual");
  if (FLAGS_heap_guard_pages) {
    XELOGI("  (heap guard pages enabled, stats will be wrong)");
  }
  struct mallinfo info = mspace_mallinfo(space_);
  XELOGI("    arena: %lld", info.arena);
  XELOGI("  ordblks: %lld", info.ordblks);
  XELOGI("    hblks: %lld", info.hblks);
  XELOGI("   hblkhd: %lld", info.hblkhd);
  XELOGI("  usmblks: %lld", info.usmblks);
  XELOGI(" uordblks: %lld", info.uordblks);
  XELOGI(" fordblks: %lld", info.fordblks);
  XELOGI(" keepcost: %lld", info.keepcost);
  mspace_inspect_all(space_, DumpHandler, this);
}

void XenonMemoryHeap::DumpHandler(
  void* start, void* end, size_t used_bytes, void* context) {
  /*xe_memory_heap_t* heap = (xe_memory_heap_t*)context;
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
  }*/
}

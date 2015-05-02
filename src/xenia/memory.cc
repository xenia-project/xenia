/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/memory.h"

#include <gflags/gflags.h>

#include <algorithm>
#include <mutex>

#include "poly/math.h"
#include "xenia/cpu/mmio_handler.h"
#include "xenia/logging.h"

using namespace xe;

// TODO(benvanik): move xbox.h out
#include "xenia/xbox.h"

#if !XE_PLATFORM_WIN32
#include <sys/mman.h>
#endif  // WIN32

#define MSPACES 1
#define USE_LOCKS 0
#define USE_DL_PREFIX 1
#define HAVE_MORECORE 0
#define HAVE_MREMAP 0
#define malloc_getpagesize 4096
#define DEFAULT_GRANULARITY 64 * 1024
#define DEFAULT_TRIM_THRESHOLD MAX_SIZE_T
#define MALLOC_ALIGNMENT 32
#define MALLOC_INSPECT_ALL 1
#if XE_DEBUG
#define FOOTERS 0
#endif  // XE_DEBUG
#include "third_party/dlmalloc/malloc.c.h"

DEFINE_bool(log_heap, false, "Log heap structure on alloc/free.");
DEFINE_uint64(
    heap_guard_pages, 0,
    "Allocate the given number of guard pages around all heap chunks.");
DEFINE_bool(scribble_heap, false,
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
 * memory_HEAP_LOW to memory_HEAP_HIGH - all normal user allocations
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

const uint32_t kMemoryPhysicalHeapLow = 0x00010000;
const uint32_t kMemoryPhysicalHeapHigh = 0x20000000;
const uint32_t kMemoryVirtualHeapLow = 0x20000000;
const uint32_t kMemoryVirtualHeapHigh = 0x40000000;

class xe::MemoryHeap {
 public:
  MemoryHeap(Memory* memory, bool is_physical);
  ~MemoryHeap();

  int Initialize(uint32_t low, uint32_t high);

  uint32_t Alloc(uint32_t base_address, uint32_t size, uint32_t flags,
                 uint32_t alignment);
  uint32_t Free(uint32_t address, uint32_t size);
  uint32_t QuerySize(uint32_t base_address);

  void Dump();

 private:
  static uint32_t next_heap_id_;
  static void DumpHandler(void* start, void* end, size_t used_bytes,
                          void* context);

 private:
  Memory* memory_;
  uint32_t heap_id_;
  bool is_physical_;
  std::mutex lock_;
  uint32_t size_;
  uint8_t* ptr_;
  mspace space_;
};
uint32_t MemoryHeap::next_heap_id_ = 1;

Memory::Memory()
    : virtual_membase_(nullptr),
      physical_membase_(nullptr),
      reserve_address_(0),
      reserve_value_(0),
      trace_base_(0),
      mapping_(0),
      mapping_base_(nullptr) {
  system_page_size_ = uint32_t(poly::page_size());
  virtual_heap_ = new MemoryHeap(this, false);
  physical_heap_ = new MemoryHeap(this, true);
}

Memory::~Memory() {
  // Uninstall the MMIO handler, as we won't be able to service more
  // requests.
  mmio_handler_.reset();

  if (mapping_base_) {
    // GPU writeback.
    VirtualFree(TranslateVirtual(0xC0000000), 0x00100000, MEM_DECOMMIT);
  }

  delete physical_heap_;
  delete virtual_heap_;

  // Unmap all views and close mapping.
  if (mapping_) {
    UnmapViews();
    CloseHandle(mapping_);
    mapping_base_ = 0;
    mapping_ = 0;
  }

  virtual_membase_ = nullptr;
  physical_membase_ = nullptr;
}

int Memory::Initialize() {
// Create main page file-backed mapping. This is all reserved but
// uncommitted (so it shouldn't expand page file).
#if XE_PLATFORM_WIN32
  mapping_ =
      CreateFileMapping(INVALID_HANDLE_VALUE, NULL,
                        PAGE_READWRITE | SEC_RESERVE, 1, 0,  // entire 4gb space
                        NULL);
#else
  char mapping_path[] = "/xenia/mapping/XXXXXX";
  mktemp(mapping_path);
  mapping_ = shm_open(mapping_path, O_CREAT, 0);
  ftruncate(mapping_, 0x100000000);
#endif  // XE_PLATFORM_WIN32
  if (!mapping_) {
    XELOGE("Unable to reserve the 4gb guest address space.");
    assert_not_null(mapping_);
    return 1;
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
    assert_always();
    return 1;
  }
  virtual_membase_ = mapping_base_;
  physical_membase_ = virtual_membase_;

  // Prepare heaps.
  virtual_heap_->Initialize(kMemoryVirtualHeapLow, kMemoryVirtualHeapHigh);
  physical_heap_->Initialize(kMemoryPhysicalHeapLow,
                             kMemoryPhysicalHeapHigh - 0x1000);

  // GPU writeback.
  // 0xC... is physical, 0x7F... is virtual. We may need to overlay these.
  VirtualAlloc(TranslatePhysical(0x00000000), 0x00100000, MEM_COMMIT,
               PAGE_READWRITE);

  // Add handlers for MMIO.
  mmio_handler_ = cpu::MMIOHandler::Install(mapping_base_);
  if (!mmio_handler_) {
    XELOGE("Unable to install MMIO handlers");
    assert_always();
    return 1;
  }

  // I have no idea what this is, but games try to read/write there.
  VirtualAlloc(TranslateVirtual(0x40000000), 0x00010000, MEM_COMMIT,
               PAGE_READWRITE);
  poly::store_and_swap<uint32_t>(TranslateVirtual(0x40000000), 0x00C40000);
  poly::store_and_swap<uint32_t>(TranslateVirtual(0x40000004), 0x00010000);

  return 0;
}

const static struct {
  uint32_t virtual_address_start;
  uint32_t virtual_address_end;
  uint32_t target_address;
} map_info[] = {
    0x00000000, 0x3FFFFFFF, 0x00000000,  // (1024mb) - virtual 4k pages
    0x40000000, 0x7EFFFFFF, 0x40000000,  // (1024mb) - virtual 64k pages (cont)
    0x7F000000, 0x7F0FFFFF, 0x00000000,  //    (1mb) - GPU writeback
    0x7F100000, 0x7FFFFFFF, 0x00100000,  //   (15mb) - XPS?
    0x80000000, 0x8FFFFFFF, 0x80000000,  //  (256mb) - xex 64k pages
    0x90000000, 0x9FFFFFFF, 0x80000000,  //  (256mb) - xex 4k pages
    0xA0000000, 0xBFFFFFFF, 0x00000000,  //  (512mb) - physical 64k pages
    0xC0000000, 0xDFFFFFFF, 0x00000000,  //          - physical 16mb pages
    0xE0000000, 0xFFFFFFFF, 0x00000000,  //          - physical 4k pages
};
int Memory::MapViews(uint8_t* mapping_base) {
  assert_true(poly::countof(map_info) == poly::countof(views_.all_views));
  for (size_t n = 0; n < poly::countof(map_info); n++) {
#if XE_PLATFORM_WIN32
    views_.all_views[n] = reinterpret_cast<uint8_t*>(MapViewOfFileEx(
        mapping_, FILE_MAP_ALL_ACCESS, 0x00000000,
        (DWORD)map_info[n].target_address,
        map_info[n].virtual_address_end - map_info[n].virtual_address_start + 1,
        mapping_base + map_info[n].virtual_address_start));
#else
    views_.all_views[n] = reinterpret_cast<uint8_t*>(mmap(
        map_info[n].virtual_address_start + mapping_base,
        map_info[n].virtual_address_end - map_info[n].virtual_address_start + 1,
        PROT_NONE, MAP_SHARED | MAP_FIXED, mapping_,
        map_info[n].target_address));
#endif  // XE_PLATFORM_WIN32
    if (!views_.all_views[n]) {
      // Failed, so bail and try again.
      UnmapViews();
      return 1;
    }
  }
  return 0;
}

void Memory::UnmapViews() {
  for (size_t n = 0; n < poly::countof(views_.all_views); n++) {
    if (views_.all_views[n]) {
#if XE_PLATFORM_WIN32
      UnmapViewOfFile(views_.all_views[n]);
#else
      size_t length = map_info[n].virtual_address_end -
                      map_info[n].virtual_address_start + 1;
      munmap(views_.all_views[n], length);
#endif  // XE_PLATFORM_WIN32
    }
  }
}

void Memory::Zero(uint32_t address, uint32_t size) {
  std::memset(TranslateVirtual(address), 0, size);
}

void Memory::Fill(uint32_t address, uint32_t size, uint8_t value) {
  std::memset(TranslateVirtual(address), value, size);
}

void Memory::Copy(uint32_t dest, uint32_t src, uint32_t size) {
  uint8_t* pdest = TranslateVirtual(dest);
  const uint8_t* psrc = TranslateVirtual(src);
  std::memcpy(pdest, psrc, size);
}

uint32_t Memory::SearchAligned(uint32_t start, uint32_t end,
                               const uint32_t* values, size_t value_count) {
  assert_true(start <= end);
  auto p = TranslateVirtual<const uint32_t*>(start);
  auto pe = TranslateVirtual<const uint32_t*>(end);
  while (p != pe) {
    if (*p == values[0]) {
      const uint32_t* pc = p + 1;
      size_t matched = 1;
      for (size_t n = 1; n < value_count; n++, pc++) {
        if (*pc != values[n]) {
          break;
        }
        matched++;
      }
      if (matched == value_count) {
        return uint32_t(reinterpret_cast<const uint8_t*>(p) - virtual_membase_);
      }
    }
    p++;
  }
  return 0;
}

bool Memory::AddMappedRange(uint32_t address, uint32_t mask, uint32_t size,
                            void* context, cpu::MMIOReadCallback read_callback,
                            cpu::MMIOWriteCallback write_callback) {
  DWORD protect = PAGE_NOACCESS;
  if (!VirtualAlloc(TranslateVirtual(address), size, MEM_COMMIT, protect)) {
    XELOGE("Unable to map range; commit/protect failed");
    return false;
  }
  return mmio_handler_->RegisterRange(address, mask, size, context,
                                      read_callback, write_callback);
}

uintptr_t Memory::AddWriteWatch(uint32_t guest_address, uint32_t length,
                                cpu::WriteWatchCallback callback,
                                void* callback_context, void* callback_data) {
  return mmio_handler_->AddWriteWatch(guest_address, length, callback,
                                      callback_context, callback_data);
}

void Memory::CancelWriteWatch(uintptr_t watch_handle) {
  mmio_handler_->CancelWriteWatch(watch_handle);
}

uint32_t Memory::SystemHeapAlloc(uint32_t size, uint32_t alignment,
                                 uint32_t system_heap_flags) {
  // TODO(benvanik): lightweight pool.
  bool is_physical = !!(system_heap_flags & kSystemHeapPhysical);
  uint32_t flags = MEMORY_FLAG_ZERO;
  if (is_physical) {
    flags |= MEMORY_FLAG_PHYSICAL;
  }
  return HeapAlloc(0, size, flags, alignment);
}

void Memory::SystemHeapFree(uint32_t address) {
  if (!address) {
    return;
  }
  // TODO(benvanik): lightweight pool.
  HeapFree(address, 0);
}

uint32_t Memory::HeapAlloc(uint32_t base_address, uint32_t size, uint32_t flags,
                           uint32_t alignment) {
  // If we were given a base address we are outside of the normal heap and
  // will place wherever asked (so long as it doesn't overlap the heap).
  if (!base_address) {
    // Normal allocation from the managed heap.
    uint32_t result;
    if (flags & MEMORY_FLAG_PHYSICAL) {
      result = physical_heap_->Alloc(base_address, size, flags, alignment);
    } else {
      result = virtual_heap_->Alloc(base_address, size, flags, alignment);
    }
    if (result) {
      if (flags & MEMORY_FLAG_ZERO) {
        memset(TranslateVirtual(result), 0, size);
      }
    }
    return result;
  } else {
    if (base_address >= kMemoryVirtualHeapLow &&
        base_address < kMemoryVirtualHeapHigh) {
      // Overlapping managed heap.
      assert_always();
      return 0;
    }
    if (base_address >= kMemoryPhysicalHeapLow &&
        base_address < kMemoryPhysicalHeapHigh) {
      // Overlapping managed heap.
      assert_always();
      return 0;
    }

    uint8_t* p = TranslateVirtual(base_address);
    // TODO(benvanik): check if address range is in use with a query.

    void* pv = VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE);
    if (!pv) {
      // Failed.
      assert_always();
      return 0;
    }

    if (flags & MEMORY_FLAG_ZERO) {
      memset(pv, 0, size);
    }

    return base_address;
  }
}

int Memory::HeapFree(uint32_t address, uint32_t size) {
  if (address >= kMemoryVirtualHeapLow && address < kMemoryVirtualHeapHigh) {
    return virtual_heap_->Free(address, size) ? 0 : 1;
  } else if (address >= kMemoryPhysicalHeapLow &&
             address < kMemoryPhysicalHeapHigh) {
    return physical_heap_->Free(address, size) ? 0 : 1;
  } else {
    // A placed address. Decommit.
    uint8_t* p = TranslateVirtual(address);
    return VirtualFree(p, size, MEM_DECOMMIT) ? 0 : 1;
  }
}

bool Memory::QueryInformation(uint32_t base_address, AllocationInfo* mem_info) {
  uint8_t* p = TranslateVirtual(base_address);
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(p, &mbi, sizeof(mbi))) {
    return false;
  }
  mem_info->base_address = base_address;
  mem_info->allocation_base = static_cast<uint32_t>(
      reinterpret_cast<uint8_t*>(mbi.AllocationBase) - virtual_membase_);
  mem_info->allocation_protect = mbi.AllocationProtect;
  mem_info->region_size = mbi.RegionSize;
  mem_info->state = mbi.State;
  mem_info->protect = mbi.Protect;
  mem_info->type = mbi.Type;
  return true;
}

uint32_t Memory::QuerySize(uint32_t base_address) {
  if (base_address >= kMemoryVirtualHeapLow &&
      base_address < kMemoryVirtualHeapHigh) {
    return virtual_heap_->QuerySize(base_address);
  } else if (base_address >= kMemoryPhysicalHeapLow &&
             base_address < kMemoryPhysicalHeapHigh) {
    return physical_heap_->QuerySize(base_address);
  } else {
    // A placed address.
    uint8_t* p = TranslateVirtual(base_address);
    MEMORY_BASIC_INFORMATION mem_info;
    if (VirtualQuery(p, &mem_info, sizeof(mem_info))) {
      return uint32_t(mem_info.RegionSize);
    } else {
      // Error.
      return 0;
    }
  }
}

int Memory::Protect(uint32_t address, uint32_t size, uint32_t access) {
  uint8_t* p = TranslateVirtual(address);

  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  p += heap_guard_size;

  DWORD new_protect = access;
  new_protect =
      new_protect &
      (X_PAGE_NOACCESS | X_PAGE_READONLY | X_PAGE_READWRITE | X_PAGE_WRITECOPY |
       X_PAGE_GUARD | X_PAGE_NOCACHE | X_PAGE_WRITECOMBINE);

  DWORD old_protect;
  return VirtualProtect(p, size, new_protect, &old_protect) == TRUE ? 0 : 1;
}

uint32_t Memory::QueryProtect(uint32_t address) {
  uint8_t* p = TranslateVirtual(address);
  MEMORY_BASIC_INFORMATION info;
  size_t info_size = VirtualQuery((void*)p, &info, sizeof(info));
  if (!info_size) {
    return 0;
  }
  return info.Protect;
}

MemoryHeap::MemoryHeap(Memory* memory, bool is_physical)
    : memory_(memory), is_physical_(is_physical) {
  heap_id_ = next_heap_id_++;
}

MemoryHeap::~MemoryHeap() {
  if (space_) {
    std::lock_guard<std::mutex> guard(lock_);
    destroy_mspace(space_);
    space_ = NULL;
  }

  if (ptr_) {
    VirtualFree(ptr_, 0, MEM_RELEASE);
  }
}

int MemoryHeap::Initialize(uint32_t low, uint32_t high) {
  // Commit the memory where our heap will live and allocate it.
  // TODO(benvanik): replace dlmalloc with an implementation that can commit
  //     as it goes.
  size_ = high - low;
  ptr_ = memory_->views_.v00000000 + low;
  void* heap_result = VirtualAlloc(ptr_, size_, MEM_COMMIT, PAGE_READWRITE);
  if (!heap_result) {
    return 1;
  }
  space_ = create_mspace_with_base(ptr_, size_, 0);

  return 0;
}

uint32_t MemoryHeap::Alloc(uint32_t base_address, uint32_t size, uint32_t flags,
                           uint32_t alignment) {
  size_t alloc_size = size;
  if (int32_t(alloc_size) < 0) {
    alloc_size = uint32_t(-alloc_size);
  }
  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  if (heap_guard_size) {
    alignment = std::max(alignment, static_cast<uint32_t>(heap_guard_size));
    alloc_size =
        static_cast<uint32_t>(poly::round_up(alloc_size, heap_guard_size));
  }

  lock_.lock();
  uint8_t* p = (uint8_t*)mspace_memalign(space_, alignment,
                                         alloc_size + heap_guard_size * 2);
  assert_true(reinterpret_cast<uint64_t>(p) <= 0xFFFFFFFFFull);
  if (FLAGS_heap_guard_pages) {
    size_t real_size = mspace_usable_size(p);
    DWORD old_protect;
    VirtualProtect(p, heap_guard_size, PAGE_NOACCESS, &old_protect);
    p += heap_guard_size;
    VirtualProtect(p + alloc_size, heap_guard_size, PAGE_NOACCESS,
                   &old_protect);
  }
  if (FLAGS_log_heap) {
    Dump();
  }
  lock_.unlock();
  if (!p) {
    return 0;
  }

  if (is_physical_) {
    // If physical, we need to commit the memory in the physical address ranges
    // so that it can be accessed.
    VirtualAlloc(memory_->views_.vA0000000 + (p - memory_->views_.v00000000),
                 alloc_size, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(memory_->views_.vC0000000 + (p - memory_->views_.v00000000),
                 alloc_size, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(memory_->views_.vE0000000 + (p - memory_->views_.v00000000),
                 alloc_size, MEM_COMMIT, PAGE_READWRITE);
  }

  if (flags & MEMORY_FLAG_ZERO) {
    memset(p, 0, alloc_size);
  } else if (FLAGS_scribble_heap) {
    // Trash the memory so that we can see bad read-before-write bugs easier.
    memset(p, 0xCD, alloc_size);
  }

  uint32_t address =
      (uint32_t)((uintptr_t)p - (uintptr_t)memory_->mapping_base_);

  return address;
}

uint32_t MemoryHeap::Free(uint32_t address, uint32_t size) {
  uint8_t* p = memory_->TranslateVirtual(address);

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

  lock_.lock();
  if (FLAGS_heap_guard_pages) {
    DWORD old_protect;
    VirtualProtect(p, heap_guard_size, PAGE_READWRITE, &old_protect);
    VirtualProtect(p + heap_guard_size + real_size, heap_guard_size,
                   PAGE_READWRITE, &old_protect);
  }
  mspace_free(space_, p);
  if (FLAGS_log_heap) {
    Dump();
  }
  lock_.unlock();

  if (is_physical_) {
    // If physical, decommit from physical ranges too.
    VirtualFree(memory_->views_.vA0000000 + (p - memory_->views_.v00000000),
                size, MEM_DECOMMIT);
    VirtualFree(memory_->views_.vC0000000 + (p - memory_->views_.v00000000),
                size, MEM_DECOMMIT);
    VirtualFree(memory_->views_.vE0000000 + (p - memory_->views_.v00000000),
                size, MEM_DECOMMIT);
  }

  return static_cast<uint32_t>(real_size);
}

uint32_t MemoryHeap::QuerySize(uint32_t base_address) {
  uint8_t* p = memory_->TranslateVirtual(base_address);

  // Heap allocated address.
  uint32_t heap_guard_size = uint32_t(FLAGS_heap_guard_pages * 4096);
  p -= heap_guard_size;
  uint32_t real_size = uint32_t(mspace_usable_size(p));
  real_size -= heap_guard_size * 2;
  if (!real_size) {
    return 0;
  }

  return real_size;
}

void MemoryHeap::Dump() {
  XELOGI("MemoryHeap::Dump - %s", is_physical_ ? "physical" : "virtual");
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

void MemoryHeap::DumpHandler(void* start, void* end, size_t used_bytes,
                             void* context) {
  MemoryHeap* heap = (MemoryHeap*)context;
  Memory* memory = heap->memory_;
  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  uint64_t start_addr = (uint64_t)start + heap_guard_size;
  uint64_t end_addr = (uint64_t)end - heap_guard_size;
  uint32_t guest_start =
      (uint32_t)(start_addr - (uintptr_t)memory->mapping_base_);
  uint32_t guest_end = (uint32_t)(end_addr - (uintptr_t)memory->mapping_base_);
  if (int32_t(end_addr - start_addr) > 0) {
    XELOGI(" - %.8X-%.8X (%10db) %.16llX-%.16llX - %9db used", guest_start,
           guest_end, (guest_end - guest_start), start_addr, end_addr,
           used_bytes);
  } else {
    XELOGI(" -                                 %.16llX-%.16llX - %9db used",
           start, end, used_bytes);
  }
}

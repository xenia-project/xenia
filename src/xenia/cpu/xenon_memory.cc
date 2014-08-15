/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/xenon_memory.h>

#include <mutex>

#include <gflags/gflags.h>

using namespace alloy;
using namespace xe::cpu;

// TODO(benvanik): move xbox.h out
#include <xenia/xbox.h>

#if !XE_PLATFORM_WIN32
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
  size_t QuerySize(uint64_t base_address);

  void Dump();

private:
  static uint32_t next_heap_id_;
  static void DumpHandler(
      void* start, void* end, size_t used_bytes, void* context);

private:
  XenonMemory*  memory_;
  uint32_t      heap_id_;
  bool          is_physical_;
  std::mutex    lock_;
  size_t        size_;
  uint8_t*      ptr_;
  mspace        space_;
};
uint32_t XenonMemoryHeap::next_heap_id_ = 1;

XenonMemory::XenonMemory()
    : Memory(),
      mapping_(0), mapping_base_(0), page_table_(0) {
  virtual_heap_ = new XenonMemoryHeap(this, false);
  physical_heap_ = new XenonMemoryHeap(this, true);
}

XenonMemory::~XenonMemory() {
  // Uninstall the MMIO handler, as we won't be able to service more
  // requests.
  mmio_handler_.reset();

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
#if XE_PLATFORM_WIN32
  mapping_ = CreateFileMapping(
      INVALID_HANDLE_VALUE,
      NULL,
      PAGE_READWRITE | SEC_RESERVE,
      1, 0, // entire 4gb space
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
    assert_always();
    XEFAIL();
  }
  membase_ = mapping_base_;

  // Prepare heaps.
  virtual_heap_->Initialize(
      XENON_MEMORY_VIRTUAL_HEAP_LOW, XENON_MEMORY_VIRTUAL_HEAP_HIGH);
  physical_heap_->Initialize(
      XENON_MEMORY_PHYSICAL_HEAP_LOW, XENON_MEMORY_PHYSICAL_HEAP_HIGH - 0x1000);

  // GPU writeback.
  // 0xC... is physical, 0x7F... is virtual. We may need to overlay these.
  VirtualAlloc(
      Translate(0xC0000000),
      0x00100000,
      MEM_COMMIT, PAGE_READWRITE);

  // Add handlers for MMIO.
  mmio_handler_ = MMIOHandler::Install(mapping_base_);
  if (!mmio_handler_) {
    XELOGE("Unable to install MMIO handlers");
    assert_always();
    XEFAIL();
  }

  // Allocate dirty page table.
  // This must live within our low heap. Ideally we'd hardcode the address but
  // this is more flexible.
  page_table_ = physical_heap_->Alloc(
      0, (512 * 1024 * 1024) / (16 * 1024),
      X_MEM_COMMIT, 16 * 1024);

  return 0;

XECLEANUP:
  return result;
}

const static struct {
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
int XenonMemory::MapViews(uint8_t* mapping_base) {
  assert_true(XECOUNT(map_info) == XECOUNT(views_.all_views));
  for (size_t n = 0; n < XECOUNT(map_info); n++) {
#if XE_PLATFORM_WIN32
    views_.all_views[n] = reinterpret_cast<uint8_t*>(MapViewOfFileEx(
        mapping_,
        FILE_MAP_ALL_ACCESS,
        0x00000000, (DWORD)map_info[n].target_address,
        map_info[n].virtual_address_end - map_info[n].virtual_address_start + 1,
        mapping_base + map_info[n].virtual_address_start));
#else
    views_.all_views[n] = reinterpret_cast<uint8_t*>(mmap(
        map_info[n].virtual_address_start + mapping_base,
        map_info[n].virtual_address_end - map_info[n].virtual_address_start + 1,
        PROT_NONE, MAP_SHARED | MAP_FIXED, mapping_, map_info[n].target_address));
#endif  // XE_PLATFORM_WIN32
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
#if XE_PLATFORM_WIN32
      UnmapViewOfFile(views_.all_views[n]);
#else
      size_t length = map_info[n].virtual_address_end - map_info[n].virtual_address_start + 1;
      munmap(views_.all_views[n], length);
#endif  // XE_PLATFORM_WIN32
    }
  }
}

bool XenonMemory::AddMappedRange(uint64_t address, uint64_t mask,
                                 uint64_t size, void* context,
                                 MMIOReadCallback read_callback,
                                 MMIOWriteCallback write_callback) {
  DWORD protect = PAGE_NOACCESS;
  if (!VirtualAlloc(Translate(address),
                    size,
                    MEM_COMMIT, protect)) {
    XELOGE("Unable to map range; commit/protect failed");
    return false;
  }
  return mmio_handler_->RegisterRange(address, mask, size, context, read_callback, write_callback);
}

uint8_t XenonMemory::LoadI8(uint64_t address) {
  uint64_t value;
  if (!mmio_handler_->CheckLoad(address, &value)) {
    value = *reinterpret_cast<uint8_t*>(Translate(address));
  }
  return static_cast<uint8_t>(value);
}

uint16_t XenonMemory::LoadI16(uint64_t address) {
  uint64_t value;
  if (!mmio_handler_->CheckLoad(address, &value)) {
    value = *reinterpret_cast<uint16_t*>(Translate(address));
  }
  return static_cast<uint16_t>(value);
}

uint32_t XenonMemory::LoadI32(uint64_t address) {
  uint64_t value;
  if (!mmio_handler_->CheckLoad(address, &value)) {
    value = *reinterpret_cast<uint32_t*>(Translate(address));
  }
  return static_cast<uint32_t>(value);
}

uint64_t XenonMemory::LoadI64(uint64_t address) {
  uint64_t value;
  if (!mmio_handler_->CheckLoad(address, &value)) {
    value = *reinterpret_cast<uint64_t*>(Translate(address));
  }
  return static_cast<uint64_t>(value);
}

void XenonMemory::StoreI8(uint64_t address, uint8_t value) {
  if (!mmio_handler_->CheckStore(address, value)) {
    *reinterpret_cast<uint8_t*>(Translate(address)) = value;
  }
}

void XenonMemory::StoreI16(uint64_t address, uint16_t value) {
  if (!mmio_handler_->CheckStore(address, value)) {
    *reinterpret_cast<uint16_t*>(Translate(address)) = value;
  }
}

void XenonMemory::StoreI32(uint64_t address, uint32_t value) {
  if (!mmio_handler_->CheckStore(address, value)) {
    *reinterpret_cast<uint32_t*>(Translate(address)) = value;
  }
}

void XenonMemory::StoreI64(uint64_t address, uint64_t value) {
  if (!mmio_handler_->CheckStore(address, value)) {
    *reinterpret_cast<uint64_t*>(Translate(address)) = value;
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
      assert_always();
      return 0;
    }
    if (base_address >= XENON_MEMORY_PHYSICAL_HEAP_LOW &&
        base_address < XENON_MEMORY_PHYSICAL_HEAP_HIGH) {
      // Overlapping managed heap.
      assert_always();
      return 0;
    }

    uint8_t* p = Translate(base_address);
    // TODO(benvanik): check if address range is in use with a query.

    void* pv = VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE);
    if (!pv) {
      // Failed.
      assert_always();
      return 0;
    }

    if (flags & MEMORY_FLAG_ZERO) {
      xe_zero_struct(pv, size);
    }

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
    uint8_t* p = Translate(address);
    return VirtualFree(p, size, MEM_DECOMMIT) ? 0 : 1;
  }
}

size_t XenonMemory::QueryInformation(uint64_t base_address, MEMORY_BASIC_INFORMATION mem_info) {
  uint8_t* p = Translate(base_address);

  return VirtualQuery(p, &mem_info, sizeof(mem_info));
}

size_t XenonMemory::QuerySize(uint64_t base_address) {
  if (base_address >= XENON_MEMORY_VIRTUAL_HEAP_LOW &&
      base_address < XENON_MEMORY_VIRTUAL_HEAP_HIGH) {
    return virtual_heap_->QuerySize(base_address);
  } else if (base_address >= XENON_MEMORY_PHYSICAL_HEAP_LOW &&
             base_address < XENON_MEMORY_PHYSICAL_HEAP_HIGH) {
    return physical_heap_->QuerySize(base_address);
  } else {
    // A placed address.
    uint8_t* p = Translate(base_address);
    MEMORY_BASIC_INFORMATION mem_info;
    if (VirtualQuery(p, &mem_info, sizeof(mem_info))) {
      return mem_info.RegionSize;
    } else {
      // Error.
      return 0;
    }
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
}

XenonMemoryHeap::~XenonMemoryHeap() {
  if (space_) {
    std::lock_guard<std::mutex> guard(lock_);
    destroy_mspace(space_);
    space_ = NULL;
  }

  if (ptr_) {
    XEIGNORE(VirtualFree(ptr_, 0, MEM_RELEASE));
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

  return 0;
}

uint64_t XenonMemoryHeap::Alloc(
    uint64_t base_address, size_t size, uint32_t flags, uint32_t alignment) {
  lock_.lock();
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
  assert_true(reinterpret_cast<uint64_t>(p) <= 0xFFFFFFFFFull);
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
  lock_.unlock();
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

  lock_.lock();
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
  lock_.unlock();

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

  return (uint64_t)real_size;
}

size_t XenonMemoryHeap::QuerySize(uint64_t base_address) {
  uint8_t* p = memory_->Translate(base_address);

  // Heap allocated address.
  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  p -= heap_guard_size;
  size_t real_size = mspace_usable_size(p);
  real_size -= heap_guard_size * 2;
  if (!real_size) {
    return 0;
  }

  return real_size;
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
  XenonMemoryHeap* heap = (XenonMemoryHeap*)context;
  XenonMemory* memory = heap->memory_;
  size_t heap_guard_size = FLAGS_heap_guard_pages * 4096;
  uint64_t start_addr = (uint64_t)start + heap_guard_size;
  uint64_t end_addr = (uint64_t)end - heap_guard_size;
  uint32_t guest_start =
      (uint32_t)(start_addr - (uintptr_t)memory->mapping_base_);
  uint32_t guest_end =
      (uint32_t)(end_addr - (uintptr_t)memory->mapping_base_);
  if (int32_t(end_addr - start_addr) > 0) {
    XELOGI(" - %.8X-%.8X (%10db) %.16llX-%.16llX - %9db used",
           guest_start, guest_end, (guest_end - guest_start),
           start_addr, end_addr,
           used_bytes);
  } else {
    XELOGI(" -                                 %.16llX-%.16llX - %9db used",
           start, end, used_bytes);
  }
}

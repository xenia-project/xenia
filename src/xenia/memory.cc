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
#include <cstring>

#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/mmio_handler.h"

// TODO(benvanik): move xbox.h out
#include "xenia/xbox.h"

DEFINE_bool(protect_zero, true, "Protect the zero page from reads and writes.");
DEFINE_bool(protect_on_release, false,
            "Protect released memory to prevent accesses.");

DEFINE_bool(scribble_heap, false,
            "Scribble 0xCD into all allocated heap memory.");

namespace xe {

uint32_t get_page_count(uint32_t value, uint32_t page_size) {
  return xe::round_up(value, page_size) / page_size;
}

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

static Memory* active_memory_ = nullptr;

void CrashDump() {
  static std::atomic<int> in_crash_dump(0);
  if (in_crash_dump.fetch_add(1)) {
    xe::FatalError(
        "Hard crash: the memory system crashed while dumping a crash dump.");
    return;
  }
  active_memory_->DumpMap();
  --in_crash_dump;
}

Memory::Memory() {
  system_page_size_ = uint32_t(xe::memory::page_size());
  assert_zero(active_memory_);
  active_memory_ = this;
}

Memory::~Memory() {
  assert_true(active_memory_ == this);
  active_memory_ = nullptr;

  // Uninstall the MMIO handler, as we won't be able to service more
  // requests.
  mmio_handler_.reset();

  heaps_.v00000000.Dispose();
  heaps_.v40000000.Dispose();
  heaps_.v80000000.Dispose();
  heaps_.v90000000.Dispose();
  heaps_.vA0000000.Dispose();
  heaps_.vC0000000.Dispose();
  heaps_.vE0000000.Dispose();
  heaps_.physical.Dispose();

  // Unmap all views and close mapping.
  if (mapping_) {
    UnmapViews();
    xe::memory::CloseFileMappingHandle(mapping_);
    mapping_base_ = nullptr;
    mapping_ = nullptr;
  }

  virtual_membase_ = nullptr;
  physical_membase_ = nullptr;
}

bool Memory::Initialize() {
  file_name_ = std::wstring(L"Local\\xenia_memory_") +
               std::to_wstring(Clock::QueryHostTickCount());

  // Create main page file-backed mapping. This is all reserved but
  // uncommitted (so it shouldn't expand page file).
  mapping_ = xe::memory::CreateFileMappingHandle(
      file_name_,
      // entire 4gb space + 512mb physical:
      0x11FFFFFFF, xe::memory::PageAccess::kReadWrite, false);
  if (!mapping_) {
    XELOGE("Unable to reserve the 4gb guest address space.");
    assert_not_null(mapping_);
    return false;
  }

  // Attempt to create our views. This may fail at the first address
  // we pick, so try a few times.
  mapping_base_ = 0;
  for (size_t n = 32; n < 64; n++) {
    auto mapping_base = reinterpret_cast<uint8_t*>(1ull << n);
    if (!MapViews(mapping_base)) {
      mapping_base_ = mapping_base;
      break;
    }
  }
  if (!mapping_base_) {
    XELOGE("Unable to find a continuous block in the 64bit address space.");
    assert_always();
    return false;
  }
  virtual_membase_ = mapping_base_;
  physical_membase_ = mapping_base_ + 0x100000000ull;

  // Prepare virtual heaps.
  heaps_.v00000000.Initialize(virtual_membase_, 0x00000000, 0x40000000, 4096);
  heaps_.v40000000.Initialize(virtual_membase_, 0x40000000,
                              0x40000000 - 0x01000000, 64 * 1024);
  heaps_.v80000000.Initialize(virtual_membase_, 0x80000000, 0x10000000,
                              64 * 1024);
  heaps_.v90000000.Initialize(virtual_membase_, 0x90000000, 0x10000000, 4096);

  // Prepare physical heaps.
  heaps_.physical.Initialize(physical_membase_, 0x00000000, 0x20000000, 4096);
  // HACK: should be 64k, but with us overlaying A and E it needs to be 4.
  /*heaps_.vA0000000.Initialize(virtual_membase_, 0xA0000000, 0x20000000,
                              64 * 1024, &heaps_.physical);*/
  heaps_.vA0000000.Initialize(virtual_membase_, 0xA0000000, 0x20000000,
                              4 * 1024, &heaps_.physical);
  heaps_.vC0000000.Initialize(virtual_membase_, 0xC0000000, 0x20000000,
                              16 * 1024 * 1024, &heaps_.physical);
  heaps_.vE0000000.Initialize(virtual_membase_, 0xE0000000, 0x1FD00000, 4096,
                              &heaps_.physical);

  // Protect the first 64kb of memory.
  heaps_.v00000000.AllocFixed(
      0x00000000, 64 * 1024, 64 * 1024,
      kMemoryAllocationReserve | kMemoryAllocationCommit,
      !FLAGS_protect_zero ? kMemoryProtectRead | kMemoryProtectWrite
                          : kMemoryProtectNoAccess);

  // GPU writeback.
  // 0xC... is physical, 0x7F... is virtual. We may need to overlay these.
  heaps_.vC0000000.AllocFixed(
      0xC0000000, 0x01000000, 32,
      kMemoryAllocationReserve | kMemoryAllocationCommit,
      kMemoryProtectRead | kMemoryProtectWrite);

  // Add handlers for MMIO.
  mmio_handler_ = cpu::MMIOHandler::Install(virtual_membase_, physical_membase_,
                                            physical_membase_ + 0x1FFFFFFF);
  if (!mmio_handler_) {
    XELOGE("Unable to install MMIO handlers");
    assert_always();
    return false;
  }

  // ?
  uint32_t unk_phys_alloc;
  heaps_.vA0000000.Alloc(0x340000, 64 * 1024, kMemoryAllocationReserve,
                         kMemoryProtectNoAccess, true, &unk_phys_alloc);

  return true;
}

static const struct {
  uint64_t virtual_address_start;
  uint64_t virtual_address_end;
  uint64_t target_address;
} map_info[] = {
    // (1024mb) - virtual 4k pages
    {
        0x00000000,
        0x3FFFFFFF,
        0x0000000000000000ull,
    },
    // (1024mb) - virtual 64k pages (cont)
    {
        0x40000000,
        0x7EFFFFFF,
        0x0000000040000000ull,
    },
    //   (16mb) - GPU writeback + 15mb of XPS?
    {
        0x7F000000,
        0x7FFFFFFF,
        0x0000000100000000ull,
    },
    //  (256mb) - xex 64k pages
    {
        0x80000000,
        0x8FFFFFFF,
        0x0000000080000000ull,
    },
    //  (256mb) - xex 4k pages
    {
        0x90000000,
        0x9FFFFFFF,
        0x0000000080000000ull,
    },
    //  (512mb) - physical 64k pages
    {
        0xA0000000,
        0xBFFFFFFF,
        0x0000000100000000ull,
    },
    //          - physical 16mb pages
    {
        0xC0000000,
        0xDFFFFFFF,
        0x0000000100000000ull,
    },
    //          - physical 4k pages
    {
        0xE0000000,
        0xFFFFFFFF,
        0x0000000100000000ull,
    },
    //          - physical raw
    {
        0x100000000,
        0x11FFFFFFF,
        0x0000000100000000ull,
    },
};
int Memory::MapViews(uint8_t* mapping_base) {
  assert_true(xe::countof(map_info) == xe::countof(views_.all_views));
  for (size_t n = 0; n < xe::countof(map_info); n++) {
    views_.all_views[n] = reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
        mapping_, mapping_base + map_info[n].virtual_address_start,
        map_info[n].virtual_address_end - map_info[n].virtual_address_start + 1,
        xe::memory::PageAccess::kReadWrite, map_info[n].target_address));
    if (!views_.all_views[n]) {
      // Failed, so bail and try again.
      UnmapViews();
      return 1;
    }
  }
  return 0;
}

void Memory::UnmapViews() {
  for (size_t n = 0; n < xe::countof(views_.all_views); n++) {
    if (views_.all_views[n]) {
      size_t length = map_info[n].virtual_address_end -
                      map_info[n].virtual_address_start + 1;
      xe::memory::UnmapFileView(mapping_, views_.all_views[n], length);
    }
  }
}

void Memory::Reset() {
  heaps_.v00000000.Reset();
  heaps_.v40000000.Reset();
  heaps_.v80000000.Reset();
  heaps_.v90000000.Reset();
  heaps_.physical.Reset();
}

BaseHeap* Memory::LookupHeap(uint32_t address) {
  if (address < 0x40000000) {
    return &heaps_.v00000000;
  } else if (address < 0x7F000000) {
    return &heaps_.v40000000;
  } else if (address < 0x80000000) {
    return nullptr;
  } else if (address < 0x90000000) {
    return &heaps_.v80000000;
  } else if (address < 0xA0000000) {
    return &heaps_.v90000000;
  } else if (address < 0xC0000000) {
    return &heaps_.vA0000000;
  } else if (address < 0xE0000000) {
    return &heaps_.vC0000000;
  } else if (address < 0xFFD00000) {
    return &heaps_.vE0000000;
  } else {
    return nullptr;
  }
}

BaseHeap* Memory::LookupHeapByType(bool physical, uint32_t page_size) {
  if (physical) {
    if (page_size <= 4096) {
      // HACK: should be vE0000000
      return &heaps_.vA0000000;
    } else if (page_size <= 64 * 1024) {
      return &heaps_.vA0000000;
    } else {
      return &heaps_.vC0000000;
    }
  } else {
    if (page_size <= 4096) {
      return &heaps_.v00000000;
    } else {
      return &heaps_.v40000000;
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

bool Memory::AddVirtualMappedRange(uint32_t virtual_address, uint32_t mask,
                                   uint32_t size, void* context,
                                   cpu::MMIOReadCallback read_callback,
                                   cpu::MMIOWriteCallback write_callback) {
  if (!xe::memory::AllocFixed(TranslateVirtual(virtual_address), size,
                              xe::memory::AllocationType::kCommit,
                              xe::memory::PageAccess::kNoAccess)) {
    XELOGE("Unable to map range; commit/protect failed");
    return false;
  }
  return mmio_handler_->RegisterRange(virtual_address, mask, size, context,
                                      read_callback, write_callback);
}

cpu::MMIORange* Memory::LookupVirtualMappedRange(uint32_t virtual_address) {
  return mmio_handler_->LookupRange(virtual_address);
}

uintptr_t Memory::AddPhysicalAccessWatch(uint32_t physical_address,
                                         uint32_t length,
                                         cpu::MMIOHandler::WatchType type,
                                         cpu::AccessWatchCallback callback,
                                         void* callback_context,
                                         void* callback_data) {
  return mmio_handler_->AddPhysicalAccessWatch(physical_address, length, type,
                                               callback, callback_context,
                                               callback_data);
}

void Memory::CancelAccessWatch(uintptr_t watch_handle) {
  mmio_handler_->CancelAccessWatch(watch_handle);
}

uint32_t Memory::SystemHeapAlloc(uint32_t size, uint32_t alignment,
                                 uint32_t system_heap_flags) {
  // TODO(benvanik): lightweight pool.
  bool is_physical = !!(system_heap_flags & kSystemHeapPhysical);
  auto heap = LookupHeapByType(is_physical, 4096);
  uint32_t address;
  if (!heap->Alloc(size, alignment,
                   kMemoryAllocationReserve | kMemoryAllocationCommit,
                   kMemoryProtectRead | kMemoryProtectWrite, false, &address)) {
    return 0;
  }
  Zero(address, size);
  return address;
}

void Memory::SystemHeapFree(uint32_t address) {
  if (!address) {
    return;
  }
  // TODO(benvanik): lightweight pool.
  auto heap = LookupHeap(address);
  heap->Release(address);
}

void Memory::DumpMap() {
  XELOGE("==================================================================");
  XELOGE("Memory Dump");
  XELOGE("==================================================================");
  XELOGE("  System Page Size: %d (%.8X)", system_page_size_, system_page_size_);
  XELOGE("   Virtual Membase: %.16llX", virtual_membase_);
  XELOGE("  Physical Membase: %.16llX", physical_membase_);
  XELOGE("");
  XELOGE("------------------------------------------------------------------");
  XELOGE("Virtual Heaps");
  XELOGE("------------------------------------------------------------------");
  XELOGE("");
  heaps_.v00000000.DumpMap();
  heaps_.v40000000.DumpMap();
  heaps_.v80000000.DumpMap();
  heaps_.v90000000.DumpMap();
  XELOGE("");
  XELOGE("------------------------------------------------------------------");
  XELOGE("Physical Heaps");
  XELOGE("------------------------------------------------------------------");
  XELOGE("");
  heaps_.physical.DumpMap();
  heaps_.vA0000000.DumpMap();
  heaps_.vC0000000.DumpMap();
  heaps_.vE0000000.DumpMap();
  XELOGE("");
}

bool Memory::Save(ByteStream* stream) {
  XELOGD("Serializing memory...");
  heaps_.v00000000.Save(stream);
  heaps_.v40000000.Save(stream);
  heaps_.v80000000.Save(stream);
  heaps_.v90000000.Save(stream);
  heaps_.physical.Save(stream);

  return true;
}

bool Memory::Restore(ByteStream* stream) {
  XELOGD("Restoring memory...");
  heaps_.v00000000.Restore(stream);
  heaps_.v40000000.Restore(stream);
  heaps_.v80000000.Restore(stream);
  heaps_.v90000000.Restore(stream);
  heaps_.physical.Restore(stream);

  return true;
}

xe::memory::PageAccess ToPageAccess(uint32_t protect) {
  if ((protect & kMemoryProtectRead) && !(protect & kMemoryProtectWrite)) {
    return xe::memory::PageAccess::kReadOnly;
  } else if ((protect & kMemoryProtectRead) &&
             (protect & kMemoryProtectWrite)) {
    return xe::memory::PageAccess::kReadWrite;
  } else {
    return xe::memory::PageAccess::kNoAccess;
  }
}

uint32_t FromPageAccess(xe::memory::PageAccess protect) {
  switch (protect) {
    case memory::PageAccess::kNoAccess:
      return kMemoryProtectNoAccess;
    case memory::PageAccess::kReadOnly:
      return kMemoryProtectRead;
    case memory::PageAccess::kReadWrite:
      return kMemoryProtectRead | kMemoryProtectWrite;
    case memory::PageAccess::kExecuteReadWrite:
      // Guest memory cannot be executable - this should never happen :)
      assert_always();
      return kMemoryProtectRead | kMemoryProtectWrite;
  }

  return kMemoryProtectNoAccess;
}

BaseHeap::BaseHeap()
    : membase_(nullptr), heap_base_(0), heap_size_(0), page_size_(0) {}

BaseHeap::~BaseHeap() = default;

void BaseHeap::Initialize(uint8_t* membase, uint32_t heap_base,
                          uint32_t heap_size, uint32_t page_size) {
  membase_ = membase;
  heap_base_ = heap_base;
  heap_size_ = heap_size - 1;
  page_size_ = page_size;
  page_table_.resize(heap_size / page_size);
}

void BaseHeap::Dispose() {
  // Walk table and release all regions.
  for (uint32_t page_number = 0; page_number < page_table_.size();
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    if (page_entry.state) {
      xe::memory::DeallocFixed(membase_ + heap_base_ + page_number * page_size_,
                               0, xe::memory::DeallocationType::kRelease);
      page_number += page_entry.region_page_count;
    }
  }
}

void BaseHeap::DumpMap() {
  auto global_lock = global_critical_region_.Acquire();
  XELOGE("------------------------------------------------------------------");
  XELOGE("Heap: %.8X-%.8X", heap_base_, heap_base_ + heap_size_);
  XELOGE("------------------------------------------------------------------");
  XELOGE("   Heap Base: %.8X", heap_base_);
  XELOGE("   Heap Size: %d (%.8X)", heap_size_, heap_size_);
  XELOGE("   Page Size: %d (%.8X)", page_size_, page_size_);
  XELOGE("  Page Count: %lld", page_table_.size());
  bool is_empty_span = false;
  uint32_t empty_span_start = 0;
  for (uint32_t i = 0; i < uint32_t(page_table_.size()); ++i) {
    auto& page = page_table_[i];
    if (!page.state) {
      if (!is_empty_span) {
        is_empty_span = true;
        empty_span_start = i;
      }
      continue;
    }
    if (is_empty_span) {
      XELOGE("  %.8X-%.8X %6dp %10db unreserved",
             heap_base_ + empty_span_start * page_size_,
             heap_base_ + i * page_size_, i - empty_span_start,
             (i - empty_span_start) * page_size_);
      is_empty_span = false;
    }
    const char* state_name = "   ";
    if (page.state & kMemoryAllocationCommit) {
      state_name = "COM";
    } else if (page.state & kMemoryAllocationReserve) {
      state_name = "RES";
    }
    char access_r = (page.current_protect & kMemoryProtectRead) ? 'R' : ' ';
    char access_w = (page.current_protect & kMemoryProtectWrite) ? 'W' : ' ';
    XELOGE("  %.8X-%.8X %6dp %10db %s %c%c", heap_base_ + i * page_size_,
           heap_base_ + (i + page.region_page_count) * page_size_,
           page.region_page_count, page.region_page_count * page_size_,
           state_name, access_r, access_w);
    i += page.region_page_count - 1;
  }
  if (is_empty_span) {
    XELOGE("  %.8X-%.8X - %d unreserved pages)",
           heap_base_ + empty_span_start * page_size_, heap_base_ + heap_size_,
           page_table_.size() - empty_span_start);
  }
}

uint32_t BaseHeap::GetTotalPageCount() { return uint32_t(page_table_.size()); }

uint32_t BaseHeap::GetUnreservedPageCount() {
  auto global_lock = global_critical_region_.Acquire();
  uint32_t count = 0;
  bool is_empty_span = false;
  uint32_t empty_span_start = 0;
  uint32_t size = uint32_t(page_table_.size());
  for (uint32_t i = 0; i < size; ++i) {
    auto& page = page_table_[i];
    if (!page.state) {
      if (!is_empty_span) {
        is_empty_span = true;
        empty_span_start = i;
      }
      continue;
    }
    if (is_empty_span) {
      is_empty_span = false;
      count += i - empty_span_start;
    }
    i += page.region_page_count - 1;
  }
  if (is_empty_span) {
    count += size - empty_span_start;
  }
  return count;
}

bool BaseHeap::Save(ByteStream* stream) {
  XELOGD("Heap %.8X-%.8X", heap_base_, heap_base_ + heap_size_);

  for (size_t i = 0; i < page_table_.size(); i++) {
    auto& page = page_table_[i];
    stream->Write(page.qword);
    if (!page.state) {
      // Unallocated.
      continue;
    }

    // TODO(DrChat): write compressed with snappy.
    if (page.state & kMemoryAllocationCommit) {
      void* addr = membase_ + heap_base_ + i * page_size_;

      memory::PageAccess old_access;
      memory::Protect(addr, page_size_, memory::PageAccess::kReadWrite,
                      &old_access);

      stream->Write(addr, page_size_);

      memory::Protect(addr, page_size_, old_access, nullptr);
    }
  }

  return true;
}

bool BaseHeap::Restore(ByteStream* stream) {
  XELOGD("Heap %.8X-%.8X", heap_base_, heap_base_ + heap_size_);

  for (size_t i = 0; i < page_table_.size(); i++) {
    auto& page = page_table_[i];
    page.qword = stream->Read<uint64_t>();
    if (!page.state) {
      // Unallocated.
      continue;
    }

    memory::PageAccess page_access = memory::PageAccess::kNoAccess;
    if ((page.current_protect & kMemoryProtectRead) &&
        (page.current_protect & kMemoryProtectWrite)) {
      page_access = memory::PageAccess::kReadWrite;
    } else if (page.current_protect & kMemoryProtectRead) {
      page_access = memory::PageAccess::kReadOnly;
    }

    // Commit the memory if it isn't already. We do not need to reserve any
    // memory, as the mapping has already taken care of that.
    if (page.state & kMemoryAllocationCommit) {
      xe::memory::AllocFixed(membase_ + heap_base_ + i * page_size_, page_size_,
                             memory::AllocationType::kCommit,
                             memory::PageAccess::kReadWrite);
    }

    // Now read into memory. We'll set R/W protection first, then set the
    // protection back to its previous state.
    // TODO(DrChat): read compressed with snappy.
    if (page.state & kMemoryAllocationCommit) {
      void* addr = membase_ + heap_base_ + i * page_size_;
      xe::memory::Protect(addr, page_size_, memory::PageAccess::kReadWrite,
                          nullptr);

      stream->Read(addr, page_size_);

      xe::memory::Protect(addr, page_size_, page_access, nullptr);
    }
  }

  return true;
}

void BaseHeap::Reset() {
  // TODO(DrChat): protect pages.
  std::memset(page_table_.data(), 0, sizeof(PageEntry) * page_table_.size());
}

bool BaseHeap::Alloc(uint32_t size, uint32_t alignment,
                     uint32_t allocation_type, uint32_t protect, bool top_down,
                     uint32_t* out_address) {
  *out_address = 0;
  size = xe::round_up(size, page_size_);
  alignment = xe::round_up(alignment, page_size_);
  uint32_t low_address = heap_base_;
  uint32_t high_address = heap_base_ + heap_size_;
  return AllocRange(low_address, high_address, size, alignment, allocation_type,
                    protect, top_down, out_address);
}

bool BaseHeap::AllocFixed(uint32_t base_address, uint32_t size,
                          uint32_t alignment, uint32_t allocation_type,
                          uint32_t protect) {
  alignment = xe::round_up(alignment, page_size_);
  size = xe::align(size, alignment);
  assert_true(base_address % alignment == 0);
  uint32_t page_count = get_page_count(size, page_size_);
  uint32_t start_page_number = (base_address - heap_base_) / page_size_;
  uint32_t end_page_number = start_page_number + page_count - 1;
  if (start_page_number >= page_table_.size() ||
      end_page_number > page_table_.size()) {
    XELOGE("BaseHeap::AllocFixed passed out of range address range");
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  // - If we are reserving the entire range requested must not be already
  //   reserved.
  // - If we are committing it's ok for pages within the range to already be
  //   committed.
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    uint32_t state = page_table_[page_number].state;
    if ((allocation_type == kMemoryAllocationReserve) && state) {
      // Already reserved.
      XELOGE(
          "BaseHeap::AllocFixed attempting to reserve an already reserved "
          "range");
      return false;
    }
    if ((allocation_type == kMemoryAllocationCommit) &&
        !(state & kMemoryAllocationReserve)) {
      // Attempting a commit-only op on an unreserved page.
      // This may be OK.
      XELOGW("BaseHeap::AllocFixed attempting commit on unreserved page");
      allocation_type |= kMemoryAllocationReserve;
      break;
    }
  }

  // Allocate from host.
  if (allocation_type == kMemoryAllocationReserve) {
    // Reserve is not needed, as we are mapped already.
  } else {
    auto alloc_type = (allocation_type & kMemoryAllocationCommit)
                          ? xe::memory::AllocationType::kCommit
                          : xe::memory::AllocationType::kReserve;
    void* result = xe::memory::AllocFixed(
        membase_ + heap_base_ + start_page_number * page_size_,
        page_count * page_size_, alloc_type, ToPageAccess(protect));
    if (!result) {
      XELOGE("BaseHeap::AllocFixed failed to alloc range from host");
      return false;
    }

    if (FLAGS_scribble_heap && protect & kMemoryProtectWrite) {
      std::memset(result, 0xCD, page_count * page_size_);
    }
  }

  // Set page state.
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    if (allocation_type & kMemoryAllocationReserve) {
      // Region is based on reservation.
      page_entry.base_address = start_page_number;
      page_entry.region_page_count = page_count;
    }
    page_entry.allocation_protect = protect;
    page_entry.current_protect = protect;
    page_entry.state = kMemoryAllocationReserve | allocation_type;
  }

  return true;
}

bool BaseHeap::AllocRange(uint32_t low_address, uint32_t high_address,
                          uint32_t size, uint32_t alignment,
                          uint32_t allocation_type, uint32_t protect,
                          bool top_down, uint32_t* out_address) {
  *out_address = 0;

  alignment = xe::round_up(alignment, page_size_);
  uint32_t page_count = get_page_count(size, page_size_);
  low_address = std::max(heap_base_, xe::align(low_address, alignment));
  high_address =
      std::min(heap_base_ + heap_size_, xe::align(high_address, alignment));
  uint32_t low_page_number = (low_address - heap_base_) / page_size_;
  uint32_t high_page_number = (high_address - heap_base_) / page_size_;
  low_page_number = std::min(uint32_t(page_table_.size()) - 1, low_page_number);
  high_page_number =
      std::min(uint32_t(page_table_.size()) - 1, high_page_number);

  if (page_count > (high_page_number - low_page_number)) {
    XELOGE("BaseHeap::Alloc page count too big for requested range");
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  // Find a free page range.
  // The base page must match the requested alignment, so we first scan for
  // a free aligned page and only then check for continuous free pages.
  // TODO(benvanik): optimized searching (free list buckets, bitmap, etc).
  uint32_t start_page_number = UINT_MAX;
  uint32_t end_page_number = UINT_MAX;
  uint32_t page_scan_stride = alignment / page_size_;
  high_page_number = high_page_number - (high_page_number % page_scan_stride);
  if (top_down) {
    for (int64_t base_page_number =
             high_page_number - xe::round_up(page_count, page_scan_stride);
         base_page_number >= low_page_number;
         base_page_number -= page_scan_stride) {
      if (page_table_[base_page_number].state != 0) {
        // Base page not free, skip to next usable page.
        continue;
      }
      // Check requested range to ensure free.
      start_page_number = uint32_t(base_page_number);
      end_page_number = uint32_t(base_page_number) + page_count - 1;
      assert_true(end_page_number < page_table_.size());
      bool any_taken = false;
      for (uint32_t page_number = uint32_t(base_page_number);
           !any_taken && page_number <= end_page_number; ++page_number) {
        bool is_free = page_table_[page_number].state == 0;
        if (!is_free) {
          // At least one page in the range is used, skip to next.
          // We know we'll be starting at least before this page.
          any_taken = true;
          if (page_count > page_number) {
            // Not enough space left to fit entire page range. Breaks outer
            // loop.
            base_page_number = -1;
          } else {
            base_page_number = page_number - page_count;
            base_page_number -= base_page_number % page_scan_stride;
            base_page_number += page_scan_stride;  // cancel out loop logic
          }
          break;
        }
      }
      if (!any_taken) {
        // Found our place.
        break;
      }
      // Retry.
      start_page_number = end_page_number = UINT_MAX;
    }
  } else {
    for (uint32_t base_page_number = low_page_number;
         base_page_number <= high_page_number - page_count;
         base_page_number += page_scan_stride) {
      if (page_table_[base_page_number].state != 0) {
        // Base page not free, skip to next usable page.
        continue;
      }
      // Check requested range to ensure free.
      start_page_number = base_page_number;
      end_page_number = base_page_number + page_count - 1;
      bool any_taken = false;
      for (uint32_t page_number = base_page_number;
           !any_taken && page_number <= end_page_number; ++page_number) {
        bool is_free = page_table_[page_number].state == 0;
        if (!is_free) {
          // At least one page in the range is used, skip to next.
          // We know we'll be starting at least after this page.
          any_taken = true;
          base_page_number = xe::round_up(page_number + 1, page_scan_stride);
          base_page_number -= page_scan_stride;  // cancel out loop logic
          break;
        }
      }
      if (!any_taken) {
        // Found our place.
        break;
      }
      // Retry.
      start_page_number = end_page_number = UINT_MAX;
    }
  }
  if (start_page_number == UINT_MAX || end_page_number == UINT_MAX) {
    // Out of memory.
    XELOGE("BaseHeap::Alloc failed to find contiguous range");
    assert_always("Heap exhausted!");
    return false;
  }

  // Allocate from host.
  if (allocation_type == kMemoryAllocationReserve) {
    // Reserve is not needed, as we are mapped already.
  } else {
    auto alloc_type = (allocation_type & kMemoryAllocationCommit)
                          ? xe::memory::AllocationType::kCommit
                          : xe::memory::AllocationType::kReserve;
    void* result = xe::memory::AllocFixed(
        membase_ + heap_base_ + start_page_number * page_size_,
        page_count * page_size_, alloc_type, ToPageAccess(protect));
    if (!result) {
      XELOGE("BaseHeap::Alloc failed to alloc range from host");
      return false;
    }

    if (FLAGS_scribble_heap && (protect & kMemoryProtectWrite)) {
      std::memset(result, 0xCD, page_count * page_size_);
    }
  }

  // Set page state.
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    page_entry.base_address = start_page_number;
    page_entry.region_page_count = page_count;
    page_entry.allocation_protect = protect;
    page_entry.current_protect = protect;
    page_entry.state = kMemoryAllocationReserve | allocation_type;
  }

  *out_address = heap_base_ + (start_page_number * page_size_);
  return true;
}

bool BaseHeap::Decommit(uint32_t address, uint32_t size) {
  uint32_t page_count = get_page_count(size, page_size_);
  uint32_t start_page_number = (address - heap_base_) / page_size_;
  uint32_t end_page_number = start_page_number + page_count - 1;
  start_page_number =
      std::min(uint32_t(page_table_.size()) - 1, start_page_number);
  end_page_number = std::min(uint32_t(page_table_.size()) - 1, end_page_number);

  auto global_lock = global_critical_region_.Acquire();

  // Release from host.
  // TODO(benvanik): find a way to actually decommit memory;
  //     mapped memory cannot be decommitted.
  /*BOOL result =
      VirtualFree(membase_ + heap_base_ + start_page_number * page_size_,
                  page_count * page_size_, MEM_DECOMMIT);
  if (!result) {
    PLOGW("BaseHeap::Decommit failed due to host VirtualFree failure");
    return false;
  }*/

  // Perform table change.
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    page_entry.state &= ~kMemoryAllocationCommit;
  }

  return true;
}

bool BaseHeap::Release(uint32_t base_address, uint32_t* out_region_size) {
  auto global_lock = global_critical_region_.Acquire();

  // Given address must be a region base address.
  uint32_t base_page_number = (base_address - heap_base_) / page_size_;
  auto base_page_entry = page_table_[base_page_number];
  if (base_page_entry.base_address != base_page_number) {
    XELOGE("BaseHeap::Release failed because address is not a region start");
    return false;
  }

  if (heap_base_ == 0x00000000 && base_page_number == 0) {
    XELOGE("BaseHeap::Release: Attempt to free 0!");
    return false;
  }

  if (out_region_size) {
    *out_region_size = (base_page_entry.region_page_count * page_size_);
  }

  // Release from host not needed as mapping reserves the range for us.
  // TODO(benvanik): protect with NOACCESS?
  /*BOOL result = VirtualFree(
      membase_ + heap_base_ + base_page_number * page_size_, 0, MEM_RELEASE);
  if (!result) {
    PLOGE("BaseHeap::Release failed due to host VirtualFree failure");
    return false;
  }*/
  // Instead, we just protect it, if we can.
  if (page_size_ == xe::memory::page_size() ||
      ((base_page_entry.region_page_count * page_size_) %
               xe::memory::page_size() ==
           0 &&
       ((base_page_number * page_size_) % xe::memory::page_size() == 0))) {
    // TODO(benvanik): figure out why games are using memory after releasing it.
    // It's possible this is some virtual/physical stuff where the GPU still can
    // access it.
    if (FLAGS_protect_on_release) {
      if (!xe::memory::Protect(
              membase_ + heap_base_ + base_page_number * page_size_,
              base_page_entry.region_page_count * page_size_,
              xe::memory::PageAccess::kNoAccess, nullptr)) {
        XELOGW("BaseHeap::Release failed due to host VirtualProtect failure");
      }
    }
  }

  // Perform table change.
  uint32_t end_page_number =
      base_page_number + base_page_entry.region_page_count - 1;
  for (uint32_t page_number = base_page_number; page_number <= end_page_number;
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    page_entry.qword = 0;
  }

  return true;
}

bool BaseHeap::Protect(uint32_t address, uint32_t size, uint32_t protect,
                       uint32_t* old_protect) {
  uint32_t page_count = xe::round_up(size, page_size_) / page_size_;
  uint32_t start_page_number = (address - heap_base_) / page_size_;
  uint32_t end_page_number = start_page_number + page_count - 1;
  start_page_number =
      std::min(uint32_t(page_table_.size()) - 1, start_page_number);
  end_page_number = std::min(uint32_t(page_table_.size()) - 1, end_page_number);

  auto global_lock = global_critical_region_.Acquire();

  // Ensure all pages are in the same reserved region and all are committed.
  uint32_t first_base_address = UINT_MAX;
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    auto page_entry = page_table_[page_number];
    if (first_base_address == UINT_MAX) {
      first_base_address = page_entry.base_address;
    } else if (first_base_address != page_entry.base_address) {
      XELOGE("BaseHeap::Protect failed due to request spanning regions");
      return false;
    }
    if (!(page_entry.state & kMemoryAllocationCommit)) {
      XELOGE("BaseHeap::Protect failed due to uncommitted page");
      return false;
    }
  }

  // Attempt host change (hopefully won't fail).
  // We can only do this if our size matches system page granularity.
  if (page_size_ == xe::memory::page_size() ||
      (((page_count * page_size_) % xe::memory::page_size() == 0) &&
       ((start_page_number * page_size_) % xe::memory::page_size() == 0))) {
    memory::PageAccess old_protect_access;
    if (!xe::memory::Protect(
            membase_ + heap_base_ + start_page_number * page_size_,
            page_count * page_size_, ToPageAccess(protect),
            old_protect ? &old_protect_access : nullptr)) {
      XELOGE("BaseHeap::Protect failed due to host VirtualProtect failure");
      return false;
    }

    if (old_protect) {
      *old_protect = FromPageAccess(old_protect_access);
    }
  } else {
    XELOGW("BaseHeap::Protect: ignoring request as not 4k page aligned");
    return false;
  }

  // Perform table change.
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    page_entry.current_protect = protect;
  }

  return true;
}

bool BaseHeap::QueryRegionInfo(uint32_t base_address,
                               HeapAllocationInfo* out_info) {
  uint32_t start_page_number = (base_address - heap_base_) / page_size_;
  if (start_page_number > page_table_.size()) {
    XELOGE("BaseHeap::QueryRegionInfo base page out of range");
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  auto start_page_entry = page_table_[start_page_number];
  out_info->base_address = base_address;
  out_info->allocation_base = 0;
  out_info->allocation_protect = 0;
  out_info->region_size = 0;
  out_info->state = 0;
  out_info->protect = 0;
  out_info->type = 0;
  if (start_page_entry.state) {
    // Committed/reserved region.
    out_info->allocation_base = start_page_entry.base_address * page_size_;
    out_info->allocation_protect = start_page_entry.allocation_protect;
    out_info->state = start_page_entry.state;
    out_info->protect = start_page_entry.current_protect;
    out_info->type = 0x20000;
    for (uint32_t page_number = start_page_number;
         page_number < start_page_number + start_page_entry.region_page_count;
         ++page_number) {
      auto page_entry = page_table_[page_number];
      if (page_entry.base_address != start_page_entry.base_address ||
          page_entry.state != start_page_entry.state ||
          page_entry.current_protect != start_page_entry.current_protect) {
        // Different region or different properties within the region; done.
        break;
      }
      out_info->region_size += page_size_;
    }
  } else {
    // Free region.
    for (uint32_t page_number = start_page_number;
         page_number < page_table_.size(); ++page_number) {
      auto page_entry = page_table_[page_number];
      if (page_entry.state) {
        // First non-free page; done with region.
        break;
      }
      out_info->region_size += page_size_;
    }
  }
  return true;
}

bool BaseHeap::QuerySize(uint32_t address, uint32_t* out_size) {
  uint32_t page_number = (address - heap_base_) / page_size_;
  if (page_number > page_table_.size()) {
    XELOGE("BaseHeap::QuerySize base page out of range");
    *out_size = 0;
    return false;
  }
  auto global_lock = global_critical_region_.Acquire();
  auto page_entry = page_table_[page_number];
  *out_size = (page_entry.region_page_count * page_size_);
  return true;
}

bool BaseHeap::QueryProtect(uint32_t address, uint32_t* out_protect) {
  uint32_t page_number = (address - heap_base_) / page_size_;
  if (page_number > page_table_.size()) {
    XELOGE("BaseHeap::QueryProtect base page out of range");
    *out_protect = 0;
    return false;
  }
  auto global_lock = global_critical_region_.Acquire();
  auto page_entry = page_table_[page_number];
  *out_protect = page_entry.current_protect;
  return true;
}

uint32_t BaseHeap::GetPhysicalAddress(uint32_t address) {
  // Only valid for memory in this range - will be bogus if the origin was
  // outside of it.
  uint32_t physical_address = address & 0x1FFFFFFF;
  if (address >= 0xE0000000) {
    physical_address += 0x1000;
  }
  return physical_address;
}

VirtualHeap::VirtualHeap() = default;

VirtualHeap::~VirtualHeap() = default;

void VirtualHeap::Initialize(uint8_t* membase, uint32_t heap_base,
                             uint32_t heap_size, uint32_t page_size) {
  BaseHeap::Initialize(membase, heap_base, heap_size, page_size);
}

PhysicalHeap::PhysicalHeap() : parent_heap_(nullptr) {}

PhysicalHeap::~PhysicalHeap() = default;

void PhysicalHeap::Initialize(uint8_t* membase, uint32_t heap_base,
                              uint32_t heap_size, uint32_t page_size,
                              VirtualHeap* parent_heap) {
  BaseHeap::Initialize(membase, heap_base, heap_size, page_size);
  parent_heap_ = parent_heap;
}

bool PhysicalHeap::Alloc(uint32_t size, uint32_t alignment,
                         uint32_t allocation_type, uint32_t protect,
                         bool top_down, uint32_t* out_address) {
  *out_address = 0;

  // Default top-down. Since parent heap is bottom-up this prevents collisions.
  top_down = true;

  // Adjust alignment size our page size differs from the parent.
  size = xe::round_up(size, page_size_);
  alignment = xe::round_up(alignment, page_size_);

  auto global_lock = global_critical_region_.Acquire();

  // Allocate from parent heap (gets our physical address in 0-512mb).
  uint32_t parent_low_address = GetPhysicalAddress(heap_base_);
  uint32_t parent_high_address = GetPhysicalAddress(heap_base_ + heap_size_);
  uint32_t parent_address;
  if (!parent_heap_->AllocRange(parent_low_address, parent_high_address, size,
                                alignment, allocation_type, protect, top_down,
                                &parent_address)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to alloc physical memory in parent heap");
    return false;
  }
  if (heap_base_ >= 0xE0000000) {
    parent_address -= 0x1000;
  }

  // Given the address we've reserved in the parent heap, pin that here.
  // Shouldn't be possible for it to be allocated already.
  uint32_t address = heap_base_ + parent_address;
  if (!BaseHeap::AllocFixed(address, size, alignment, allocation_type,
                            protect)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to pin physical memory in physical heap");
    // TODO(benvanik): don't leak parent memory.
    return false;
  }
  *out_address = address;
  return true;
}

bool PhysicalHeap::AllocFixed(uint32_t base_address, uint32_t size,
                              uint32_t alignment, uint32_t allocation_type,
                              uint32_t protect) {
  // Adjust alignment size our page size differs from the parent.
  size = xe::round_up(size, page_size_);
  alignment = xe::round_up(alignment, page_size_);

  auto global_lock = global_critical_region_.Acquire();

  // Allocate from parent heap (gets our physical address in 0-512mb).
  // NOTE: this can potentially overwrite heap contents if there are already
  // committed pages in the requested physical range.
  // TODO(benvanik): flag for ensure-not-committed?
  uint32_t parent_base_address = GetPhysicalAddress(base_address);
  if (!parent_heap_->AllocFixed(parent_base_address, size, alignment,
                                allocation_type, protect)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to alloc physical memory in parent heap");
    return false;
  }
  if (heap_base_ >= 0xE0000000) {
    parent_base_address -= 0x1000;
  }

  // Given the address we've reserved in the parent heap, pin that here.
  // Shouldn't be possible for it to be allocated already.
  uint32_t address = heap_base_ + parent_base_address;
  if (!BaseHeap::AllocFixed(address, size, alignment, allocation_type,
                            protect)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to pin physical memory in physical heap");
    // TODO(benvanik): don't leak parent memory.
    return false;
  }

  return true;
}

bool PhysicalHeap::AllocRange(uint32_t low_address, uint32_t high_address,
                              uint32_t size, uint32_t alignment,
                              uint32_t allocation_type, uint32_t protect,
                              bool top_down, uint32_t* out_address) {
  *out_address = 0;

  // Adjust alignment size our page size differs from the parent.
  size = xe::round_up(size, page_size_);
  alignment = xe::round_up(alignment, page_size_);

  auto global_lock = global_critical_region_.Acquire();

  // Allocate from parent heap (gets our physical address in 0-512mb).
  low_address = std::max(heap_base_, low_address);
  high_address = std::min(heap_base_ + heap_size_, high_address);
  uint32_t parent_low_address = GetPhysicalAddress(low_address);
  uint32_t parent_high_address = GetPhysicalAddress(high_address);
  uint32_t parent_address;
  if (!parent_heap_->AllocRange(parent_low_address, parent_high_address, size,
                                alignment, allocation_type, protect, top_down,
                                &parent_address)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to alloc physical memory in parent heap");
    return false;
  }
  if (heap_base_ >= 0xE0000000) {
    parent_address -= 0x1000;
  }

  // Given the address we've reserved in the parent heap, pin that here.
  // Shouldn't be possible for it to be allocated already.
  uint32_t address = heap_base_ + parent_address;
  if (!BaseHeap::AllocFixed(address, size, alignment, allocation_type,
                            protect)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to pin physical memory in physical heap");
    // TODO(benvanik): don't leak parent memory.
    return false;
  }
  *out_address = address;
  return true;
}

bool PhysicalHeap::Decommit(uint32_t address, uint32_t size) {
  auto global_lock = global_critical_region_.Acquire();
  uint32_t parent_address = GetPhysicalAddress(address);
  if (!parent_heap_->Decommit(parent_address, size)) {
    XELOGE("PhysicalHeap::Decommit failed due to parent heap failure");
    return false;
  }
  return BaseHeap::Decommit(address, size);
}

bool PhysicalHeap::Release(uint32_t base_address, uint32_t* out_region_size) {
  auto global_lock = global_critical_region_.Acquire();
  uint32_t parent_base_address = GetPhysicalAddress(base_address);
  uint32_t region_size = 0;
  if (QuerySize(base_address, &region_size)) {
    cpu::MMIOHandler::global_handler()->InvalidateRange(parent_base_address,
                                                        region_size);
  }

  if (!parent_heap_->Release(parent_base_address, out_region_size)) {
    XELOGE("PhysicalHeap::Release failed due to parent heap failure");
    return false;
  }

  return BaseHeap::Release(base_address, out_region_size);
}

bool PhysicalHeap::Protect(uint32_t address, uint32_t size, uint32_t protect,
                           uint32_t* old_protect) {
  auto global_lock = global_critical_region_.Acquire();
  uint32_t parent_address = GetPhysicalAddress(address);
  cpu::MMIOHandler::global_handler()->InvalidateRange(parent_address, size);

  if (!parent_heap_->Protect(parent_address, size, protect, old_protect)) {
    XELOGE("PhysicalHeap::Protect failed due to parent heap failure");
    return false;
  }

  return BaseHeap::Protect(address, size, protect);
}

}  // namespace xe

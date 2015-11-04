/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

uint32_t ToXdkProtectFlags(uint32_t protect) {
  uint32_t result = 0;
  if (!(protect & kMemoryProtectRead) && !(protect & kMemoryProtectWrite)) {
    result = X_PAGE_NOACCESS;
  } else if ((protect & kMemoryProtectRead) &&
             !(protect & kMemoryProtectWrite)) {
    result = X_PAGE_READONLY;
  } else {
    result = X_PAGE_READWRITE;
  }
  if (protect & kMemoryProtectNoCache) {
    result = X_PAGE_NOCACHE;
  }
  if (protect & kMemoryProtectWriteCombine) {
    result = X_PAGE_WRITECOMBINE;
  }
  return result;
}

uint32_t FromXdkProtectFlags(uint32_t protect) {
  uint32_t result = 0;
  if ((protect & X_PAGE_READONLY) | (protect & X_PAGE_EXECUTE_READ)) {
    result |= kMemoryProtectRead;
  } else if ((protect & X_PAGE_READWRITE) |
             (protect & X_PAGE_EXECUTE_READWRITE)) {
    result |= kMemoryProtectRead | kMemoryProtectWrite;
  }
  if (protect & X_PAGE_NOCACHE) {
    result |= kMemoryProtectNoCache;
  }
  if (protect & X_PAGE_WRITECOMBINE) {
    result |= kMemoryProtectWriteCombine;
  }
  return result;
}

SHIM_CALL NtAllocateVirtualMemory_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  uint32_t base_addr_ptr = SHIM_GET_ARG_32(0);
  uint32_t base_addr_value = SHIM_MEM_32(base_addr_ptr);
  uint32_t region_size_ptr = SHIM_GET_ARG_32(1);
  uint32_t region_size_value = SHIM_MEM_32(region_size_ptr);
  uint32_t alloc_type = SHIM_GET_ARG_32(2);    // X_MEM_* bitmask
  uint32_t protect_bits = SHIM_GET_ARG_32(3);  // X_PAGE_* bitmask
  uint32_t unknown = SHIM_GET_ARG_32(4);

  XELOGD("NtAllocateVirtualMemory(%.8X(%.8X), %.8X(%.8X), %.8X, %.8X, %.8X)",
         base_addr_ptr, base_addr_value, region_size_ptr, region_size_value,
         alloc_type, protect_bits, unknown);

  // NTSTATUS
  // _Inout_  PVOID *BaseAddress,
  // _Inout_  PSIZE_T RegionSize,
  // _In_     ULONG AllocationType,
  // _In_     ULONG Protect
  // ? handle?

  // I've only seen zero.
  assert_true(unknown == 0);

  // This allocates memory from the kernel heap, which is initialized on startup
  // and shared by both the kernel implementation and user code.
  // The xe_memory_ref object is used to actually get the memory, and although
  // it's simple today we could extend it to do better things in the future.

  // Must request a size.
  if (!region_size_value) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
    return;
  }
  // Check allocation type.
  if (!(alloc_type & (X_MEM_COMMIT | X_MEM_RESET | X_MEM_RESERVE))) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
    return;
  }
  // If MEM_RESET is set only MEM_RESET can be set.
  if (alloc_type & X_MEM_RESET && (alloc_type & ~X_MEM_RESET)) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
    return;
  }
  // Don't allow games to set execute bits.
  if (protect_bits & (X_PAGE_EXECUTE | X_PAGE_EXECUTE_READ |
                      X_PAGE_EXECUTE_READWRITE | X_PAGE_EXECUTE_WRITECOPY)) {
    XELOGW("Game setting EXECUTE bit on allocation");
  }

  // Adjust size.
  uint32_t page_size = 4096;
  if (alloc_type & X_MEM_LARGE_PAGES) {
    page_size = 64 * 1024;
  }
  if (int32_t(region_size_value) < 0) {
    // Some games pass in negative sizes.
    region_size_value = -int32_t(region_size_value);
  }
  uint32_t adjusted_size = xe::round_up(region_size_value, page_size);

  // Some games (BF1943) do this, but then if we return an error code it'll
  // allocate with a smaller page size.
  if (base_addr_value % page_size != 0) {
    SHIM_SET_RETURN_32(X_STATUS_MAPPED_ALIGNMENT);
    return;
  }

  // Allocate.
  uint32_t allocation_type = 0;
  if (alloc_type & X_MEM_RESERVE) {
    allocation_type |= kMemoryAllocationReserve;
  }
  if (alloc_type & X_MEM_COMMIT) {
    allocation_type |= kMemoryAllocationCommit;
  }
  if (alloc_type & X_MEM_RESET) {
    XELOGE("X_MEM_RESET not implemented");
    assert_always();
  }
  uint32_t protect = FromXdkProtectFlags(protect_bits);
  uint32_t address = 0;
  if (base_addr_value) {
    auto heap = kernel_state->memory()->LookupHeap(base_addr_value);
    if (heap->AllocFixed(base_addr_value, adjusted_size, page_size,
                         allocation_type, protect)) {
      address = base_addr_value;
    }
  } else {
    bool top_down = !!(alloc_type & X_MEM_TOP_DOWN);
    auto heap = kernel_state->memory()->LookupHeapByType(false, page_size);
    heap->Alloc(adjusted_size, page_size, allocation_type, protect, top_down,
                &address);
  }
  if (!address) {
    // Failed - assume no memory available.
    SHIM_SET_RETURN_32(X_STATUS_NO_MEMORY);
    return;
  }

  // Zero memory, if needed.
  if (address && !(alloc_type & X_MEM_NOZERO)) {
    if (alloc_type & X_MEM_COMMIT) {
      kernel_state->memory()->Zero(address, adjusted_size);
    }
  }

  XELOGD("NtAllocateVirtualMemory = %.8X", address);

  // Stash back.
  // Maybe set X_STATUS_ALREADY_COMMITTED if MEM_COMMIT?
  SHIM_SET_MEM_32(base_addr_ptr, address);
  SHIM_SET_MEM_32(region_size_ptr, adjusted_size);
  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL NtFreeVirtualMemory_shim(PPCContext* ppc_context,
                                   KernelState* kernel_state) {
  uint32_t base_addr_ptr = SHIM_GET_ARG_32(0);
  uint32_t base_addr_value = SHIM_MEM_32(base_addr_ptr);
  uint32_t region_size_ptr = SHIM_GET_ARG_32(1);
  uint32_t region_size_value = SHIM_MEM_32(region_size_ptr);
  // X_MEM_DECOMMIT | X_MEM_RELEASE
  uint32_t free_type = SHIM_GET_ARG_32(2);
  uint32_t unknown = SHIM_GET_ARG_32(3);

  XELOGD("NtFreeVirtualMemory(%.8X(%.8X), %.8X(%.8X), %.8X, %.8X)",
         base_addr_ptr, base_addr_value, region_size_ptr, region_size_value,
         free_type, unknown);

  // NTSTATUS
  // _Inout_  PVOID *BaseAddress,
  // _Inout_  PSIZE_T RegionSize,
  // _In_     ULONG FreeType
  // ? handle?

  // I've only seen zero.
  assert_true(unknown == 0);

  if (!base_addr_value) {
    SHIM_SET_RETURN_32(X_STATUS_MEMORY_NOT_ALLOCATED);
    return;
  }

  auto heap = kernel_state->memory()->LookupHeap(base_addr_value);
  bool result = false;
  if (free_type == X_MEM_DECOMMIT) {
    // If zero, we may need to query size (free whole region).
    assert_not_zero(region_size_value);

    region_size_value = xe::round_up(region_size_value, heap->page_size());
    result = heap->Decommit(base_addr_value, region_size_value);
  } else {
    result = heap->Release(base_addr_value, &region_size_value);
  }
  if (!result) {
    SHIM_SET_RETURN_32(X_STATUS_UNSUCCESSFUL);
    return;
  }

  SHIM_SET_MEM_32(base_addr_ptr, base_addr_value);
  SHIM_SET_MEM_32(region_size_ptr, region_size_value);
  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

struct X_MEMORY_BASIC_INFORMATION {
  be<uint32_t> base_address;
  be<uint32_t> allocation_base;
  be<uint32_t> allocation_protect;
  be<uint32_t> region_size;
  be<uint32_t> state;
  be<uint32_t> protect;
  be<uint32_t> type;
};

SHIM_CALL NtQueryVirtualMemory_shim(PPCContext* ppc_context,
                                    KernelState* kernel_state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);
  uint32_t memory_basic_information_ptr = SHIM_GET_ARG_32(1);
  auto memory_basic_information =
      SHIM_STRUCT(X_MEMORY_BASIC_INFORMATION, memory_basic_information_ptr);

  XELOGD("NtQueryVirtualMemory(%.8X, %.8X)", base_address,
         memory_basic_information_ptr);

  auto heap = kernel_state->memory()->LookupHeap(base_address);
  HeapAllocationInfo alloc_info;
  if (heap == nullptr || !heap->QueryRegionInfo(base_address, &alloc_info)) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
    return;
  }

  memory_basic_information->base_address =
      static_cast<uint32_t>(alloc_info.base_address);
  memory_basic_information->allocation_base =
      static_cast<uint32_t>(alloc_info.allocation_base);
  memory_basic_information->allocation_protect =
      ToXdkProtectFlags(alloc_info.allocation_protect);
  memory_basic_information->region_size =
      static_cast<uint32_t>(alloc_info.region_size);
  uint32_t x_state = 0;
  if (alloc_info.state & kMemoryAllocationReserve) {
    x_state |= X_MEM_RESERVE;
  }
  if (alloc_info.state & kMemoryAllocationCommit) {
    x_state |= X_MEM_COMMIT;
  }
  memory_basic_information->state = x_state;
  memory_basic_information->protect = ToXdkProtectFlags(alloc_info.protect);
  memory_basic_information->type = alloc_info.type;

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL MmAllocatePhysicalMemoryEx_shim(PPCContext* ppc_context,
                                          KernelState* kernel_state) {
  uint32_t type = SHIM_GET_ARG_32(0);
  uint32_t region_size = SHIM_GET_ARG_32(1);
  uint32_t protect_bits = SHIM_GET_ARG_32(2);
  uint32_t min_addr_range = SHIM_GET_ARG_32(3);
  uint32_t max_addr_range = SHIM_GET_ARG_32(4);
  uint32_t alignment = SHIM_GET_ARG_32(5);

  XELOGD("MmAllocatePhysicalMemoryEx(%d, %.8X, %.8X, %.8X, %.8X, %.8X)", type,
         region_size, protect_bits, min_addr_range, max_addr_range, alignment);

  // Type will usually be 0 (user request?), where 1 and 2 are sometimes made
  // by D3D/etc.

  // Check protection bits.
  if (!(protect_bits & (X_PAGE_READONLY | X_PAGE_READWRITE))) {
    XELOGE("MmAllocatePhysicalMemoryEx: bad protection bits");
    SHIM_SET_RETURN_32(0);
    return;
  }

  // Either may be OR'ed into protect_bits:
  // X_PAGE_NOCACHE
  // X_PAGE_WRITECOMBINE
  // We could use this to detect what's likely GPU-synchronized memory
  // and let the GPU know we're messing with it (or even allocate from
  // the GPU). At least the D3D command buffer is X_PAGE_WRITECOMBINE.

  // Calculate page size.
  // Default            = 4KB
  // X_MEM_LARGE_PAGES  = 64KB
  // X_MEM_16MB_PAGES   = 16MB
  uint32_t page_size = 4 * 1024;
  if (protect_bits & X_MEM_LARGE_PAGES) {
    page_size = 64 * 1024;
  } else if (protect_bits & X_MEM_16MB_PAGES) {
    page_size = 16 * 1024 * 1024;
  }

  // Round up the region size and alignment to the next page.
  uint32_t adjusted_size = xe::round_up(region_size, page_size);
  uint32_t adjusted_alignment = xe::round_up(alignment, page_size);

  // Callers can pick an address to allocate with min_addr_range/max_addr_range
  // and the memory must be allocated there. I haven't seen a game do this,
  // and instead they all do min=0 / max=-1 to indicate the system should pick.
  // If we have to suport arbitrary placement things will get nasty.

  uint32_t allocation_type = kMemoryAllocationReserve | kMemoryAllocationCommit;
  uint32_t protect = FromXdkProtectFlags(protect_bits);
  bool top_down = true;
  auto heap = kernel_state->memory()->LookupHeapByType(true, page_size);
  uint32_t base_address;
  if (!heap->AllocRange(min_addr_range, max_addr_range, adjusted_size,
                        adjusted_alignment, allocation_type, protect, top_down,
                        &base_address)) {
    // Failed - assume no memory available.
    SHIM_SET_RETURN_32(0);
    return;
  }
  XELOGD("MmAllocatePhysicalMemoryEx = %.8X", base_address);

  SHIM_SET_RETURN_32(base_address);
}

SHIM_CALL MmFreePhysicalMemory_shim(PPCContext* ppc_context,
                                    KernelState* kernel_state) {
  uint32_t type = SHIM_GET_ARG_32(0);
  uint32_t base_address = SHIM_GET_ARG_32(1);

  XELOGD("MmFreePhysicalAddress(%d, %.8X)", type, base_address);

  // base_address = result of MmAllocatePhysicalMemory.

  assert_true((base_address & 0x1F) == 0);

  auto heap = kernel_state->memory()->LookupHeap(base_address);
  heap->Release(base_address);
}

SHIM_CALL MmQueryAddressProtect_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD("MmQueryAddressProtect(%.8X)", base_address);

  auto heap = kernel_state->memory()->LookupHeap(base_address);
  uint32_t access;
  if (!heap->QueryProtect(base_address, &access)) {
    access = 0;
  }
  access = ToXdkProtectFlags(access);

  SHIM_SET_RETURN_32(access);
}

void MmSetAddressProtect(lpvoid_t base_address, dword_t region_size,
                         dword_t protect_bits) {
  uint32_t protect = FromXdkProtectFlags(protect_bits);
  auto heap = kernel_memory()->LookupHeap(base_address);
  heap->Protect(base_address.guest_address(), region_size, protect);
}
DECLARE_XBOXKRNL_EXPORT(MmSetAddressProtect, ExportTag::kMemory);

SHIM_CALL MmQueryAllocationSize_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD("MmQueryAllocationSize(%.8X)", base_address);

  auto heap = kernel_state->memory()->LookupHeap(base_address);
  uint32_t size;
  if (!heap->QuerySize(base_address, &size)) {
    size = 0;
  }

  SHIM_SET_RETURN_32(size);
}

// https://code.google.com/p/vdash/source/browse/trunk/vdash/include/kernel.h
struct X_MM_QUERY_STATISTICS_SECTION {
  xe::be<uint32_t> available_pages;
  xe::be<uint32_t> total_virtual_memory_bytes;
  xe::be<uint32_t> reserved_virtual_memory_bytes;
  xe::be<uint32_t> physical_pages;
  xe::be<uint32_t> pool_pages;
  xe::be<uint32_t> stack_pages;
  xe::be<uint32_t> image_pages;
  xe::be<uint32_t> heap_pages;
  xe::be<uint32_t> virtual_pages;
  xe::be<uint32_t> page_table_pages;
  xe::be<uint32_t> cache_pages;
};

struct X_MM_QUERY_STATISTICS_RESULT {
  xe::be<uint32_t> size;
  xe::be<uint32_t> total_physical_pages;
  xe::be<uint32_t> kernel_pages;
  X_MM_QUERY_STATISTICS_SECTION title;
  X_MM_QUERY_STATISTICS_SECTION system;
  xe::be<uint32_t> highest_physical_page;
};
static_assert_size(X_MM_QUERY_STATISTICS_RESULT, 104);

SHIM_CALL MmQueryStatistics_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t stats_ptr = SHIM_GET_ARG_32(0);

  XELOGD("MmQueryStatistics(%.8X)", stats_ptr);

  if (stats_ptr == 0) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
    return;
  }

  const uint32_t size = sizeof(X_MM_QUERY_STATISTICS_RESULT);

  auto stats =
      reinterpret_cast<X_MM_QUERY_STATISTICS_RESULT*>(SHIM_MEM_ADDR(stats_ptr));
  if (stats->size != size) {
    SHIM_SET_RETURN_32(X_STATUS_BUFFER_TOO_SMALL);
    return;
  }

  // Zero out the struct.
  std::memset(stats, 0, size);

  // Set the constants the game is likely asking for.
  // These numbers are mostly guessed. If the game is just checking for
  // memory, this should satisfy it. If it's actually verifying things
  // this won't work :/
  stats->size = size;

  stats->total_physical_pages = 0x00020000;
  stats->kernel_pages = 0x00000300;

  stats->title.available_pages = 0x00013300;
  stats->title.total_virtual_memory_bytes = 0x2FFF0000;
  stats->title.reserved_virtual_memory_bytes = 0x00160000;
  stats->title.physical_pages = 0x00001000;
  stats->title.pool_pages = 0x00000010;
  stats->title.stack_pages = 0x00000100;
  stats->title.image_pages = 0x00000100;
  stats->title.heap_pages = 0x00000100;
  stats->title.virtual_pages = 0x00000100;
  stats->title.page_table_pages = 0x00000100;
  stats->title.cache_pages = 0x00000100;

  stats->system.available_pages = 0x00000000;
  stats->system.total_virtual_memory_bytes = 0x00000000;
  stats->system.reserved_virtual_memory_bytes = 0x00000000;
  stats->system.physical_pages = 0x00000000;
  stats->system.pool_pages = 0x00000000;
  stats->system.stack_pages = 0x00000000;
  stats->system.image_pages = 0x00000000;
  stats->system.heap_pages = 0x00000000;
  stats->system.virtual_pages = 0x00000000;
  stats->system.page_table_pages = 0x00000000;
  stats->system.cache_pages = 0x00000000;

  stats->highest_physical_page = 0x0001FFFF;

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff554547(v=vs.85).aspx
SHIM_CALL MmGetPhysicalAddress_shim(PPCContext* ppc_context,
                                    KernelState* kernel_state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD("MmGetPhysicalAddress(%.8X)", base_address);

  // PHYSICAL_ADDRESS MmGetPhysicalAddress(
  //   _In_  PVOID BaseAddress
  // );
  // base_address = result of MmAllocatePhysicalMemory.
  assert_true(base_address >= 0xA0000000);

  uint32_t physical_address = base_address & 0x1FFFFFFF;
  if (base_address >= 0xE0000000) {
    physical_address += 0x1000;
  }

  SHIM_SET_RETURN_32(physical_address);
}

SHIM_CALL MmMapIoSpace_shim(PPCContext* ppc_context,
                            KernelState* kernel_state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);
  uint32_t src_address = SHIM_GET_ARG_32(1);  // from MmGetPhysicalAddress
  uint32_t size = SHIM_GET_ARG_32(2);
  uint32_t flags = SHIM_GET_ARG_32(3);

  XELOGD("MmMapIoSpace(%.8X, %.8X, %d, %.8X)", unk0, src_address, size, flags);

  // I've only seen this used to map XMA audio contexts.
  // The code seems fine with taking the src address, so this just returns that.
  // If others start using it there could be problems.
  assert_true(unk0 == 2);
  assert_true(size == 0x40);
  assert_true(flags == 0x404);

  SHIM_SET_RETURN_32(src_address);
}

SHIM_CALL ExAllocatePoolTypeWithTag_shim(PPCContext* ppc_context,
                                         KernelState* kernel_state) {
  uint32_t size = SHIM_GET_ARG_32(0);
  uint32_t tag = SHIM_GET_ARG_32(1);
  uint32_t zero = SHIM_GET_ARG_32(2);

  XELOGD("ExAllocatePoolTypeWithTag(%d, %.4s, %d)", size, &tag, zero);

  uint32_t alignment = 8;
  uint32_t adjusted_size = size;
  if (adjusted_size < 4 * 1024) {
    adjusted_size = xe::round_up(adjusted_size, 4 * 1024);
  } else {
    alignment = 4 * 1024;
  }

  uint32_t addr =
      kernel_state->memory()->SystemHeapAlloc(adjusted_size, alignment);

  SHIM_SET_RETURN_32(addr);
}

SHIM_CALL ExFreePool_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD("ExFreePool(%.8X)", base_address);

  kernel_state->memory()->SystemHeapFree(base_address);
}

SHIM_CALL KeLockL2_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  // Ignored for now. This is just a perf optimization, I think.
  // It may be useful as a hint for CPU-GPU transfer.

  XELOGD("KeLockL2(?)");

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL KeUnlockL2_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  XELOGD("KeUnlockL2(?)");
}

dword_result_t MmCreateKernelStack(dword_t stack_size, dword_t r4) {
  assert_zero(r4);  // Unknown argument.

  auto stack_size_aligned = (stack_size + 0xFFF) & 0xFFFFF000;
  uint32_t stack_alignment = (stack_size & 0xF000) ? 0x1000 : 0x10000;

  uint32_t stack_address;
  kernel_memory()
      ->LookupHeap(0x70000000)
      ->AllocRange(0x70000000, 0x7FFFFFFF, stack_size_aligned, stack_alignment,
                   kMemoryAllocationReserve | kMemoryAllocationCommit,
                   kMemoryProtectRead | kMemoryProtectWrite, false,
                   &stack_address);
  return stack_address + stack_size;
}
DECLARE_XBOXKRNL_EXPORT(MmCreateKernelStack, ExportTag::kImplemented);

dword_result_t MmDeleteKernelStack(lpvoid_t stack_base, lpvoid_t stack_end) {
  // Release the stack (where stack_end is the low address)
  if (kernel_memory()->LookupHeap(0x70000000)->Release(stack_end)) {
    return X_STATUS_SUCCESS;
  }

  return X_STATUS_UNSUCCESSFUL;
}
DECLARE_XBOXKRNL_EXPORT(MmDeleteKernelStack, ExportTag::kImplemented);

void RegisterMemoryExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", NtAllocateVirtualMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtFreeVirtualMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryVirtualMemory, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", MmAllocatePhysicalMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmAllocatePhysicalMemoryEx, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmFreePhysicalMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmQueryAddressProtect, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmQueryAllocationSize, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmQueryStatistics, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmGetPhysicalAddress, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmMapIoSpace, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", ExAllocatePoolTypeWithTag, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", ExFreePool, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeLockL2, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeUnlockL2, state);
}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

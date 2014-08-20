/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/math.h>
#include <xenia/common.h>
#include <xenia/core.h>
#include <xenia/xbox.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/util/shim_utils.h>

namespace xe {
namespace kernel {

SHIM_CALL NtAllocateVirtualMemory_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t base_addr_ptr = SHIM_GET_ARG_32(0);
  uint32_t base_addr_value = SHIM_MEM_32(base_addr_ptr);
  uint32_t region_size_ptr = SHIM_GET_ARG_32(1);
  uint32_t region_size_value = SHIM_MEM_32(region_size_ptr);
  uint32_t allocation_type = SHIM_GET_ARG_32(2);  // X_MEM_* bitmask
  uint32_t protect_bits = SHIM_GET_ARG_32(3);     // X_PAGE_* bitmask
  uint32_t unknown = SHIM_GET_ARG_32(4);

  XELOGD("NtAllocateVirtualMemory(%.8X(%.8X), %.8X(%.8X), %.8X, %.8X, %.8X)",
         base_addr_ptr, base_addr_value, region_size_ptr, region_size_value,
         allocation_type, protect_bits, unknown);

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
  if (!(allocation_type & (X_MEM_COMMIT | X_MEM_RESET | X_MEM_RESERVE))) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
    return;
  }
  // If MEM_RESET is set only MEM_RESET can be set.
  if (allocation_type & X_MEM_RESET && (allocation_type & ~X_MEM_RESET)) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
    return;
  }
  // Don't allow games to set execute bits.
  if (protect_bits & (X_PAGE_EXECUTE | X_PAGE_EXECUTE_READ |
                      X_PAGE_EXECUTE_READWRITE | X_PAGE_EXECUTE_WRITECOPY)) {
    SHIM_SET_RETURN_32(X_STATUS_ACCESS_DENIED);
    return;
  }

  // Adjust size.
  uint32_t adjusted_size = region_size_value;
  // TODO(benvanik): adjust based on page size flags/etc?

  // TODO(benvanik): support different allocation types.
  // Right now we treat everything as a commit and ignore allocations that have
  // already happened.
  if (base_addr_value) {
    // Having a pointer already means that this is likely a follow-on COMMIT.
    assert_true(!(allocation_type & X_MEM_RESERVE) &&
                (allocation_type & X_MEM_COMMIT));
    SHIM_SET_MEM_32(base_addr_ptr, base_addr_value);
    SHIM_SET_MEM_32(region_size_ptr, adjusted_size);
    SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
    return;
  }

  // Allocate.
  uint32_t flags = (allocation_type & X_MEM_NOZERO);
  uint32_t addr = (uint32_t)state->memory()->HeapAlloc(base_addr_value,
                                                       adjusted_size, flags);
  if (!addr) {
    // Failed - assume no memory available.
    SHIM_SET_RETURN_32(X_STATUS_NO_MEMORY);
    return;
  }

  // Stash back.
  // Maybe set X_STATUS_ALREADY_COMMITTED if MEM_COMMIT?
  SHIM_SET_MEM_32(base_addr_ptr, addr);
  SHIM_SET_MEM_32(region_size_ptr, adjusted_size);
  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL NtFreeVirtualMemory_shim(PPCContext* ppc_state, KernelState* state) {
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

  // TODO(benvanik): ignore decommits for now.
  if (free_type == X_MEM_DECOMMIT) {
    SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
    return;
  }

  // Free.
  uint32_t flags = 0;
  uint32_t freed_size = state->memory()->HeapFree(base_addr_value, flags);
  if (!freed_size) {
    SHIM_SET_RETURN_32(X_STATUS_UNSUCCESSFUL);
    return;
  }

  SHIM_SET_MEM_32(base_addr_ptr, base_addr_value);
  SHIM_SET_MEM_32(region_size_ptr, freed_size);
  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL NtQueryVirtualMemory_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);
  uint32_t memory_basic_information_ptr = SHIM_GET_ARG_32(1);
  X_MEMORY_BASIC_INFORMATION* memory_basic_information =
      (X_MEMORY_BASIC_INFORMATION*)SHIM_MEM_ADDR(memory_basic_information_ptr);

  XELOGD("NtQueryVirtualMemory(%.8X, %.8X)", base_address,
         memory_basic_information_ptr);

  alloy::AllocationInfo mem_info;
  size_t result = state->memory()->QueryInformation(base_address, &mem_info);
  if (!result) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
    return;
  }

  auto membase = state->memory()->membase();
  memory_basic_information->base_address =
      static_cast<uint32_t>(mem_info.base_address);
  memory_basic_information->allocation_base =
      static_cast<uint32_t>(mem_info.allocation_base);
  memory_basic_information->allocation_protect = mem_info.allocation_protect;
  memory_basic_information->region_size =
      static_cast<uint32_t>(mem_info.region_size);
  memory_basic_information->state = mem_info.state;
  memory_basic_information->protect = mem_info.protect;
  memory_basic_information->type = mem_info.type;

  // TODO(benvanik): auto swap structure.
  memory_basic_information->base_address =
      poly::byte_swap(memory_basic_information->base_address);
  memory_basic_information->allocation_base =
      poly::byte_swap(memory_basic_information->allocation_base);
  memory_basic_information->allocation_protect =
      poly::byte_swap(memory_basic_information->allocation_protect);
  memory_basic_information->region_size =
      poly::byte_swap(memory_basic_information->region_size);
  memory_basic_information->state =
      poly::byte_swap(memory_basic_information->state);
  memory_basic_information->protect =
      poly::byte_swap(memory_basic_information->protect);
  memory_basic_information->type =
      poly::byte_swap(memory_basic_information->type);

  XELOGE("NtQueryVirtualMemory NOT IMPLEMENTED");

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL MmAllocatePhysicalMemoryEx_shim(PPCContext* ppc_state,
                                          KernelState* state) {
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
  uint32_t adjusted_size = poly::round_up(region_size, page_size);
  uint32_t adjusted_alignment = poly::round_up(alignment, page_size);

  // Callers can pick an address to allocate with min_addr_range/max_addr_range
  // and the memory must be allocated there. I haven't seen a game do this,
  // and instead they all do min=0 / max=-1 to indicate the system should pick.
  // If we have to suport arbitrary placement things will get nasty.
  assert_true(min_addr_range == 0);
  assert_true(max_addr_range == 0xFFFFFFFF);

  // Allocate.
  uint32_t flags = alloy::MEMORY_FLAG_PHYSICAL;
  uint32_t base_address = (uint32_t)state->memory()->HeapAlloc(
      0, adjusted_size, flags, adjusted_alignment);
  if (!base_address) {
    // Failed - assume no memory available.
    SHIM_SET_RETURN_32(0);
    return;
  }

  // Move the address into the right range.
  // if (protect_bits & X_MEM_LARGE_PAGES) {
  //  base_address += 0xA0000000;
  //} else if (protect_bits & X_MEM_16MB_PAGES) {
  //  base_address += 0xC0000000;
  //} else {
  //  base_address += 0xE0000000;
  //}
  base_address += 0xA0000000;

  SHIM_SET_RETURN_32(base_address);
}

SHIM_CALL MmFreePhysicalMemory_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t type = SHIM_GET_ARG_32(0);
  uint32_t base_address = SHIM_GET_ARG_32(1);

  XELOGD("MmFreePhysicalAddress(%d, %.8X)", type, base_address);

  // base_address = result of MmAllocatePhysicalMemory.

  // Strip off physical bits before passing down.
  base_address &= ~0xE0000000;

  // TODO(benvanik): free memory.
  XELOGE("xeMmFreePhysicalMemory NOT IMPLEMENTED");
  // uint32_t size = ?;
  // xe_memory_heap_free(
  //    state->memory(), base_address, size);
}

SHIM_CALL MmQueryAddressProtect_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD("MmQueryAddressProtect(%.8X)", base_address);

  uint32_t access = state->memory()->QueryProtect(base_address);

  SHIM_SET_RETURN_32(access);
}

SHIM_CALL MmQueryAllocationSize_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD("MmQueryAllocationSize(%.8X)", base_address);

  size_t size = state->memory()->QuerySize(base_address);

  SHIM_SET_RETURN_32(static_cast<uint32_t>(size));
}

SHIM_CALL MmQueryStatistics_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t stats_ptr = SHIM_GET_ARG_32(0);

  XELOGD("MmQueryStatistics(%.8X)", stats_ptr);

  uint32_t size = SHIM_MEM_32(stats_ptr + 0);
  if (size != 104) {
    SHIM_SET_RETURN_32(X_STATUS_BUFFER_TOO_SMALL);
    return;
  }

  X_STATUS result = X_STATUS_SUCCESS;

  // Zero out the struct.
  xe_zero_struct(SHIM_MEM_ADDR(stats_ptr), 104);
  SHIM_SET_MEM_32(stats_ptr + 0, 104);

  // Set the constants the game is likely asking for.
  // These numbers are mostly guessed. If the game is just checking for
  // memory, this should satisfy it. If it's actually verifying things
  // this won't work :/
  // https://code.google.com/p/vdash/source/browse/trunk/vdash/include/kernel.h
  SHIM_SET_MEM_32(stats_ptr + 4 * 1, 0x00020000);  // TotalPhysicalPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 2, 0x00000300);  // KernelPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 3, 0x00020000);  // TitleAvailablePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 4,
                  0x2FFF0000);  // TitleTotalVirtualMemoryBytes
  SHIM_SET_MEM_32(stats_ptr + 4 * 5,
                  0x00160000);  // TitleReservedVirtualMemoryBytes
  SHIM_SET_MEM_32(stats_ptr + 4 * 6, 0x00001000);   // TitlePhysicalPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 7, 0x00000010);   // TitlePoolPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 8, 0x00000100);   // TitleStackPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 9, 0x00000100);   // TitleImagePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 10, 0x00000100);  // TitleHeapPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 11, 0x00000100);  // TitleVirtualPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 12, 0x00000100);  // TitlePageTablePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 13, 0x00000100);  // TitleCachePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 14, 0x00000000);  // SystemAvailablePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 15,
                  0x00000000);  // SystemTotalVirtualMemoryBytes
  SHIM_SET_MEM_32(stats_ptr + 4 * 16,
                  0x00000000);  // SystemReservedVirtualMemoryBytes
  SHIM_SET_MEM_32(stats_ptr + 4 * 17, 0x00000000);  // SystemPhysicalPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 18, 0x00000000);  // SystemPoolPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 19, 0x00000000);  // SystemStackPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 20, 0x00000000);  // SystemImagePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 21, 0x00000000);  // SystemHeapPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 22, 0x00000000);  // SystemVirtualPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 23, 0x00000000);  // SystemPageTablePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 24, 0x00000000);  // SystemCachePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 25, 0x0001FFFF);  // HighestPhysicalPage

  SHIM_SET_RETURN_32(result);
}

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff554547(v=vs.85).aspx
SHIM_CALL MmGetPhysicalAddress_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD("MmGetPhysicalAddress(%.8X)", base_address);

  // PHYSICAL_ADDRESS MmGetPhysicalAddress(
  //   _In_  PVOID BaseAddress
  // );
  // base_address = result of MmAllocatePhysicalMemory.

  // We are always using virtual addresses, right now, since we don't need
  // physical ones. We could munge up the address here to another mapped view
  // of memory.

  /*if (protect_bits & X_MEM_LARGE_PAGES) {
    base_address |= 0xA0000000;
  } else if (protect_bits & X_MEM_16MB_PAGES) {
    base_address |= 0xC0000000;
  } else {
    base_address |= 0xE0000000;
  }*/

  SHIM_SET_RETURN_32(base_address);
}

SHIM_CALL ExAllocatePoolTypeWithTag_shim(PPCContext* ppc_state,
                                         KernelState* state) {
  uint32_t size = SHIM_GET_ARG_32(0);
  uint32_t tag = SHIM_GET_ARG_32(1);
  uint32_t zero = SHIM_GET_ARG_32(2);

  XELOGD("ExAllocatePoolTypeWithTag(%d, %.8X, %d)", size, tag, zero);

  uint32_t alignment = 8;
  uint32_t adjusted_size = size;
  if (adjusted_size < 4 * 1024) {
    adjusted_size = poly::round_up(adjusted_size, 4 * 1024);
  } else {
    alignment = 4 * 1024;
  }

  uint32_t addr = (uint32_t)state->memory()->HeapAlloc(
      0, adjusted_size, alloy::MEMORY_FLAG_ZERO, alignment);

  SHIM_SET_RETURN_32(addr);
}

SHIM_CALL ExFreePool_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD("ExFreePool(%.8X)", base_address);

  state->memory()->HeapFree(base_address, 0);
}

SHIM_CALL KeLockL2_shim(PPCContext* ppc_state, KernelState* state) {
  // Ignored for now. This is just a perf optimization, I think.
  // It may be useful as a hint for CPU-GPU transfer.

  XELOGD("KeLockL2(?)");

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL KeUnlockL2_shim(PPCContext* ppc_state, KernelState* state) {
  XELOGD("KeUnlockL2(?)");
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterMemoryExports(
    ExportResolver* export_resolver, KernelState* state) {
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

  SHIM_SET_MAPPING("xboxkrnl.exe", ExAllocatePoolTypeWithTag, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", ExFreePool, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeLockL2, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeUnlockL2, state);
}

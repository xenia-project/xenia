/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl_memory.h>

#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace alloy;
using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {


X_STATUS xeNtAllocateVirtualMemory(
    uint32_t* base_addr_ptr, uint32_t* region_size_ptr,
    uint32_t allocation_type, uint32_t protect_bits,
    uint32_t unknown) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

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
  if (!*region_size_ptr) {
    return X_STATUS_INVALID_PARAMETER;
  }
  // Check allocation type.
  if (!(allocation_type & (X_MEM_COMMIT | X_MEM_RESET | X_MEM_RESERVE))) {
    return X_STATUS_INVALID_PARAMETER;
  }
  // If MEM_RESET is set only MEM_RESET can be set.
  if (allocation_type & X_MEM_RESET && (allocation_type & ~X_MEM_RESET)) {
    return X_STATUS_INVALID_PARAMETER;
  }
  // Don't allow games to set execute bits.
  if (protect_bits & (X_PAGE_EXECUTE | X_PAGE_EXECUTE_READ |
      X_PAGE_EXECUTE_READWRITE | X_PAGE_EXECUTE_WRITECOPY)) {
    return X_STATUS_ACCESS_DENIED;
  }

  // Adjust size.
  uint32_t adjusted_size = *region_size_ptr;
  // TODO(benvanik): adjust based on page size flags/etc?

  // TODO(benvanik): support different allocation types.
  // Right now we treat everything as a commit and ignore allocations that have
  // already happened.
  if (*base_addr_ptr) {
    // Having a pointer already means that this is likely a follow-on COMMIT.
    assert_true(!(allocation_type & X_MEM_RESERVE) &&
             (allocation_type & X_MEM_COMMIT));
    return X_STATUS_SUCCESS;
  }

  // Allocate.
  uint32_t flags = (allocation_type & X_MEM_NOZERO);
  uint32_t addr = (uint32_t)state->memory()->HeapAlloc(
      *base_addr_ptr, adjusted_size, flags);
  if (!addr) {
    // Failed - assume no memory available.
    return X_STATUS_NO_MEMORY;
  }

  // Stash back.
  // Maybe set X_STATUS_ALREADY_COMMITTED if MEM_COMMIT?
  *base_addr_ptr = addr;
  *region_size_ptr = adjusted_size;
  return X_STATUS_SUCCESS;
}


SHIM_CALL NtAllocateVirtualMemory_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t base_addr_ptr      = SHIM_GET_ARG_32(0);
  uint32_t base_addr_value    = SHIM_MEM_32(base_addr_ptr);
  uint32_t region_size_ptr    = SHIM_GET_ARG_32(1);
  uint32_t region_size_value  = SHIM_MEM_32(region_size_ptr);
  uint32_t allocation_type    = SHIM_GET_ARG_32(2); // X_MEM_* bitmask
  uint32_t protect_bits       = SHIM_GET_ARG_32(3); // X_PAGE_* bitmask
  uint32_t unknown            = SHIM_GET_ARG_32(4);

  XELOGD(
      "NtAllocateVirtualMemory(%.8X(%.8X), %.8X(%.8X), %.8X, %.8X, %.8X)",
      base_addr_ptr, base_addr_value,
      region_size_ptr, region_size_value,
      allocation_type, protect_bits, unknown);

  X_STATUS result = xeNtAllocateVirtualMemory(
      &base_addr_value, &region_size_value,
      allocation_type, protect_bits, unknown);

  if (XSUCCEEDED(result)) {
    SHIM_SET_MEM_32(base_addr_ptr, base_addr_value);
    SHIM_SET_MEM_32(region_size_ptr, region_size_value);
  }
  SHIM_SET_RETURN_32(result);
}


X_STATUS xeNtFreeVirtualMemory(
    uint32_t* base_addr_ptr, uint32_t* region_size_ptr,
    uint32_t free_type, uint32_t unknown) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // NTSTATUS
  // _Inout_  PVOID *BaseAddress,
  // _Inout_  PSIZE_T RegionSize,
  // _In_     ULONG FreeType
  // ? handle?

  // I've only seen zero.
  assert_true(unknown == 0);

  if (!*base_addr_ptr) {
    return X_STATUS_MEMORY_NOT_ALLOCATED;
  }

  // TODO(benvanik): ignore decommits for now.
  if (free_type == X_MEM_DECOMMIT) {
    return X_STATUS_SUCCESS;
  }

  // Free.
  uint32_t flags = 0;
  uint32_t freed_size = state->memory()->HeapFree(
      *base_addr_ptr, flags);
  if (!freed_size) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // Stash back.
  *region_size_ptr = freed_size;
  return X_STATUS_SUCCESS;
}


SHIM_CALL NtFreeVirtualMemory_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t base_addr_ptr      = SHIM_GET_ARG_32(0);
  uint32_t base_addr_value    = SHIM_MEM_32(base_addr_ptr);
  uint32_t region_size_ptr    = SHIM_GET_ARG_32(1);
  uint32_t region_size_value  = SHIM_MEM_32(region_size_ptr);
  // X_MEM_DECOMMIT | X_MEM_RELEASE
  uint32_t free_type          = SHIM_GET_ARG_32(2);
  uint32_t unknown            = SHIM_GET_ARG_32(3);

  XELOGD(
      "NtFreeVirtualMemory(%.8X(%.8X), %.8X(%.8X), %.8X, %.8X)",
      base_addr_ptr, base_addr_value,
      region_size_ptr, region_size_value,
      free_type, unknown);

  X_STATUS result = xeNtFreeVirtualMemory(
      &base_addr_value, &region_size_value,
      free_type, unknown);

  if (XSUCCEEDED(result)) {
    SHIM_SET_MEM_32(base_addr_ptr, base_addr_value);
    SHIM_SET_MEM_32(region_size_ptr, region_size_value);
  }
  SHIM_SET_RETURN_32(result);
}


X_STATUS xeNtQueryVirtualMemory(
    uint32_t base_address, X_MEMORY_BASIC_INFORMATION *memory_basic_information, bool swap) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  MEMORY_BASIC_INFORMATION mem_info;
  size_t result = state->memory()->QueryInformation(base_address, mem_info);

  if (!result) {
    return STATUS_INVALID_PARAMETER;
  }

  memory_basic_information->base_address        = (uint32_t) mem_info.BaseAddress;
  memory_basic_information->allocation_base     = (uint32_t) mem_info.AllocationBase;
  memory_basic_information->allocation_protect  = mem_info.AllocationProtect;
  memory_basic_information->region_size         = mem_info.RegionSize;
  memory_basic_information->state               = mem_info.State;
  memory_basic_information->protect             = mem_info.Protect;
  memory_basic_information->type                = mem_info.Type;

  if (swap) {
    memory_basic_information->base_address        = poly::byte_swap(memory_basic_information->base_address);
    memory_basic_information->allocation_base     = poly::byte_swap(memory_basic_information->allocation_base);
    memory_basic_information->allocation_protect  = poly::byte_swap(memory_basic_information->allocation_protect);
    memory_basic_information->region_size         = poly::byte_swap(memory_basic_information->region_size);
    memory_basic_information->state               = poly::byte_swap(memory_basic_information->state);
    memory_basic_information->protect             = poly::byte_swap(memory_basic_information->protect);
    memory_basic_information->type                = poly::byte_swap(memory_basic_information->type);
  }

  XELOGE("NtQueryVirtualMemory NOT IMPLEMENTED");

  return X_STATUS_SUCCESS;
}


SHIM_CALL NtQueryVirtualMemory_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);
  uint32_t memory_basic_information_ptr = SHIM_GET_ARG_32(1);
  X_MEMORY_BASIC_INFORMATION *memory_basic_information = (X_MEMORY_BASIC_INFORMATION*)SHIM_MEM_ADDR(memory_basic_information_ptr);

  XELOGD(
  	"NtQueryVirtualMemory(%.8X, %.8X)",
    base_address, memory_basic_information_ptr);

  X_STATUS result = xeNtQueryVirtualMemory(base_address, memory_basic_information, true);
  SHIM_SET_RETURN_32(result);
}


uint32_t xeMmAllocatePhysicalMemoryEx(
    uint32_t type, uint32_t region_size, uint32_t protect_bits,
    uint32_t min_addr_range, uint32_t max_addr_range, uint32_t alignment) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // Type will usually be 0 (user request?), where 1 and 2 are sometimes made
  // by D3D/etc.

  // Check protection bits.
  if (!(protect_bits & (X_PAGE_READONLY | X_PAGE_READWRITE))) {
    XELOGE("MmAllocatePhysicalMemoryEx: bad protection bits");
    return 0;
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
  uint32_t adjusted_size = XEROUNDUP(region_size, page_size);
  uint32_t adjusted_alignment = XEROUNDUP(alignment, page_size);

  // Callers can pick an address to allocate with min_addr_range/max_addr_range
  // and the memory must be allocated there. I haven't seen a game do this,
  // and instead they all do min=0 / max=-1 to indicate the system should pick.
  // If we have to suport arbitrary placement things will get nasty.
  assert_true(min_addr_range == 0);
  assert_true(max_addr_range == 0xFFFFFFFF);

  // Allocate.
  uint32_t flags = MEMORY_FLAG_PHYSICAL;
  uint32_t base_address = (uint32_t)state->memory()->HeapAlloc(
      0, adjusted_size, flags, adjusted_alignment);
  if (!base_address) {
    // Failed - assume no memory available.
    return 0;
  }

  // Move the address into the right range.
  //if (protect_bits & X_MEM_LARGE_PAGES) {
  //  base_address += 0xA0000000;
  //} else if (protect_bits & X_MEM_16MB_PAGES) {
  //  base_address += 0xC0000000;
  //} else {
  //  base_address += 0xE0000000;
  //}
  base_address += 0xA0000000;

  return base_address;
}


SHIM_CALL MmAllocatePhysicalMemoryEx_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t type = SHIM_GET_ARG_32(0);
  uint32_t region_size = SHIM_GET_ARG_32(1);
  uint32_t protect_bits = SHIM_GET_ARG_32(2);
  uint32_t min_addr_range = SHIM_GET_ARG_32(3);
  uint32_t max_addr_range = SHIM_GET_ARG_32(4);
  uint32_t alignment = SHIM_GET_ARG_32(5);

  XELOGD(
      "MmAllocatePhysicalMemoryEx(%d, %.8X, %.8X, %.8X, %.8X, %.8X)",
      type, region_size, protect_bits,
      min_addr_range, max_addr_range, alignment);

  uint32_t base_address = xeMmAllocatePhysicalMemoryEx(
      type, region_size, protect_bits,
      min_addr_range, max_addr_range, alignment);

  SHIM_SET_RETURN_32(base_address);
}


void xeMmFreePhysicalMemory(uint32_t type, uint32_t base_address) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // base_address = result of MmAllocatePhysicalMemory.

  // Strip off physical bits before passing down.
  base_address &= ~0xE0000000;

  // TODO(benvanik): free memory.
  XELOGE("xeMmFreePhysicalMemory NOT IMPLEMENTED");
  //uint32_t size = ?;
  //xe_memory_heap_free(
  //    state->memory(), base_address, size);
}


SHIM_CALL MmFreePhysicalMemory_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t type = SHIM_GET_ARG_32(0);
  uint32_t base_address = SHIM_GET_ARG_32(1);

  XELOGD(
      "MmFreePhysicalAddress(%d, %.8X)",
      type, base_address);

  xeMmFreePhysicalMemory(type, base_address);
}


uint32_t xeMmQueryAddressProtect(uint32_t base_address) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  uint32_t access = state->memory()->QueryProtect(base_address);

  return access;
}


SHIM_CALL MmQueryAddressProtect_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD(
      "MmQueryAddressProtect(%.8X)",
      base_address);

  uint32_t result = xeMmQueryAddressProtect(base_address);

  SHIM_SET_RETURN_32(result);
}


uint32_t xeMmQueryAllocationSize(uint32_t base_address) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  size_t size = state->memory()->QuerySize(base_address);

  return (uint32_t)size;
}


SHIM_CALL MmQueryAllocationSize_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD(
      "MmQueryAllocationSize(%.8X)",
      base_address);

  uint32_t result = xeMmQueryAllocationSize(base_address);

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL MmQueryStatistics_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t stats_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "MmQueryStatistics(%.8X)",
      stats_ptr);

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
  SHIM_SET_MEM_32(stats_ptr + 4 *  1, 0x00020000); // TotalPhysicalPages
  SHIM_SET_MEM_32(stats_ptr + 4 *  2, 0x00000300); // KernelPages
  SHIM_SET_MEM_32(stats_ptr + 4 *  3, 0x00020000); // TitleAvailablePages
  SHIM_SET_MEM_32(stats_ptr + 4 *  4, 0x2FFF0000); // TitleTotalVirtualMemoryBytes
  SHIM_SET_MEM_32(stats_ptr + 4 *  5, 0x00160000); // TitleReservedVirtualMemoryBytes
  SHIM_SET_MEM_32(stats_ptr + 4 *  6, 0x00001000); // TitlePhysicalPages
  SHIM_SET_MEM_32(stats_ptr + 4 *  7, 0x00000010); // TitlePoolPages
  SHIM_SET_MEM_32(stats_ptr + 4 *  8, 0x00000100); // TitleStackPages
  SHIM_SET_MEM_32(stats_ptr + 4 *  9, 0x00000100); // TitleImagePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 10, 0x00000100); // TitleHeapPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 11, 0x00000100); // TitleVirtualPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 12, 0x00000100); // TitlePageTablePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 13, 0x00000100); // TitleCachePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 14, 0x00000000); // SystemAvailablePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 15, 0x00000000); // SystemTotalVirtualMemoryBytes
  SHIM_SET_MEM_32(stats_ptr + 4 * 16, 0x00000000); // SystemReservedVirtualMemoryBytes
  SHIM_SET_MEM_32(stats_ptr + 4 * 17, 0x00000000); // SystemPhysicalPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 18, 0x00000000); // SystemPoolPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 19, 0x00000000); // SystemStackPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 20, 0x00000000); // SystemImagePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 21, 0x00000000); // SystemHeapPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 22, 0x00000000); // SystemVirtualPages
  SHIM_SET_MEM_32(stats_ptr + 4 * 23, 0x00000000); // SystemPageTablePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 24, 0x00000000); // SystemCachePages
  SHIM_SET_MEM_32(stats_ptr + 4 * 25, 0x0001FFFF); // HighestPhysicalPage

  SHIM_SET_RETURN_32(result);
}


// http://msdn.microsoft.com/en-us/library/windows/hardware/ff554547(v=vs.85).aspx
uint32_t xeMmGetPhysicalAddress(uint32_t base_address) {
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

  return base_address;
}


SHIM_CALL MmGetPhysicalAddress_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD(
      "MmGetPhysicalAddress(%.8X)",
      base_address);

  uint32_t result = xeMmGetPhysicalAddress(base_address);

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL ExAllocatePoolTypeWithTag_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t size = SHIM_GET_ARG_32(0);
  uint32_t tag = SHIM_GET_ARG_32(1);
  uint32_t zero = SHIM_GET_ARG_32(2);

  XELOGD(
      "ExAllocatePoolTypeWithTag(%d, %.8X, %d)",
      size, tag, zero);

  uint32_t alignment = 8;
  uint32_t adjusted_size = size;
  if (adjusted_size < 4 * 1024) {
    adjusted_size = XEROUNDUP(adjusted_size, 4 * 1024);
  } else {
    alignment = 4 * 1024;
  }

  uint32_t addr = (uint32_t)state->memory()->HeapAlloc(
      0, adjusted_size, MEMORY_FLAG_ZERO, alignment);

  SHIM_SET_RETURN_32(addr);
}



SHIM_CALL ExFreePool_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t base_address = SHIM_GET_ARG_32(0);

  XELOGD(
      "ExFreePool(%.8X)",
      base_address);

  state->memory()->HeapFree(base_address, 0);
}


SHIM_CALL KeLockL2_shim(
    PPCContext* ppc_state, KernelState* state) {
  // Ignored for now. This is just a perf optimization, I think.
  // It may be useful as a hint for CPU-GPU transfer.

  XELOGD(
      "KeLockL2(?)");

  SHIM_SET_RETURN_32(0);
}


SHIM_CALL KeUnlockL2_shim(
    PPCContext* ppc_state, KernelState* state) {
  XELOGD(
      "KeUnlockL2(?)");
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterMemoryExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", NtAllocateVirtualMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtFreeVirtualMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryVirtualMemory, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", MmAllocatePhysicalMemory, state);
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

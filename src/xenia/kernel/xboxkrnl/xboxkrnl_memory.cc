/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/xboxkrnl_memory.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xboxkrnl/kernel_state.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_private.h>


using namespace alloy;
using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xboxkrnl {


X_STATUS xeNtAllocateVirtualMemory(
    uint32_t* base_addr_ptr, uint32_t* region_size_ptr,
    uint32_t allocation_type, uint32_t protect_bits,
    uint32_t unknown) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // NTSTATUS
  // _Inout_  PVOID *BaseAddress,
  // _Inout_  PSIZE_T RegionSize,
  // _In_     ULONG AllocationType,
  // _In_     ULONG Protect
  // ? handle?

  // I've only seen zero.
  XEASSERT(unknown == 0);

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
  SHIM_SET_RETURN(result);
}


X_STATUS xeNtFreeVirtualMemory(
    uint32_t* base_addr_ptr, uint32_t* region_size_ptr,
    uint32_t free_type, uint32_t unknown) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // NTSTATUS
  // _Inout_  PVOID *BaseAddress,
  // _Inout_  PSIZE_T RegionSize,
  // _In_     ULONG FreeType
  // ? handle?

  // I've only seen zero.
  XEASSERT(unknown == 0);

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
  SHIM_SET_RETURN(result);
}


uint32_t xeMmAllocatePhysicalMemoryEx(
    uint32_t type, uint32_t region_size, uint32_t protect_bits,
    uint32_t min_addr_range, uint32_t max_addr_range, uint32_t alignment) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

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
  XEASSERT(min_addr_range == 0);
  XEASSERT(max_addr_range == 0xFFFFFFFF);

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

  SHIM_SET_RETURN(base_address);
}


void xeMmFreePhysicalMemory(uint32_t type, uint32_t base_address) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

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
  XEASSERTNOTNULL(state);

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

  SHIM_SET_RETURN(result);
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

  SHIM_SET_RETURN(result);
}


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterMemoryExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", NtAllocateVirtualMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtFreeVirtualMemory, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", MmAllocatePhysicalMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmAllocatePhysicalMemoryEx, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmFreePhysicalMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmQueryAddressProtect, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", MmGetPhysicalAddress, state);
}

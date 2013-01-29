/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/xboxkrnl_memory.h"

#include "kernel/shim_utils.h"
#include "kernel/modules/xboxkrnl/xboxkrnl.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {


void NtAllocateVirtualMemory_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // NTSTATUS
  // _Inout_  PVOID *BaseAddress,
  // _In_     ULONG_PTR ZeroBits,
  // _Inout_  PSIZE_T RegionSize,
  // _In_     ULONG AllocationType,
  // _In_     ULONG Protect
  // ? handle?

  uint32_t base_addr_ptr      = SHIM_GET_ARG_32(0);
  uint32_t base_addr_value    = SHIM_MEM_32(base_addr_ptr);
  uint32_t region_size_ptr    = SHIM_GET_ARG_32(1);
  uint32_t region_size_value  = SHIM_MEM_32(region_size_ptr);
  uint32_t allocation_type    = SHIM_GET_ARG_32(2); // X_MEM_* bitmask
  uint32_t protect_bits       = SHIM_GET_ARG_32(3); // X_PAGE_* bitmask
  uint32_t unknown            = SHIM_GET_ARG_32(4);

  // I've only seen zero.
  XEASSERT(unknown == 0);

  XELOGD(
      XT("NtAllocateVirtualMemory(%.8X(%.8X), %.8X(%.8X), %.8X, %.8X, %.8X)"),
      base_addr_ptr, base_addr_value,
      region_size_ptr, region_size_value,
      allocation_type, protect_bits, unknown);

  // This allocates memory from the kernel heap, which is initialized on startup
  // and shared by both the kernel implementation and user code.
  // The xe_memory_ref object is used to actually get the memory, and although
  // it's simple today we could extend it to do better things in the future.

  // Must request a size.
  if (!region_size_value) {
    SHIM_SET_RETURN(X_STATUS_INVALID_PARAMETER);
    return;
  }
  // Check allocation type.
  if (!(allocation_type & (X_MEM_COMMIT | X_MEM_RESET | X_MEM_RESERVE))) {
    SHIM_SET_RETURN(X_STATUS_INVALID_PARAMETER);
    return;
  }
  // If MEM_RESET is set only MEM_RESET can be set.
  if (allocation_type & X_MEM_RESET && (allocation_type & ~X_MEM_RESET)) {
    SHIM_SET_RETURN(X_STATUS_INVALID_PARAMETER);
    return;
  }
  // Don't allow games to set execute bits.
  if (protect_bits & (X_PAGE_EXECUTE | X_PAGE_EXECUTE_READ |
      X_PAGE_EXECUTE_READWRITE | X_PAGE_EXECUTE_WRITECOPY)) {
    SHIM_SET_RETURN(X_STATUS_ACCESS_DENIED);
    return;
  }

  // Adjust size.
  uint32_t adjusted_size = region_size_value;
  // TODO(benvanik): adjust based on page size flags/etc?

  // Allocate.
  uint32_t flags = 0;
  uint32_t addr = xe_memory_heap_alloc(
      state->memory, base_addr_value, adjusted_size, flags);
  if (!addr) {
    // Failed - assume no memory available.
    SHIM_SET_RETURN(X_STATUS_NO_MEMORY);
    return;
  }

  // Stash back.
  // Maybe set X_STATUS_ALREADY_COMMITTED if MEM_COMMIT?
  SHIM_SET_MEM_32(base_addr_ptr, addr);
  SHIM_SET_MEM_32(region_size_ptr, adjusted_size);
  SHIM_SET_RETURN(X_STATUS_SUCCESS);
}

void NtFreeVirtualMemory_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // NTSTATUS
  // _Inout_  PVOID *BaseAddress,
  // _Inout_  PSIZE_T RegionSize,
  // _In_     ULONG FreeType
  // ? handle?

  uint32_t base_addr_ptr      = SHIM_GET_ARG_32(0);
  uint32_t base_addr_value    = SHIM_MEM_32(base_addr_ptr);
  uint32_t region_size_ptr    = SHIM_GET_ARG_32(1);
  uint32_t region_size_value  = SHIM_MEM_32(region_size_ptr);
  // X_MEM_DECOMMIT | X_MEM_RELEASE
  uint32_t free_type          = SHIM_GET_ARG_32(2);
  uint32_t unknown            = SHIM_GET_ARG_32(3);

  // I've only seen zero.
  XEASSERT(unknown == 0);

  XELOGD(
      XT("NtFreeVirtualMemory(%.8X(%.8X), %.8X(%.8X), %.8X, %.8X)"),
      base_addr_ptr, base_addr_value,
      region_size_ptr, region_size_value,
      free_type, unknown);

  // Free.
  uint32_t flags = 0;
  uint32_t freed_size = xe_memory_heap_free(state->memory, base_addr_value,
                                            flags);
  if (!freed_size) {
    SHIM_SET_RETURN(X_STATUS_UNSUCCESSFUL);
    return;
  }

  // Stash back.
  SHIM_SET_MEM_32(base_addr_ptr, base_addr_value);
  SHIM_SET_MEM_32(region_size_ptr, freed_size);
  SHIM_SET_RETURN(X_STATUS_SUCCESS);
}


}


void xe::kernel::xboxkrnl::RegisterMemoryExports(
    ExportResolver* export_resolver, KernelState* state) {
  #define SHIM_SET_MAPPING(ordinal, shim, impl) \
    export_resolver->SetFunctionMapping("xboxkrnl.exe", ordinal, \
        state, (xe_kernel_export_shim_fn)shim, (xe_kernel_export_impl_fn)impl)

  SHIM_SET_MAPPING(0x000000CC, NtAllocateVirtualMemory_shim, NULL);
  SHIM_SET_MAPPING(0x000000DC, NtFreeVirtualMemory_shim, NULL);

  #undef SET_MAPPING
}

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
  // MEM_COMMIT | MEM_PHYSICAL | MEM_RESERVE | MEM_RESET | MEM_TOP_DOWN
  uint32_t allocation_type    = SHIM_GET_ARG_32(2);
  // PAGE_NOACCESS | PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE |
  // PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_GUARD | PAGE_NOCACHE |
  // PAGE_WRITECOMBINE
  uint32_t protect_bits       = SHIM_GET_ARG_32(3);
  uint32_t unknown            = SHIM_GET_ARG_32(4);

  XELOGD(
    XT("NtAllocateVirtualMemory(%.8X(%.8X), %.8X(%.8X), %.8X, %.8X, %.8X)"),
    base_addr_ptr, base_addr_value,
    region_size_ptr, region_size_value,
    allocation_type, protect_bits, unknown);

  // TODO(benvanik): alloc memory

  // Possible return codes:
  // STATUS_ACCESS_DENIED
  // STATUS_ALREADY_COMMITTED
  // STATUS_COMMITMENT_LIMIT
  // STATUS_CONFLICTING_ADDRESSES
  // STATUS_INSUFFICIENT_RESOURCES
  // STATUS_INVALID_HANDLE
  // STATUS_INVALID_PAGE_PROTECTION
  // STATUS_NO_MEMORY
  // STATUS_OBJECT_TYPE_MISMATCH
  // STATUS_PROCESS_IS_TERMINATING
  SHIM_SET_RETURN(0xC0000017);
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
  // MEM_DECOMMIT | MEM_RELEASE
  uint32_t free_type          = SHIM_GET_ARG_32(2);
  uint32_t unknown            = SHIM_GET_ARG_32(3);

  XELOGD(
    XT("NtFreeVirtualMemory(%.8X(%.8X), %.8X(%.8X), %.8X, %.8X)"),
    base_addr_ptr, base_addr_value,
    region_size_ptr, region_size_value,
    free_type, unknown);

  // TODO(benvanik): free memory

  // Possible return codes:
  // STATUS_ACCESS_DENIED
  // STATUS_INVALID_HANDLE
  // STATUS_OBJECT_TYPE_MISMATCH
  SHIM_SET_RETURN(0xFFFFFFFF);
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

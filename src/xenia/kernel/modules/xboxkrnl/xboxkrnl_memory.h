/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_MEMORY_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_MEMORY_H_

#include <xenia/kernel/modules/xboxkrnl/kernel_state.h>
#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_ordinals.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


X_STATUS xeNtAllocateVirtualMemory(
    uint32_t* base_addr_ptr, uint32_t* region_size_ptr,
    uint32_t allocation_type, uint32_t protect_bits,
    uint32_t unknown);


void RegisterMemoryExports(ExportResolver* export_resolver, KernelState* state);


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_MEMORY_H_

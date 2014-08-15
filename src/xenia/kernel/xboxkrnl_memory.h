/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_MEMORY_H_
#define XENIA_KERNEL_XBOXKRNL_MEMORY_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {


#pragma pack(push, 1)
typedef struct {
	uint32_t base_address;
	uint32_t allocation_base;
	uint32_t allocation_protect;
	uint32_t region_size;
	uint32_t state;
	uint32_t protect;
	uint32_t type;
}
X_MEMORY_BASIC_INFORMATION;
#pragma pack(pop)


X_STATUS xeNtAllocateVirtualMemory(
    uint32_t* base_addr_ptr, uint32_t* region_size_ptr,
    uint32_t allocation_type, uint32_t protect_bits,
    uint32_t unknown);
X_STATUS xeNtFreeVirtualMemory(
    uint32_t* base_addr_ptr, uint32_t* region_size_ptr,
    uint32_t free_type, uint32_t unknown);

uint32_t xeMmAllocatePhysicalMemoryEx(
    uint32_t type, uint32_t region_size, uint32_t protect_bits,
    uint32_t min_addr_range, uint32_t max_addr_range, uint32_t alignment);
void xeMmFreePhysicalMemory(uint32_t type, uint32_t base_address);
uint32_t xeMmQueryAddressProtect(uint32_t base_address);
uint32_t xeMmGetPhysicalAddress(uint32_t base_address);


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_MEMORY_H_

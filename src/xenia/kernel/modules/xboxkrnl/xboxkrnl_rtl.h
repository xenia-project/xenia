/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_RTL_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_RTL_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/xbox.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


uint32_t xeRtlCompareMemory(uint32_t source1_ptr, uint32_t source2_ptr,
                            uint32_t length);

uint32_t xeRtlCompareMemoryUlong(uint32_t source_ptr, uint32_t length,
                                 uint32_t pattern);

void xeRtlFillMemoryUlong(uint32_t destination_ptr, uint32_t length,
                          uint32_t pattern);


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_RTL_H_

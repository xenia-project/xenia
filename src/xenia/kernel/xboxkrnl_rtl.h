/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_RTL_H_
#define XENIA_KERNEL_XBOXKRNL_RTL_H_

#include <xenia/common.h>
#include <xenia/core.h>
#include <xenia/xbox.h>


namespace xe {
namespace kernel {


void xeRtlInitializeCriticalSection(uint32_t cs_ptr);
X_STATUS xeRtlInitializeCriticalSectionAndSpinCount(
    uint32_t cs_ptr, uint32_t spin_count);
void xeRtlEnterCriticalSection(uint32_t cs_ptr, uint32_t thread_id);
uint32_t xeRtlTryEnterCriticalSection(uint32_t cs_ptr, uint32_t thread_id);
void xeRtlLeaveCriticalSection(uint32_t cs_ptr);


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_RTL_H_

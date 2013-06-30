/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_THREADING_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_THREADING_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/xbox.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


X_STATUS xeExCreateThread(
    uint32_t* handle_ptr, uint32_t stack_size, uint32_t* thread_id_ptr,
    uint32_t xapi_thread_startup,
    uint32_t start_address, uint32_t start_context, uint32_t creation_flags);

uint32_t xeKeGetCurrentProcessType();

uint32_t xeKeTlsAlloc();
int KeTlsFree(uint32_t tls_index);
uint32_t xeKeTlsGetValue(uint32_t tls_index);
int xeKeTlsSetValue(uint32_t tls_index, uint32_t tls_value);

X_STATUS xeKeWaitForSingleObject(
    void* object_ptr, uint32_t wait_reason, uint32_t processor_mode,
    uint32_t alertable, uint64_t* opt_timeout);


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_THREADING_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_THREADING_H_
#define XENIA_KERNEL_XBOXKRNL_THREADING_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {


X_STATUS xeExCreateThread(
    uint32_t* handle_ptr, uint32_t stack_size, uint32_t* thread_id_ptr,
    uint32_t xapi_thread_startup,
    uint32_t start_address, uint32_t start_context, uint32_t creation_flags);

X_STATUS xeNtResumeThread(uint32_t handle, uint32_t* out_suspend_count);
X_STATUS xeNtSuspendThread(uint32_t handle, uint32_t* out_suspend_count);

uint32_t xeKeSetAffinityThread(void* thread_ptr, uint32_t affinity);

uint32_t xeKeGetCurrentProcessType();

uint64_t xeKeQueryPerformanceFrequency();
void xeKeQuerySystemTime(uint64_t* time_ptr);

X_STATUS xeKeDelayExecutionThread(
    uint32_t processor_mode, uint32_t alertable, uint64_t interval);

uint32_t xeKeTlsAlloc();
int KeTlsFree(uint32_t tls_index);
uint64_t xeKeTlsGetValue(uint32_t tls_index);
int xeKeTlsSetValue(uint32_t tls_index, uint64_t tls_value);

X_STATUS xeNtCreateEvent(uint32_t* handle_ptr, void* obj_attributes,
                         uint32_t event_type, uint32_t initial_state);
int32_t xeKeSetEvent(void* event_ptr, uint32_t increment, uint32_t wait);
int32_t xeKeResetEvent(void* event_ptr);

X_STATUS xeKeWaitForSingleObject(
    void* object_ptr, uint32_t wait_reason, uint32_t processor_mode,
    uint32_t alertable, uint64_t* opt_timeout);

uint32_t xeKfAcquireSpinLock(uint32_t* lock_ptr);
void xeKfReleaseSpinLock(uint32_t* lock_ptr, uint32_t old_irql);

void xeKeEnterCriticalRegion();
void xeKeLeaveCriticalRegion();


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_THREADING_H_

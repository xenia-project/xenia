/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XBOXKRNL_THREADING_H_
#define XENIA_KERNEL_XBOXKRNL_XBOXKRNL_THREADING_H_

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
struct X_KEVENT;

namespace xboxkrnl {

uint32_t xeNtSetEvent(uint32_t handle, xe::be<uint32_t>* previous_state_ptr);

uint32_t xeNtClearEvent(uint32_t handle);

uint32_t xeNtWaitForMultipleObjectsEx(uint32_t count, xe::be<uint32_t>* handles,
                                      uint32_t wait_type, uint32_t wait_mode,
                                      uint32_t alertable,
                                      uint64_t* timeout_ptr);

uint32_t xeKeWaitForSingleObject(void* object_ptr, uint32_t wait_reason,
                                 uint32_t processor_mode, uint32_t alertable,
                                 uint64_t* timeout_ptr);

uint32_t NtWaitForSingleObjectEx(uint32_t object_handle, uint32_t wait_mode,
                                 uint32_t alertable, uint64_t* timeout_ptr);

uint32_t xeKeSetEvent(X_KEVENT* event_ptr, uint32_t increment, uint32_t wait);

uint32_t KeDelayExecutionThread(uint32_t processor_mode, uint32_t alertable,
                                uint64_t* interval_ptr,
                                cpu::ppc::PPCContext* ctx);

uint32_t ExCreateThread(xe::be<uint32_t>* handle_ptr, uint32_t stack_size,
                        xe::be<uint32_t>* thread_id_ptr,
                        uint32_t xapi_thread_startup, uint32_t start_address,
                        uint32_t start_context, uint32_t creation_flags);

uint32_t ExTerminateThread(uint32_t exit_code);

uint32_t NtResumeThread(uint32_t handle, uint32_t* suspend_count_ptr);

uint32_t NtClose(uint32_t handle);
void xeKeInitializeApc(XAPC* apc, uint32_t thread_ptr, uint32_t kernel_routine,
                       uint32_t rundown_routine, uint32_t normal_routine,
                       uint32_t apc_mode, uint32_t normal_context);
uint32_t xeKeInsertQueueApc(XAPC* apc, uint32_t arg1, uint32_t arg2,
                            uint32_t priority_increment,
                            cpu::ppc::PPCContext* context);
uint32_t xeNtQueueApcThread(uint32_t thread_handle, uint32_t apc_routine,
                            uint32_t apc_routine_context, uint32_t arg1,
                            uint32_t arg2, cpu::ppc::PPCContext* context);
void xeKfLowerIrql(PPCContext* ctx, unsigned char new_irql);
unsigned char xeKfRaiseIrql(PPCContext* ctx, unsigned char new_irql);

void xeKeKfReleaseSpinLock(PPCContext* ctx, X_KSPINLOCK* lock, dword_t old_irql, bool change_irql=true);
uint32_t xeKeKfAcquireSpinLock(PPCContext* ctx, X_KSPINLOCK* lock, bool change_irql=true);

X_STATUS xeProcessUserApcs(PPCContext* ctx);

void xeRundownApcs(PPCContext* ctx);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XBOXKRNL_THREADING_H_

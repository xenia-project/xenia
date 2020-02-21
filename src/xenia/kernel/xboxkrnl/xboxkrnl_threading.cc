/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <vector>

#include "xenia/base/atomic.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/mutex.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xevent.h"
#include "xenia/kernel/xmutant.h"
#include "xenia/kernel/xsemaphore.h"
#include "xenia/kernel/xthread.h"
#include "xenia/kernel/xtimer.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

// r13 + 0x100: pointer to thread local state
// Thread local state:
//   0x058: kernel time
//   0x14C: thread id
//   0x150: if >0 then error states don't get set
//   0x160: last error

// GetCurrentThreadId:
// lwz       r11, 0x100(r13)
// lwz       r3, 0x14C(r11)

// RtlGetLastError:
// lwz r11, 0x150(r13)
// if (r11 == 0) {
//   lwz r11, 0x100(r13)
//   stw r3, 0x160(r11)
// }

// RtlSetLastError:
// lwz r11, 0x150(r13)
// if (r11 == 0) {
//   lwz r11, 0x100(r13)
//   stw r3, 0x160(r11)
// }

// RtlSetLastNTError:
// r3 = RtlNtStatusToDosError(r3)
// lwz r11, 0x150(r13)
// if (r11 == 0) {
//   lwz r11, 0x100(r13)
//   stw r3, 0x160(r11)
// }

template <typename T>
object_ref<T> LookupNamedObject(KernelState* kernel_state,
                                uint32_t obj_attributes_ptr) {
  // If the name exists and its type matches, we can return that (ref+1)
  // with a success of NAME_EXISTS.
  // If the name exists and its type doesn't match, we do NAME_COLLISION.
  // Otherwise, we add like normal.
  if (!obj_attributes_ptr) {
    return nullptr;
  }
  auto name = util::TranslateAnsiStringAddress(
      kernel_state->memory(),
      xe::load_and_swap<uint32_t>(
          kernel_state->memory()->TranslateVirtual(obj_attributes_ptr + 4)));
  if (!name.empty()) {
    X_HANDLE handle = X_INVALID_HANDLE_VALUE;
    X_RESULT result =
        kernel_state->object_table()->GetObjectByName(name, &handle);
    if (XSUCCEEDED(result)) {
      // Found something! It's been retained, so return.
      auto obj = kernel_state->object_table()->LookupObject<T>(handle);
      if (obj) {
        // The caller will do as it likes.
        obj->ReleaseHandle();
        return obj;
      }
    }
  }
  return nullptr;
}

dword_result_t ExCreateThread(lpdword_t handle_ptr, dword_t stack_size,
                              lpdword_t thread_id_ptr,
                              dword_t xapi_thread_startup,
                              lpvoid_t start_address, lpvoid_t start_context,
                              dword_t creation_flags) {
  // http://jafile.com/uploads/scoop/main.cpp.txt
  // DWORD
  // LPHANDLE Handle,
  // DWORD    StackSize,
  // LPDWORD  ThreadId,
  // LPVOID   XapiThreadStartup, ?? often 0
  // LPVOID   StartAddress,
  // LPVOID   StartContext,
  // DWORD    CreationFlags // 0x80?

  // Inherit default stack size
  uint32_t actual_stack_size = stack_size;

  if (actual_stack_size == 0) {
    actual_stack_size = kernel_state()->GetExecutableModule()->stack_size();
  }

  // Stack must be aligned to 16kb pages
  actual_stack_size =
      std::max((uint32_t)0x4000, ((actual_stack_size + 0xFFF) & 0xFFFFF000));

  auto thread = object_ref<XThread>(
      new XThread(kernel_state(), actual_stack_size, xapi_thread_startup,
                  start_address.guest_address(), start_context.guest_address(),
                  creation_flags, true));

  X_STATUS result = thread->Create();
  if (XFAILED(result)) {
    // Failed!
    XELOGE("Thread creation failed: %.8X", result);
    return result;
  }

  if (XSUCCEEDED(result)) {
    if (handle_ptr) {
      if (creation_flags & 0x80) {
        *handle_ptr = thread->guest_object();
      } else {
        *handle_ptr = thread->handle();
      }
    }
    if (thread_id_ptr) {
      *thread_id_ptr = thread->thread_id();
    }
  }
  return result;
}
DECLARE_XBOXKRNL_EXPORT1(ExCreateThread, kThreading, kImplemented);

dword_result_t ExTerminateThread(dword_t exit_code) {
  XThread* thread = XThread::GetCurrentThread();

  // NOTE: this kills us right now. We won't return from it.
  return thread->Exit(exit_code);
}
DECLARE_XBOXKRNL_EXPORT1(ExTerminateThread, kThreading, kImplemented);

dword_result_t NtResumeThread(dword_t handle, lpdword_t suspend_count_ptr) {
  X_RESULT result = X_STATUS_INVALID_HANDLE;
  uint32_t suspend_count = 0;

  auto thread = kernel_state()->object_table()->LookupObject<XThread>(handle);
  if (thread) {
    result = thread->Resume(&suspend_count);
  }
  if (suspend_count_ptr) {
    *suspend_count_ptr = suspend_count;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtResumeThread, kThreading, kImplemented);

dword_result_t KeResumeThread(lpvoid_t thread_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;
  auto thread = XObject::GetNativeObject<XThread>(kernel_state(), thread_ptr);
  if (thread) {
    result = thread->Resume();
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(KeResumeThread, kThreading, kImplemented);

dword_result_t NtSuspendThread(dword_t handle, lpdword_t suspend_count_ptr) {
  X_RESULT result = X_STATUS_SUCCESS;
  uint32_t suspend_count = 0;

  auto thread = kernel_state()->object_table()->LookupObject<XThread>(handle);
  if (thread) {
    result = thread->Suspend(&suspend_count);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  if (suspend_count_ptr) {
    *suspend_count_ptr = suspend_count;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtSuspendThread, kThreading, kImplemented);

void KeSetCurrentStackPointers(lpvoid_t stack_ptr,
                               pointer_t<X_KTHREAD> cur_thread,
                               lpvoid_t stack_alloc_base, lpvoid_t stack_base,
                               lpvoid_t stack_limit) {
  auto thread = XThread::GetCurrentThread();
  auto context = thread->thread_state()->context();
  context->r[1] = stack_ptr.guest_address();

  auto pcr =
      kernel_memory()->TranslateVirtual<X_KPCR*>((uint32_t)context->r[13]);
  pcr->stack_base_ptr = stack_base.guest_address();
  pcr->stack_end_ptr = stack_limit.guest_address();

  // TODO: Do we need to set the stack info on cur_thread?
}
DECLARE_XBOXKRNL_EXPORT1(KeSetCurrentStackPointers, kThreading, kImplemented);

dword_result_t KeSetAffinityThread(lpvoid_t thread_ptr, dword_t affinity) {
  auto thread = XObject::GetNativeObject<XThread>(kernel_state(), thread_ptr);
  if (thread) {
    thread->SetAffinity(affinity);
  }

  return (uint32_t)affinity;
}
DECLARE_XBOXKRNL_EXPORT1(KeSetAffinityThread, kThreading, kImplemented);

dword_result_t KeQueryBasePriorityThread(lpvoid_t thread_ptr) {
  int32_t priority = 0;

  auto thread = XObject::GetNativeObject<XThread>(kernel_state(), thread_ptr);
  if (thread) {
    priority = thread->QueryPriority();
  }

  return priority;
}
DECLARE_XBOXKRNL_EXPORT1(KeQueryBasePriorityThread, kThreading, kImplemented);

dword_result_t KeSetBasePriorityThread(lpvoid_t thread_ptr, dword_t increment) {
  int32_t prev_priority = 0;
  auto thread = XObject::GetNativeObject<XThread>(kernel_state(), thread_ptr);

  if (thread) {
    prev_priority = thread->QueryPriority();
    thread->SetPriority(increment);
  }

  return prev_priority;
}
DECLARE_XBOXKRNL_EXPORT1(KeSetBasePriorityThread, kThreading, kImplemented);

dword_result_t KeSetDisableBoostThread(lpvoid_t thread_ptr, dword_t disabled) {
  auto thread = XObject::GetNativeObject<XThread>(kernel_state(), thread_ptr);
  if (thread) {
    // Uhm?
  }

  return 0;
}
DECLARE_XBOXKRNL_EXPORT1(KeSetDisableBoostThread, kThreading, kImplemented);

dword_result_t KeGetCurrentProcessType() {
  return kernel_state()->process_type();
}
DECLARE_XBOXKRNL_EXPORT2(KeGetCurrentProcessType, kThreading, kImplemented,
                         kHighFrequency);

void KeSetCurrentProcessType(dword_t type) {
  // One of X_PROCTYPE_?

  assert_true(type <= 2);

  kernel_state()->set_process_type(type);
}
DECLARE_XBOXKRNL_EXPORT1(KeSetCurrentProcessType, kThreading, kImplemented);

dword_result_t KeQueryPerformanceFrequency() {
  uint64_t result = Clock::guest_tick_frequency();
  return static_cast<uint32_t>(result);
}
DECLARE_XBOXKRNL_EXPORT2(KeQueryPerformanceFrequency, kThreading, kImplemented,
                         kHighFrequency);

dword_result_t KeDelayExecutionThread(dword_t processor_mode, dword_t alertable,
                                      lpqword_t interval_ptr) {
  XThread* thread = XThread::GetCurrentThread();
  X_STATUS result = thread->Delay(processor_mode, alertable, *interval_ptr);

  return result;
}
DECLARE_XBOXKRNL_EXPORT3(KeDelayExecutionThread, kThreading, kImplemented,
                         kBlocking, kHighFrequency);

dword_result_t NtYieldExecution() {
  auto thread = XThread::GetCurrentThread();
  thread->Delay(0, 0, 0);
  return 0;
}
DECLARE_XBOXKRNL_EXPORT2(NtYieldExecution, kThreading, kImplemented,
                         kHighFrequency);

void KeQuerySystemTime(lpqword_t time_ptr) {
  uint64_t time = Clock::QueryGuestSystemTime();
  if (time_ptr) {
    *time_ptr = time;
  }
}
DECLARE_XBOXKRNL_EXPORT1(KeQuerySystemTime, kThreading, kImplemented);

// https://msdn.microsoft.com/en-us/library/ms686801
dword_result_t KeTlsAlloc() {
  uint32_t slot = kernel_state()->AllocateTLS();
  XThread::GetCurrentThread()->SetTLSValue(slot, 0);

  return slot;
}
DECLARE_XBOXKRNL_EXPORT1(KeTlsAlloc, kThreading, kImplemented);

// https://msdn.microsoft.com/en-us/library/ms686804
dword_result_t KeTlsFree(dword_t tls_index) {
  if (tls_index == X_TLS_OUT_OF_INDEXES) {
    return 0;
  }

  kernel_state()->FreeTLS(tls_index);
  return 1;
}
DECLARE_XBOXKRNL_EXPORT1(KeTlsFree, kThreading, kImplemented);

// https://msdn.microsoft.com/en-us/library/ms686812
dword_result_t KeTlsGetValue(dword_t tls_index) {
  // xboxkrnl doesn't actually have an error branch - it always succeeds, even
  // if it overflows the TLS.
  uint32_t value = 0;
  if (XThread::GetCurrentThread()->GetTLSValue(tls_index, &value)) {
    return value;
  }

  return 0;
}
DECLARE_XBOXKRNL_EXPORT2(KeTlsGetValue, kThreading, kImplemented,
                         kHighFrequency);

// https://msdn.microsoft.com/en-us/library/ms686818
dword_result_t KeTlsSetValue(dword_t tls_index, dword_t tls_value) {
  // xboxkrnl doesn't actually have an error branch - it always succeeds, even
  // if it overflows the TLS.
  if (XThread::GetCurrentThread()->SetTLSValue(tls_index, tls_value)) {
    return 1;
  }

  return 0;
}
DECLARE_XBOXKRNL_EXPORT1(KeTlsSetValue, kThreading, kImplemented);

void KeInitializeEvent(pointer_t<X_KEVENT> event_ptr, dword_t event_type,
                       dword_t initial_state) {
  event_ptr.Zero();
  event_ptr->header.type = event_type;
  event_ptr->header.signal_state = (uint32_t)initial_state;
  auto ev =
      XObject::GetNativeObject<XEvent>(kernel_state(), event_ptr, event_type);
  if (!ev) {
    assert_always();
    return;
  }
}
DECLARE_XBOXKRNL_EXPORT1(KeInitializeEvent, kThreading, kImplemented);

uint32_t xeKeSetEvent(X_KEVENT* event_ptr, uint32_t increment, uint32_t wait) {
  auto ev = XObject::GetNativeObject<XEvent>(kernel_state(), event_ptr);
  if (!ev) {
    assert_always();
    return 0;
  }

  return ev->Set(increment, !!wait);
}

dword_result_t KeSetEvent(pointer_t<X_KEVENT> event_ptr, dword_t increment,
                          dword_t wait) {
  return xeKeSetEvent(event_ptr, increment, wait);
}
DECLARE_XBOXKRNL_EXPORT2(KeSetEvent, kThreading, kImplemented, kHighFrequency);

dword_result_t KePulseEvent(pointer_t<X_KEVENT> event_ptr, dword_t increment,
                            dword_t wait) {
  auto ev = XObject::GetNativeObject<XEvent>(kernel_state(), event_ptr);
  if (!ev) {
    assert_always();
    return 0;
  }

  return ev->Pulse(increment, !!wait);
}
DECLARE_XBOXKRNL_EXPORT2(KePulseEvent, kThreading, kImplemented,
                         kHighFrequency);

dword_result_t KeResetEvent(pointer_t<X_KEVENT> event_ptr) {
  auto ev = XObject::GetNativeObject<XEvent>(kernel_state(), event_ptr);
  if (!ev) {
    assert_always();
    return 0;
  }

  return ev->Reset();
}
DECLARE_XBOXKRNL_EXPORT1(KeResetEvent, kThreading, kImplemented);

dword_result_t NtCreateEvent(lpdword_t handle_ptr,
                             pointer_t<X_OBJECT_ATTRIBUTES> obj_attributes_ptr,
                             dword_t event_type, dword_t initial_state) {
  // Check for an existing timer with the same name.
  auto existing_object =
      LookupNamedObject<XEvent>(kernel_state(), obj_attributes_ptr);
  if (existing_object) {
    if (existing_object->type() == XObject::kTypeEvent) {
      if (handle_ptr) {
        existing_object->RetainHandle();
        *handle_ptr = existing_object->handle();
      }
      return X_STATUS_SUCCESS;
    } else {
      return X_STATUS_INVALID_HANDLE;
    }
  }

  auto ev = object_ref<XEvent>(new XEvent(kernel_state()));
  ev->Initialize(!event_type, !!initial_state);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    ev->SetAttributes(obj_attributes_ptr);
  }

  if (handle_ptr) {
    *handle_ptr = ev->handle();
  }
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtCreateEvent, kThreading, kImplemented);

uint32_t xeNtSetEvent(uint32_t handle, xe::be<uint32_t>* previous_state_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto ev = kernel_state()->object_table()->LookupObject<XEvent>(handle);
  if (ev) {
    int32_t was_signalled = ev->Set(0, false);
    if (previous_state_ptr) {
      *previous_state_ptr = static_cast<uint32_t>(was_signalled);
    }
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}

dword_result_t NtSetEvent(dword_t handle, lpdword_t previous_state_ptr) {
  return xeNtSetEvent(handle, previous_state_ptr);
}
DECLARE_XBOXKRNL_EXPORT2(NtSetEvent, kThreading, kImplemented, kHighFrequency);

dword_result_t NtPulseEvent(dword_t handle, lpdword_t previous_state_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto ev = kernel_state()->object_table()->LookupObject<XEvent>(handle);
  if (ev) {
    int32_t was_signalled = ev->Pulse(0, false);
    if (previous_state_ptr) {
      *previous_state_ptr = static_cast<uint32_t>(was_signalled);
    }
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT2(NtPulseEvent, kThreading, kImplemented,
                         kHighFrequency);

uint32_t xeNtClearEvent(uint32_t handle) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto ev = kernel_state()->object_table()->LookupObject<XEvent>(handle);
  if (ev) {
    ev->Reset();
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}

dword_result_t NtClearEvent(dword_t handle) { return xeNtClearEvent(handle); }
DECLARE_XBOXKRNL_EXPORT2(NtClearEvent, kThreading, kImplemented,
                         kHighFrequency);

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff552150(v=vs.85).aspx
void KeInitializeSemaphore(pointer_t<X_KSEMAPHORE> semaphore_ptr, dword_t count,
                           dword_t limit) {
  semaphore_ptr->header.type = 5;  // SemaphoreObject
  semaphore_ptr->header.signal_state = (uint32_t)count;
  semaphore_ptr->limit = (uint32_t)limit;

  auto sem = XObject::GetNativeObject<XSemaphore>(kernel_state(), semaphore_ptr,
                                                  5 /* SemaphoreObject */);
  if (!sem) {
    assert_always();
    return;
  }
}
DECLARE_XBOXKRNL_EXPORT1(KeInitializeSemaphore, kThreading, kImplemented);

uint32_t xeKeReleaseSemaphore(X_KSEMAPHORE* semaphore_ptr, uint32_t increment,
                              uint32_t adjustment, uint32_t wait) {
  auto sem =
      XObject::GetNativeObject<XSemaphore>(kernel_state(), semaphore_ptr);
  if (!sem) {
    assert_always();
    return 0;
  }

  // TODO(benvanik): increment thread priority?
  // TODO(benvanik): wait?

  return sem->ReleaseSemaphore(adjustment);
}

dword_result_t KeReleaseSemaphore(pointer_t<X_KSEMAPHORE> semaphore_ptr,
                                  dword_t increment, dword_t adjustment,
                                  dword_t wait) {
  return xeKeReleaseSemaphore(semaphore_ptr, increment, adjustment, wait);
}
DECLARE_XBOXKRNL_EXPORT1(KeReleaseSemaphore, kThreading, kImplemented);

dword_result_t NtCreateSemaphore(lpdword_t handle_ptr,
                                 lpvoid_t obj_attributes_ptr, dword_t count,
                                 dword_t limit) {
  // Check for an existing timer with the same name.
  auto existing_object =
      LookupNamedObject<XSemaphore>(kernel_state(), obj_attributes_ptr);
  if (existing_object) {
    if (existing_object->type() == XObject::kTypeSemaphore) {
      if (handle_ptr) {
        existing_object->RetainHandle();
        *handle_ptr = existing_object->handle();
      }
      return X_STATUS_SUCCESS;
    } else {
      return X_STATUS_INVALID_HANDLE;
    }
  }

  auto sem = object_ref<XSemaphore>(new XSemaphore(kernel_state()));
  sem->Initialize((int32_t)count, (int32_t)limit);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    sem->SetAttributes(obj_attributes_ptr);
  }

  if (handle_ptr) {
    *handle_ptr = sem->handle();
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtCreateSemaphore, kThreading, kImplemented);

dword_result_t NtReleaseSemaphore(dword_t sem_handle, dword_t release_count,
                                  lpdword_t previous_count_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;
  int32_t previous_count = 0;

  auto sem =
      kernel_state()->object_table()->LookupObject<XSemaphore>(sem_handle);
  if (sem) {
    previous_count = sem->ReleaseSemaphore((int32_t)release_count);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }
  if (previous_count_ptr) {
    *previous_count_ptr = (uint32_t)previous_count;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtReleaseSemaphore, kThreading, kImplemented);

dword_result_t NtCreateMutant(lpdword_t handle_out,
                              pointer_t<X_OBJECT_ATTRIBUTES> obj_attributes,
                              dword_t initial_owner) {
  // Check for an existing timer with the same name.
  auto existing_object = LookupNamedObject<XMutant>(
      kernel_state(), obj_attributes.guest_address());
  if (existing_object) {
    if (existing_object->type() == XObject::kTypeMutant) {
      if (handle_out) {
        existing_object->RetainHandle();
        *handle_out = existing_object->handle();
      }
      return X_STATUS_SUCCESS;
    } else {
      return X_STATUS_INVALID_HANDLE;
    }
  }

  auto mutant = object_ref<XMutant>(new XMutant(kernel_state()));
  mutant->Initialize(initial_owner ? true : false);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes) {
    mutant->SetAttributes(obj_attributes);
  }

  if (handle_out) {
    *handle_out = mutant->handle();
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtCreateMutant, kThreading, kImplemented);

dword_result_t NtReleaseMutant(dword_t mutant_handle, dword_t unknown) {
  // This doesn't seem to be supported.
  // int32_t previous_count_ptr = SHIM_GET_ARG_32(2);

  // Whatever arg 1 is all games seem to set it to 0, so whether it's
  // abandon or wait we just say false. Which is good, cause they are
  // both ignored.
  assert_zero(unknown);
  uint32_t priority_increment = 0;
  bool abandon = false;
  bool wait = false;

  X_STATUS result = X_STATUS_SUCCESS;

  auto mutant =
      kernel_state()->object_table()->LookupObject<XMutant>(mutant_handle);
  if (mutant) {
    result = mutant->ReleaseMutant(priority_increment, abandon, wait);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtReleaseMutant, kThreading, kImplemented);

dword_result_t NtCreateTimer(lpdword_t handle_ptr, lpvoid_t obj_attributes_ptr,
                             dword_t timer_type) {
  // timer_type = NotificationTimer (0) or SynchronizationTimer (1)

  // Check for an existing timer with the same name.
  auto existing_object =
      LookupNamedObject<XTimer>(kernel_state(), obj_attributes_ptr);
  if (existing_object) {
    if (existing_object->type() == XObject::kTypeTimer) {
      if (handle_ptr) {
        existing_object->RetainHandle();
        *handle_ptr = existing_object->handle();
      }
      return X_STATUS_SUCCESS;
    } else {
      return X_STATUS_INVALID_HANDLE;
    }
  }

  auto timer = object_ref<XTimer>(new XTimer(kernel_state()));
  timer->Initialize(timer_type);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    timer->SetAttributes(obj_attributes_ptr);
  }

  if (handle_ptr) {
    *handle_ptr = timer->handle();
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtCreateTimer, kThreading, kImplemented);

dword_result_t NtSetTimerEx(dword_t timer_handle, lpqword_t due_time_ptr,
                            lpvoid_t routine_ptr /*PTIMERAPCROUTINE*/,
                            dword_t unk_one, lpvoid_t routine_arg,
                            dword_t resume, dword_t period_ms,
                            dword_t unk_zero) {
  assert_true(unk_one == 1);
  assert_true(unk_zero == 0);

  uint64_t due_time = *due_time_ptr;

  X_STATUS result = X_STATUS_SUCCESS;

  auto timer =
      kernel_state()->object_table()->LookupObject<XTimer>(timer_handle);
  if (timer) {
    result =
        timer->SetTimer(due_time, period_ms, routine_ptr.guest_address(),
                        routine_arg.guest_address(), resume ? true : false);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtSetTimerEx, kThreading, kImplemented);

dword_result_t NtCancelTimer(dword_t timer_handle,
                             lpdword_t current_state_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto timer =
      kernel_state()->object_table()->LookupObject<XTimer>(timer_handle);
  if (timer) {
    result = timer->Cancel();
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }
  if (current_state_ptr) {
    *current_state_ptr = 0;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtCancelTimer, kThreading, kImplemented);

uint32_t xeKeWaitForSingleObject(void* object_ptr, uint32_t wait_reason,
                                 uint32_t processor_mode, uint32_t alertable,
                                 uint64_t* timeout_ptr) {
  auto object = XObject::GetNativeObject<XObject>(kernel_state(), object_ptr);

  if (!object) {
    // The only kind-of failure code (though this should never happen)
    assert_always();
    return X_STATUS_ABANDONED_WAIT_0;
  }

  X_STATUS result =
      object->Wait(wait_reason, processor_mode, alertable, timeout_ptr);

  return result;
}

dword_result_t KeWaitForSingleObject(lpvoid_t object_ptr, dword_t wait_reason,
                                     dword_t processor_mode, dword_t alertable,
                                     lpqword_t timeout_ptr) {
  uint64_t timeout = timeout_ptr ? static_cast<uint64_t>(*timeout_ptr) : 0u;
  return xeKeWaitForSingleObject(object_ptr, wait_reason, processor_mode,
                                 alertable, timeout_ptr ? &timeout : nullptr);
}
DECLARE_XBOXKRNL_EXPORT3(KeWaitForSingleObject, kThreading, kImplemented,
                         kBlocking, kHighFrequency);

dword_result_t NtWaitForSingleObjectEx(dword_t object_handle, dword_t wait_mode,
                                       dword_t alertable,
                                       lpqword_t timeout_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto object =
      kernel_state()->object_table()->LookupObject<XObject>(object_handle);
  if (object) {
    uint64_t timeout = timeout_ptr ? static_cast<uint64_t>(*timeout_ptr) : 0u;
    result =
        object->Wait(3, wait_mode, alertable, timeout_ptr ? &timeout : nullptr);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT3(NtWaitForSingleObjectEx, kThreading, kImplemented,
                         kBlocking, kHighFrequency);

dword_result_t KeWaitForMultipleObjects(dword_t count, lpdword_t objects_ptr,
                                        dword_t wait_type, dword_t wait_reason,
                                        dword_t processor_mode,
                                        dword_t alertable,
                                        lpqword_t timeout_ptr,
                                        lpvoid_t wait_block_array_ptr) {
  assert_true(wait_type <= 1);

  std::vector<object_ref<XObject>> objects;
  for (uint32_t n = 0; n < count; n++) {
    auto object_ptr = kernel_memory()->TranslateVirtual(objects_ptr[n]);
    auto object_ref =
        XObject::GetNativeObject<XObject>(kernel_state(), object_ptr);
    if (!object_ref) {
      return X_STATUS_INVALID_PARAMETER;
    }

    objects.push_back(std::move(object_ref));
  }

  uint64_t timeout = timeout_ptr ? static_cast<uint64_t>(*timeout_ptr) : 0u;
  return XObject::WaitMultiple(uint32_t(objects.size()),
                               reinterpret_cast<XObject**>(objects.data()),
                               wait_type, wait_reason, processor_mode,
                               alertable, timeout_ptr ? &timeout : nullptr);
}
DECLARE_XBOXKRNL_EXPORT3(KeWaitForMultipleObjects, kThreading, kImplemented,
                         kBlocking, kHighFrequency);

uint32_t xeNtWaitForMultipleObjectsEx(uint32_t count, xe::be<uint32_t>* handles,
                                      uint32_t wait_type, uint32_t wait_mode,
                                      uint32_t alertable,
                                      uint64_t* timeout_ptr) {
  assert_true(wait_type <= 1);

  std::vector<object_ref<XObject>> objects;
  for (uint32_t n = 0; n < count; n++) {
    uint32_t object_handle = handles[n];
    auto object =
        kernel_state()->object_table()->LookupObject<XObject>(object_handle);
    if (!object) {
      return X_STATUS_INVALID_PARAMETER;
    }
    objects.push_back(std::move(object));
  }

  return XObject::WaitMultiple(count,
                               reinterpret_cast<XObject**>(objects.data()),
                               wait_type, 6, wait_mode, alertable, timeout_ptr);
}

dword_result_t NtWaitForMultipleObjectsEx(dword_t count, lpdword_t handles,
                                          dword_t wait_type, dword_t wait_mode,
                                          dword_t alertable,
                                          lpqword_t timeout_ptr) {
  uint64_t timeout = timeout_ptr ? static_cast<uint64_t>(*timeout_ptr) : 0u;
  return xeNtWaitForMultipleObjectsEx(count, handles, wait_type, wait_mode,
                                      alertable,
                                      timeout_ptr ? &timeout : nullptr);
}
DECLARE_XBOXKRNL_EXPORT3(NtWaitForMultipleObjectsEx, kThreading, kImplemented,
                         kBlocking, kHighFrequency);

dword_result_t NtSignalAndWaitForSingleObjectEx(dword_t signal_handle,
                                                dword_t wait_handle,
                                                dword_t alertable, dword_t r6,
                                                lpqword_t timeout_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto signal_object =
      kernel_state()->object_table()->LookupObject<XObject>(signal_handle);
  auto wait_object =
      kernel_state()->object_table()->LookupObject<XObject>(wait_handle);
  if (signal_object && wait_object) {
    uint64_t timeout = timeout_ptr ? static_cast<uint64_t>(*timeout_ptr) : 0u;
    result =
        XObject::SignalAndWait(signal_object.get(), wait_object.get(), 3, 1,
                               alertable, timeout_ptr ? &timeout : nullptr);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT3(NtSignalAndWaitForSingleObjectEx, kThreading,
                         kImplemented, kBlocking, kHighFrequency);

uint32_t xeKeKfAcquireSpinLock(uint32_t* lock) {
  // XELOGD(
  //     "KfAcquireSpinLock(%.8X)",
  //     lock_ptr);

  // Lock.
  while (!xe::atomic_cas(0, 1, lock)) {
    // Spin!
    // TODO(benvanik): error on deadlock?
    xe::threading::MaybeYield();
  }

  // Raise IRQL to DISPATCH.
  XThread* thread = XThread::GetCurrentThread();
  auto old_irql = thread->RaiseIrql(2);

  return old_irql;
}

dword_result_t KfAcquireSpinLock(lpdword_t lock_ptr) {
  auto lock = reinterpret_cast<uint32_t*>(lock_ptr.host_address());
  return xeKeKfAcquireSpinLock(lock);
}
DECLARE_XBOXKRNL_EXPORT3(KfAcquireSpinLock, kThreading, kImplemented, kBlocking,
                         kHighFrequency);

void xeKeKfReleaseSpinLock(uint32_t* lock, dword_t old_irql) {
  // Restore IRQL.
  XThread* thread = XThread::GetCurrentThread();
  thread->LowerIrql(old_irql);

  // Unlock.
  xe::atomic_dec(lock);
}

void KfReleaseSpinLock(lpdword_t lock_ptr, dword_t old_irql) {
  auto lock = reinterpret_cast<uint32_t*>(lock_ptr.host_address());
  xeKeKfReleaseSpinLock(lock, old_irql);
}
DECLARE_XBOXKRNL_EXPORT2(KfReleaseSpinLock, kThreading, kImplemented,
                         kHighFrequency);

void KeAcquireSpinLockAtRaisedIrql(lpdword_t lock_ptr) {
  // Lock.
  auto lock = reinterpret_cast<uint32_t*>(lock_ptr.host_address());
  while (!xe::atomic_cas(0, 1, lock)) {
    // Spin!
    // TODO(benvanik): error on deadlock?
  }
}
DECLARE_XBOXKRNL_EXPORT3(KeAcquireSpinLockAtRaisedIrql, kThreading,
                         kImplemented, kBlocking, kHighFrequency);

dword_result_t KeTryToAcquireSpinLockAtRaisedIrql(lpdword_t lock_ptr) {
  // Lock.
  auto lock = reinterpret_cast<uint32_t*>(lock_ptr.host_address());
  if (!xe::atomic_cas(0, 1, lock)) {
    return 0;
  }
  return 1;
}
DECLARE_XBOXKRNL_EXPORT4(KeTryToAcquireSpinLockAtRaisedIrql, kThreading,
                         kImplemented, kBlocking, kHighFrequency, kSketchy);

void KeReleaseSpinLockFromRaisedIrql(lpdword_t lock_ptr) {
  // Unlock.
  auto lock = reinterpret_cast<uint32_t*>(lock_ptr.host_address());
  xe::atomic_dec(lock);
}
DECLARE_XBOXKRNL_EXPORT2(KeReleaseSpinLockFromRaisedIrql, kThreading,
                         kImplemented, kHighFrequency);

void KeEnterCriticalRegion() { XThread::EnterCriticalRegion(); }
DECLARE_XBOXKRNL_EXPORT2(KeEnterCriticalRegion, kThreading, kImplemented,
                         kHighFrequency);

void KeLeaveCriticalRegion() {
  XThread::LeaveCriticalRegion();

  XThread::GetCurrentThread()->CheckApcs();
}
DECLARE_XBOXKRNL_EXPORT2(KeLeaveCriticalRegion, kThreading, kImplemented,
                         kHighFrequency);

dword_result_t KeRaiseIrqlToDpcLevel() {
  auto old_value = kernel_state()->processor()->RaiseIrql(cpu::Irql::DPC);
  return (uint32_t)old_value;
}
DECLARE_XBOXKRNL_EXPORT2(KeRaiseIrqlToDpcLevel, kThreading, kImplemented,
                         kHighFrequency);

void KfLowerIrql(dword_t old_value) {
  kernel_state()->processor()->LowerIrql(
      static_cast<cpu::Irql>((uint32_t)old_value));

  XThread::GetCurrentThread()->CheckApcs();
}
DECLARE_XBOXKRNL_EXPORT2(KfLowerIrql, kThreading, kImplemented, kHighFrequency);

void NtQueueApcThread(dword_t thread_handle, lpvoid_t apc_routine,
                      lpvoid_t arg1, lpvoid_t arg2, lpvoid_t arg3) {
  // Alloc APC object (from somewhere) and insert.

  assert_always("not implemented");
}
// DECLARE_XBOXKRNL_EXPORT1(NtQueueApcThread, kThreading, kStub);

void KeInitializeApc(pointer_t<XAPC> apc, lpvoid_t thread_ptr,
                     lpvoid_t kernel_routine, lpvoid_t rundown_routine,
                     lpvoid_t normal_routine, dword_t processor_mode,
                     lpvoid_t normal_context) {
  apc->Initialize();
  apc->processor_mode = processor_mode;
  apc->thread_ptr = thread_ptr.guest_address();
  apc->kernel_routine = kernel_routine.guest_address();
  apc->rundown_routine = rundown_routine.guest_address();
  apc->normal_routine = normal_routine.guest_address();
  apc->normal_context =
      normal_routine.guest_address() ? normal_context.guest_address() : 0;
}
DECLARE_XBOXKRNL_EXPORT1(KeInitializeApc, kThreading, kImplemented);

dword_result_t KeInsertQueueApc(pointer_t<XAPC> apc, lpvoid_t arg1,
                                lpvoid_t arg2, dword_t priority_increment) {
  auto thread = XObject::GetNativeObject<XThread>(
      kernel_state(),
      kernel_state()->memory()->TranslateVirtual(apc->thread_ptr));
  if (!thread) {
    return 0;
  }

  // Lock thread.
  thread->LockApc();

  // Fail if already inserted.
  if (apc->enqueued) {
    thread->UnlockApc(false);
    return 0;
  }

  // Prep APC.
  apc->arg1 = arg1.guest_address();
  apc->arg2 = arg2.guest_address();
  apc->enqueued = 1;

  auto apc_list = thread->apc_list();

  uint32_t list_entry_ptr = apc.guest_address() + 8;
  apc_list->Insert(list_entry_ptr);

  // Unlock thread.
  thread->UnlockApc(true);

  return 1;
}
DECLARE_XBOXKRNL_EXPORT1(KeInsertQueueApc, kThreading, kImplemented);

dword_result_t KeRemoveQueueApc(pointer_t<XAPC> apc) {
  bool result = false;

  auto thread = XObject::GetNativeObject<XThread>(
      kernel_state(),
      kernel_state()->memory()->TranslateVirtual(apc->thread_ptr));
  if (!thread) {
    return 0;
  }

  thread->LockApc();

  if (!apc->enqueued) {
    thread->UnlockApc(false);
    return 0;
  }

  auto apc_list = thread->apc_list();
  uint32_t list_entry_ptr = apc.guest_address() + 8;
  if (apc_list->IsQueued(list_entry_ptr)) {
    apc_list->Remove(list_entry_ptr);
    result = true;
  }

  thread->UnlockApc(true);

  return result ? 1 : 0;
}
DECLARE_XBOXKRNL_EXPORT1(KeRemoveQueueApc, kThreading, kImplemented);

dword_result_t KiApcNormalRoutineNop(dword_t unk0 /* output? */,
                                     dword_t unk1 /* 0x13 */) {
  return 0;
}
DECLARE_XBOXKRNL_EXPORT1(KiApcNormalRoutineNop, kThreading, kStub);

typedef struct {
  xe::be<uint32_t> unknown;
  xe::be<uint32_t> flink;
  xe::be<uint32_t> blink;
  xe::be<uint32_t> routine;
  xe::be<uint32_t> context;
  xe::be<uint32_t> arg1;
  xe::be<uint32_t> arg2;
} XDPC;

void KeInitializeDpc(pointer_t<XDPC> dpc, lpvoid_t routine, lpvoid_t context) {
  // KDPC (maybe) 0x18 bytes?
  uint32_t type = 19;  // DpcObject
  uint32_t importance = 0;
  uint32_t number = 0;  // ?
  dpc->unknown = (type << 24) | (importance << 16) | (number);
  dpc->flink = 0;
  dpc->blink = 0;
  dpc->routine = routine.guest_address();
  dpc->context = context.guest_address();
  dpc->arg1 = 0;
  dpc->arg2 = 0;
}
DECLARE_XBOXKRNL_EXPORT2(KeInitializeDpc, kThreading, kImplemented, kSketchy);

dword_result_t KeInsertQueueDpc(pointer_t<XDPC> dpc, dword_t arg1,
                                dword_t arg2) {
  assert_always("DPC does not dispatch yet; going to hang!");

  uint32_t list_entry_ptr = dpc.guest_address() + 4;

  // Lock dispatcher.
  auto global_lock = xe::global_critical_region::AcquireDirect();
  auto dpc_list = kernel_state()->dpc_list();

  // If already in a queue, abort.
  if (dpc_list->IsQueued(list_entry_ptr)) {
    return 0;
  }

  // Prep DPC.
  dpc->arg1 = (uint32_t)arg1;
  dpc->arg2 = (uint32_t)arg2;

  dpc_list->Insert(list_entry_ptr);

  return 1;
}
DECLARE_XBOXKRNL_EXPORT2(KeInsertQueueDpc, kThreading, kStub, kSketchy);

dword_result_t KeRemoveQueueDpc(pointer_t<XDPC> dpc) {
  bool result = false;

  uint32_t list_entry_ptr = dpc.guest_address() + 4;

  auto global_lock = xe::global_critical_region::AcquireDirect();
  auto dpc_list = kernel_state()->dpc_list();
  if (dpc_list->IsQueued(list_entry_ptr)) {
    dpc_list->Remove(list_entry_ptr);
    result = true;
  }

  return result ? 1 : 0;
}
DECLARE_XBOXKRNL_EXPORT1(KeRemoveQueueDpc, kThreading, kImplemented);

// https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/51e4dfcaacfdbd1a9692272931a436371492f72d/import/OpenXDK/include/xboxkrnl/xboxkrnl.h#L1372
struct X_ERWLOCK {
  be<int32_t> lock_count;              // 0x0
  be<uint32_t> writers_waiting_count;  // 0x4
  be<uint32_t> readers_waiting_count;  // 0x8
  be<uint32_t> readers_entry_count;    // 0xC
  X_KEVENT writer_event;               // 0x10
  X_KSEMAPHORE reader_semaphore;       // 0x20
  uint32_t spin_lock;                  // 0x34
};
static_assert_size(X_ERWLOCK, 0x38);

void ExInitializeReadWriteLock(pointer_t<X_ERWLOCK> lock_ptr) {
  lock_ptr->lock_count = -1;
  lock_ptr->writers_waiting_count = 0;
  lock_ptr->readers_waiting_count = 0;
  lock_ptr->readers_entry_count = 0;
  KeInitializeEvent(&lock_ptr->writer_event, 1, 0);
  KeInitializeSemaphore(&lock_ptr->reader_semaphore, 0, 0x7FFFFFFF);
  lock_ptr->spin_lock = 0;
}
DECLARE_XBOXKRNL_EXPORT1(ExInitializeReadWriteLock, kThreading, kImplemented);

void ExAcquireReadWriteLockExclusive(pointer_t<X_ERWLOCK> lock_ptr) {
  auto old_irql = xeKeKfAcquireSpinLock(&lock_ptr->spin_lock);
  lock_ptr->lock_count++;
  xeKeKfReleaseSpinLock(&lock_ptr->spin_lock, old_irql);

  if (!lock_ptr->lock_count) {
    return;
  }

  lock_ptr->writers_waiting_count++;
  xeKeWaitForSingleObject(&lock_ptr->writer_event, 0, 0, 0, nullptr);
}
DECLARE_XBOXKRNL_EXPORT3(ExAcquireReadWriteLockExclusive, kThreading,
                         kImplemented, kBlocking, kSketchy);

void ExReleaseReadWriteLock(pointer_t<X_ERWLOCK> lock_ptr) {
  auto old_irql = xeKeKfAcquireSpinLock(&lock_ptr->spin_lock);
  lock_ptr->lock_count--;

  if (lock_ptr->lock_count < 0) {
    xeKeKfReleaseSpinLock(&lock_ptr->spin_lock, old_irql);
    return;
  }

  if (!lock_ptr->readers_entry_count) {
    auto readers_waiting_count = lock_ptr->readers_waiting_count;
    if (readers_waiting_count) {
      lock_ptr->readers_waiting_count = 0;
      lock_ptr->readers_entry_count = readers_waiting_count;
      xeKeKfReleaseSpinLock(&lock_ptr->spin_lock, old_irql);
      xeKeReleaseSemaphore(&lock_ptr->reader_semaphore, 1,
                           readers_waiting_count, 0);
      return;
    }
  }

  auto count = lock_ptr->readers_entry_count--;
  xeKeKfReleaseSpinLock(&lock_ptr->spin_lock, old_irql);
  if (!count) {
    xeKeSetEvent(&lock_ptr->writer_event, 1, 0);
  }
}
DECLARE_XBOXKRNL_EXPORT2(ExReleaseReadWriteLock, kThreading, kImplemented,
                         kSketchy);

// NOTE: This function is very commonly inlined, and probably won't be called!
pointer_result_t InterlockedPushEntrySList(
    pointer_t<X_SLIST_HEADER> plist_ptr, pointer_t<X_SINGLE_LIST_ENTRY> entry) {
  assert_not_null(plist_ptr);
  assert_not_null(entry);

  alignas(8) X_SLIST_HEADER old_hdr = *plist_ptr;
  alignas(8) X_SLIST_HEADER new_hdr = {0};
  uint32_t old_head = 0;
  do {
    old_hdr = *plist_ptr;
    new_hdr.depth = old_hdr.depth + 1;
    new_hdr.sequence = old_hdr.sequence + 1;

    old_head = old_hdr.next.next;
    entry->next = old_hdr.next.next;
    new_hdr.next.next = entry.guest_address();
  } while (
      !xe::atomic_cas(*(uint64_t*)(&old_hdr), *(uint64_t*)(&new_hdr),
                      reinterpret_cast<uint64_t*>(plist_ptr.host_address())));

  return old_head;
}
DECLARE_XBOXKRNL_EXPORT2(InterlockedPushEntrySList, kThreading, kImplemented,
                         kHighFrequency);

pointer_result_t InterlockedPopEntrySList(pointer_t<X_SLIST_HEADER> plist_ptr) {
  assert_not_null(plist_ptr);

  uint32_t popped = 0;
  alignas(8) X_SLIST_HEADER old_hdr = {0};
  alignas(8) X_SLIST_HEADER new_hdr = {0};
  do {
    old_hdr = *plist_ptr;
    auto next = kernel_memory()->TranslateVirtual<X_SINGLE_LIST_ENTRY*>(
        old_hdr.next.next);
    if (!old_hdr.next.next) {
      return 0;
    }
    popped = old_hdr.next.next;

    new_hdr.depth = old_hdr.depth - 1;
    new_hdr.next.next = next->next;
    new_hdr.sequence = old_hdr.sequence;
  } while (
      !xe::atomic_cas(*(uint64_t*)(&old_hdr), *(uint64_t*)(&new_hdr),
                      reinterpret_cast<uint64_t*>(plist_ptr.host_address())));

  return popped;
}
DECLARE_XBOXKRNL_EXPORT2(InterlockedPopEntrySList, kThreading, kImplemented,
                         kHighFrequency);

pointer_result_t InterlockedFlushSList(pointer_t<X_SLIST_HEADER> plist_ptr) {
  assert_not_null(plist_ptr);

  alignas(8) X_SLIST_HEADER old_hdr = *plist_ptr;
  alignas(8) X_SLIST_HEADER new_hdr = {0};
  uint32_t first = 0;
  do {
    old_hdr = *plist_ptr;
    first = old_hdr.next.next;
    new_hdr.next.next = 0;
    new_hdr.depth = 0;
    new_hdr.sequence = 0;
  } while (
      !xe::atomic_cas(*(uint64_t*)(&old_hdr), *(uint64_t*)(&new_hdr),
                      reinterpret_cast<uint64_t*>(plist_ptr.host_address())));

  return first;
}
DECLARE_XBOXKRNL_EXPORT1(InterlockedFlushSList, kThreading, kImplemented);

void RegisterThreadingExports(xe::cpu::ExportResolver* export_resolver,
                              KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

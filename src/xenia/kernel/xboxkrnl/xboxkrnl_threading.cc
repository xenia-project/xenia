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
  auto name = X_ANSI_STRING::to_string_indirect(
      kernel_state->memory()->virtual_membase(), obj_attributes_ptr + 4);
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

SHIM_CALL ExCreateThread_shim(PPCContext* ppc_context,
                              KernelState* kernel_state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t stack_size = SHIM_GET_ARG_32(1);
  uint32_t thread_id_ptr = SHIM_GET_ARG_32(2);
  uint32_t xapi_thread_startup = SHIM_GET_ARG_32(3);
  uint32_t start_address = SHIM_GET_ARG_32(4);
  uint32_t start_context = SHIM_GET_ARG_32(5);
  uint32_t creation_flags = SHIM_GET_ARG_32(6);

  XELOGD("ExCreateThread(%.8X, %d, %.8X, %.8X, %.8X, %.8X, %.8X)", handle_ptr,
         stack_size, thread_id_ptr, xapi_thread_startup, start_address,
         start_context, creation_flags);

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
  if (stack_size == 0) {
    stack_size = kernel_state->GetExecutableModule()->stack_size();
  }

  // Stack must be aligned to 16kb pages
  stack_size = std::max((uint32_t)0x4000, ((stack_size + 0xFFF) & 0xFFFFF000));

  auto thread = object_ref<XThread>(
      new XThread(kernel_state, stack_size, xapi_thread_startup, start_address,
                  start_context, creation_flags, true));

  X_STATUS result = thread->Create();
  if (XFAILED(result)) {
    // Failed!
    XELOGE("Thread creation failed: %.8X", result);
    SHIM_SET_RETURN_32(result);
    return;
  }

  if (XSUCCEEDED(result)) {
    if (handle_ptr) {
      if (creation_flags & 0x80) {
        SHIM_SET_MEM_32(handle_ptr, thread->guest_object());
      } else {
        SHIM_SET_MEM_32(handle_ptr, thread->handle());
      }
    }
    if (thread_id_ptr) {
      SHIM_SET_MEM_32(thread_id_ptr, thread->thread_id());
    }
  }
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL ExTerminateThread_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t exit_code = SHIM_GET_ARG_32(0);

  XELOGD("ExTerminateThread(%d)", exit_code);

  XThread* thread = XThread::GetCurrentThread();

  // NOTE: this kills us right now. We won't return from it.
  X_STATUS result = thread->Exit(exit_code);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtResumeThread_shim(PPCContext* ppc_context,
                              KernelState* kernel_state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t suspend_count_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NtResumeThread(%.8X, %.8X)", handle, suspend_count_ptr);

  X_RESULT result = X_STATUS_INVALID_HANDLE;
  uint32_t suspend_count = 0;

  auto thread = kernel_state->object_table()->LookupObject<XThread>(handle);
  if (thread) {
    result = thread->Resume(&suspend_count);
  }
  if (suspend_count_ptr) {
    SHIM_SET_MEM_32(suspend_count_ptr, suspend_count);
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL KeResumeThread_shim(PPCContext* ppc_context,
                              KernelState* kernel_state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);

  XELOGD("KeResumeThread(%.8X)", thread_ptr);

  X_STATUS result = X_STATUS_SUCCESS;
  auto thread = XObject::GetNativeObject<XThread>(kernel_state,
                                                  SHIM_MEM_ADDR(thread_ptr));
  if (thread) {
    result = thread->Resume();
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtSuspendThread_shim(PPCContext* ppc_context,
                               KernelState* kernel_state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t suspend_count_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NtSuspendThread(%.8X, %.8X)", handle, suspend_count_ptr);

  X_RESULT result = X_STATUS_SUCCESS;
  uint32_t suspend_count = 0;

  auto thread = kernel_state->object_table()->LookupObject<XThread>(handle);
  if (thread) {
    result = thread->Suspend(&suspend_count);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  if (suspend_count_ptr) {
    SHIM_SET_MEM_32(suspend_count_ptr, suspend_count);
  }

  SHIM_SET_RETURN_32(result);
}

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
DECLARE_XBOXKRNL_EXPORT(KeSetCurrentStackPointers,
                        ExportTag::kThreading | ExportTag::kImplemented);

SHIM_CALL KeSetAffinityThread_shim(PPCContext* ppc_context,
                                   KernelState* kernel_state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);
  uint32_t affinity = SHIM_GET_ARG_32(1);

  XELOGD("KeSetAffinityThread(%.8X, %.8X)", thread_ptr, affinity);

  auto thread = XObject::GetNativeObject<XThread>(kernel_state,
                                                  SHIM_MEM_ADDR(thread_ptr));
  if (thread) {
    thread->SetAffinity(affinity);
  }

  SHIM_SET_RETURN_32(affinity);
}

SHIM_CALL KeQueryBasePriorityThread_shim(PPCContext* ppc_context,
                                         KernelState* kernel_state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);

  XELOGD("KeQueryBasePriorityThread(%.8X)", thread_ptr);

  int32_t priority = 0;

  auto thread = XObject::GetNativeObject<XThread>(kernel_state,
                                                  SHIM_MEM_ADDR(thread_ptr));
  if (thread) {
    priority = thread->QueryPriority();
  }

  SHIM_SET_RETURN_32(priority);
}

SHIM_CALL KeSetBasePriorityThread_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);
  uint32_t increment = SHIM_GET_ARG_32(1);

  XELOGD("KeSetBasePriorityThread(%.8X, %.8X)", thread_ptr, increment);

  int32_t prev_priority = 0;
  auto thread = XObject::GetNativeObject<XThread>(kernel_state,
                                                  SHIM_MEM_ADDR(thread_ptr));

  if (thread) {
    prev_priority = thread->QueryPriority();
    thread->SetPriority(increment);
  }

  SHIM_SET_RETURN_32(prev_priority);
}

SHIM_CALL KeSetDisableBoostThread_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);
  uint32_t disabled = SHIM_GET_ARG_32(1);

  XELOGD("KeSetDisableBoostThread(%.8X, %.8X)", thread_ptr, disabled);

  auto thread = XObject::GetNativeObject<XThread>(kernel_state,
                                                  SHIM_MEM_ADDR(thread_ptr));
  if (thread) {
    // Uhm?
  }

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL KeGetCurrentProcessType_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  // XELOGD(
  //     "KeGetCurrentProcessType()");

  // DWORD

  SHIM_SET_RETURN_32(kernel_state->process_type());
}

SHIM_CALL KeSetCurrentProcessType_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  uint32_t type = SHIM_GET_ARG_32(0);
  // One of X_PROCTYPE_?

  XELOGD("KeSetCurrentProcessType(%d)", type);

  assert_true(type <= 2);

  kernel_state->set_process_type(type);
}

SHIM_CALL KeQueryPerformanceFrequency_shim(PPCContext* ppc_context,
                                           KernelState* kernel_state) {
  // XELOGD(
  //     "KeQueryPerformanceFrequency()");

  uint64_t result = Clock::guest_tick_frequency();
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL KeDelayExecutionThread_shim(PPCContext* ppc_context,
                                      KernelState* kernel_state) {
  uint32_t processor_mode = SHIM_GET_ARG_32(0);
  uint32_t alertable = SHIM_GET_ARG_32(1);
  uint32_t interval_ptr = SHIM_GET_ARG_32(2);
  uint64_t interval = SHIM_MEM_64(interval_ptr);

  // XELOGD(
  //     "KeDelayExecutionThread(%.8X, %d, %.8X(%.16llX)",
  //     processor_mode, alertable, interval_ptr, interval);

  XThread* thread = XThread::GetCurrentThread();
  X_STATUS result = thread->Delay(processor_mode, alertable, interval);

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtYieldExecution_shim(PPCContext* ppc_context,
                                KernelState* kernel_state) {
  // XELOGD("NtYieldExecution()");
  auto thread = XThread::GetCurrentThread();
  thread->Delay(0, 0, 0);
  SHIM_SET_RETURN_32(0);
}

SHIM_CALL KeQuerySystemTime_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t time_ptr = SHIM_GET_ARG_32(0);

  XELOGD("KeQuerySystemTime(%.8X)", time_ptr);

  uint64_t time = Clock::QueryGuestSystemTime();
  if (time_ptr) {
    SHIM_SET_MEM_64(time_ptr, time);
  }
}

// The TLS system used here is a bit hacky, but seems to work.
// Both Win32 and pthreads use unsigned longs as TLS indices, so we can map
// right into the system for these calls. We're just round tripping the IDs and
// hoping for the best.

// http://msdn.microsoft.com/en-us/library/ms686801
SHIM_CALL KeTlsAlloc_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  XELOGD("KeTlsAlloc()");

  auto tls_index = xe::threading::AllocateTlsHandle();
  if (tls_index == xe::threading::kInvalidTlsHandle) {
    tls_index = X_TLS_OUT_OF_INDEXES;
  }

  SHIM_SET_RETURN_32(tls_index);
}

// http://msdn.microsoft.com/en-us/library/ms686804
SHIM_CALL KeTlsFree_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);

  XELOGD("KeTlsFree(%.8X)", tls_index);

  if (tls_index == X_TLS_OUT_OF_INDEXES) {
    SHIM_SET_RETURN_32(0);
    return;
  }

  uint32_t result = xe::threading::FreeTlsHandle(tls_index) ? 1 : 0;
  SHIM_SET_RETURN_32(result);
}

// http://msdn.microsoft.com/en-us/library/ms686812
SHIM_CALL KeTlsGetValue_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);

  // Logging disabled, as some games spam this.
  // XELOGD(
  //    "KeTlsGetValue(%.8X)",
  //    tls_index);

  uint32_t value = static_cast<uint32_t>(xe::threading::GetTlsValue(tls_index));
  if (!value) {
    // XELOGW("KeTlsGetValue should SetLastError if result is NULL");
    // TODO(benvanik): SetLastError? Or does user code do this?
  }

  SHIM_SET_RETURN_32(value);
}

// http://msdn.microsoft.com/en-us/library/ms686818
SHIM_CALL KeTlsSetValue_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);
  uint32_t tls_value = SHIM_GET_ARG_32(1);

  XELOGD("KeTlsSetValue(%.8X, %.8X)", tls_index, tls_value);

  uint32_t result = xe::threading::SetTlsValue(tls_index, tls_value) ? 1 : 0;
  SHIM_SET_RETURN_32(result);
}

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
DECLARE_XBOXKRNL_EXPORT(KeInitializeEvent,
                        ExportTag::kImplemented | ExportTag::kThreading);

dword_result_t KeSetEvent(pointer_t<X_KEVENT> event_ptr, dword_t increment,
                          dword_t wait) {
  // Update dispatch header.
  xe::atomic_exchange(
      xe::byte_swap<uint32_t>(1),
      reinterpret_cast<uint32_t*>(&event_ptr->header.signal_state));

  auto ev = XObject::GetNativeObject<XEvent>(kernel_state(), event_ptr);
  if (!ev) {
    assert_always();
    return 0;
  }

  return ev->Set(increment, !!wait);
}
DECLARE_XBOXKRNL_EXPORT(KeSetEvent,
                        ExportTag::kImplemented | ExportTag::kThreading);

dword_result_t KePulseEvent(pointer_t<X_KEVENT> event_ptr, dword_t increment,
                            dword_t wait) {
  auto ev = XObject::GetNativeObject<XEvent>(kernel_state(), event_ptr);
  if (!ev) {
    assert_always();
    return 0;
  }

  return ev->Pulse(increment, !!wait);
}
DECLARE_XBOXKRNL_EXPORT(KePulseEvent, ExportTag::kImplemented);

dword_result_t KeResetEvent(pointer_t<X_KEVENT> event_ptr) {
  // Update dispatch header.
  xe::atomic_exchange(
      0, reinterpret_cast<uint32_t*>(&event_ptr->header.signal_state));

  auto ev = XObject::GetNativeObject<XEvent>(kernel_state(), event_ptr);
  if (!ev) {
    assert_always();
    return 0;
  }

  return ev->Reset();
}
DECLARE_XBOXKRNL_EXPORT(KeResetEvent,
                        ExportTag::kImplemented | ExportTag::kThreading);

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

  XEvent* ev = new XEvent(kernel_state());
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
DECLARE_XBOXKRNL_EXPORT(NtCreateEvent,
                        ExportTag::kImplemented | ExportTag::kThreading);

dword_result_t NtSetEvent(dword_t handle, lpdword_t previous_state_ptr) {
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
DECLARE_XBOXKRNL_EXPORT(NtSetEvent,
                        ExportTag::kImplemented | ExportTag::kThreading);

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
DECLARE_XBOXKRNL_EXPORT(NtPulseEvent,
                        ExportTag::kImplemented | ExportTag::kThreading);

dword_result_t NtClearEvent(dword_t handle) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto ev = kernel_state()->object_table()->LookupObject<XEvent>(handle);
  if (ev) {
    ev->Reset();
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT(NtClearEvent,
                        ExportTag::kImplemented | ExportTag::kThreading);

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
DECLARE_XBOXKRNL_EXPORT(KeInitializeSemaphore,
                        ExportTag::kImplemented | ExportTag::kThreading);

dword_result_t KeReleaseSemaphore(pointer_t<X_KSEMAPHORE> semaphore_ptr,
                                  dword_t increment, dword_t adjustment,
                                  dword_t wait) {
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
DECLARE_XBOXKRNL_EXPORT(KeReleaseSemaphore,
                        ExportTag::kImplemented | ExportTag::kThreading);

SHIM_CALL NtCreateSemaphore_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(1);
  int32_t count = SHIM_GET_ARG_32(2);
  int32_t limit = SHIM_GET_ARG_32(3);

  XELOGD("NtCreateSemaphore(%.8X, %.8X, %d, %d)", handle_ptr,
         obj_attributes_ptr, count, limit);

  // Check for an existing timer with the same name.
  auto existing_object =
      LookupNamedObject<XSemaphore>(kernel_state, obj_attributes_ptr);
  if (existing_object) {
    if (existing_object->type() == XObject::kTypeSemaphore) {
      if (handle_ptr) {
        existing_object->RetainHandle();
        SHIM_SET_MEM_32(handle_ptr, existing_object->handle());
      }
      SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
    } else {
      SHIM_SET_RETURN_32(X_STATUS_INVALID_HANDLE);
    }
    return;
  }

  auto sem = object_ref<XSemaphore>(new XSemaphore(kernel_state));
  sem->Initialize(count, limit);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    sem->SetAttributes(obj_attributes_ptr);
  }

  if (handle_ptr) {
    SHIM_SET_MEM_32(handle_ptr, sem->handle());
  }

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL NtReleaseSemaphore_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t sem_handle = SHIM_GET_ARG_32(0);
  int32_t release_count = SHIM_GET_ARG_32(1);
  int32_t previous_count_ptr = SHIM_GET_ARG_32(2);

  XELOGD("NtReleaseSemaphore(%.8X, %d, %.8X)", sem_handle, release_count,
         previous_count_ptr);

  X_STATUS result = X_STATUS_SUCCESS;
  int32_t previous_count = 0;

  auto sem = kernel_state->object_table()->LookupObject<XSemaphore>(sem_handle);
  if (sem) {
    previous_count = sem->ReleaseSemaphore(release_count);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }
  if (previous_count_ptr) {
    SHIM_SET_MEM_32(previous_count_ptr, previous_count);
  }

  SHIM_SET_RETURN_32(result);
}

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

  XMutant* mutant = new XMutant(kernel_state());
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
DECLARE_XBOXKRNL_EXPORT(NtCreateMutant, ExportTag::kImplemented);

SHIM_CALL NtReleaseMutant_shim(PPCContext* ppc_context,
                               KernelState* kernel_state) {
  uint32_t mutant_handle = SHIM_GET_ARG_32(0);
  int32_t unknown = SHIM_GET_ARG_32(1);
  // This doesn't seem to be supported.
  // int32_t previous_count_ptr = SHIM_GET_ARG_32(2);

  // Whatever arg 1 is all games seem to set it to 0, so whether it's
  // abandon or wait we just say false. Which is good, cause they are
  // both ignored.
  assert_zero(unknown);
  uint32_t priority_increment = 0;
  bool abandon = false;
  bool wait = false;

  XELOGD("NtReleaseMutant(%.8X, %.8X)", mutant_handle, unknown);

  X_STATUS result = X_STATUS_SUCCESS;

  auto mutant =
      kernel_state->object_table()->LookupObject<XMutant>(mutant_handle);
  if (mutant) {
    result = mutant->ReleaseMutant(priority_increment, abandon, wait);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtCreateTimer_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(1);
  uint32_t timer_type = SHIM_GET_ARG_32(2);

  // timer_type = NotificationTimer (0) or SynchronizationTimer (1)

  XELOGD("NtCreateTimer(%.8X, %.8X, %.1X)", handle_ptr, obj_attributes_ptr,
         timer_type);

  // Check for an existing timer with the same name.
  auto existing_object =
      LookupNamedObject<XTimer>(kernel_state, obj_attributes_ptr);
  if (existing_object) {
    if (existing_object->type() == XObject::kTypeTimer) {
      if (handle_ptr) {
        existing_object->RetainHandle();
        SHIM_SET_MEM_32(handle_ptr, existing_object->handle());
      }
      SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
    } else {
      SHIM_SET_RETURN_32(X_STATUS_INVALID_HANDLE);
    }
    return;
  }

  XTimer* timer = new XTimer(kernel_state);
  timer->Initialize(timer_type);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    timer->SetAttributes(obj_attributes_ptr);
  }

  if (handle_ptr) {
    SHIM_SET_MEM_32(handle_ptr, timer->handle());
  }

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL NtSetTimerEx_shim(PPCContext* ppc_context,
                            KernelState* kernel_state) {
  uint32_t timer_handle = SHIM_GET_ARG_32(0);
  uint32_t due_time_ptr = SHIM_GET_ARG_32(1);
  uint32_t routine = SHIM_GET_ARG_32(2);  // PTIMERAPCROUTINE
  uint32_t unk_one = SHIM_GET_ARG_32(3);
  uint32_t routine_arg = SHIM_GET_ARG_32(4);
  uint32_t resume = SHIM_GET_ARG_32(5);
  uint32_t period_ms = SHIM_GET_ARG_32(6);
  uint32_t unk_zero = SHIM_GET_ARG_32(7);

  assert_true(unk_one == 1);
  assert_true(unk_zero == 0);

  uint64_t due_time = SHIM_MEM_64(due_time_ptr);

  XELOGD("NtSetTimerEx(%.8X, %.8X(%lld), %.8X, %.8X, %.8X, %.1X, %d, %.8X)",
         timer_handle, due_time_ptr, due_time, routine, unk_one, routine_arg,
         resume, period_ms, unk_zero);

  X_STATUS result = X_STATUS_SUCCESS;

  auto timer = kernel_state->object_table()->LookupObject<XTimer>(timer_handle);
  if (timer) {
    result = timer->SetTimer(due_time, period_ms, routine, routine_arg,
                             resume ? true : false);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtCancelTimer_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t timer_handle = SHIM_GET_ARG_32(0);
  uint32_t current_state_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NtCancelTimer(%.8X, %.8X)", timer_handle, current_state_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  auto timer = kernel_state->object_table()->LookupObject<XTimer>(timer_handle);
  if (timer) {
    result = timer->Cancel();
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }
  if (current_state_ptr) {
    SHIM_SET_MEM_32(current_state_ptr, 0);
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL KeWaitForSingleObject_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  uint32_t object_ptr = SHIM_GET_ARG_32(0);
  uint32_t wait_reason = SHIM_GET_ARG_32(1);
  uint32_t processor_mode = SHIM_GET_ARG_32(2);
  uint32_t alertable = SHIM_GET_ARG_32(3);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(4);

  XELOGD("KeWaitForSingleObject(%.8X, %.8X, %.8X, %.1X, %.8X)", object_ptr,
         wait_reason, processor_mode, alertable, timeout_ptr);

  auto object = XObject::GetNativeObject<XObject>(kernel_state,
                                                  SHIM_MEM_ADDR(object_ptr));

  if (!object) {
    // The only kind-of failure code.
    SHIM_SET_RETURN_32(X_STATUS_ABANDONED_WAIT_0);
    return;
  }

  uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
  X_STATUS result = object->Wait(wait_reason, processor_mode, alertable,
                                 timeout_ptr ? &timeout : nullptr);

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtWaitForSingleObjectEx_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  uint32_t object_handle = SHIM_GET_ARG_32(0);
  uint8_t wait_mode = SHIM_GET_ARG_8(1);
  uint32_t alertable = SHIM_GET_ARG_32(2);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(3);

  XELOGD("NtWaitForSingleObjectEx(%.8X, %u, %.1X, %.8X)", object_handle,
         (uint32_t)wait_mode, alertable, timeout_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  auto object =
      kernel_state->object_table()->LookupObject<XObject>(object_handle);
  if (object) {
    uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
    result =
        object->Wait(3, wait_mode, alertable, timeout_ptr ? &timeout : nullptr);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL KeWaitForMultipleObjects_shim(PPCContext* ppc_context,
                                        KernelState* kernel_state) {
  uint32_t count = SHIM_GET_ARG_32(0);
  uint32_t objects_ptr = SHIM_GET_ARG_32(1);
  uint32_t wait_type = SHIM_GET_ARG_32(2);
  uint32_t wait_reason = SHIM_GET_ARG_32(3);
  uint32_t processor_mode = SHIM_GET_ARG_32(4);
  uint32_t alertable = SHIM_GET_ARG_32(5);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(6);
  uint32_t wait_block_array_ptr = SHIM_GET_ARG_32(7);

  XELOGD(
      "KeWaitForMultipleObjects(%d, %.8X, %.8X, %.8X, %.8X, %.1X, %.8X, %.8X)",
      count, objects_ptr, wait_type, wait_reason, processor_mode, alertable,
      timeout_ptr, wait_block_array_ptr);

  assert_true(wait_type <= 1);

  X_STATUS result = X_STATUS_SUCCESS;

  std::vector<object_ref<XObject>> objects;
  for (uint32_t n = 0; n < count; n++) {
    uint32_t object_ptr_ptr = SHIM_MEM_32(objects_ptr + n * 4);
    void* object_ptr = SHIM_MEM_ADDR(object_ptr_ptr);
    auto object_ref =
        XObject::GetNativeObject<XObject>(kernel_state, object_ptr);
    if (!object_ref) {
      SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
      return;
    }
    objects.push_back(std::move(object_ref));
  }

  uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
  result = XObject::WaitMultiple(uint32_t(objects.size()),
                                 reinterpret_cast<XObject**>(objects.data()),
                                 wait_type, wait_reason, processor_mode,
                                 alertable, timeout_ptr ? &timeout : nullptr);

  SHIM_SET_RETURN_32(result);
}

dword_result_t NtWaitForMultipleObjectsEx(
    dword_t count, pointer_t<xe::be<uint32_t>> handles, dword_t wait_type,
    dword_t wait_mode, dword_t alertable,
    pointer_t<xe::be<uint64_t>> timeout_ptr) {
  assert_true(wait_type <= 1);
  X_STATUS result = X_STATUS_SUCCESS;

  std::vector<object_ref<XObject>> objects(count);
  for (uint32_t n = 0; n < count; n++) {
    uint32_t object_handle = handles[n];
    auto object =
        kernel_state()->object_table()->LookupObject<XObject>(object_handle);
    if (!object) {
      return X_STATUS_INVALID_PARAMETER;
    }
    objects[n] = std::move(object);
  }

  uint64_t timeout = timeout_ptr ? uint64_t(*timeout_ptr) : 0;
  result = XObject::WaitMultiple(
      count, reinterpret_cast<XObject**>(objects.data()), wait_type, 6,
      wait_mode, alertable, timeout_ptr ? &timeout : nullptr);

  return result;
}
DECLARE_XBOXKRNL_EXPORT(NtWaitForMultipleObjectsEx,
                        ExportTag::kImplemented | ExportTag::kThreading);

SHIM_CALL NtSignalAndWaitForSingleObjectEx_shim(PPCContext* ppc_context,
                                                KernelState* kernel_state) {
  uint32_t signal_handle = SHIM_GET_ARG_32(0);
  uint32_t wait_handle = SHIM_GET_ARG_32(1);
  uint32_t alertable = SHIM_GET_ARG_32(2);
  uint32_t unk_3 = SHIM_GET_ARG_32(3);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(4);

  XELOGD("NtSignalAndWaitForSingleObjectEx(%.8X, %.8X, %.1X, %.8X, %.8X)",
         signal_handle, wait_handle, alertable, unk_3, timeout_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  auto signal_object =
      kernel_state->object_table()->LookupObject<XObject>(signal_handle);
  auto wait_object =
      kernel_state->object_table()->LookupObject<XObject>(wait_handle);
  if (signal_object && wait_object) {
    uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
    result =
        XObject::SignalAndWait(signal_object.get(), wait_object.get(), 3, 1,
                               alertable, timeout_ptr ? &timeout : nullptr);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL KfAcquireSpinLock_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t lock_ptr = SHIM_GET_ARG_32(0);

  // XELOGD(
  //     "KfAcquireSpinLock(%.8X)",
  //     lock_ptr);

  // Lock.
  auto lock = reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(lock_ptr));
  while (!xe::atomic_cas(0, 1, lock)) {
    // Spin!
    // TODO(benvanik): error on deadlock?
    xe::threading::MaybeYield();
  }

  // Raise IRQL to DISPATCH.
  XThread* thread = XThread::GetCurrentThread();
  auto old_irql = thread->RaiseIrql(2);

  SHIM_SET_RETURN_32(old_irql);
}

SHIM_CALL KfReleaseSpinLock_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t lock_ptr = SHIM_GET_ARG_32(0);
  uint32_t old_irql = SHIM_GET_ARG_32(1);

  // XELOGD(
  //     "KfReleaseSpinLock(%.8X, %d)",
  //     lock_ptr,
  //     old_irql);

  // Restore IRQL.
  XThread* thread = XThread::GetCurrentThread();
  thread->LowerIrql(old_irql);

  // Unlock.
  auto lock = reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(lock_ptr));
  xe::atomic_dec(lock);
}

SHIM_CALL KeAcquireSpinLockAtRaisedIrql_shim(PPCContext* ppc_context,
                                             KernelState* kernel_state) {
  uint32_t lock_ptr = SHIM_GET_ARG_32(0);

  // XELOGD(
  //     "KeAcquireSpinLockAtRaisedIrql(%.8X)",
  //     lock_ptr);

  // Lock.
  auto lock = reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(lock_ptr));
  while (!xe::atomic_cas(0, 1, lock)) {
    // Spin!
    // TODO(benvanik): error on deadlock?
  }
}

SHIM_CALL KeReleaseSpinLockFromRaisedIrql_shim(PPCContext* ppc_context,
                                               KernelState* kernel_state) {
  uint32_t lock_ptr = SHIM_GET_ARG_32(0);

  // XELOGD(
  //     "KeReleaseSpinLockFromRaisedIrql(%.8X)",
  //     lock_ptr);

  // Unlock.
  auto lock = reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(lock_ptr));
  xe::atomic_dec(lock);
}

SHIM_CALL KeEnterCriticalRegion_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  // XELOGD(
  //     "KeEnterCriticalRegion()");
  XThread::EnterCriticalRegion();
}

SHIM_CALL KeLeaveCriticalRegion_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  // XELOGD(
  //     "KeLeaveCriticalRegion()");
  XThread::LeaveCriticalRegion();

  XThread::GetCurrentThread()->CheckApcs();
}

SHIM_CALL KeRaiseIrqlToDpcLevel_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  // XELOGD(
  //     "KeRaiseIrqlToDpcLevel()");
  auto old_value = kernel_state->processor()->RaiseIrql(cpu::Irql::DPC);
  SHIM_SET_RETURN_32(old_value);
}

SHIM_CALL KfLowerIrql_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t old_value = SHIM_GET_ARG_32(0);
  // XELOGD(
  //     "KfLowerIrql(%d)",
  //     old_value);
  kernel_state->processor()->LowerIrql(static_cast<cpu::Irql>(old_value));

  XThread::GetCurrentThread()->CheckApcs();
}

SHIM_CALL NtQueueApcThread_shim(PPCContext* ppc_context,
                                KernelState* kernel_state) {
  uint32_t thread_handle = SHIM_GET_ARG_32(0);
  uint32_t apc_routine = SHIM_GET_ARG_32(1);
  uint32_t arg1 = SHIM_GET_ARG_32(2);
  uint32_t arg2 = SHIM_GET_ARG_32(3);
  uint32_t arg3 = SHIM_GET_ARG_32(4);  // ?
  XELOGD("NtQueueApcThread(%.8X, %.8X, %.8X, %.8X, %.8X)", thread_handle,
         apc_routine, arg1, arg2, arg3);

  // Alloc APC object (from somewhere) and insert.

  assert_always("not implemented");
}

SHIM_CALL KeInitializeApc_shim(PPCContext* ppc_context,
                               KernelState* kernel_state) {
  uint32_t apc_ptr = SHIM_GET_ARG_32(0);
  uint32_t thread = SHIM_GET_ARG_32(1);
  uint32_t kernel_routine = SHIM_GET_ARG_32(2);
  uint32_t rundown_routine = SHIM_GET_ARG_32(3);
  uint32_t normal_routine = SHIM_GET_ARG_32(4);
  uint32_t processor_mode = SHIM_GET_ARG_32(5);
  uint32_t normal_context = SHIM_GET_ARG_32(6);

  XELOGD("KeInitializeApc(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %.8X)", apc_ptr,
         thread, kernel_routine, rundown_routine, normal_routine,
         processor_mode, normal_context);

  auto apc = SHIM_STRUCT(XAPC, apc_ptr);
  apc->Initialize();
  apc->processor_mode = processor_mode;
  apc->thread_ptr = thread;
  apc->kernel_routine = kernel_routine;
  apc->rundown_routine = rundown_routine;
  apc->normal_routine = normal_routine;
  apc->normal_context = normal_routine ? normal_context : 0;
}

SHIM_CALL KeInsertQueueApc_shim(PPCContext* ppc_context,
                                KernelState* kernel_state) {
  uint32_t apc_ptr = SHIM_GET_ARG_32(0);
  uint32_t arg1 = SHIM_GET_ARG_32(1);
  uint32_t arg2 = SHIM_GET_ARG_32(2);
  uint32_t priority_increment = SHIM_GET_ARG_32(3);

  XELOGD("KeInsertQueueApc(%.8X, %.8X, %.8X, %.8X)", apc_ptr, arg1, arg2,
         priority_increment);

  auto apc = SHIM_STRUCT(XAPC, apc_ptr);

  auto thread = XObject::GetNativeObject<XThread>(
      kernel_state, SHIM_MEM_ADDR(apc->thread_ptr));
  if (!thread) {
    SHIM_SET_RETURN_32(0);
    return;
  }

  // Lock thread.
  thread->LockApc();

  // Fail if already inserted.
  if (apc->enqueued) {
    thread->UnlockApc(false);
    SHIM_SET_RETURN_32(0);
    return;
  }

  // Prep APC.
  apc->arg1 = arg1;
  apc->arg2 = arg2;
  apc->enqueued = 1;

  auto apc_list = thread->apc_list();

  uint32_t list_entry_ptr = apc_ptr + 8;
  apc_list->Insert(list_entry_ptr);

  // Unlock thread.
  thread->UnlockApc(true);

  SHIM_SET_RETURN_32(1);
}

SHIM_CALL KeRemoveQueueApc_shim(PPCContext* ppc_context,
                                KernelState* kernel_state) {
  uint32_t apc_ptr = SHIM_GET_ARG_32(0);

  XELOGD("KeRemoveQueueApc(%.8X)", apc_ptr);

  bool result = false;

  auto apc = SHIM_STRUCT(XAPC, apc_ptr);

  auto thread = XObject::GetNativeObject<XThread>(
      kernel_state, SHIM_MEM_ADDR(apc->thread_ptr));
  if (!thread) {
    SHIM_SET_RETURN_32(0);
    return;
  }

  thread->LockApc();

  if (!apc->enqueued) {
    thread->UnlockApc(false);
    SHIM_SET_RETURN_32(0);
    return;
  }

  auto apc_list = thread->apc_list();
  uint32_t list_entry_ptr = apc_ptr + 8;
  if (apc_list->IsQueued(list_entry_ptr)) {
    apc_list->Remove(list_entry_ptr);
    result = true;
  }

  thread->UnlockApc(true);

  SHIM_SET_RETURN_32(result ? 1 : 0);
}

SHIM_CALL KiApcNormalRoutineNop_shim(PPCContext* ppc_context,
                                     KernelState* kernel_state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);  // output?
  uint32_t unk1 = SHIM_GET_ARG_32(1);  // 0x13

  XELOGD("KiApcNormalRoutineNop(%.8X, %.8X)", unk0, unk1);

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL KeInitializeDpc_shim(PPCContext* ppc_context,
                               KernelState* kernel_state) {
  uint32_t dpc_ptr = SHIM_GET_ARG_32(0);
  uint32_t routine = SHIM_GET_ARG_32(1);
  uint32_t context = SHIM_GET_ARG_32(2);

  XELOGD("KeInitializeDpc(%.8X, %.8X, %.8X)", dpc_ptr, routine, context);

  // KDPC (maybe) 0x18 bytes?
  uint32_t type = 19;  // DpcObject
  uint32_t importance = 0;
  uint32_t number = 0;  // ?
  SHIM_SET_MEM_32(dpc_ptr + 0, (type << 24) | (importance << 16) | (number));
  SHIM_SET_MEM_32(dpc_ptr + 4, 0);  // flink
  SHIM_SET_MEM_32(dpc_ptr + 8, 0);  // blink
  SHIM_SET_MEM_32(dpc_ptr + 12, routine);
  SHIM_SET_MEM_32(dpc_ptr + 16, context);
  SHIM_SET_MEM_32(dpc_ptr + 20, 0);  // arg1
  SHIM_SET_MEM_32(dpc_ptr + 24, 0);  // arg2
}

SHIM_CALL KeInsertQueueDpc_shim(PPCContext* ppc_context,
                                KernelState* kernel_state) {
  uint32_t dpc_ptr = SHIM_GET_ARG_32(0);
  uint32_t arg1 = SHIM_GET_ARG_32(1);
  uint32_t arg2 = SHIM_GET_ARG_32(2);

  assert_always("DPC does not dispatch yet; going to hang!");

  XELOGD("KeInsertQueueDpc(%.8X, %.8X, %.8X)", dpc_ptr, arg1, arg2);

  uint32_t list_entry_ptr = dpc_ptr + 4;

  // Lock dispatcher.
  auto global_lock = xe::global_critical_region::AcquireDirect();
  auto dpc_list = kernel_state->dpc_list();

  // If already in a queue, abort.
  if (dpc_list->IsQueued(list_entry_ptr)) {
    SHIM_SET_RETURN_32(0);
    return;
  }

  // Prep DPC.
  SHIM_SET_MEM_32(dpc_ptr + 20, arg1);
  SHIM_SET_MEM_32(dpc_ptr + 24, arg2);

  dpc_list->Insert(list_entry_ptr);

  SHIM_SET_RETURN_32(1);
}

SHIM_CALL KeRemoveQueueDpc_shim(PPCContext* ppc_context,
                                KernelState* kernel_state) {
  uint32_t dpc_ptr = SHIM_GET_ARG_32(0);

  XELOGD("KeRemoveQueueDpc(%.8X)", dpc_ptr);

  bool result = false;

  uint32_t list_entry_ptr = dpc_ptr + 4;

  auto global_lock = xe::global_critical_region::AcquireDirect();
  auto dpc_list = kernel_state->dpc_list();
  if (dpc_list->IsQueued(list_entry_ptr)) {
    dpc_list->Remove(list_entry_ptr);
    result = true;
  }

  SHIM_SET_RETURN_32(result ? 1 : 0);
}

// NOTE: This function is very commonly inlined, and probably won't be called!
pointer_result_t InterlockedPushEntrySList(
    pointer_t<X_SLIST_HEADER> plist_ptr, pointer_t<X_SINGLE_LIST_ENTRY> entry) {
  assert_not_null(plist_ptr);
  assert_not_null(entry);

  // Hold a global lock during this method. Once in the lock we assume we have
  // exclusive access to the structure.
  auto global_lock = xe::global_critical_region::AcquireDirect();

  alignas(8) X_SLIST_HEADER old_hdr = *plist_ptr;
  alignas(8) X_SLIST_HEADER new_hdr = {0};
  new_hdr.depth = old_hdr.depth + 1;
  new_hdr.sequence = old_hdr.sequence + 1;

  uint32_t old_head = old_hdr.next.next;
  entry->next = old_hdr.next.next;
  new_hdr.next.next = entry.guest_address();

  *reinterpret_cast<uint64_t*>(plist_ptr.host_address()) =
      *reinterpret_cast<uint64_t*>(&new_hdr);
  xe::threading::SyncMemory();

  return old_head;
}
DECLARE_XBOXKRNL_EXPORT(InterlockedPushEntrySList,
                        ExportTag::kImplemented | ExportTag::kHighFrequency);

pointer_result_t InterlockedPopEntrySList(pointer_t<X_SLIST_HEADER> plist_ptr) {
  assert_not_null(plist_ptr);

  // Hold a global lock during this method. Once in the lock we assume we have
  // exclusive access to the structure.
  auto global_lock = xe::global_critical_region::AcquireDirect();

  uint32_t popped = 0;

  alignas(8) X_SLIST_HEADER old_hdr = *plist_ptr;
  alignas(8) X_SLIST_HEADER new_hdr = {0};
  auto next = kernel_memory()->TranslateVirtual<X_SINGLE_LIST_ENTRY*>(
      old_hdr.next.next);
  if (!old_hdr.next.next) {
    return 0;
  }
  popped = old_hdr.next.next;

  new_hdr.depth = old_hdr.depth - 1;
  new_hdr.next.next = next->next;
  new_hdr.sequence = old_hdr.sequence;

  *reinterpret_cast<uint64_t*>(plist_ptr.host_address()) =
      *reinterpret_cast<uint64_t*>(&new_hdr);
  xe::threading::SyncMemory();

  return popped;
}
DECLARE_XBOXKRNL_EXPORT(InterlockedPopEntrySList,
                        ExportTag::kImplemented | ExportTag::kHighFrequency);

pointer_result_t InterlockedFlushSList(pointer_t<X_SLIST_HEADER> plist_ptr) {
  assert_not_null(plist_ptr);

  // Hold a global lock during this method. Once in the lock we assume we have
  // exclusive access to the structure.
  auto global_lock = xe::global_critical_region::AcquireDirect();

  alignas(8) X_SLIST_HEADER old_hdr = *plist_ptr;
  alignas(8) X_SLIST_HEADER new_hdr = {0};
  uint32_t first = old_hdr.next.next;
  new_hdr.next.next = 0;
  new_hdr.depth = 0;
  new_hdr.sequence = 0;

  *reinterpret_cast<uint64_t*>(plist_ptr.host_address()) =
      *reinterpret_cast<uint64_t*>(&new_hdr);
  xe::threading::SyncMemory();

  return first;
}
DECLARE_XBOXKRNL_EXPORT(InterlockedFlushSList, ExportTag::kImplemented);

void RegisterThreadingExports(xe::cpu::ExportResolver* export_resolver,
                              KernelState* kernel_state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", ExCreateThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", ExTerminateThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtResumeThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeResumeThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtSuspendThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeSetAffinityThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeQueryBasePriorityThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeSetBasePriorityThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeSetDisableBoostThread, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeGetCurrentProcessType, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeSetCurrentProcessType, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeQueryPerformanceFrequency, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeDelayExecutionThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtYieldExecution, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeQuerySystemTime, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsAlloc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsFree, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsGetValue, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsSetValue, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", NtCreateSemaphore, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtReleaseSemaphore, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", NtReleaseMutant, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", NtCreateTimer, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtSetTimerEx, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtCancelTimer, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeWaitForSingleObject, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtWaitForSingleObjectEx, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeWaitForMultipleObjects, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtSignalAndWaitForSingleObjectEx, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KfAcquireSpinLock, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KfReleaseSpinLock, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeAcquireSpinLockAtRaisedIrql, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeReleaseSpinLockFromRaisedIrql, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeEnterCriticalRegion, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeLeaveCriticalRegion, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeRaiseIrqlToDpcLevel, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KfLowerIrql, state);

  // SHIM_SET_MAPPING("xboxkrnl.exe", NtQueueApcThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeInitializeApc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeInsertQueueApc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeRemoveQueueApc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KiApcNormalRoutineNop, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeInitializeDpc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeInsertQueueDpc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeRemoveQueueDpc, state);
}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

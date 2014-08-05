/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl_threading.h>

#include <xenia/cpu/processor.h>
#include <xenia/kernel/dispatcher.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/native_list.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/objects/xevent.h>
#include <xenia/kernel/objects/xmutant.h>
#include <xenia/kernel/objects/xsemaphore.h>
#include <xenia/kernel/objects/xthread.h>
#include <xenia/kernel/objects/xtimer.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {


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
// if (r11 != 0) {
//   lwz r11, 0x100(r13)
//   stw r3, 0x160(r11)
// }

// RtlSetLastError:
// lwz r11, 0x150(r13)
// if (r11 != 0) {
//   lwz r11, 0x100(r13)
//   stw r3, 0x160(r11)
// }

// RtlSetLastNTError:
// r3 = RtlNtStatusToDosError(r3)
// lwz r11, 0x150(r13)
// if (r11 != 0) {
//   lwz r11, 0x100(r13)
//   stw r3, 0x160(r11)
// }


X_STATUS xeExCreateThread(
    uint32_t* handle_ptr, uint32_t stack_size, uint32_t* thread_id_ptr,
    uint32_t xapi_thread_startup,
    uint32_t start_address, uint32_t start_context, uint32_t creation_flags) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // DWORD
  // LPHANDLE Handle,
  // DWORD    StackSize,
  // LPDWORD  ThreadId,
  // LPVOID   XapiThreadStartup, ?? often 0
  // LPVOID   StartAddress,
  // LPVOID   StartContext,
  // DWORD    CreationFlags // 0x80?

  XThread* thread = new XThread(
      state, stack_size, xapi_thread_startup, start_address, start_context,
      creation_flags);

  X_STATUS result_code = thread->Create();
  if (XFAILED(result_code)) {
    // Failed!
    thread->Release();
    XELOGE("Thread creation failed: %.8X", result_code);
    return result_code;
  }

  if (handle_ptr) {
    *handle_ptr = thread->handle();
  }
  if (thread_id_ptr) {
    *thread_id_ptr = thread->thread_id();
  }
  return result_code;
}


SHIM_CALL ExCreateThread_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t stack_size = SHIM_GET_ARG_32(1);
  uint32_t thread_id_ptr = SHIM_GET_ARG_32(2);
  uint32_t xapi_thread_startup = SHIM_GET_ARG_32(3);
  uint32_t start_address = SHIM_GET_ARG_32(4);
  uint32_t start_context = SHIM_GET_ARG_32(5);
  uint32_t creation_flags = SHIM_GET_ARG_32(6);

  XELOGD(
      "ExCreateThread(%.8X, %d, %.8X, %.8X, %.8X, %.8X, %.8X)",
      handle_ptr,
      stack_size,
      thread_id_ptr,
      xapi_thread_startup,
      start_address,
      start_context,
      creation_flags);

  uint32_t handle;
  uint32_t thread_id;
  X_STATUS result = xeExCreateThread(
      &handle, stack_size, &thread_id, xapi_thread_startup,
      start_address, start_context, creation_flags);

  if (XSUCCEEDED(result)) {
    if (handle_ptr) {
      SHIM_SET_MEM_32(handle_ptr, handle);
    }
    if (thread_id_ptr) {
      SHIM_SET_MEM_32(thread_id_ptr, thread_id);
    }
  }
  SHIM_SET_RETURN_32(result);
}


SHIM_CALL ExTerminateThread_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t exit_code = SHIM_GET_ARG_32(0);

  XELOGD(
      "ExTerminateThread(%d)",
      exit_code);

  XThread* thread = XThread::GetCurrentThread();

  // NOTE: this kills us right now. We won't return from it.
  X_STATUS result = thread->Exit(exit_code);
  SHIM_SET_RETURN_32(result);
}


X_STATUS xeNtResumeThread(uint32_t handle, uint32_t* out_suspend_count) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  X_STATUS result = X_STATUS_SUCCESS;

  XThread* thread = NULL;
  result = state->object_table()->GetObject(
      handle, (XObject**)&thread);
  if (XSUCCEEDED(result)) {
    result = thread->Resume(out_suspend_count);
    thread->Release();
  }

  return result;
}


SHIM_CALL NtResumeThread_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t suspend_count_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
    "NtResumeThread(%.8X, %.8X)",
    handle,
    suspend_count_ptr);

  uint32_t suspend_count;
  X_STATUS result = xeNtResumeThread(handle, &suspend_count);
  if (XSUCCEEDED(result)) {
    if (suspend_count_ptr) {
      SHIM_SET_MEM_32(suspend_count_ptr, suspend_count);
    }
  }

  SHIM_SET_RETURN_32(result);
}


X_STATUS xeKeResumeThread(void* thread_ptr, uint32_t* out_suspend_count) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  X_STATUS result = X_STATUS_SUCCESS;

  XThread* thread = (XThread*)XObject::GetObject(state, thread_ptr);
  if (thread) {
    result = thread->Resume(out_suspend_count);
  }

  return result;
}


SHIM_CALL KeResumeThread_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t thread = SHIM_GET_ARG_32(0);
  uint32_t suspend_count_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
    "KeResumeThread(%.8X, %.8X)",
    thread,
    suspend_count_ptr);

  void* thread_ptr = SHIM_MEM_ADDR(thread);
  uint32_t suspend_count;
  X_STATUS result = xeKeResumeThread(thread_ptr, &suspend_count);
  if (XSUCCEEDED(result)) {
    if (suspend_count_ptr) {
      SHIM_SET_MEM_32(suspend_count_ptr, suspend_count);
    }
  }

  SHIM_SET_RETURN_32(result);
}


X_STATUS xeNtSuspendThread(uint32_t handle, uint32_t* out_suspend_count) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  X_STATUS result = X_STATUS_SUCCESS;

  XThread* thread = NULL;
  result = state->object_table()->GetObject(
      handle, (XObject**)&thread);
  if (XSUCCEEDED(result)) {
    result = thread->Suspend(out_suspend_count);
    thread->Release();
  }

  return result;
}


SHIM_CALL NtSuspendThread_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t suspend_count_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
    "NtSuspendThread(%.8X, %.8X)",
    handle,
    suspend_count_ptr);

  uint32_t suspend_count;
  X_STATUS result = xeNtSuspendThread(handle, &suspend_count);
  if (XSUCCEEDED(result)) {
    if (suspend_count_ptr) {
      SHIM_SET_MEM_32(suspend_count_ptr, suspend_count);
    }
  }

  SHIM_SET_RETURN_32(result);
}


uint32_t xeKeSetAffinityThread(void* thread_ptr, uint32_t affinity) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  XThread* thread = (XThread*)XObject::GetObject(state, thread_ptr);
  if (thread) {
    thread->SetAffinity(affinity);
  }

  return affinity;
}


SHIM_CALL KeSetAffinityThread_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t thread = SHIM_GET_ARG_32(0);
  uint32_t affinity = SHIM_GET_ARG_32(1);

  XELOGD(
      "KeSetAffinityThread(%.8X, %.8X)",
      thread,
      affinity);

  void* thread_ptr = SHIM_MEM_ADDR(thread);
  uint32_t result = xeKeSetAffinityThread(thread_ptr, affinity);
  SHIM_SET_RETURN_32(result);
}


SHIM_CALL KeQueryBasePriorityThread_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "KeQueryBasePriorityThread(%.8X)",
      thread_ptr);

  int32_t priority = 0;

  XThread* thread = (XThread*)XObject::GetObject(
      state, SHIM_MEM_ADDR(thread_ptr));
  if (thread) {
    priority = thread->QueryPriority();
  }

  SHIM_SET_RETURN_32(priority);
}


uint32_t xeKeSetBasePriorityThread(void* thread_ptr, int32_t increment) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  int32_t prev_priority = 0;

  XThread* thread = (XThread*)XObject::GetObject(state, thread_ptr);
  if (thread) {
    prev_priority = thread->QueryPriority();
    thread->SetPriority(increment);
  }

  return prev_priority;
}


SHIM_CALL KeSetBasePriorityThread_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t thread = SHIM_GET_ARG_32(0);
  uint32_t increment = SHIM_GET_ARG_32(1);

  XELOGD(
      "KeSetBasePriorityThread(%.8X, %.8X)",
      thread,
      increment);

  void* thread_ptr = SHIM_MEM_ADDR(thread);
  uint32_t result = xeKeSetBasePriorityThread(thread_ptr, increment);
  SHIM_SET_RETURN_32(result);
}


uint32_t xeKeGetCurrentProcessType() {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // DWORD

  return X_PROCTYPE_USER;
}


SHIM_CALL KeGetCurrentProcessType_shim(
    PPCContext* ppc_state, KernelState* state) {
  // XELOGD(
  //     "KeGetCurrentProcessType()");

  int result = xeKeGetCurrentProcessType();
  SHIM_SET_RETURN_64(result);
}


uint64_t xeKeQueryPerformanceFrequency() {
  LARGE_INTEGER frequency;
  if (QueryPerformanceFrequency(&frequency)) {
    return frequency.QuadPart;
  } else {
    return 0;
  }
}


SHIM_CALL KeQueryPerformanceFrequency_shim(
    PPCContext* ppc_state, KernelState* state) {
  // XELOGD(
  //     "KeQueryPerformanceFrequency()");

  uint64_t result = xeKeQueryPerformanceFrequency();
  SHIM_SET_RETURN_64(result);
}


X_STATUS xeKeDelayExecutionThread(
    uint32_t processor_mode, uint32_t alertable, uint64_t interval) {
  XThread* thread = XThread::GetCurrentThread();
  return thread->Delay(processor_mode, alertable, interval);
}


SHIM_CALL KeDelayExecutionThread_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t processor_mode = SHIM_GET_ARG_32(0);
  uint32_t alertable = SHIM_GET_ARG_32(1);
  uint32_t interval_ptr = SHIM_GET_ARG_32(2);
  uint64_t interval = SHIM_MEM_64(interval_ptr);

  // XELOGD(
  //     "KeDelayExecutionThread(%.8X, %d, %.8X(%.16llX)",
  //     processor_mode, alertable, interval_ptr, interval);

  X_STATUS result = xeKeDelayExecutionThread(
      processor_mode, alertable, interval);

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL NtYieldExecution_shim(
    PPCContext* ppc_state, KernelState* state) {
  XELOGD("NtYieldExecution()");
  xeKeDelayExecutionThread(0, 0, 0);
  SHIM_SET_RETURN_64(0);
}


void xeKeQuerySystemTime(uint64_t* time_ptr) {
  FILETIME t;
  GetSystemTimeAsFileTime(&t);
  *time_ptr = ((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime;
}


SHIM_CALL KeQuerySystemTime_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t time_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "KeQuerySystemTime(%.8X)",
      time_ptr);

  uint64_t time;
  xeKeQuerySystemTime(&time);

  if (time_ptr) {
    SHIM_SET_MEM_64(time_ptr, time);
  }
}


// The TLS system used here is a bit hacky, but seems to work.
// Both Win32 and pthreads use unsigned longs as TLS indices, so we can map
// right into the system for these calls. We're just round tripping the IDs and
// hoping for the best.


// http://msdn.microsoft.com/en-us/library/ms686801
uint32_t xeKeTlsAlloc() {
  // DWORD

  uint32_t tls_index;

#if XE_PLATFORM_WIN32
  tls_index = TlsAlloc();
#else
  pthread_key_t key;
  if (pthread_key_create(&key, NULL)) {
    tls_index = X_TLS_OUT_OF_INDEXES;
  } else {
    tls_index = (uint32_t)key;
  }
#endif  // WIN32

  return tls_index;
}


SHIM_CALL KeTlsAlloc_shim(
    PPCContext* ppc_state, KernelState* state) {
  XELOGD(
      "KeTlsAlloc()");

  uint32_t result = xeKeTlsAlloc();
  SHIM_SET_RETURN_64(result);
}


// http://msdn.microsoft.com/en-us/library/ms686804
int KeTlsFree(uint32_t tls_index) {
  // BOOL
  // _In_  DWORD dwTlsIndex

  if (tls_index == X_TLS_OUT_OF_INDEXES) {
    return 0;
  }

  int result_code = 0;

#if XE_PLATFORM_WIN32
  result_code = TlsFree(tls_index);
#else
  result_code = pthread_key_delete(tls_index) == 0;
#endif  // WIN32

  return result_code;
}


SHIM_CALL KeTlsFree_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);

  XELOGD(
      "KeTlsFree(%.8X)",
      tls_index);

  int result = xeKeTlsAlloc();
  SHIM_SET_RETURN_64(result);
}


// http://msdn.microsoft.com/en-us/library/ms686812
uint64_t xeKeTlsGetValue(uint32_t tls_index) {
  // LPVOID
  // _In_  DWORD dwTlsIndex

  uint64_t value = 0;

#if XE_PLATFORM_WIN32
  value = (uint64_t)TlsGetValue(tls_index);
#else
  value = (uint64_t)pthread_getspecific(tls_index);
#endif  // WIN32

  if (!value) {
    XELOGW("KeTlsGetValue should SetLastError if result is NULL");
    // TODO(benvanik): SetLastError
  }

  return value;
}


SHIM_CALL KeTlsGetValue_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);

  // Logging disabled, as some games spam this.
  //XELOGD(
  //    "KeTlsGetValue(%.8X)",
  //    tls_index);

  uint64_t result = xeKeTlsGetValue(tls_index);
  SHIM_SET_RETURN_64(result);
}


// http://msdn.microsoft.com/en-us/library/ms686818
int xeKeTlsSetValue(uint32_t tls_index, uint64_t tls_value) {
  // BOOL
  // _In_      DWORD dwTlsIndex,
  // _In_opt_  LPVOID lpTlsValue

  int result_code = 0;

#if XE_PLATFORM_WIN32
  result_code = TlsSetValue(tls_index, (LPVOID)tls_value);
#else
  result_code = pthread_setspecific(tls_index, (void*)tls_value) == 0;
#endif  // WIN32

  return result_code;
}


SHIM_CALL KeTlsSetValue_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);
  uint32_t tls_value = SHIM_GET_ARG_32(1);

  XELOGD(
      "KeTlsSetValue(%.8X, %.8X)",
      tls_index, tls_value);

  int result = xeKeTlsSetValue(tls_index, tls_value);
  SHIM_SET_RETURN_64(result);
}


X_STATUS xeNtCreateEvent(uint32_t* handle_ptr, void* obj_attributes,
                         uint32_t event_type, uint32_t initial_state) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  XEvent* ev = new XEvent(state);
  ev->Initialize(!event_type, !!initial_state);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes) {
    //ev->SetName(...);
  }

  *handle_ptr = ev->handle();

  return X_STATUS_SUCCESS;
}


SHIM_CALL NtCreateEvent_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(1);
  uint32_t event_type = SHIM_GET_ARG_32(2);
  uint32_t initial_state = SHIM_GET_ARG_32(3);

  XELOGD(
      "NtCreateEvent(%.8X, %.8X, %d, %d)",
      handle_ptr, obj_attributes_ptr, event_type, initial_state);

  uint32_t handle;
  X_STATUS result = xeNtCreateEvent(
      &handle, SHIM_MEM_ADDR(obj_attributes_ptr),
      event_type, initial_state);

  if (XSUCCEEDED(result)) {
    if (handle_ptr) {
      SHIM_SET_MEM_32(handle_ptr, handle);
    }
  }
  SHIM_SET_RETURN_32(result);
}


int32_t xeKeSetEvent(void* event_ptr, uint32_t increment, uint32_t wait) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  XEvent* ev = (XEvent*)XObject::GetObject(state, event_ptr);
  assert_not_null(ev);
  if (!ev) {
    return 0;
  }

  return ev->Set(increment, !!wait);
}


SHIM_CALL KeSetEvent_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t event_ref = SHIM_GET_ARG_32(0);
  uint32_t increment = SHIM_GET_ARG_32(1);
  uint32_t wait = SHIM_GET_ARG_32(2);

  XELOGD(
      "KeSetEvent(%.8X, %.8X, %.8X)",
      event_ref, increment, wait);

  void* event_ptr = SHIM_MEM_ADDR(event_ref);
  int32_t result = xeKeSetEvent(event_ptr, increment, wait);

  SHIM_SET_RETURN_64(result);
}


SHIM_CALL NtSetEvent_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t event_handle = SHIM_GET_ARG_32(0);
  uint32_t previous_state_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "NtSetEvent(%.8X, %.8X)",
      event_handle, previous_state_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  XEvent* ev = NULL;
  result = state->object_table()->GetObject(
      event_handle, (XObject**)&ev);
  if (XSUCCEEDED(result)) {
    int32_t was_signalled = ev->Set(0, false);
    if (previous_state_ptr) {
      SHIM_SET_MEM_32(previous_state_ptr, was_signalled);
    }

    ev->Release();
  }

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL KePulseEvent_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t event_ref = SHIM_GET_ARG_32(0);
  uint32_t increment = SHIM_GET_ARG_32(1);
  uint32_t wait = SHIM_GET_ARG_32(2);

  XELOGD(
      "KePulseEvent(%.8X, %.8X, %.8X)",
      event_ref, increment, wait);

  int32_t result = 0;

  void* event_ptr = SHIM_MEM_ADDR(event_ref);
  XEvent* ev = (XEvent*)XObject::GetObject(state, event_ptr);
  assert_not_null(ev);
  if (ev) {
    result = ev->Pulse(increment, !!wait);
  }

  SHIM_SET_RETURN_64(result);
}


SHIM_CALL NtPulseEvent_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t event_handle = SHIM_GET_ARG_32(0);
  uint32_t previous_state_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "NtPulseEvent(%.8X, %.8X)",
      event_handle, previous_state_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  XEvent* ev = NULL;
  result = state->object_table()->GetObject(
      event_handle, (XObject**)&ev);
  if (XSUCCEEDED(result)) {
    int32_t was_signalled = ev->Pulse(0, false);
    if (previous_state_ptr) {
      SHIM_SET_MEM_32(previous_state_ptr, was_signalled);
    }

    ev->Release();
  }

  SHIM_SET_RETURN_32(result);
}


int32_t xeKeResetEvent(void* event_ptr) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  XEvent* ev = (XEvent*)XEvent::GetObject(state, event_ptr);
  assert_not_null(ev);
  if (!ev) {
    return 0;
  }

  return ev->Reset();
}


SHIM_CALL KeResetEvent_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t event_ref = SHIM_GET_ARG_32(0);

  XELOGD(
      "KeResetEvent(%.8X)",
      event_ref);

  void* event_ptr = SHIM_MEM_ADDR(event_ref);
  int32_t result = xeKeResetEvent(event_ptr);

  SHIM_SET_RETURN_64(result);
}


SHIM_CALL NtClearEvent_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t event_handle = SHIM_GET_ARG_32(0);

  XELOGD(
      "NtClearEvent(%.8X)",
      event_handle);

  X_STATUS result = X_STATUS_SUCCESS;

  XEvent* ev = NULL;
  result = state->object_table()->GetObject(
      event_handle, (XObject**)&ev);
  if (XSUCCEEDED(result)) {
    ev->Reset();
    ev->Release();
  }

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL NtCreateSemaphore_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(1);
  int32_t count = SHIM_GET_ARG_32(2);
  int32_t limit = SHIM_GET_ARG_32(3);

  XELOGD(
      "NtCreateSemaphore(%.8X, %.8X, %d, %d)",
      handle_ptr, obj_attributes_ptr, count, limit);

  XSemaphore* sem = new XSemaphore(state);
  sem->Initialize(count, limit);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    //sem->SetName(...);
  }

  if (handle_ptr) {
    SHIM_SET_MEM_32(handle_ptr, sem->handle());
  }

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}


void xeKeInitializeSemaphore(
    void* semaphore_ptr, int32_t count, int32_t limit) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  XSemaphore* sem = (XSemaphore*)XSemaphore::GetObject(
      state, semaphore_ptr, 5 /* SemaphoreObject */);
  assert_not_null(sem);
  if (!sem) {
    return;
  }

  sem->Initialize(count, limit);
}


SHIM_CALL KeInitializeSemaphore_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t semaphore_ref = SHIM_GET_ARG_32(0);
  int32_t count = SHIM_GET_ARG_32(1);
  int32_t limit = SHIM_GET_ARG_32(2);

  XELOGD(
      "KeInitializeSemaphore(%.8X, %d, %d)",
      semaphore_ref, count, limit);

  void* semaphore_ptr = SHIM_MEM_ADDR(semaphore_ref);
  xeKeInitializeSemaphore(semaphore_ptr, count, limit);
}


int32_t xeKeReleaseSemaphore(
    void* semaphore_ptr, int32_t increment, int32_t adjustment, bool wait) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  XSemaphore* sem = (XSemaphore*)XSemaphore::GetObject(state, semaphore_ptr);
  assert_not_null(sem);
  if (!sem) {
    return 0;
  }

  // TODO(benvanik): increment thread priority?
  // TODO(benvanik): wait?

  return sem->ReleaseSemaphore(adjustment);
}


SHIM_CALL KeReleaseSemaphore_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t semaphore_ref = SHIM_GET_ARG_32(0);
  int32_t increment = SHIM_GET_ARG_32(1);
  int32_t adjustment = SHIM_GET_ARG_32(2);
  int32_t wait = SHIM_GET_ARG_32(3);

  XELOGD(
      "KeReleaseSemaphore(%.8X, %d, %d, %d)",
      semaphore_ref, increment, adjustment, wait);

  void* semaphore_ptr = SHIM_MEM_ADDR(semaphore_ref);
  int32_t result = xeKeReleaseSemaphore(
      semaphore_ptr, increment, adjustment, wait == 1);

  SHIM_SET_RETURN_64(result);
}


SHIM_CALL NtReleaseSemaphore_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t sem_handle = SHIM_GET_ARG_32(0);
  int32_t release_count = SHIM_GET_ARG_32(1);
  int32_t previous_count_ptr = SHIM_GET_ARG_32(2);

  XELOGD(
      "NtReleaseSemaphore(%.8X, %d, %.8X)",
      sem_handle, release_count, previous_count_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  XSemaphore* sem = NULL;
  result = state->object_table()->GetObject(
      sem_handle, (XObject**)&sem);
  if (XSUCCEEDED(result)) {
    int32_t previous_count = sem->ReleaseSemaphore(release_count);
    sem->Release();

    if (previous_count_ptr) {
      SHIM_SET_MEM_32(previous_count_ptr, previous_count);
    }
  }

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL NtCreateMutant_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(1);
  uint32_t initial_owner = SHIM_GET_ARG_32(2);

  XELOGD(
      "NtCreateMutant(%.8X, %.8X, %.1X)",
      handle_ptr, obj_attributes_ptr, initial_owner);

  XMutant* mutant = new XMutant(state);
  mutant->Initialize(initial_owner ? true : false);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    //mutant->SetName(...);
  }

  if (handle_ptr) {
    SHIM_SET_MEM_32(handle_ptr, mutant->handle());
  }

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}


SHIM_CALL NtReleaseMutant_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t mutant_handle = SHIM_GET_ARG_32(0);
  int32_t unknown = SHIM_GET_ARG_32(1);
  // This doesn't seem to be supported.
  //int32_t previous_count_ptr = SHIM_GET_ARG_32(2);

  // Whatever arg 1 is all games seem to set it to 0, so whether it's
  // abandon or wait we just say false. Which is good, cause they are
  // both ignored.
  assert_zero(unknown);
  uint32_t priority_increment = 0;
  bool abandon = false;
  bool wait = false;

  XELOGD(
      "NtReleaseMutant(%.8X, %.8X)",
      mutant_handle, unknown);

  X_STATUS result = X_STATUS_SUCCESS;

  XMutant* mutant = NULL;
  result = state->object_table()->GetObject(
      mutant_handle, (XObject**)&mutant);
  if (XSUCCEEDED(result)) {
    result = mutant->ReleaseMutant(priority_increment, abandon, wait);
    mutant->Release();
  }

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL NtCreateTimer_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(1);
  uint32_t timer_type = SHIM_GET_ARG_32(2);

  // timer_type = NotificationTimer (0) or SynchronizationTimer (1)

  XELOGD(
      "NtCreateTimer(%.8X, %.8X, %.1X)",
      handle_ptr, obj_attributes_ptr, timer_type);

  XTimer* timer = new XTimer(state);
  timer->Initialize(timer_type);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    //timer->SetName(...);
  }

  if (handle_ptr) {
    SHIM_SET_MEM_32(handle_ptr, timer->handle());
  }

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}


SHIM_CALL NtSetTimerEx_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t timer_handle = SHIM_GET_ARG_32(0);
  uint32_t due_time_ptr = SHIM_GET_ARG_32(1);
  uint32_t routine = SHIM_GET_ARG_32(2); // PTIMERAPCROUTINE
  uint32_t unk_one = SHIM_GET_ARG_32(3);
  uint32_t routine_arg = SHIM_GET_ARG_32(4);
  uint32_t resume = SHIM_GET_ARG_32(5);
  uint32_t period_ms = SHIM_GET_ARG_32(6);
  uint32_t unk_zero = SHIM_GET_ARG_32(7);

  assert_true(unk_one == 1);
  assert_true(unk_zero == 0);

  uint64_t due_time = SHIM_MEM_64(due_time_ptr);

  XELOGD(
      "NtSetTimerEx(%.8X, %.8X(%lld), %.8X, %.8X, %.8X, %.1X, %d, %.8X)",
      timer_handle, due_time_ptr, due_time, routine, unk_one,
      routine_arg, resume, period_ms, unk_zero);

  X_STATUS result = X_STATUS_SUCCESS;

  XTimer* timer = NULL;
  result = state->object_table()->GetObject(
      timer_handle, (XObject**)&timer);
  if (XSUCCEEDED(result)) {
    result = timer->SetTimer(
        due_time, period_ms, routine, routine_arg, resume ? true : false);
    timer->Release();
  }

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL NtCancelTimer_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t timer_handle = SHIM_GET_ARG_32(0);
  uint32_t current_state_ptr = SHIM_GET_ARG_32(1);

  // UNVERIFIED
  DebugBreak();

  XELOGD(
      "NtCancelTimer(%.8X, %.8X)",
      timer_handle, current_state_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  XTimer* timer = NULL;
  result = state->object_table()->GetObject(
      timer_handle, (XObject**)&timer);
  if (XSUCCEEDED(result)) {
    result = timer->Cancel();
    timer->Release();

    if (current_state_ptr) {
      SHIM_SET_MEM_32(current_state_ptr, 0);
    }
  }

  SHIM_SET_RETURN_32(result);
}


X_STATUS xeKeWaitForSingleObject(
    void* object_ptr, uint32_t wait_reason, uint32_t processor_mode,
    uint32_t alertable, uint64_t* opt_timeout) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  XObject* object = XObject::GetObject(state, object_ptr);
  if (!object) {
    // The only kind-of failure code.
    return X_STATUS_ABANDONED_WAIT_0;
  }

  return object->Wait(wait_reason, processor_mode, alertable, opt_timeout);
}


SHIM_CALL KeWaitForSingleObject_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t object = SHIM_GET_ARG_32(0);
  uint32_t wait_reason = SHIM_GET_ARG_32(1);
  uint32_t processor_mode = SHIM_GET_ARG_32(2);
  uint32_t alertable = SHIM_GET_ARG_32(3);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(4);

  XELOGD(
      "KeWaitForSingleObject(%.8X, %.8X, %.8X, %.1X, %.8X)",
      object, wait_reason, processor_mode, alertable, timeout_ptr);

  void* object_ptr = SHIM_MEM_ADDR(object);
  uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
  X_STATUS result = xeKeWaitForSingleObject(
      object_ptr, wait_reason, processor_mode, alertable,
      timeout_ptr ? &timeout : NULL);

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL NtWaitForSingleObjectEx_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t object_handle = SHIM_GET_ARG_32(0);
  uint8_t wait_mode = SHIM_GET_ARG_8(1);
  uint32_t alertable = SHIM_GET_ARG_32(2);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(3);

  XELOGD(
      "NtWaitForSingleObjectEx(%.8X, %u, %.1X, %.8X)",
      object_handle, (uint32_t)wait_mode, alertable, timeout_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  XObject* object = NULL;
  result = state->object_table()->GetObject(
      object_handle, &object);
  if (XSUCCEEDED(result)) {
    uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
    result = object->Wait(
        3, wait_mode, alertable,
        timeout_ptr ? &timeout : NULL);
    object->Release();
  }

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL KeWaitForMultipleObjects_shim(
    PPCContext* ppc_state, KernelState* state) {
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
      count, objects_ptr, wait_type, wait_reason, processor_mode,
      alertable, timeout_ptr, wait_block_array_ptr);

  assert_true(wait_type >= 0 && wait_type <= 1);

  X_STATUS result = X_STATUS_SUCCESS;

  XObject** objects = (XObject**)alloca(sizeof(XObject*) * count);
  for (uint32_t n = 0; n < count; n++) {
    uint32_t object_ptr_ptr = SHIM_MEM_32(objects_ptr + n * 4);
    void* object_ptr = SHIM_MEM_ADDR(object_ptr_ptr);
    objects[n] = XObject::GetObject(state, object_ptr);
    if (!objects[n]) {
      SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
      return;
    }
  }

  uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
  result = XObject::WaitMultiple(
      count, objects,
      wait_type, wait_reason, processor_mode, alertable,
      timeout_ptr ? &timeout : NULL);

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL NtWaitForMultipleObjectsEx_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t count = SHIM_GET_ARG_32(0);
  uint32_t handles_ptr = SHIM_GET_ARG_32(1);
  uint32_t wait_type = SHIM_GET_ARG_32(2);
  uint8_t  wait_mode = SHIM_GET_ARG_8(3);
  uint32_t alertable = SHIM_GET_ARG_32(4);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(5);

  XELOGD(
      "NtWaitForMultipleObjectsEx(%d, %.8X, %.8X, %.8X, %.8X, %.8X)",
      count, handles_ptr, wait_type, wait_mode,
      alertable, timeout_ptr);

  assert_true(wait_type >= 0 && wait_type <= 1);

  X_STATUS result = X_STATUS_SUCCESS;

  XObject** objects = (XObject**)alloca(sizeof(XObject*) * count);
  for (uint32_t n = 0; n < count; n++) {
    uint32_t object_handle = SHIM_MEM_32(handles_ptr + n * 4);
    XObject* object = NULL;
    result = state->object_table()->GetObject(object_handle, &object);
    if (XFAILED(result)) {
      SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
      return;
    }
    objects[n] = object;
  }

  uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
  result = XObject::WaitMultiple(
    count, objects,
    wait_type, 6, wait_mode, alertable,
    timeout_ptr ? &timeout : NULL);

  SHIM_SET_RETURN_32(result);
}


SHIM_CALL NtSignalAndWaitForSingleObjectEx_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t signal_handle = SHIM_GET_ARG_32(0);
  uint32_t wait_handle = SHIM_GET_ARG_32(1);
  uint32_t alertable = SHIM_GET_ARG_32(2);
  uint32_t unk_3 = SHIM_GET_ARG_32(3);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(4);

  XELOGD(
      "NtSignalAndWaitForSingleObjectEx(%.8X, %.8X, %.1X, %.8X, %.8X)",
      signal_handle, wait_handle, alertable, unk_3,
      timeout_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  XObject* signal_object = NULL;
  XObject* wait_object = NULL;
  result = state->object_table()->GetObject(
      signal_handle, &signal_object);
  if (XSUCCEEDED(result)) {
    result = state->object_table()->GetObject(
        wait_handle, &wait_object);
  }
  if (XSUCCEEDED(result)) {
    uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
    result = XObject::SignalAndWait(
        signal_object, wait_object, 3, 1, alertable,
        timeout_ptr ? &timeout : NULL);
  }
  if (signal_object) {
    signal_object->Release();
  }
  if (wait_object) {
    wait_object->Release();
  }

  SHIM_SET_RETURN_32(result);
}


uint32_t xeKfAcquireSpinLock(uint32_t* lock_ptr) {
  // Lock.
  while (!poly::atomic_cas(0, 1, lock_ptr)) {
    // Spin!
    // TODO(benvanik): error on deadlock?
  }

  // Raise IRQL to DISPATCH.
  XThread* thread = XThread::GetCurrentThread();
  return thread->RaiseIrql(2);
}


SHIM_CALL KfAcquireSpinLock_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t lock_ptr = SHIM_GET_ARG_32(0);

  // XELOGD(
  //     "KfAcquireSpinLock(%.8X)",
  //     lock_ptr);

  auto lock = reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(lock_ptr));
  uint32_t old_irql = xeKfAcquireSpinLock(lock);

  SHIM_SET_RETURN_64(old_irql);
}


void xeKfReleaseSpinLock(uint32_t* lock_ptr, uint32_t old_irql) {
  // Restore IRQL.
  XThread* thread = XThread::GetCurrentThread();
  thread->LowerIrql(old_irql);

  // Unlock.
  poly::atomic_dec(lock_ptr);
}


SHIM_CALL KfReleaseSpinLock_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t lock_ptr = SHIM_GET_ARG_32(0);
  uint32_t old_irql = SHIM_GET_ARG_32(1);

  // XELOGD(
  //     "KfReleaseSpinLock(%.8X, %d)",
  //     lock_ptr,
  //     old_irql);

  xeKfReleaseSpinLock(reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(lock_ptr)),
                      old_irql);
}


SHIM_CALL KeAcquireSpinLockAtRaisedIrql_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t lock_ptr = SHIM_GET_ARG_32(0);

  // XELOGD(
  //     "KeAcquireSpinLockAtRaisedIrql(%.8X)",
  //     lock_ptr);

  // Lock.
  auto lock = reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(lock_ptr));
  while (!poly::atomic_cas(0, 1, lock)) {
    // Spin!
    // TODO(benvanik): error on deadlock?
  }
}


SHIM_CALL KeReleaseSpinLockFromRaisedIrql_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t lock_ptr = SHIM_GET_ARG_32(0);

  // XELOGD(
  //     "KeReleaseSpinLockFromRaisedIrql(%.8X)",
  //     lock_ptr);

  // Unlock.
  auto lock = reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(lock_ptr));
  poly::atomic_dec(lock);
}


void xeKeEnterCriticalRegion() {
  XThread::EnterCriticalRegion();
}


SHIM_CALL KeEnterCriticalRegion_shim(
    PPCContext* ppc_state, KernelState* state) {
  // XELOGD(
  //     "KeEnterCriticalRegion()");
  xeKeEnterCriticalRegion();
}


void xeKeLeaveCriticalRegion() {
  XThread::LeaveCriticalRegion();
}


SHIM_CALL KeLeaveCriticalRegion_shim(
    PPCContext* ppc_state, KernelState* state) {
  // XELOGD(
  //     "KeLeaveCriticalRegion()");
  xeKeLeaveCriticalRegion();
}


SHIM_CALL KeRaiseIrqlToDpcLevel_shim(
    PPCContext* ppc_state, KernelState* state) {
  // XELOGD(
  //     "KeRaiseIrqlToDpcLevel()");
  auto old_value = state->processor()->RaiseIrql(cpu::Irql::DPC);
  SHIM_SET_RETURN_32(old_value);
}


SHIM_CALL KfLowerIrql_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t old_value = SHIM_GET_ARG_32(0);
  // XELOGD(
  //     "KfLowerIrql(%d)",
  //     old_value);
  state->processor()->LowerIrql(static_cast<cpu::Irql>(old_value));
}


SHIM_CALL NtQueueApcThread_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t thread_handle = SHIM_GET_ARG_32(0);
  uint32_t apc_routine = SHIM_GET_ARG_32(1);
  uint32_t arg1 = SHIM_GET_ARG_32(2);
  uint32_t arg2 = SHIM_GET_ARG_32(3);
  uint32_t arg3 = SHIM_GET_ARG_32(4); // ?
  XELOGD(
      "NtQueueApcThread(%.8X, %.8X, %.8X, %.8X, %.8X)",
      thread_handle, apc_routine, arg1, arg2, arg3);

  // Alloc APC object (from somewhere) and insert.
}


SHIM_CALL KeInitializeApc_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t apc_ptr = SHIM_GET_ARG_32(0);
  uint32_t thread = SHIM_GET_ARG_32(1);
  uint32_t kernel_routine = SHIM_GET_ARG_32(2);
  uint32_t rundown_routine = SHIM_GET_ARG_32(3);
  uint32_t normal_routine = SHIM_GET_ARG_32(4);
  uint32_t processor_mode = SHIM_GET_ARG_32(5);
  uint32_t normal_context = SHIM_GET_ARG_32(6);

  XELOGD(
      "KeInitializeApc(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %.8X)",
      apc_ptr, thread, kernel_routine, rundown_routine, normal_routine,
      processor_mode, normal_context);

  // KAPC is 0x28(40) bytes? (what's passed to ExAllocatePoolWithTag)
  // This is 4b shorter than NT - looks like the reserved dword at +4 is gone
  uint32_t type = 18; // ApcObject
  uint32_t unk0 = 0;
  uint32_t size = 0x28;
  uint32_t unk1 = 0;
  SHIM_SET_MEM_32(apc_ptr + 0,
      (type << 24) | (unk0 << 16) | (size << 8) | (unk1));
  SHIM_SET_MEM_32(apc_ptr + 4, thread); // known offset - derefed by games
  SHIM_SET_MEM_32(apc_ptr + 8, 0); // flink
  SHIM_SET_MEM_32(apc_ptr + 12, 0); // blink
  SHIM_SET_MEM_32(apc_ptr + 16, kernel_routine);
  SHIM_SET_MEM_32(apc_ptr + 20, rundown_routine);
  SHIM_SET_MEM_32(apc_ptr + 24, normal_routine);
  SHIM_SET_MEM_32(apc_ptr + 28, normal_routine ? normal_context : 0);
  SHIM_SET_MEM_32(apc_ptr + 32, 0); // arg1
  SHIM_SET_MEM_32(apc_ptr + 36, 0); // arg2
  uint32_t state_index = 0;
  uint32_t inserted = 0;
  SHIM_SET_MEM_32(apc_ptr + 40,
      (state_index << 24) | (processor_mode << 16) | (inserted << 8));
}


SHIM_CALL KeInsertQueueApc_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t apc_ptr = SHIM_GET_ARG_32(0);
  uint32_t arg1 = SHIM_GET_ARG_32(1);
  uint32_t arg2 = SHIM_GET_ARG_32(2);
  uint32_t priority_increment = SHIM_GET_ARG_32(3);

  XELOGD(
      "KeInsertQueueApc(%.8X, %.8X, %.8X, %.8X)",
      apc_ptr, arg1, arg2, priority_increment);

  uint32_t thread_ptr = SHIM_MEM_32(apc_ptr + 4);
  XThread* thread = (XThread*)XObject::GetObject(
      state, SHIM_MEM_ADDR(thread_ptr));
  if (!thread) {
    SHIM_SET_RETURN_64(0);
    return;
  }

  // Lock thread.
  thread->LockApc();

  // Fail if already inserted.
  if (SHIM_MEM_32(apc_ptr + 40) & 0xFF00) {
    thread->UnlockApc();
    SHIM_SET_RETURN_64(0);
    return;
  }

  // Prep APC.
  SHIM_SET_MEM_32(apc_ptr + 32, arg1);
  SHIM_SET_MEM_32(apc_ptr + 36, arg2);
  SHIM_SET_MEM_32(apc_ptr + 40,
      (SHIM_MEM_32(apc_ptr + 40) & ~0xFF00) | (1 << 8));

  auto apc_list = thread->apc_list();

  uint32_t list_entry_ptr = apc_ptr + 8;
  apc_list->Insert(list_entry_ptr);

  // Unlock thread.
  thread->UnlockApc();

  SHIM_SET_RETURN_64(1);
}


SHIM_CALL KeRemoveQueueApc_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t apc_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "KeRemoveQueueApc(%.8X)",
      apc_ptr);

  bool result = false;

  uint32_t thread_ptr = SHIM_MEM_32(apc_ptr + 4);
  XThread* thread = (XThread*)XObject::GetObject(
      state, SHIM_MEM_ADDR(thread_ptr));
  if (!thread) {
    SHIM_SET_RETURN_64(0);
    return;
  }

  thread->LockApc();

  if (!(SHIM_MEM_32(apc_ptr + 40) & 0xFF00)) {
    thread->UnlockApc();
    SHIM_SET_RETURN_64(0);
    return;
  }

  auto apc_list = thread->apc_list();
  uint32_t list_entry_ptr = apc_ptr + 8;
  if (apc_list->IsQueued(list_entry_ptr)) {
    apc_list->Remove(list_entry_ptr);
    result = true;
  }

  thread->UnlockApc();

  SHIM_SET_RETURN_64(result ? 1 : 0);
}


SHIM_CALL KiApcNormalRoutineNop_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0); // output?
  uint32_t unk1 = SHIM_GET_ARG_32(1); // 0x13

  XELOGD(
      "KiApcNormalRoutineNop(%.8X, %.8X)",
      unk0, unk1);

  SHIM_SET_RETURN_64(0);
}


SHIM_CALL KeInitializeDpc_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t dpc_ptr = SHIM_GET_ARG_32(0);
  uint32_t routine = SHIM_GET_ARG_32(1);
  uint32_t context = SHIM_GET_ARG_32(2);

  XELOGD(
      "KeInitializeDpc(%.8X, %.8X, %.8X)",
      dpc_ptr, routine, context);

  // KDPC (maybe) 0x18 bytes?
  uint32_t type = 19; // DpcObject
  uint32_t importance = 0;
  uint32_t number = 0; // ?
  SHIM_SET_MEM_32(dpc_ptr + 0,
      (type << 24) | (importance << 16) | (number));
  SHIM_SET_MEM_32(dpc_ptr + 4, 0); // flink
  SHIM_SET_MEM_32(dpc_ptr + 8, 0); // blink
  SHIM_SET_MEM_32(dpc_ptr + 12, routine);
  SHIM_SET_MEM_32(dpc_ptr + 16, context);
  SHIM_SET_MEM_32(dpc_ptr + 20, 0); // arg1
  SHIM_SET_MEM_32(dpc_ptr + 24, 0); // arg2
}


SHIM_CALL KeInsertQueueDpc_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t dpc_ptr = SHIM_GET_ARG_32(0);
  uint32_t arg1 = SHIM_GET_ARG_32(1);
  uint32_t arg2 = SHIM_GET_ARG_32(2);

  XELOGD(
      "KeInsertQueueDpc(%.8X, %.8X, %.8X)",
      dpc_ptr, arg1, arg2);

  uint32_t list_entry_ptr = dpc_ptr + 4;

  // Lock dispatcher.
  auto dispatcher = state->dispatcher();
  dispatcher->Lock();

  auto dpc_list = dispatcher->dpc_list();

  // If already in a queue, abort.
  if (dpc_list->IsQueued(list_entry_ptr)) {
    SHIM_SET_RETURN_64(0);
    dispatcher->Unlock();
    return;
  }

  // Prep DPC.
  SHIM_SET_MEM_32(dpc_ptr + 20, arg1);
  SHIM_SET_MEM_32(dpc_ptr + 24, arg2);

  dpc_list->Insert(list_entry_ptr);

  dispatcher->Unlock();

  SHIM_SET_RETURN_64(1);
}


SHIM_CALL KeRemoveQueueDpc_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t dpc_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "KeRemoveQueueDpc(%.8X)",
      dpc_ptr);

  bool result = false;

  uint32_t list_entry_ptr = dpc_ptr + 4;

  auto dispatcher = state->dispatcher();
  dispatcher->Lock();

  auto dpc_list = dispatcher->dpc_list();
  if (dpc_list->IsQueued(list_entry_ptr)) {
    dpc_list->Remove(list_entry_ptr);
    result = true;
  }

  dispatcher->Unlock();

  SHIM_SET_RETURN_64(result ? 1 : 0);
}



}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterThreadingExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", ExCreateThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", ExTerminateThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtResumeThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeResumeThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtSuspendThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeSetAffinityThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeQueryBasePriorityThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeSetBasePriorityThread, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeGetCurrentProcessType, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeQueryPerformanceFrequency, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeDelayExecutionThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtYieldExecution, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeQuerySystemTime, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsAlloc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsFree, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsGetValue, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsSetValue, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", NtCreateEvent, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeSetEvent, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtSetEvent, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KePulseEvent, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtPulseEvent, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeResetEvent, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtClearEvent, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", NtCreateSemaphore, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeInitializeSemaphore, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeReleaseSemaphore, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtReleaseSemaphore, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", NtCreateMutant, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtReleaseMutant, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", NtCreateTimer, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtSetTimerEx, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtCancelTimer, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeWaitForSingleObject, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtWaitForSingleObjectEx, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeWaitForMultipleObjects, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtWaitForMultipleObjectsEx, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtSignalAndWaitForSingleObjectEx, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KfAcquireSpinLock, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KfReleaseSpinLock, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeAcquireSpinLockAtRaisedIrql, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeReleaseSpinLockFromRaisedIrql, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeEnterCriticalRegion, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeLeaveCriticalRegion, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeRaiseIrqlToDpcLevel, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KfLowerIrql, state);

  //SHIM_SET_MAPPING("xboxkrnl.exe", NtQueueApcThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeInitializeApc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeInsertQueueApc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeRemoveQueueApc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KiApcNormalRoutineNop, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeInitializeDpc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeInsertQueueDpc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeRemoveQueueDpc, state);
}

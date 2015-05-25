/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/atomic.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/dispatcher.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/native_list.h"
#include "xenia/kernel/objects/xevent.h"
#include "xenia/kernel/objects/xmutant.h"
#include "xenia/kernel/objects/xsemaphore.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/kernel/objects/xtimer.h"
#include "xenia/kernel/objects/xuser_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/xbox.h"

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

void AssertNoNameCollision(KernelState* state, uint32_t obj_attributes_ptr) {
  // If the name exists and its type matches, we can return that (ref+1)
  // with a success of NAME_EXISTS.
  // If the name exists and its type doesn't match, we do NAME_COLLISION.
  // Otherwise, we add like normal.
  if (!obj_attributes_ptr) {
    return;
  }
  uint32_t name_str_ptr = xe::load_and_swap<uint32_t>(
      state->memory()->TranslateVirtual(obj_attributes_ptr + 4));
  if (name_str_ptr) {
    X_ANSI_STRING name_str(state->memory()->virtual_membase(), name_str_ptr);
    auto name = name_str.to_string();
    X_HANDLE handle = X_INVALID_HANDLE_VALUE;
    X_RESULT result = state->object_table()->GetObjectByName(name, &handle);
    if (XSUCCEEDED(result)) {
      // Found something!
      assert_always("Existing names not implemented");
    }
  }
}

SHIM_CALL ExCreateThread_shim(PPCContext* ppc_state, KernelState* state) {
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
    stack_size = state->GetExecutableModule()->xex_header()->exe_stack_size;
  }

  // Stack must be aligned to 16kb pages
  stack_size = std::max((uint32_t)0x4000, ((stack_size + 0xFFF) & 0xFFFFF000));

  auto thread = object_ref<XThread>(
      new XThread(state, stack_size, xapi_thread_startup, start_address,
                  start_context, creation_flags));

  X_STATUS result = thread->Create();
  if (XFAILED(result)) {
    // Failed!
    XELOGE("Thread creation failed: %.8X", result);
    SHIM_SET_RETURN_32(result);
    return;
  }

  if (XSUCCEEDED(result)) {
    if (handle_ptr) {
      SHIM_SET_MEM_32(handle_ptr, thread->handle());
    }
    if (thread_id_ptr) {
      SHIM_SET_MEM_32(thread_id_ptr, thread->thread_id());
    }
  }
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL ExTerminateThread_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t exit_code = SHIM_GET_ARG_32(0);

  XELOGD("ExTerminateThread(%d)", exit_code);

  XThread* thread = XThread::GetCurrentThread();

  // NOTE: this kills us right now. We won't return from it.
  X_STATUS result = thread->Exit(exit_code);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtResumeThread_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t suspend_count_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NtResumeThread(%.8X, %.8X)", handle, suspend_count_ptr);

  X_RESULT result = X_STATUS_INVALID_HANDLE;
  uint32_t suspend_count = 0;

  auto thread = state->object_table()->LookupObject<XThread>(handle);
  if (thread) {
    result = thread->Resume(&suspend_count);
  }
  if (suspend_count_ptr) {
    SHIM_SET_MEM_32(suspend_count_ptr, suspend_count);
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL KeResumeThread_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);

  XELOGD("KeResumeThread(%.8X)", thread_ptr);

  X_STATUS result = X_STATUS_SUCCESS;
  auto thread =
      XObject::GetNativeObject<XThread>(state, SHIM_MEM_ADDR(thread_ptr));
  if (thread) {
    result = thread->Resume();
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtSuspendThread_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t suspend_count_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NtSuspendThread(%.8X, %.8X)", handle, suspend_count_ptr);

  X_RESULT result = X_STATUS_SUCCESS;
  uint32_t suspend_count = 0;

  auto thread = state->object_table()->LookupObject<XThread>(handle);
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

SHIM_CALL KeSetAffinityThread_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);
  uint32_t affinity = SHIM_GET_ARG_32(1);

  XELOGD("KeSetAffinityThread(%.8X, %.8X)", thread_ptr, affinity);

  auto thread =
      XObject::GetNativeObject<XThread>(state, SHIM_MEM_ADDR(thread_ptr));
  if (thread) {
    thread->SetAffinity(affinity);
  }

  SHIM_SET_RETURN_32(affinity);
}

SHIM_CALL KeQueryBasePriorityThread_shim(PPCContext* ppc_state,
                                         KernelState* state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);

  XELOGD("KeQueryBasePriorityThread(%.8X)", thread_ptr);

  int32_t priority = 0;

  auto thread =
      XObject::GetNativeObject<XThread>(state, SHIM_MEM_ADDR(thread_ptr));
  if (thread) {
    priority = thread->QueryPriority();
  }

  SHIM_SET_RETURN_32(priority);
}

SHIM_CALL KeSetBasePriorityThread_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);
  uint32_t increment = SHIM_GET_ARG_32(1);

  XELOGD("KeSetBasePriorityThread(%.8X, %.8X)", thread_ptr, increment);

  int32_t prev_priority = 0;

  object_ref<XThread> thread;
  if (thread_ptr < 0x1000) {
    // They passed in a handle (for some reason)
    thread = state->object_table()->LookupObject<XThread>(thread_ptr);

    // Log it in case this is the source of any problems in the future
    XELOGD("KeSetBasePriorityThread - Interpreting thread ptr as handle!");
  } else {
    thread =
        XObject::GetNativeObject<XThread>(state, SHIM_MEM_ADDR(thread_ptr));
  }

  if (thread) {
    prev_priority = thread->QueryPriority();
    thread->SetPriority(increment);
  }

  SHIM_SET_RETURN_32(prev_priority);
}

SHIM_CALL KeSetDisableBoostThread_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t thread_ptr = SHIM_GET_ARG_32(0);
  uint32_t disabled = SHIM_GET_ARG_32(1);

  XELOGD("KeSetDisableBoostThread(%.8X, %.8X)", thread_ptr, disabled);

  auto thread =
      XObject::GetNativeObject<XThread>(state, SHIM_MEM_ADDR(thread_ptr));
  if (thread) {
    // Uhm?
  }

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL KeGetCurrentProcessType_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  // XELOGD(
  //     "KeGetCurrentProcessType()");

  // DWORD

  SHIM_SET_RETURN_64(state->process_type());
}

SHIM_CALL KeSetCurrentProcessType_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t type = SHIM_GET_ARG_32(0);
  // One of X_PROCTYPE_?

  XELOGD("KeSetCurrentProcessType(%d)", type);

  assert_true(type >= 0 && type <= 2);

  state->set_process_type(type);
}

SHIM_CALL KeQueryPerformanceFrequency_shim(PPCContext* ppc_state,
                                           KernelState* state) {
  // XELOGD(
  //     "KeQueryPerformanceFrequency()");

  // TODO(benvanik): return fixed 50000000?

  uint64_t result = 0;
  LARGE_INTEGER frequency;
  if (QueryPerformanceFrequency(&frequency)) {
    result = frequency.QuadPart;
  }
  SHIM_SET_RETURN_64(result);
}

SHIM_CALL KeDelayExecutionThread_shim(PPCContext* ppc_state,
                                      KernelState* state) {
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

SHIM_CALL NtYieldExecution_shim(PPCContext* ppc_state, KernelState* state) {
  // XELOGD("NtYieldExecution()");
  XThread* thread = XThread::GetCurrentThread();
  X_STATUS result = thread->Delay(0, 0, 0);
  SHIM_SET_RETURN_64(0);
}

SHIM_CALL KeQuerySystemTime_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t time_ptr = SHIM_GET_ARG_32(0);

  XELOGD("KeQuerySystemTime(%.8X)", time_ptr);

  FILETIME t;
  GetSystemTimeAsFileTime(&t);
  uint64_t time = ((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime;

  if (time_ptr) {
    SHIM_SET_MEM_64(time_ptr, time);
  }
}

// The TLS system used here is a bit hacky, but seems to work.
// Both Win32 and pthreads use unsigned longs as TLS indices, so we can map
// right into the system for these calls. We're just round tripping the IDs and
// hoping for the best.

// http://msdn.microsoft.com/en-us/library/ms686801
SHIM_CALL KeTlsAlloc_shim(PPCContext* ppc_state, KernelState* state) {
  XELOGD("KeTlsAlloc()");

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

  SHIM_SET_RETURN_64(tls_index);
}

// http://msdn.microsoft.com/en-us/library/ms686804
SHIM_CALL KeTlsFree_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);

  XELOGD("KeTlsFree(%.8X)", tls_index);

  if (tls_index == X_TLS_OUT_OF_INDEXES) {
    SHIM_SET_RETURN_64(0);
    return;
  }

  int result = 0;

#if XE_PLATFORM_WIN32
  result = TlsFree(tls_index);
#else
  result = pthread_key_delete(tls_index) == 0;
#endif  // WIN32

  SHIM_SET_RETURN_64(result);
}

// http://msdn.microsoft.com/en-us/library/ms686812
SHIM_CALL KeTlsGetValue_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);

  // Logging disabled, as some games spam this.
  // XELOGD(
  //    "KeTlsGetValue(%.8X)",
  //    tls_index);

  uint64_t value = 0;

#if XE_PLATFORM_WIN32
  value = (uint64_t)TlsGetValue(tls_index);
#else
  value = (uint64_t)pthread_getspecific(tls_index);
#endif  // WIN32

  if (!value) {
    // XELOGW("KeTlsGetValue should SetLastError if result is NULL");
    // TODO(benvanik): SetLastError
  }

  SHIM_SET_RETURN_64(value);
}

// http://msdn.microsoft.com/en-us/library/ms686818
SHIM_CALL KeTlsSetValue_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);
  uint32_t tls_value = SHIM_GET_ARG_32(1);

  XELOGD("KeTlsSetValue(%.8X, %.8X)", tls_index, tls_value);

  int result = 0;

#if XE_PLATFORM_WIN32
  result = TlsSetValue(
      tls_index, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(tls_value)));
#else
  result = pthread_setspecific(tls_index, (void*)tls_value) == 0;
#endif  // WIN32

  SHIM_SET_RETURN_64(result);
}

SHIM_CALL NtCreateEvent_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(1);
  uint32_t event_type = SHIM_GET_ARG_32(2);
  uint32_t initial_state = SHIM_GET_ARG_32(3);

  XELOGD("NtCreateEvent(%.8X, %.8X, %d, %d)", handle_ptr, obj_attributes_ptr,
         event_type, initial_state);

  // TODO(benvanik): check for name collision. May return existing object if
  // type matches.
  AssertNoNameCollision(state, obj_attributes_ptr);

  XEvent* ev = new XEvent(state);
  ev->Initialize(!event_type, !!initial_state);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    ev->SetAttributes(SHIM_MEM_ADDR(obj_attributes_ptr));
  }

  if (handle_ptr) {
    SHIM_SET_MEM_32(handle_ptr, ev->handle());
  }
  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL KeSetEvent_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t event_ref = SHIM_GET_ARG_32(0);
  uint32_t increment = SHIM_GET_ARG_32(1);
  uint32_t wait = SHIM_GET_ARG_32(2);

  XELOGD("KeSetEvent(%.8X, %.8X, %.8X)", event_ref, increment, wait);

  void* event_ptr = SHIM_MEM_ADDR(event_ref);

  auto ev = XObject::GetNativeObject<XEvent>(state, event_ptr);
  if (!ev) {
    SHIM_SET_RETURN_64(0);
    return;
  }

  auto result = ev->Set(increment, !!wait);
  SHIM_SET_RETURN_64(result);
}

SHIM_CALL NtSetEvent_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t event_handle = SHIM_GET_ARG_32(0);
  uint32_t previous_state_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NtSetEvent(%.8X, %.8X)", event_handle, previous_state_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  auto ev = state->object_table()->LookupObject<XEvent>(event_handle);
  if (ev) {
    int32_t was_signalled = ev->Set(0, false);
    if (previous_state_ptr) {
      SHIM_SET_MEM_32(previous_state_ptr, was_signalled);
    }
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL KePulseEvent_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t event_ref = SHIM_GET_ARG_32(0);
  uint32_t increment = SHIM_GET_ARG_32(1);
  uint32_t wait = SHIM_GET_ARG_32(2);

  XELOGD("KePulseEvent(%.8X, %.8X, %.8X)", event_ref, increment, wait);

  int32_t result = 0;

  void* event_ptr = SHIM_MEM_ADDR(event_ref);
  auto ev = XObject::GetNativeObject<XEvent>(state, event_ptr);
  if (ev) {
    result = ev->Pulse(increment, !!wait);
  }

  SHIM_SET_RETURN_64(result);
}

SHIM_CALL NtPulseEvent_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t event_handle = SHIM_GET_ARG_32(0);
  uint32_t previous_state_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NtPulseEvent(%.8X, %.8X)", event_handle, previous_state_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  auto ev = state->object_table()->LookupObject<XEvent>(event_handle);
  if (ev) {
    int32_t was_signalled = ev->Pulse(0, false);
    if (previous_state_ptr) {
      SHIM_SET_MEM_32(previous_state_ptr, was_signalled);
    }
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL KeResetEvent_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t event_ref = SHIM_GET_ARG_32(0);

  XELOGD("KeResetEvent(%.8X)", event_ref);

  void* event_ptr = SHIM_MEM_ADDR(event_ref);
  auto ev = XObject::GetNativeObject<XEvent>(state, event_ptr);
  if (!ev) {
    SHIM_SET_RETURN_64(0);
    return;
  }

  auto result = ev->Reset();
  SHIM_SET_RETURN_64(result);
}

SHIM_CALL NtClearEvent_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t event_handle = SHIM_GET_ARG_32(0);

  XELOGD("NtClearEvent(%.8X)", event_handle);

  X_STATUS result = X_STATUS_SUCCESS;

  auto ev = state->object_table()->LookupObject<XEvent>(event_handle);
  if (ev) {
    ev->Reset();
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtCreateSemaphore_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(1);
  int32_t count = SHIM_GET_ARG_32(2);
  int32_t limit = SHIM_GET_ARG_32(3);

  XELOGD("NtCreateSemaphore(%.8X, %.8X, %d, %d)", handle_ptr,
         obj_attributes_ptr, count, limit);

  // TODO(benvanik): check for name collision. May return existing object if
  // type matches.
  if (obj_attributes_ptr) {
    AssertNoNameCollision(state, obj_attributes_ptr);
  }

  auto sem = object_ref<XSemaphore>(new XSemaphore(state));
  sem->Initialize(count, limit);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    sem->SetAttributes(SHIM_MEM_ADDR(obj_attributes_ptr));
  }

  if (handle_ptr) {
    SHIM_SET_MEM_32(handle_ptr, sem->handle());
  }

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL KeInitializeSemaphore_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  uint32_t semaphore_ref = SHIM_GET_ARG_32(0);
  int32_t count = SHIM_GET_ARG_32(1);
  int32_t limit = SHIM_GET_ARG_32(2);

  XELOGD("KeInitializeSemaphore(%.8X, %d, %d)", semaphore_ref, count, limit);

  void* semaphore_ptr = SHIM_MEM_ADDR(semaphore_ref);
  auto sem = XObject::GetNativeObject<XSemaphore>(state, semaphore_ptr,
                                                  5 /* SemaphoreObject */);
  if (!sem) {
    return;
  }

  sem->Initialize(count, limit);
}

SHIM_CALL KeReleaseSemaphore_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t semaphore_ref = SHIM_GET_ARG_32(0);
  int32_t increment = SHIM_GET_ARG_32(1);
  int32_t adjustment = SHIM_GET_ARG_32(2);
  int32_t wait = SHIM_GET_ARG_32(3);

  XELOGD("KeReleaseSemaphore(%.8X, %d, %d, %d)", semaphore_ref, increment,
         adjustment, wait);

  void* semaphore_ptr = SHIM_MEM_ADDR(semaphore_ref);
  auto sem = XObject::GetNativeObject<XSemaphore>(state, semaphore_ptr);
  if (!sem) {
    SHIM_SET_RETURN_64(0);
    return;
  }

  // TODO(benvanik): increment thread priority?
  // TODO(benvanik): wait?

  int32_t result = sem->ReleaseSemaphore(adjustment);
  SHIM_SET_RETURN_64(result);
}

SHIM_CALL NtReleaseSemaphore_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t sem_handle = SHIM_GET_ARG_32(0);
  int32_t release_count = SHIM_GET_ARG_32(1);
  int32_t previous_count_ptr = SHIM_GET_ARG_32(2);

  XELOGD("NtReleaseSemaphore(%.8X, %d, %.8X)", sem_handle, release_count,
         previous_count_ptr);

  X_STATUS result = X_STATUS_SUCCESS;
  int32_t previous_count = 0;

  auto sem = state->object_table()->LookupObject<XSemaphore>(sem_handle);
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

SHIM_CALL NtCreateMutant_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(1);
  uint32_t initial_owner = SHIM_GET_ARG_32(2);

  XELOGD("NtCreateMutant(%.8X, %.8X, %.1X)", handle_ptr, obj_attributes_ptr,
         initial_owner);

  // TODO(benvanik): check for name collision. May return existing object if
  // type matches.
  if (obj_attributes_ptr) {
    AssertNoNameCollision(state, obj_attributes_ptr);
  }

  XMutant* mutant = new XMutant(state);
  mutant->Initialize(initial_owner ? true : false);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    mutant->SetAttributes(SHIM_MEM_ADDR(obj_attributes_ptr));
  }

  if (handle_ptr) {
    SHIM_SET_MEM_32(handle_ptr, mutant->handle());
  }

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL NtReleaseMutant_shim(PPCContext* ppc_state, KernelState* state) {
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

  auto mutant = state->object_table()->LookupObject<XMutant>(mutant_handle);
  if (mutant) {
    result = mutant->ReleaseMutant(priority_increment, abandon, wait);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtCreateTimer_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(1);
  uint32_t timer_type = SHIM_GET_ARG_32(2);

  // timer_type = NotificationTimer (0) or SynchronizationTimer (1)

  XELOGD("NtCreateTimer(%.8X, %.8X, %.1X)", handle_ptr, obj_attributes_ptr,
         timer_type);

  // TODO(benvanik): check for name collision. May return existing object if
  // type matches.
  if (obj_attributes_ptr) {
    AssertNoNameCollision(state, obj_attributes_ptr);
  }

  XTimer* timer = new XTimer(state);
  timer->Initialize(timer_type);

  // obj_attributes may have a name inside of it, if != NULL.
  if (obj_attributes_ptr) {
    timer->SetAttributes(SHIM_MEM_ADDR(obj_attributes_ptr));
  }

  if (handle_ptr) {
    SHIM_SET_MEM_32(handle_ptr, timer->handle());
  }

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL NtSetTimerEx_shim(PPCContext* ppc_state, KernelState* state) {
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

  auto timer = state->object_table()->LookupObject<XTimer>(timer_handle);
  if (timer) {
    result = timer->SetTimer(due_time, period_ms, routine, routine_arg,
                             resume ? true : false);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtCancelTimer_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t timer_handle = SHIM_GET_ARG_32(0);
  uint32_t current_state_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NtCancelTimer(%.8X, %.8X)", timer_handle, current_state_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  auto timer = state->object_table()->LookupObject<XTimer>(timer_handle);
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

SHIM_CALL KeWaitForSingleObject_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  uint32_t object_ptr = SHIM_GET_ARG_32(0);
  uint32_t wait_reason = SHIM_GET_ARG_32(1);
  uint32_t processor_mode = SHIM_GET_ARG_32(2);
  uint32_t alertable = SHIM_GET_ARG_32(3);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(4);

  XELOGD("KeWaitForSingleObject(%.8X, %.8X, %.8X, %.1X, %.8X)", object_ptr,
         wait_reason, processor_mode, alertable, timeout_ptr);

  auto object =
      XObject::GetNativeObject<XObject>(state, SHIM_MEM_ADDR(object_ptr));
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

SHIM_CALL NtWaitForSingleObjectEx_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t object_handle = SHIM_GET_ARG_32(0);
  uint8_t wait_mode = SHIM_GET_ARG_8(1);
  uint32_t alertable = SHIM_GET_ARG_32(2);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(3);

  XELOGD("NtWaitForSingleObjectEx(%.8X, %u, %.1X, %.8X)", object_handle,
         (uint32_t)wait_mode, alertable, timeout_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  auto object = state->object_table()->LookupObject<XObject>(object_handle);
  if (object) {
    uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
    result =
        object->Wait(3, wait_mode, alertable, timeout_ptr ? &timeout : nullptr);
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL KeWaitForMultipleObjects_shim(PPCContext* ppc_state,
                                        KernelState* state) {
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

  assert_true(wait_type >= 0 && wait_type <= 1);

  X_STATUS result = X_STATUS_SUCCESS;

  std::vector<object_ref<XObject>> objects(count);
  for (uint32_t n = 0; n < count; n++) {
    uint32_t object_ptr_ptr = SHIM_MEM_32(objects_ptr + n * 4);
    void* object_ptr = SHIM_MEM_ADDR(object_ptr_ptr);
    objects[n] = XObject::GetNativeObject<XObject>(state, object_ptr);
    if (!objects[n]) {
      SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
      return;
    }
  }

  uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
  result = XObject::WaitMultiple(
      count, reinterpret_cast<XObject**>(objects.data()), wait_type,
      wait_reason, processor_mode, alertable, timeout_ptr ? &timeout : nullptr);

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtWaitForMultipleObjectsEx_shim(PPCContext* ppc_state,
                                          KernelState* state) {
  uint32_t count = SHIM_GET_ARG_32(0);
  uint32_t handles_ptr = SHIM_GET_ARG_32(1);
  uint32_t wait_type = SHIM_GET_ARG_32(2);
  uint8_t wait_mode = SHIM_GET_ARG_8(3);
  uint32_t alertable = SHIM_GET_ARG_32(4);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(5);

  XELOGD("NtWaitForMultipleObjectsEx(%d, %.8X, %.8X, %.8X, %.8X, %.8X)", count,
         handles_ptr, wait_type, wait_mode, alertable, timeout_ptr);

  assert_true(wait_type >= 0 && wait_type <= 1);

  X_STATUS result = X_STATUS_SUCCESS;

  std::vector<object_ref<XObject>> objects(count);
  for (uint32_t n = 0; n < count; n++) {
    uint32_t object_handle = SHIM_MEM_32(handles_ptr + n * 4);
    auto object = state->object_table()->LookupObject<XObject>(object_handle);
    if (!object) {
      SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
      return;
    }
    objects[n] = std::move(object);
  }

  uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
  result = XObject::WaitMultiple(
      count, reinterpret_cast<XObject**>(objects.data()), wait_type, 6,
      wait_mode, alertable, timeout_ptr ? &timeout : nullptr);

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtSignalAndWaitForSingleObjectEx_shim(PPCContext* ppc_state,
                                                KernelState* state) {
  uint32_t signal_handle = SHIM_GET_ARG_32(0);
  uint32_t wait_handle = SHIM_GET_ARG_32(1);
  uint32_t alertable = SHIM_GET_ARG_32(2);
  uint32_t unk_3 = SHIM_GET_ARG_32(3);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(4);

  XELOGD("NtSignalAndWaitForSingleObjectEx(%.8X, %.8X, %.1X, %.8X, %.8X)",
         signal_handle, wait_handle, alertable, unk_3, timeout_ptr);

  X_STATUS result = X_STATUS_SUCCESS;

  auto signal_object =
      state->object_table()->LookupObject<XObject>(signal_handle);
  auto wait_object = state->object_table()->LookupObject<XObject>(wait_handle);
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

SHIM_CALL KfAcquireSpinLock_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t lock_ptr = SHIM_GET_ARG_32(0);

  // XELOGD(
  //     "KfAcquireSpinLock(%.8X)",
  //     lock_ptr);

  // Lock.
  auto lock = reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(lock_ptr));
  while (!xe::atomic_cas(0, 1, lock)) {
    // Spin!
    // TODO(benvanik): error on deadlock?
  }

  // Raise IRQL to DISPATCH.
  XThread* thread = XThread::GetCurrentThread();
  auto old_irql = thread->RaiseIrql(2);

  SHIM_SET_RETURN_64(old_irql);
}

SHIM_CALL KfReleaseSpinLock_shim(PPCContext* ppc_state, KernelState* state) {
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

SHIM_CALL KeAcquireSpinLockAtRaisedIrql_shim(PPCContext* ppc_state,
                                             KernelState* state) {
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

SHIM_CALL KeReleaseSpinLockFromRaisedIrql_shim(PPCContext* ppc_state,
                                               KernelState* state) {
  uint32_t lock_ptr = SHIM_GET_ARG_32(0);

  // XELOGD(
  //     "KeReleaseSpinLockFromRaisedIrql(%.8X)",
  //     lock_ptr);

  // Unlock.
  auto lock = reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(lock_ptr));
  xe::atomic_dec(lock);
}

SHIM_CALL KeEnterCriticalRegion_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  // XELOGD(
  //     "KeEnterCriticalRegion()");
  XThread::EnterCriticalRegion();
}

SHIM_CALL KeLeaveCriticalRegion_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  // XELOGD(
  //     "KeLeaveCriticalRegion()");
  XThread::LeaveCriticalRegion();
}

SHIM_CALL KeRaiseIrqlToDpcLevel_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  // XELOGD(
  //     "KeRaiseIrqlToDpcLevel()");
  auto old_value = state->processor()->RaiseIrql(cpu::Irql::DPC);
  SHIM_SET_RETURN_32(old_value);
}

SHIM_CALL KfLowerIrql_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t old_value = SHIM_GET_ARG_32(0);
  // XELOGD(
  //     "KfLowerIrql(%d)",
  //     old_value);
  state->processor()->LowerIrql(static_cast<cpu::Irql>(old_value));
}

SHIM_CALL NtQueueApcThread_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t thread_handle = SHIM_GET_ARG_32(0);
  uint32_t apc_routine = SHIM_GET_ARG_32(1);
  uint32_t arg1 = SHIM_GET_ARG_32(2);
  uint32_t arg2 = SHIM_GET_ARG_32(3);
  uint32_t arg3 = SHIM_GET_ARG_32(4);  // ?
  XELOGD("NtQueueApcThread(%.8X, %.8X, %.8X, %.8X, %.8X)", thread_handle,
         apc_routine, arg1, arg2, arg3);

  // Alloc APC object (from somewhere) and insert.
}

SHIM_CALL KeInitializeApc_shim(PPCContext* ppc_state, KernelState* state) {
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

  // KAPC is 0x28(40) bytes? (what's passed to ExAllocatePoolWithTag)
  // This is 4b shorter than NT - looks like the reserved dword at +4 is gone
  uint32_t type = 18;  // ApcObject
  uint32_t unk0 = 0;
  uint32_t size = 0x28;
  uint32_t unk1 = 0;
  SHIM_SET_MEM_32(apc_ptr + 0,
                  (type << 24) | (unk0 << 16) | (size << 8) | (unk1));
  SHIM_SET_MEM_32(apc_ptr + 4, thread);  // known offset - derefed by games
  SHIM_SET_MEM_32(apc_ptr + 8, 0);       // flink
  SHIM_SET_MEM_32(apc_ptr + 12, 0);      // blink
  SHIM_SET_MEM_32(apc_ptr + 16, kernel_routine);
  SHIM_SET_MEM_32(apc_ptr + 20, rundown_routine);
  SHIM_SET_MEM_32(apc_ptr + 24, normal_routine);
  SHIM_SET_MEM_32(apc_ptr + 28, normal_routine ? normal_context : 0);
  SHIM_SET_MEM_32(apc_ptr + 32, 0);  // arg1
  SHIM_SET_MEM_32(apc_ptr + 36, 0);  // arg2
  uint32_t state_index = 0;
  uint32_t inserted = 0;
  SHIM_SET_MEM_32(apc_ptr + 40, (state_index << 24) | (processor_mode << 16) |
                                    (inserted << 8));
}

SHIM_CALL KeInsertQueueApc_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t apc_ptr = SHIM_GET_ARG_32(0);
  uint32_t arg1 = SHIM_GET_ARG_32(1);
  uint32_t arg2 = SHIM_GET_ARG_32(2);
  uint32_t priority_increment = SHIM_GET_ARG_32(3);

  XELOGD("KeInsertQueueApc(%.8X, %.8X, %.8X, %.8X)", apc_ptr, arg1, arg2,
         priority_increment);

  uint32_t thread_ptr = SHIM_MEM_32(apc_ptr + 4);
  auto thread =
      XObject::GetNativeObject<XThread>(state, SHIM_MEM_ADDR(thread_ptr));
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

SHIM_CALL KeRemoveQueueApc_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t apc_ptr = SHIM_GET_ARG_32(0);

  XELOGD("KeRemoveQueueApc(%.8X)", apc_ptr);

  bool result = false;

  uint32_t thread_ptr = SHIM_MEM_32(apc_ptr + 4);
  auto thread =
      XObject::GetNativeObject<XThread>(state, SHIM_MEM_ADDR(thread_ptr));
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

SHIM_CALL KiApcNormalRoutineNop_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);  // output?
  uint32_t unk1 = SHIM_GET_ARG_32(1);  // 0x13

  XELOGD("KiApcNormalRoutineNop(%.8X, %.8X)", unk0, unk1);

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL KeInitializeDpc_shim(PPCContext* ppc_state, KernelState* state) {
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

SHIM_CALL KeInsertQueueDpc_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t dpc_ptr = SHIM_GET_ARG_32(0);
  uint32_t arg1 = SHIM_GET_ARG_32(1);
  uint32_t arg2 = SHIM_GET_ARG_32(2);

  assert_always("DPC does not dispatch yet; going to hang!");

  XELOGD("KeInsertQueueDpc(%.8X, %.8X, %.8X)", dpc_ptr, arg1, arg2);

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

SHIM_CALL KeRemoveQueueDpc_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t dpc_ptr = SHIM_GET_ARG_32(0);

  XELOGD("KeRemoveQueueDpc(%.8X)", dpc_ptr);

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

std::mutex global_list_mutex_;

// http://www.nirsoft.net/kernel_struct/vista/SLIST_HEADER.html
SHIM_CALL InterlockedPopEntrySList_shim(PPCContext* ppc_state,
                                        KernelState* state) {
  uint32_t plist_ptr = SHIM_GET_ARG_32(0);

  XELOGD("InterlockedPopEntrySList(%.8X)", plist_ptr);

  std::lock_guard<std::mutex> lock(global_list_mutex_);

  uint8_t* p = state->memory()->TranslateVirtual(plist_ptr);
  auto first = xe::load_and_swap<uint32_t>(p);
  if (first == 0) {
    // List empty!
    SHIM_SET_RETURN_32(0);
    return;
  }

  uint8_t* p2 = state->memory()->TranslateVirtual(first);
  auto second = xe::load_and_swap<uint32_t>(p2);

  // Now drop the first element
  xe::store_and_swap<uint32_t>(p, second);

  // Return the one we popped
  SHIM_SET_RETURN_32(first);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterThreadingExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
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

  // SHIM_SET_MAPPING("xboxkrnl.exe", NtQueueApcThread, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeInitializeApc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeInsertQueueApc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeRemoveQueueApc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KiApcNormalRoutineNop, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeInitializeDpc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeInsertQueueDpc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeRemoveQueueDpc, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", InterlockedPopEntrySList, state);
}

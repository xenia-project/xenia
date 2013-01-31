/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/xboxkrnl_threading.h"

#include <xenia/kernel/xbox.h>

#include "kernel/shim_utils.h"
#include "kernel/modules/xboxkrnl/objects/xthread.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {


// r13 + 0x100: pointer to thread local state
// Thread local state:
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


void ExCreateThread_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // DWORD
  // LPHANDLE Handle,
  // DWORD    StackSize,
  // LPDWORD  ThreadId,
  // LPVOID   XapiThreadStartup, ?? often 0
  // LPVOID   StartAddress,
  // LPVOID   StartContext,
  // DWORD    CreationFlags // 0x80?

  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t stack_size = SHIM_GET_ARG_32(1);
  uint32_t thread_id_ptr = SHIM_GET_ARG_32(2);
  uint32_t xapi_thread_startup = SHIM_GET_ARG_32(3);
  uint32_t start_address = SHIM_GET_ARG_32(4);
  uint32_t start_context = SHIM_GET_ARG_32(5);
  uint32_t creation_flags = SHIM_GET_ARG_32(6);

  XELOGD(
      XT("ExCreateThread(%.8X, %d, %.8X, %.8X, %.8X, %.8X, %.8X)"),
      handle_ptr,
      stack_size,
      thread_id_ptr,
      xapi_thread_startup,
      start_address,
      start_context,
      creation_flags);

  XThread* thread = new XThread(
      state, stack_size, xapi_thread_startup, start_address, start_context,
      creation_flags);

  X_STATUS result_code = thread->Create();
  if (XFAILED(result_code)) {
    // Failed!
    thread->Release();
    XELOGE(XT("Thread creation failed: %.8X"), result_code);
    SHIM_SET_RETURN(result_code);
    return;
  }

  if (handle_ptr) {
    SHIM_SET_MEM_32(handle_ptr, thread->handle());
  }
  if (thread_id_ptr) {
    SHIM_SET_MEM_32(thread_id_ptr, thread->thread_id());
  }
  SHIM_SET_RETURN(result_code);
}


void KeGetCurrentProcessType_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // DWORD

  XELOGD(
      XT("KeGetCurrentProcessType()"));

  SHIM_SET_RETURN(X_PROCTYPE_USER);
}


// The TLS system used here is a bit hacky, but seems to work.
// Both Win32 and pthreads use unsigned longs as TLS indices, so we can map
// right into the system for these calls. We're just round tripping the IDs and
// hoping for the best.


// http://msdn.microsoft.com/en-us/library/ms686801
void KeTlsAlloc_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // DWORD

  XELOGD(
      XT("KeTlsAlloc()"));

  uint32_t tls_index;

#if XE_PLATFORM(WIN32)
  tls_index = TlsAlloc();
#else
  pthread_key_t key;
  if (pthread_key_create(&key, NULL)) {
    tls_index = X_TLS_OUT_OF_INDEXES;
  } else {
    tls_index = (uint32_t)key;
  }
#endif  // WIN32

  SHIM_SET_RETURN(tls_index);
}


// http://msdn.microsoft.com/en-us/library/ms686804
void KeTlsFree_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // BOOL
  // _In_  DWORD dwTlsIndex

  uint32_t tls_index = SHIM_GET_ARG_32(0);

  XELOGD(
      XT("KeTlsFree(%.8X)"),
      tls_index);

  if (tls_index == X_TLS_OUT_OF_INDEXES) {
    SHIM_SET_RETURN(0);
    return;
  }

  int result_code = 0;

#if XE_PLATFORM(WIN32)
  result_code = TlsFree(tls_index);
#else
  result_code = pthread_key_delete(tls_index) == 0;
#endif  // WIN32

  SHIM_SET_RETURN(result_code);
}


// http://msdn.microsoft.com/en-us/library/ms686812
void KeTlsGetValue_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // LPVOID
  // _In_  DWORD dwTlsIndex

  uint32_t tls_index = SHIM_GET_ARG_32(0);

  XELOGD(
      XT("KeTlsGetValue(%.8X)"),
      tls_index);

  uint32_t value = 0;

#if XE_PLATFORM(WIN32)
  value = (uint32_t)((uint64_t)TlsGetValue(tls_index));
#else
  value = (uint32_t)((uint64_t)pthread_getspecific(tls_index));
#endif  // WIN32

  if (!value) {
    XELOGW(XT("KeTlsGetValue should SetLastError if result is NULL"));
    // TODO(benvanik): SetLastError
  }

  SHIM_SET_RETURN(value);
}


// http://msdn.microsoft.com/en-us/library/ms686818
void KeTlsSetValue_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // BOOL
  // _In_      DWORD dwTlsIndex,
  // _In_opt_  LPVOID lpTlsValue

  uint32_t tls_index = SHIM_GET_ARG_32(0);
  uint32_t tls_value = SHIM_GET_ARG_32(1);

  XELOGD(
      XT("KeTlsSetValue(%.8X, %.8X)"),
      tls_index, tls_value);

  int result_code = 0;

#if XE_PLATFORM(WIN32)
  result_code = TlsSetValue(tls_index, tls_value);
#else
  result_code = pthread_setspecific(tls_index, (void*)tls_value) == 0;
#endif  // WIN32

  SHIM_SET_RETURN(result_code);
}


}


void xe::kernel::xboxkrnl::RegisterThreadingExports(
    ExportResolver* export_resolver, KernelState* state) {
  #define SHIM_SET_MAPPING(ordinal, shim, impl) \
    export_resolver->SetFunctionMapping("xboxkrnl.exe", ordinal, \
        state, (xe_kernel_export_shim_fn)shim, (xe_kernel_export_impl_fn)impl)

  SHIM_SET_MAPPING(0x0000000D, ExCreateThread_shim, NULL);

  SHIM_SET_MAPPING(0x00000066, KeGetCurrentProcessType_shim, NULL);

  SHIM_SET_MAPPING(0x00000152, KeTlsAlloc_shim, NULL);
  SHIM_SET_MAPPING(0x00000153, KeTlsFree_shim, NULL);
  SHIM_SET_MAPPING(0x00000154, KeTlsGetValue_shim, NULL);
  SHIM_SET_MAPPING(0x00000155, KeTlsSetValue_shim, NULL);

  #undef SET_MAPPING
}

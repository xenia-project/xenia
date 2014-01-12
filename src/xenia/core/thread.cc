/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/thread.h>


typedef struct xe_thread {
  xe_ref_t ref;

  char* name;

  xe_thread_callback callback;
  void* callback_param;

  void* handle;
} xe_thread_t;



xe_thread_ref xe_thread_create(
    const char* name, xe_thread_callback callback, void* param) {
  xe_thread_ref thread = (xe_thread_ref)xe_calloc(sizeof(xe_thread_t));
  xe_ref_init((xe_ref)thread);

  thread->name = xestrdupa(name);
  thread->callback = callback;
  thread->callback_param = param;

  return thread;
}

void xe_thread_dealloc(xe_thread_ref thread) {
  thread->handle = NULL;
  xe_free(thread->name);
}

xe_thread_ref xe_thread_retain(xe_thread_ref thread) {
  xe_ref_retain((xe_ref)thread);
  return thread;
}

void xe_thread_release(xe_thread_ref thread) {
  xe_ref_release((xe_ref)thread, (xe_ref_dealloc_t)xe_thread_dealloc);
}

#if XE_PLATFORM_WIN32

// http://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
   DWORD dwType; // Must be 0x1000.
   LPCSTR szName; // Pointer to name (in user addr space).
   DWORD dwThreadID; // Thread ID (-1=caller thread).
   DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

#pragma warning(disable : 6320; disable : 6322)
static uint32_t __stdcall xe_thread_callback_win32(void* param) {
  xe_thread_t* thread = reinterpret_cast<xe_thread_t*>(param);

  if (IsDebuggerPresent()) {
    THREADNAME_INFO info;
    info.dwType     = 0x1000;
    info.szName     = thread->name;
    info.dwThreadID = (DWORD)-1;
    info.dwFlags    = 0;
    __try {
        RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR),
                       (ULONG_PTR*)&info);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }
  }

  thread->callback(thread->callback_param);
  return 0;
}
#pragma warning(default : 6320; default : 6322)

int xe_thread_start(xe_thread_ref thread) {
  HANDLE thread_handle = CreateThread(
      NULL,
      0,
      (LPTHREAD_START_ROUTINE)xe_thread_callback_win32,
      thread,
      0,
      NULL);
  if (!thread_handle) {
    uint32_t last_error = GetLastError();
    // TODO(benvanik): translate?
    XELOGE("CreateThread failed with %d", last_error);
    return last_error;
  }

  thread->handle = reinterpret_cast<void*>(thread_handle);

  return 0;
}

void xe_thread_join(xe_thread_ref thread) {
  HANDLE thread_handle = (HANDLE)thread->handle;
  WaitForSingleObject(thread_handle, INFINITE);
}

#else

static void* xe_thread_callback_pthreads(void* param) {
  xe_thread_t* thread = reinterpret_cast<xe_thread_t*>(param);
#if XE_LIKE_OSX
  XEIGNORE(pthread_setname_np(thread->name));
#else
  pthread_setname_np(pthread_self(), thread->name);
#endif  // OSX
  thread->callback(thread->callback_param);
  return 0;
}

int xe_thread_start(xe_thread_ref thread) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t thread_handle;
  int result_code = pthread_create(
      &thread_handle,
      &attr,
      &xe_thread_callback_pthreads,
      thread);
  pthread_attr_destroy(&attr);
  if (result_code) {
    return result_code;
  }

  thread->handle = reinterpret_cast<void*>(thread_handle);

  return 0;
}

void xe_thread_join(xe_thread_ref thread) {
  pthread_join((pthread_t)thread->handle, NULL);
}

#endif  // WIN32

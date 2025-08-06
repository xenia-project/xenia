/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/thread_monitor.h"

#include "xenia/base/platform.h"
#include "xenia/base/threading.h"

#if XE_PLATFORM_MAC
#include <execinfo.h>
#include <pthread.h>
#include <mach/mach.h>
#elif XE_PLATFORM_LINUX
#include <execinfo.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif XE_PLATFORM_WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace xe {
namespace threading {

uint32_t ThreadMonitor::GetCurrentThreadSystemId() {
#if XE_PLATFORM_MAC
  uint64_t tid;
  pthread_threadid_np(nullptr, &tid);
  return static_cast<uint32_t>(tid);
#elif XE_PLATFORM_LINUX
  return static_cast<uint32_t>(syscall(SYS_gettid));
#elif XE_PLATFORM_WIN32
  return GetCurrentThreadId();
#else
  return 0;
#endif
}

int ThreadMonitor::CaptureStackTrace(void** buffer, int max_frames) {
#if XE_PLATFORM_MAC || XE_PLATFORM_LINUX
  return backtrace(buffer, max_frames);
#elif XE_PLATFORM_WIN32
  return CaptureStackBackTrace(0, max_frames, buffer, nullptr);
#else
  return 0;
#endif
}

}  // namespace threading
}  // namespace xe
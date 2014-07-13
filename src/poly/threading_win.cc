/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/threading.h>

#include <poly/platform.h>

namespace poly {
namespace threading {

uint64_t ticks() {
  LARGE_INTEGER counter;
  uint64_t time = 0;
  if (QueryPerformanceCounter(&counter)) {
    time = counter.QuadPart;
  }
  return time;
}

uint32_t current_thread_id() {
  return static_cast<uint32_t>(GetCurrentThreadId());
}

void Yield() { SwitchToThread(); }

void Sleep(std::chrono::microseconds duration) {
  if (duration.count() < 100) {
    SwitchToThread();
  } else {
    ::Sleep(static_cast<DWORD>(duration.count() / 1000));
  }
}

}  // namespace threading
}  // namespace poly

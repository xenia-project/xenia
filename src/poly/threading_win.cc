/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/threading.h>

namespace poly {
namespace threading {

uint32_t current_thread_id() {
  return static_cast<uint32_t>(GetCurrentThreadId());
}

void Yield() { SwitchToThread(); }

void Sleep(std::chrono::microseconds duration) {
  if (duration.count() < 100) {
    SwitchToThread();
  } else {
    Sleep(duration.count() / 1000);
  }
}

}  // namespace threading
}  // namespace poly

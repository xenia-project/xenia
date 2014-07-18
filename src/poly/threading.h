/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_THREADING_H_
#define POLY_THREADING_H_

#include <chrono>
#include <cstdint>

#include <poly/config.h>

namespace poly {
namespace threading {

// Gets the current high-performance tick count.
uint64_t ticks();

// Gets a stable thread-specific ID, but may not be. Use for informative
// purposes only.
uint32_t current_thread_id();

// Yields the current thread to the scheduler. Maybe.
void Yield();

// Sleeps the current thread for at least as long as the given duration.
void Sleep(std::chrono::microseconds duration);
template <typename Rep, typename Period>
void Sleep(std::chrono::duration<Rep, Period> duration) {
  Sleep(std::chrono::duration_cast<std::chrono::microseconds>(duration));
}

}  // namespace threading
}  // namespace poly

#endif  // POLY_THREADING_H_

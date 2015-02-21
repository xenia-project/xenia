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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "poly/config.h"

namespace poly {
namespace threading {

class Fence {
 public:
  Fence() : signaled_(false) {}
  void Signal() {
    std::unique_lock<std::mutex> lock(mutex_);
    signaled_.store(true);
    cond_.notify_all();
  }
  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!signaled_.load()) {
      cond_.wait(lock);
    }
  }

 private:
  std::mutex mutex_;
  std::condition_variable cond_;
  std::atomic<bool> signaled_;
};

// Gets the current high-performance tick count.
uint64_t ticks();
uint64_t ticks_per_second();

// TODO(benvanik): processor info API.

// Gets a stable thread-specific ID, but may not be. Use for informative
// purposes only.
uint32_t current_thread_id();

// Sets the current thread name.
void set_name(const std::string& name);
// Sets the target thread name.
void set_name(std::thread::native_handle_type handle, const std::string& name);

// Yields the current thread to the scheduler. Maybe.
void MaybeYield();

// Sleeps the current thread for at least as long as the given duration.
void Sleep(std::chrono::microseconds duration);
template <typename Rep, typename Period>
void Sleep(std::chrono::duration<Rep, Period> duration) {
  Sleep(std::chrono::duration_cast<std::chrono::microseconds>(duration));
}

}  // namespace threading
}  // namespace poly

#endif  // POLY_THREADING_H_

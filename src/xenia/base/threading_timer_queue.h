/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_THREADING_TIMER_QUEUE_H_
#define XENIA_BASE_THREADING_TIMER_QUEUE_H_

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

// This is a platform independent implementation of a timer queue similar to
// Windows CreateTimerQueueTimer with WT_EXECUTEINTIMERTHREAD.

namespace xe::threading {

class TimerQueue;

struct TimerQueueWaitItem {
  using clock = std::chrono::steady_clock;

  TimerQueueWaitItem(std::function<void(void*)> callback, void* userdata,
                     TimerQueue* parent_queue, clock::time_point due,
                     clock::duration interval)
      : callback_(std::move(callback)),
        userdata_(userdata),
        parent_queue_(parent_queue),
        due_(due),
        interval_(interval),
        state_(State::kIdle) {}

  // Cancel the pending wait item. No callbacks will be running after this call.
  // The function blocks if a callback is running and returns only after the
  // callback has finished (except when called from the corresponding callback
  // itself, where it will mark the wait item for disarmament and return
  // immediately). Deadlocks are possible when a lock is held during disamament
  // and the corresponding callback is running concurrently, trying to acquire
  // said lock.
  void Disarm();

  friend TimerQueue;

 private:
  enum class State : uint_least8_t {
    kIdle = 0,                // Waiting for the due time
    kInCallback,              // Callback is being executed
    kInCallbackSelfDisarmed,  // Callback is being executed and disarmed itself
    kDisarmed                 // Disarmed, waiting for destruction
  };
  static_assert(std::atomic<State>::is_always_lock_free);

  std::function<void(void*)> callback_;
  void* userdata_;
  TimerQueue* parent_queue_;
  clock::time_point due_;
  clock::duration interval_;  // zero if not recurring
  std::atomic<State> state_;
};

std::weak_ptr<TimerQueueWaitItem> QueueTimerOnce(
    std::function<void(void*)> callback, void* userdata,
    TimerQueueWaitItem::clock::time_point due);

// Callback is first executed at due, then again repeatedly after interval
// passes (unless interval == 0). The first callback will be scheduled at
// `max(now() - interval, due)` to mitigate callback flooding.
std::weak_ptr<TimerQueueWaitItem> QueueTimerRecurring(
    std::function<void(void*)> callback, void* userdata,
    TimerQueueWaitItem::clock::time_point due,
    TimerQueueWaitItem::clock::duration interval);
}  // namespace xe::threading

#endif

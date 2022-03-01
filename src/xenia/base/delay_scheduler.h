/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_DELAY_SCHEDULER_H_
#define XENIA_BASE_DELAY_SCHEDULER_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <vector>

namespace xe {

namespace internal {
// Put the implementation in a non templated base class to reduce compile time
// and code duplication
class DelaySchedulerBase {
 protected:  // Types
  enum class State : uint_least8_t {
    kFree = 0,      // Slot is unsued
    kInitializing,  // Slot is reserved and currently written to by a thread
    kWaiting,       // The slot contains a pointer scheduled for deletion
    kDeleting       // A thread is currently deleting the pointer
  };
  using atomic_state_t = std::atomic<State>;
  using clock = std::chrono::steady_clock;
  struct deletion_record_t {
    atomic_state_t state;
    void* info;
    clock::time_point due;
  };
  using deletion_queue_t = std::vector<deletion_record_t>;
  using callback_t = std::function<void(void*)>;

 public:
  /// Check all scheduled items in the queue and free any that are due using
  /// `callback(info);`. Call this to reliably collect all due wait items in the
  /// queue.
  void Collect();

  size_t size() { return deletion_queue_.size(); }

 protected:  // Implementation
  DelaySchedulerBase(size_t queue_size, callback_t callback,
                     bool call_on_destruct)
      : deletion_queue_(queue_size),
        callback_(callback),
        call_on_destruct_(call_on_destruct) {}
  virtual ~DelaySchedulerBase();

  void ScheduleAt_impl(void* info, const clock::time_point& timeout_time);
  [[nodiscard]] bool TryScheduleAt_impl(void* info,
                                        const clock::time_point& timeout_time);

  /// Checks if the slot is due and if so, call back for it.
  bool TryCollectOne_impl(deletion_record_t& slot);

  [[nodiscard]] bool TryScheduleAtOne_impl(deletion_record_t& slot, void* info,
                                           clock::time_point due);

 private:
  deletion_queue_t deletion_queue_;
  callback_t callback_;
  bool call_on_destruct_;
};
}  // namespace internal

/// A lazy scheduler/timer.
/// Will wait at least the specified duration before invoking the callbacks but
/// might wait until it is destructed. Lockless thread-safe, will spinlock
/// though if the wait queue is full (except for `Try`... methods). Might use
/// any thread that calls any member to invoke callbacks of due wait items.
template <typename INFO>
class DelayScheduler : internal::DelaySchedulerBase {
 public:
  DelayScheduler(size_t queue_size, std::function<void(INFO*)> callback,
                 bool call_on_destruct)
      : DelaySchedulerBase(
            queue_size,
            [callback](void* info) { callback(reinterpret_cast<INFO*>(info)); },
            call_on_destruct){};
  DelayScheduler(const DelayScheduler&) = delete;
  DelayScheduler& operator=(const DelayScheduler&) = delete;
  virtual ~DelayScheduler() {}

  // From base class:
  // void Collect();

  /// Schedule an object for deletion at some point after `timeout_time` using
  /// `callback(info);`. Will collect any wait items it encounters which can be
  /// 0 or all, use `Collect()` to collect all due wait items. Blocks until a
  /// free wait slot is found.
  template <class Clock, class Duration>
  void ScheduleAt(
      INFO* info,
      const std::chrono::time_point<Clock, Duration>& timeout_time) {
    ScheduleAt(info,
               std::chrono::time_point_cast<clock::time_point>(timeout_time));
  }
  /// Like `ScheduleAt` but does not block on full list.
  template <class Clock, class Duration>
  [[nodiscard]] bool TryScheduleAt(
      INFO* info,
      const std::chrono::time_point<Clock, Duration>& timeout_time) {
    return TryScheduleAt(
        info, std::chrono::time_point_cast<clock::time_point>(timeout_time));
  }

  void ScheduleAt(INFO* info, const clock::time_point& timeout_time) {
    ScheduleAt_impl(info, timeout_time);
  }
  [[nodiscard]] bool TryScheduleAt(INFO* info,
                                   const clock::time_point& timeout_time) {
    return TryScheduleAt_impl(info, timeout_time);
  }

  /// Schedule a callback at some point after `rel_time` has passed.
  template <class Rep, class Period>
  void ScheduleAfter(INFO* info,
                     const std::chrono::duration<Rep, Period>& rel_time) {
    ScheduleAt(info,
               clock::now() + std::chrono::ceil<clock::duration>(rel_time));
  }
  /// Like `ScheduleAfter` but does not block.
  template <class Rep, class Period>
  [[nodiscard]] bool TryScheduleAfter(
      INFO* info, const std::chrono::duration<Rep, Period>& rel_time) {
    return TryScheduleAt(
        info, clock::now() + std::chrono::ceil<clock::duration>(rel_time));
  }
};

}  // namespace xe

#endif

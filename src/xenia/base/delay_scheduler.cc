/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/delay_scheduler.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe::internal {

DelaySchedulerBase::~DelaySchedulerBase() {
  if (call_on_destruct_) {
    for (auto& slot : deletion_queue_) {
      // No thread safety in destructors anyway
      if (slot.state == State::kWaiting) {
        callback_(slot.info);
      }
    }
  }
}

void DelaySchedulerBase::Collect() {
  static_assert(atomic_state_t::is_always_lock_free,
                "Locks are unsafe to use with thread suspension");

  for (auto& slot : deletion_queue_) {
    TryCollectOne_impl(slot);
  }
}

bool DelaySchedulerBase::TryCollectOne_impl(deletion_record_t& slot) {
  auto now = clock::now();

  auto state = State::kWaiting;
  // Try to lock the waiting slot for reading the other fields
  if (slot.state.compare_exchange_strong(state, State::kDeleting,
                                         std::memory_order_acq_rel)) {
    if (now > slot.due) {
      // Time has passed, call back now
      callback_(slot.info);
      slot.info = nullptr;
      slot.state.store(State::kFree, std::memory_order_release);
      return true;
    } else {
      // Oops it's not yet due
      slot.state.store(State::kWaiting, std::memory_order_release);
    }
  }
  return false;
}

void DelaySchedulerBase::ScheduleAt_impl(
    void* info, const clock::time_point& timeout_time) {
  bool first_pass = true;

  if (info == nullptr) {
    return;
  }

  for (;;) {
    if (TryScheduleAt_impl(info, timeout_time)) {
      return;
    }

    if (first_pass) {
      first_pass = false;
      XELOGE(
          "`DelayScheduler::ScheduleAt(...)` stalled: list is full! Find out "
          "why or increase `MAX_QUEUE`.");
    }
  }
}

bool DelaySchedulerBase::TryScheduleAt_impl(
    void* info, const clock::time_point& timeout_time) {
  if (info == nullptr) {
    return false;
  }

  for (auto& slot : deletion_queue_) {
    // Clean up due item
    TryCollectOne_impl(slot);

    if (TryScheduleAtOne_impl(slot, info, timeout_time)) {
      return true;
    }
  }
  return false;
}

bool DelaySchedulerBase::TryScheduleAtOne_impl(deletion_record_t& slot,
                                               void* info,
                                               clock::time_point due) {
  auto state = State::kFree;
  if (slot.state.compare_exchange_strong(state, State::kInitializing,
                                         std::memory_order_acq_rel)) {
    slot.info = info;
    slot.due = due;
    slot.state.store(State::kWaiting, std::memory_order_release);
    return true;
  }

  return false;
}

}  // namespace xe::internal
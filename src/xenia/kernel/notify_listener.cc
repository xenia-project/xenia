/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/notify_listener.h"

#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {

NotifyListener::NotifyListener(KernelState* kernel_state)
    : XObject(kernel_state, kTypeNotifyListener) {}

NotifyListener::~NotifyListener() {
  kernel_state_->UnregisterNotifyListener(this);
}

void NotifyListener::Initialize(uint64_t mask) {
  assert_false(wait_handle_);

  wait_handle_ = xe::threading::Event::CreateManualResetEvent(false);
  mask_ = mask;

  kernel_state_->RegisterNotifyListener(this);
}

void NotifyListener::EnqueueNotification(XNotificationID id, uint32_t data) {
  // Ignore if the notification doesn't match our mask.
  if ((mask_ & uint64_t(1 << (id >> 25))) == 0) {
    return;
  }

  auto global_lock = global_critical_region_.Acquire();
  if (notifications_.count(id)) {
    // Already exists. Overwrite.
    notifications_[id] = data;
  } else {
    // New.
    notification_count_++;
    notifications_.insert({id, data});
  }
  wait_handle_->Set();
}

bool NotifyListener::DequeueNotification(XNotificationID* out_id,
                                         uint32_t* out_data) {
  auto global_lock = global_critical_region_.Acquire();
  bool dequeued = false;
  if (notification_count_) {
    dequeued = true;
    auto it = notifications_.begin();
    *out_id = it->first;
    *out_data = it->second;
    notifications_.erase(it);
    notification_count_--;
    if (!notification_count_) {
      wait_handle_->Reset();
    }
  }
  return dequeued;
}

bool NotifyListener::DequeueNotification(XNotificationID id,
                                         uint32_t* out_data) {
  auto global_lock = global_critical_region_.Acquire();
  bool dequeued = false;
  if (notification_count_) {
    auto it = notifications_.find(id);
    if (it != notifications_.end()) {
      dequeued = true;
      *out_data = it->second;
      notifications_.erase(it);
      notification_count_--;
      if (!notification_count_) {
        wait_handle_->Reset();
      }
    }
  }
  return dequeued;
}

}  // namespace kernel
}  // namespace xe

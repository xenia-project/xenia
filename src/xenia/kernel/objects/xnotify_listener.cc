/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/objects/xnotify_listener.h>


using namespace xe;
using namespace xe::kernel;


XNotifyListener::XNotifyListener(KernelState* kernel_state) :
    XObject(kernel_state, kTypeNotifyListener),
    wait_handle_(NULL), lock_(0), mask_(0), notification_count_(0) {
}

XNotifyListener::~XNotifyListener() {
  kernel_state_->UnregisterNotifyListener(this);
  xe_mutex_free(lock_);
  if (wait_handle_) {
    CloseHandle(wait_handle_);
  }
}

void XNotifyListener::Initialize(uint64_t mask) {
  assert_null(wait_handle_);

  lock_ = xe_mutex_alloc();
  wait_handle_ = CreateEvent(NULL, TRUE, FALSE, NULL);
  mask_ = mask;

  kernel_state_->RegisterNotifyListener(this);
}

void XNotifyListener::EnqueueNotification(XNotificationID id, uint32_t data) {
  // Ignore if the notification doesn't match our mask.
  if ((mask_ & uint64_t(1 << ((id >> 25) + 1))) == 0) {
    return;
  }

  xe_mutex_lock(lock_);
  if (notifications_.count(id)) {
    // Already exists. Overwrite.
    notifications_[id] = data;
  } else {
    // New.
    notification_count_++;
    notifications_.insert({ id, data });
  }
  SetEvent(wait_handle_);
  xe_mutex_unlock(lock_);
}

bool XNotifyListener::DequeueNotification(
    XNotificationID* out_id, uint32_t* out_data) {
  bool dequeued = false;
  xe_mutex_lock(lock_);
  if (notification_count_) {
    dequeued = true;
    auto it = notifications_.begin();
    *out_id = it->first;
    *out_data = it->second;
    notifications_.erase(it);
    notification_count_--;
    if (!notification_count_) {
      ResetEvent(wait_handle_);
    }
  }
  xe_mutex_unlock(lock_);
  return dequeued;
}

bool XNotifyListener::DequeueNotification(
    XNotificationID id, uint32_t* out_data) {
  bool dequeued = false;
  xe_mutex_lock(lock_);
  if (notification_count_) {
    dequeued = true;
    auto it = notifications_.find(id);
    if (it != notifications_.end()) {
      *out_data = it->second;
      notifications_.erase(it);
      notification_count_--;
      if (!notification_count_) {
        ResetEvent(wait_handle_);
      }
    }
  }
  xe_mutex_unlock(lock_);
  return dequeued;
}

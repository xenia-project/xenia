/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/objects/xnotify_listener.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


XNotifyListener::XNotifyListener(KernelState* kernel_state) :
    XObject(kernel_state, kTypeNotifyListener),
    wait_handle_(NULL), lock_(0), mask_(0), notification_count_(0) {
}

XNotifyListener::~XNotifyListener() {
  xe_mutex_free(lock_);
  if (wait_handle_) {
    CloseHandle(wait_handle_);
  }
}

void XNotifyListener::Initialize(uint64_t mask) {
  XEASSERTNULL(wait_handle_);

  lock_ = xe_mutex_alloc();
  wait_handle_ = CreateEvent(NULL, TRUE, FALSE, NULL);
  mask_ = mask;
}

void XNotifyListener::EnqueueNotification(XNotificationID id, uint32_t data) {
  xe_mutex_lock(lock_);
  auto existing = notifications_.find(id);
  if (existing != notifications_.end()) {
    // Already exists. Overwrite.
    notifications_[id] = data;
  } else {
    // New.
    notification_count_++;
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

// TODO(benvanik): move this to common class
X_STATUS XNotifyListener::Wait(uint32_t wait_reason, uint32_t processor_mode,
                               uint32_t alertable, uint64_t* opt_timeout) {
  DWORD timeout_ms;
  if (opt_timeout) {
    int64_t timeout_ticks = (int64_t)(*opt_timeout);
    if (timeout_ticks > 0) {
      // Absolute time, based on January 1, 1601.
      // TODO(benvanik): convert time to relative time.
      XEASSERTALWAYS();
      timeout_ms = 0;
    } else if (timeout_ticks < 0) {
      // Relative time.
      timeout_ms = (DWORD)(-timeout_ticks / 10000); // Ticks -> MS
    } else {
      timeout_ms = 0;
    }
  } else {
    timeout_ms = INFINITE;
  }

  DWORD result = WaitForSingleObjectEx(wait_handle_, timeout_ms, alertable);
  switch (result) {
  case WAIT_OBJECT_0:
    return X_STATUS_SUCCESS;
  case WAIT_IO_COMPLETION:
    // Or X_STATUS_ALERTED?
    return X_STATUS_USER_APC;
  case WAIT_TIMEOUT:
    return X_STATUS_TIMEOUT;
  default:
  case WAIT_FAILED:
  case WAIT_ABANDONED:
    return X_STATUS_ABANDONED_WAIT_0;
  }
}

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xnotifylistener.h"

#include "xenia/base/byte_stream.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {

XNotifyListener::XNotifyListener(KernelState* kernel_state)
    : XObject(kernel_state, kType) {}

XNotifyListener::~XNotifyListener() {}

void XNotifyListener::Initialize(uint64_t mask) {
  assert_false(wait_handle_);

  wait_handle_ = xe::threading::Event::CreateManualResetEvent(false);
  mask_ = mask;

  kernel_state_->RegisterNotifyListener(this);
}

void XNotifyListener::EnqueueNotification(XNotificationID id, uint32_t data) {
  // Ignore if the notification doesn't match our mask.
  // TODO(Gliniak): (confirm) mask 0x01 means accept all
  if ((mask_ & ((id >> 25) & 0x3F)) == 0 && mask_ != 0x01) {
    return;
  }

  auto global_lock = global_critical_region_.Acquire();
  notifications_.push_back(std::pair<XNotificationID, uint32_t>(id, data));
  wait_handle_->Set();
}

bool XNotifyListener::DequeueNotification(XNotificationID* out_id,
                                          uint32_t* out_data) {
  auto global_lock = global_critical_region_.Acquire();
  bool dequeued = false;
  if (notifications_.size()) {
    dequeued = true;
    auto it = notifications_.begin();
    *out_id = it->first;
    *out_data = it->second;
    notifications_.erase(it);
    if (!notifications_.size()) {
      wait_handle_->Reset();
    }
  }
  return dequeued;
}

bool XNotifyListener::DequeueNotification(XNotificationID id,
                                          uint32_t* out_data) {
  auto global_lock = global_critical_region_.Acquire();
  bool dequeued = false;
  if (!notifications_.size()) {
    return dequeued;
  }

  for (auto it = notifications_.begin(); it != notifications_.end(); ++it) {
    if (it->first != id) {
      continue;
    }

    dequeued = true;
    *out_data = it->second;
    notifications_.erase(it);
    if (!notifications_.size()) {
      wait_handle_->Reset();
    }
    break;
  }
  return dequeued;
}

bool XNotifyListener::Save(ByteStream* stream) {
  SaveObject(stream);

  stream->Write(mask_);
  stream->Write(notifications_.size());
  if (notifications_.size()) {
    for (auto pair : notifications_) {
      stream->Write<uint32_t>(pair.first);
      stream->Write<uint32_t>(pair.second);
    }
  }

  return true;
}

object_ref<XNotifyListener> XNotifyListener::Restore(KernelState* kernel_state,
                                                     ByteStream* stream) {
  auto notify = new XNotifyListener(nullptr);
  notify->kernel_state_ = kernel_state;

  notify->RestoreObject(stream);
  notify->Initialize(stream->Read<uint64_t>());

  auto notification_count_ = stream->Read<size_t>();
  for (size_t i = 0; i < notification_count_; i++) {
    std::pair<XNotificationID, uint32_t> pair;
    pair.first = stream->Read<uint32_t>();
    pair.second = stream->Read<uint32_t>();
    notify->notifications_.push_back(pair);
  }

  return object_ref<XNotifyListener>(notify);
}

}  // namespace kernel
}  // namespace xe

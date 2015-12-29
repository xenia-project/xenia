/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XIOCOMPLETION_H_
#define XENIA_KERNEL_XIOCOMPLETION_H_

#include <queue>

#include "xenia/base/threading.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XIOCompletion : public XObject {
 public:
  static const Type kType = kTypeIOCompletion;

  explicit XIOCompletion(KernelState* kernel_state);
  ~XIOCompletion() override;

  struct IONotification {
    uint32_t key_context;
    uint32_t apc_context;
    uint32_t status;
    uint32_t num_bytes;
  };

  void QueueNotification(IONotification& notification);
  IONotification DequeueNotification();

  // If you call this and it returns true, you MUST dequeue a notification!
  // Returns true if the wait was cancelled because a notification was queued.
  bool WaitForNotification(uint64_t wait_ticks);

 private:
  static const uint32_t kMaxNotifications = 1024;

  std::mutex notification_lock_;
  std::queue<IONotification> notifications_;
  std::unique_ptr<threading::Semaphore> notification_semaphore_ = nullptr;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XIOCOMPLETION_H_

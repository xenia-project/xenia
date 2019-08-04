/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XNOTIFYLISTENER_H_
#define XENIA_KERNEL_XNOTIFYLISTENER_H_

#include <memory>
#include <unordered_map>

#include "xenia/base/mutex.h"
#include "xenia/base/threading.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XNotifyListener : public XObject {
 public:
  static const Type kType = kTypeNotifyListener;

  explicit XNotifyListener(KernelState* kernel_state);
  ~XNotifyListener() override;

  uint64_t mask() const { return mask_; }

  void Initialize(uint64_t mask);

  void EnqueueNotification(XNotificationID id, uint32_t data);
  bool DequeueNotification(XNotificationID* out_id, uint32_t* out_data);
  bool DequeueNotification(XNotificationID id, uint32_t* out_data);

  bool Save(ByteStream* stream) override;
  static object_ref<XNotifyListener> Restore(KernelState* kernel_state,
                                             ByteStream* stream);

 protected:
  xe::threading::WaitHandle* GetWaitHandle() override {
    return wait_handle_.get();
  }

 private:
  std::unique_ptr<xe::threading::Event> wait_handle_;
  xe::global_critical_region global_critical_region_;
  std::vector<std::pair<XNotificationID, uint32_t>> notifications_;
  uint64_t mask_ = 0;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XNOTIFYLISTENER_H_

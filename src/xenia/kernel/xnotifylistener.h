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

#include "xenia/base/assert.h"
#include "xenia/base/mutex.h"
#include "xenia/base/threading.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

union XNotificationKey {
  XNotificationID id;
  struct {
    uint32_t local_id : 16;
    uint32_t version : 9;
    uint32_t mask_index : 6;
    uint32_t : 1;
  };

  constexpr XNotificationKey(
      XNotificationID notification_id = XNotificationID(0))
      : id(notification_id) {
    static_assert_size(*this, sizeof(id));
  }

  constexpr operator XNotificationID() { return id; }
};

class XNotifyListener : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::NotifyListener;

  explicit XNotifyListener(KernelState* kernel_state);
  ~XNotifyListener() override;

  uint64_t mask() const { return mask_; }
  uint32_t max_version() const { return max_version_; }

  void Initialize(uint64_t mask, uint32_t max_version);

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
  uint32_t max_version_ = 0;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XNOTIFYLISTENER_H_

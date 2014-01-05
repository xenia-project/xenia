/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// This should probably be in XAM, but I don't want to build an extensible
// object system. Meh.

#ifndef XENIA_KERNEL_XBOXKRNL_XNOTIFY_LISTENER_H_
#define XENIA_KERNEL_XBOXKRNL_XNOTIFY_LISTENER_H_

#include <xenia/kernel/xboxkrnl/xobject.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {

// Values seem to be all over the place - GUIDs?
typedef uint32_t XNotificationID;


class XNotifyListener : public XObject {
public:
  XNotifyListener(KernelState* kernel_state);
  virtual ~XNotifyListener();

  void Initialize(uint64_t mask);

  void EnqueueNotification(XNotificationID id, uint32_t data);
  bool DequeueNotification(XNotificationID* out_id, uint32_t* out_data);
  bool DequeueNotification(XNotificationID id, uint32_t* out_data);

  virtual X_STATUS Wait(uint32_t wait_reason, uint32_t processor_mode,
                        uint32_t alertable, uint64_t* opt_timeout);

private:
  HANDLE wait_handle_;
  xe_mutex_t* lock_;
  std::unordered_map<XNotificationID, uint32_t> notifications_;
  size_t notification_count_;
  uint64_t mask_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_XNOTIFY_LISTENER_H_

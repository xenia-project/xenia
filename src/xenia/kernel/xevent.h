/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XEVENT_H_
#define XENIA_KERNEL_XEVENT_H_

#include "xenia/base/threading.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

// http://www.nirsoft.net/kernel_struct/vista/KEVENT.html
struct X_KEVENT {
  X_DISPATCH_HEADER header;
};

class XEvent : public XObject {
 public:
  static const Type kType = kTypeEvent;

  explicit XEvent(KernelState* kernel_state);
  ~XEvent() override;

  void Initialize(bool manual_reset, bool initial_state);
  void InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header);

  int32_t Set(uint32_t priority_increment, bool wait);
  int32_t Pulse(uint32_t priority_increment, bool wait);
  int32_t Reset();
  void Clear();

  xe::threading::WaitHandle* GetWaitHandle() override { return event_.get(); }

 private:
  std::unique_ptr<xe::threading::Event> event_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XEVENT_H_

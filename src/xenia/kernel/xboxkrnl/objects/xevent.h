/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XEVENT_H_
#define XENIA_KERNEL_XBOXKRNL_XEVENT_H_

#include <xenia/kernel/xboxkrnl/xobject.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


class XEvent : public XObject {
public:
  XEvent(KernelState* kernel_state);
  virtual ~XEvent();

  void Initialize(bool manual_reset, bool initial_state);
  void InitializeNative(void* native_ptr, DISPATCH_HEADER& header);

  int32_t Set(uint32_t priority_increment, bool wait);
  int32_t Reset();
  void Clear();

  virtual X_STATUS Wait(uint32_t wait_reason, uint32_t processor_mode,
                        uint32_t alertable, uint64_t* opt_timeout);

private:
  HANDLE handle_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_XEVENT_H_

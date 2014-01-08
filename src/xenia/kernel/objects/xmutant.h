/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XMUTANT_H_
#define XENIA_KERNEL_XBOXKRNL_XMUTANT_H_

#include <xenia/kernel/xobject.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {


class XMutant : public XObject {
public:
  XMutant(KernelState* kernel_state);
  virtual ~XMutant();

  void Initialize(bool initial_owner);
  void InitializeNative(void* native_ptr, DISPATCH_HEADER& header);

  X_STATUS ReleaseMutant(uint32_t priority_increment, bool abandon, bool wait);

  virtual void* GetWaitHandle() { return handle_; }

private:
  HANDLE handle_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_XMUTANT_H_

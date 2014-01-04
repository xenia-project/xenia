/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XSEMAPHORE_H_
#define XENIA_KERNEL_XBOXKRNL_XSEMAPHORE_H_

#include <xenia/kernel/xboxkrnl/xobject.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


class XSemaphore : public XObject {
public:
  XSemaphore(KernelState* kernel_state);
  virtual ~XSemaphore();

  void Initialize(int32_t initial_count, int32_t maximum_count);
  void InitializeNative(void* native_ptr, DISPATCH_HEADER& header);

  int32_t ReleaseSemaphore(int32_t release_count);

  virtual X_STATUS Wait(uint32_t wait_reason, uint32_t processor_mode,
                        uint32_t alertable, uint64_t* opt_timeout);

private:
  HANDLE handle_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_XSEMAPHORE_H_

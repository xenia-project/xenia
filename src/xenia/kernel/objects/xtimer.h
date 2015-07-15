/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XTIMER_H_
#define XENIA_KERNEL_XBOXKRNL_XTIMER_H_

#include "xenia/base/threading.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XTimer : public XObject {
 public:
  XTimer(KernelState* kernel_state);
  ~XTimer() override;

  void Initialize(uint32_t timer_type);

  // completion routine, arg to completion routine
  X_STATUS SetTimer(int64_t due_time, uint32_t period_ms, uint32_t routine,
                    uint32_t routine_arg, bool resume);
  X_STATUS Cancel();

  xe::threading::WaitHandle* GetWaitHandle() override { return timer_.get(); }

 private:
  std::unique_ptr<xe::threading::Timer> timer_;

  uint32_t current_routine_;
  uint32_t current_routine_arg_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_TIMER_H_

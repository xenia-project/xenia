/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_THREAD_STATE_H_
#define XENIA_CPU_THREAD_STATE_H_

#include <xenia/core.h>

#include <xenia/cpu/ppc.h>


namespace xe {
namespace cpu {


class Processor;


class ThreadState {
public:
  ThreadState(Processor* processor,
              uint32_t stack_size, uint32_t thread_state_address);
  ~ThreadState();

  xe_ppc_state_t* ppc_state();

private:
  uint32_t stack_size_;
  uint32_t thread_state_address;
  xe_memory_ref memory_;

  uint32_t stack_address_;
  uint32_t thread_state_address_;

  xe_ppc_state_t  ppc_state_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_THREAD_STATE_H_

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

#include <string>

#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/memory.h"

namespace xe {
namespace cpu {

class Processor;

class ThreadState {
 public:
  ThreadState(Processor* processor, uint32_t thread_id, uint32_t stack_base = 0,
              uint32_t pcr_address = 0);
  ~ThreadState();

  Processor* processor() const { return processor_; }
  Memory* memory() const { return memory_; }
  void* backend_data() const { return backend_data_; }
  ppc::PPCContext* context() const { return context_; }
  uint32_t thread_id() const { return thread_id_; }

  static void Bind(ThreadState* thread_state);
  static ThreadState* Get();
  static uint32_t GetThreadID();

 private:
  Processor* processor_;
  Memory* memory_;
  void* backend_data_;

  uint32_t pcr_address_ = 0;
  uint32_t thread_id_ = 0;

  // NOTE: must be 64b aligned for SSE ops.
  ppc::PPCContext* context_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_THREAD_STATE_H_

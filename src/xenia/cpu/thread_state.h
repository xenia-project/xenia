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

#include "xenia/cpu/frontend/ppc_context.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/memory.h"

namespace xe {
namespace cpu {

class Processor;

enum class ThreadStackType {
  kKernelStack,
  kUserStack,
};

class ThreadState {
 public:
  ThreadState(Processor* processor, uint32_t thread_id,
              ThreadStackType stack_type, uint32_t stack_address,
              uint32_t stack_size, uint32_t pcr_address);
  ~ThreadState();

  Processor* processor() const { return processor_; }
  Memory* memory() const { return memory_; }
  uint32_t thread_id() const { return thread_id_; }
  ThreadStackType stack_type() const { return stack_type_; }
  const std::string& name() const { return name_; }
  void set_name(const std::string& value) { name_ = value; }
  void* backend_data() const { return backend_data_; }
  uint32_t stack_address() const { return stack_address_; }
  uint32_t stack_size() const { return stack_size_; }
  uint32_t stack_base() const { return stack_base_; }
  uint32_t stack_limit() const { return stack_limit_; }
  uint32_t pcr_address() const { return pcr_address_; }
  xe::cpu::frontend::PPCContext* context() const { return context_; }

  static void Bind(ThreadState* thread_state);
  static ThreadState* Get();
  static uint32_t GetThreadID();

 private:
  Processor* processor_;
  Memory* memory_;
  uint32_t thread_id_;
  ThreadStackType stack_type_;
  std::string name_;
  void* backend_data_;
  uint32_t stack_address_;
  bool stack_allocated_;
  uint32_t stack_size_;
  uint32_t stack_base_;
  uint32_t stack_limit_;
  uint32_t pcr_address_;

  // NOTE: must be 64b aligned for SSE ops.
  xe::cpu::frontend::PPCContext* context_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_THREAD_STATE_H_

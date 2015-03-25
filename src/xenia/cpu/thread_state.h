/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_thread_state_H_
#define XENIA_CPU_thread_state_H_

#include "xenia/cpu/frontend/ppc_context.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/common.h"
#include "xenia/memory.h"

namespace xdb {
namespace protocol {
struct Registers;
}  // namespace protocol
}  // namespace xdb

namespace xe {
namespace cpu {

class Runtime;

class ThreadState {
 public:
  ThreadState(Runtime* runtime, uint32_t thread_id, uint32_t stack_address,
              uint32_t stack_size, uint32_t thread_state_address);
  ~ThreadState();

  Runtime* runtime() const { return runtime_; }
  Memory* memory() const { return memory_; }
  uint32_t thread_id() const { return thread_id_; }
  const std::string& name() const { return name_; }
  void set_name(const std::string& value) { name_ = value; }
  void* backend_data() const { return backend_data_; }
  void* raw_context() const { return raw_context_; }
  uint32_t stack_address() const { return stack_address_; }
  size_t stack_size() const { return stack_size_; }
  uint32_t thread_state_address() const { return thread_state_address_; }
  xe::cpu::frontend::PPCContext* context() const { return context_; }

  int Suspend() { return Suspend(~0); }
  int Suspend(uint32_t timeout_ms) { return 1; }
  int Resume(bool force = false) { return 1; }

  static void Bind(ThreadState* thread_state);
  static ThreadState* Get();
  static uint32_t GetThreadID();

  void WriteRegisters(xdb::protocol::Registers* registers);

 private:
  Runtime* runtime_;
  Memory* memory_;
  uint32_t thread_id_;
  std::string name_;
  void* backend_data_;
  void* raw_context_;
  uint32_t stack_address_;
  bool stack_allocated_;
  uint32_t stack_size_;
  uint32_t thread_state_address_;

  // NOTE: must be 64b aligned for SSE ops.
  xe::cpu::frontend::PPCContext* context_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_thread_state_H_

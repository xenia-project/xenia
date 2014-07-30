/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_THREAD_STATE_H_
#define ALLOY_RUNTIME_THREAD_STATE_H_

#include <string>

#include <alloy/core.h>

#include <alloy/memory.h>

namespace alloy {
namespace runtime {

class Runtime;

class ThreadState {
 public:
  ThreadState(Runtime* runtime, uint32_t thread_id);
  virtual ~ThreadState();

  Runtime* runtime() const { return runtime_; }
  Memory* memory() const { return memory_; }
  uint32_t thread_id() const { return thread_id_; }
  const std::string& name() const { return name_; }
  void set_name(const std::string& value) { name_ = value; }
  void* backend_data() const { return backend_data_; }
  void* raw_context() const { return raw_context_; }

  int Suspend() { return Suspend(~0); }
  virtual int Suspend(uint32_t timeout_ms) { return 1; }
  virtual int Resume(bool force = false) { return 1; }

  static void Bind(ThreadState* thread_state);
  static ThreadState* Get();
  static uint32_t GetThreadID();

 protected:
  Runtime* runtime_;
  Memory* memory_;
  uint32_t thread_id_;
  std::string name_;
  void* backend_data_;
  void* raw_context_;
};

}  // namespace runtime
}  // namespace alloy

#endif  // ALLOY_RUNTIME_THREAD_STATE_H_

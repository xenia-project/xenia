/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PROCESSOR_H_
#define XENIA_CPU_PROCESSOR_H_

#include <mutex>
#include <vector>

#include "xenia/export_resolver.h"
#include "xenia/memory.h"

namespace xe {
namespace cpu {

class Runtime;
class ThreadState;
class XexModule;

enum class Irql : uint32_t {
  PASSIVE = 0,
  APC = 1,
  DISPATCH = 2,
  DPC = 3,
};

class Processor {
 public:
  Processor(Memory* memory, ExportResolver* export_resolver);
  ~Processor();

  ExportResolver* export_resolver() const { return export_resolver_; }
  Runtime* runtime() const { return runtime_; }
  Memory* memory() const { return memory_; }

  int Setup();

  int Execute(ThreadState* thread_state, uint32_t address);
  uint64_t Execute(ThreadState* thread_state, uint32_t address, uint64_t args[],
                   size_t arg_count);

  Irql RaiseIrql(Irql new_value);
  void LowerIrql(Irql old_value);

  uint64_t ExecuteInterrupt(uint32_t cpu, uint32_t address, uint64_t args[],
                            size_t arg_count);

 private:
  ExportResolver* export_resolver_;

  Runtime* runtime_;
  Memory* memory_;

  Irql irql_;
  std::mutex interrupt_thread_lock_;
  ThreadState* interrupt_thread_state_;
  uint32_t interrupt_thread_block_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PROCESSOR_H_

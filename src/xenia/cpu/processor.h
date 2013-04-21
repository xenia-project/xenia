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

#include <xenia/core.h>

#include <vector>

#include <xenia/cpu/backend.h>
#include <xenia/cpu/exec_module.h>
#include <xenia/cpu/function_table.h>
#include <xenia/cpu/thread_state.h>
#include <xenia/kernel/export.h>
#include <xenia/kernel/xex2.h>


namespace xe {
namespace cpu {

class JIT;


class Processor {
public:
  Processor(xe_memory_ref memory, shared_ptr<Backend> backend);
  ~Processor();

  xe_memory_ref memory();
  shared_ptr<kernel::ExportResolver> export_resolver();
  void set_export_resolver(shared_ptr<kernel::ExportResolver> export_resolver);

  int Setup();

  int LoadRawBinary(const xechar_t* path, uint32_t start_address);
  int LoadXexModule(const char* name, const char* path, xe_xex2_ref xex);

  uint32_t CreateCallback(void (*callback)(void* data), void* data);

  ThreadState* AllocThread(uint32_t stack_size, uint32_t thread_state_address);
  void DeallocThread(ThreadState* thread_state);
  int Execute(ThreadState* thread_state, uint32_t address);
  uint64_t Execute(ThreadState* thread_state, uint32_t address, uint64_t arg0);

private:
  FunctionPointer GenerateFunction(uint32_t address);

  xe_memory_ref       memory_;
  shared_ptr<Backend> backend_;
  shared_ptr<kernel::ExportResolver> export_resolver_;

  FunctionTable*  fn_table_;
  JIT*            jit_;
  std::vector<ExecModule*> modules_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PROCESSOR_H_

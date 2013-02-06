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

#include <xenia/cpu/exec_module.h>
#include <xenia/cpu/thread_state.h>
#include <xenia/kernel/export.h>
#include <xenia/kernel/xex2.h>


namespace llvm {
  class ExecutionEngine;
  class Function;
}


namespace xe {
namespace cpu {


class Processor {
public:
  Processor(xe_pal_ref pal, xe_memory_ref memory);
  ~Processor();

  xe_pal_ref pal();
  xe_memory_ref memory();

  int Setup();

  int LoadBinary(const xechar_t* path, uint32_t start_address,
                 shared_ptr<kernel::ExportResolver> export_resolver);

  int PrepareModule(const char* name, const char* path, xe_xex2_ref xex,
                    shared_ptr<kernel::ExportResolver> export_resolver);

  uint32_t CreateCallback(void (*callback)(void* data), void* data);

  ThreadState* AllocThread(uint32_t stack_size, uint32_t thread_state_address);
  void DeallocThread(ThreadState* thread_state);
  int Execute(ThreadState* thread_state, uint32_t address);
  uint64_t Execute(ThreadState* thread_state, uint32_t address, uint64_t arg0);

private:
  llvm::Function* GetFunction(uint32_t address);

  xe_pal_ref              pal_;
  xe_memory_ref           memory_;
  shared_ptr<llvm::ExecutionEngine> engine_;

  auto_ptr<llvm::LLVMContext> dummy_context_;

  std::vector<ExecModule*> modules_;

  FunctionMap all_fns_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PROCESSOR_H_

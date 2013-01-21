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
#include <xenia/kernel/export.h>
#include <xenia/kernel/user_module.h>


namespace llvm {
  class ExecutionEngine;
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

  int PrepareModule(kernel::UserModule* user_module,
                    shared_ptr<kernel::ExportResolver> export_resolver);

  int Execute(uint32_t address);
  uint32_t CreateCallback(void (*callback)(void* data), void* data);

private:
  xe_pal_ref              pal_;
  xe_memory_ref           memory_;
  shared_ptr<llvm::ExecutionEngine> engine_;

  auto_ptr<llvm::LLVMContext> dummy_context_;

  std::vector<ExecModule*> modules_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PROCESSOR_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_USERMODULE_H_
#define XENIA_CPU_USERMODULE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/cpu/sdb.h>
#include <xenia/kernel/export.h>
#include <xenia/kernel/user_module.h>


namespace llvm {
  class LLVMContext;
  class Module;
  class ExecutionEngine;
}


namespace xe {
namespace cpu {


class ExecModule {
public:
  ExecModule(
      xe_memory_ref memory, shared_ptr<kernel::ExportResolver> export_resolver,
      kernel::UserModule* user_module,
      shared_ptr<llvm::ExecutionEngine>& engine);
  ~ExecModule();

  int Prepare();

  void Dump();

private:
  int Init();
  int Uninit();

  xe_memory_ref         memory_;
  shared_ptr<kernel::ExportResolver> export_resolver_;
  kernel::UserModule*   module_;
  shared_ptr<llvm::ExecutionEngine> engine_;
  shared_ptr<sdb::SymbolDatabase>   sdb_;
  shared_ptr<llvm::LLVMContext>     context_;
  shared_ptr<llvm::Module>          gen_module_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_USERMODULE_H_

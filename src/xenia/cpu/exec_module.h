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
#include <xenia/kernel/xex2.h>


namespace llvm {
  class ExecutionEngine;
  class Function;
  class LLVMContext;
  class Module;
}

namespace xe {
namespace cpu {
namespace codegen {
  class ModuleGenerator;
}
}
}


namespace xe {
namespace cpu {


typedef std::tr1::unordered_map<uint32_t, llvm::Function*> FunctionMap;


class ExecModule {
public:
  ExecModule(
      xe_memory_ref memory, shared_ptr<kernel::ExportResolver> export_resolver,
      const char* module_name, const char* module_path,
      shared_ptr<llvm::ExecutionEngine>& engine);
  ~ExecModule();

  int PrepareXex(xe_xex2_ref xex);
  int PrepareRawBinary(uint32_t start_address, uint32_t end_address);

  void AddFunctionsToMap(FunctionMap& map);

  void Dump();

private:
  int Prepare();
  int InjectGlobals();
  int Init();
  int Uninit();

  xe_memory_ref                       memory_;
  shared_ptr<kernel::ExportResolver>  export_resolver_;
  char*                               module_name_;
  char*                               module_path_;
  shared_ptr<llvm::ExecutionEngine>   engine_;
  shared_ptr<sdb::SymbolDatabase>     sdb_;
  shared_ptr<llvm::LLVMContext>       context_;
  shared_ptr<llvm::Module>            gen_module_;
  auto_ptr<codegen::ModuleGenerator>  codegen_;

  uint32_t    code_addr_low_;
  uint32_t    code_addr_high_;
  FunctionMap fns_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_USERMODULE_H_

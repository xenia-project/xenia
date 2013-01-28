/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_CODEGEN_MODULE_GENERATOR_H_
#define XENIA_CPU_CODEGEN_MODULE_GENERATOR_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <tr1/unordered_map>

#include <xenia/cpu/sdb.h>
#include <xenia/core/memory.h>
#include <xenia/kernel/export.h>
#include <xenia/kernel/user_module.h>


namespace llvm {
  class DIBuilder;
  class ExecutionEngine;
  class Function;
  class FunctionType;
  class LLVMContext;
  class Module;
  class MDNode;
}


namespace xe {
namespace cpu {
namespace codegen {


class ModuleGenerator {
public:
  ModuleGenerator(
      xe_memory_ref memory, kernel::ExportResolver* export_resolver,
      const char* module_name, const char* module_path,
      sdb::SymbolDatabase* sdb,
      llvm::LLVMContext* context, llvm::Module* gen_module,
      llvm::ExecutionEngine* engine);
  ~ModuleGenerator();

  int Generate();

  void AddFunctionsToMap(
      std::tr1::unordered_map<uint32_t, llvm::Function*>& map);

private:
  class CodegenFunction {
  public:
    sdb::FunctionSymbol*    symbol;
    llvm::FunctionType*     function_type;
    llvm::Function*         function;
  };

  CodegenFunction* GetCodegenFunction(uint32_t address);

  void AddImports();
  llvm::Function* CreateFunctionDefinition(const char* name);
  void AddMissingImport(sdb::FunctionSymbol* fn);
  void AddPresentImport(sdb::FunctionSymbol* fn);
  void PrepareFunction(sdb::FunctionSymbol* fn);
  void BuildFunction(CodegenFunction* cgf);
  void OptimizeFunction(llvm::Module* m, llvm::Function* fn);

  xe_memory_ref memory_;
  kernel::ExportResolver* export_resolver_;
  char* module_name_;
  char* module_path_;
  sdb::SymbolDatabase* sdb_;

  llvm::LLVMContext*  context_;
  llvm::Module*       gen_module_;
  llvm::ExecutionEngine* engine_;
  llvm::DIBuilder*    di_builder_;
  llvm::MDNode*       cu_;

  std::map<uint32_t, CodegenFunction*> functions_;
};


}  // namespace codegen
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_CODEGEN_MODULE_GENERATOR_H_

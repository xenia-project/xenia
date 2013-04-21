/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_LLVMBE_LLVM_CODE_UNIT_BUILDER_H_
#define XENIA_CPU_LLVMBE_LLVM_CODE_UNIT_BUILDER_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/cpu/code_unit_builder.h>
#include <xenia/cpu/sdb.h>


namespace llvm {
  class DIBuilder;
  class Function;
  class FunctionPassManager;
  class LLVMContext;
  class Module;
  class MDNode;
}


namespace xe {
namespace cpu {
namespace llvmbe {


class EmitterContext;


class LLVMCodeUnitBuilder : public CodeUnitBuilder {
public:
  LLVMCodeUnitBuilder(xe_memory_ref memory, llvm::LLVMContext* context);
  virtual ~LLVMCodeUnitBuilder();

  llvm::Module* module();

  virtual int Init(const char* module_name, const char* module_path);
  virtual int MakeFunction(sdb::FunctionSymbol* symbol);
  int MakeFunction(sdb::FunctionSymbol* symbol, llvm::Function** out_fn);
  virtual int Finalize();
  virtual void Reset();

private:
  llvm::Function* CreateFunctionDeclaration(const char* name);
  int MakeUserFunction(sdb::FunctionSymbol* symbol,
                       llvm::Function* f);
  int MakePresentImportFunction(sdb::FunctionSymbol* symbol,
                                llvm::Function* f);
  int MakeMissingImportFunction(sdb::FunctionSymbol* symbol,
                                llvm::Function* f);
  void OptimizeFunction(llvm::Function* f);

  llvm::LLVMContext*          context_;
  llvm::Module*               module_;
  llvm::FunctionPassManager*  fpm_;
  llvm::DIBuilder*            di_builder_;
  llvm::MDNode*               cu_;
  EmitterContext*             emitter_;

  std::map<uint32_t, llvm::Function*> functions_;
};


}  // namespace llvmbe
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_LLVMBE_LLVM_CODE_UNIT_BUILDER_H_

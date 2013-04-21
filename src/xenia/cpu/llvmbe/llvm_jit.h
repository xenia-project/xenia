/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_LLVMBE_LLVM_JIT_H_
#define XENIA_CPU_LLVMBE_LLVM_JIT_H_

#include <xenia/core.h>

#include <xenia/cpu/function_table.h>
#include <xenia/cpu/jit.h>
#include <xenia/cpu/ppc.h>
#include <xenia/cpu/sdb.h>


namespace llvm {
  class ExecutionEngine;
  class LLVMContext;
  class Module;
}


namespace xe {
namespace cpu {
namespace llvmbe {


class LLVMCodeUnitBuilder;


class LLVMJIT : public JIT {
public:
  LLVMJIT(xe_memory_ref memory, FunctionTable* fn_table);
  virtual ~LLVMJIT();

  virtual int Setup();

  virtual int InitModule(ExecModule* module);
  virtual int UninitModule(ExecModule* module);

  virtual FunctionPointer GenerateFunction(sdb::FunctionSymbol* symbol);

protected:
  int InjectGlobals();

  llvm::LLVMContext*      context_;
  llvm::ExecutionEngine*  engine_;
  llvm::Module*           module_;

  LLVMCodeUnitBuilder*    cub_;
};


}  // namespace llvmbe
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_LLVMBE_LLVM_JIT_H_

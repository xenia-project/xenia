/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_CODEGEN_FUNCTION_GENERATOR_H_
#define XENIA_CPU_CODEGEN_FUNCTION_GENERATOR_H_

#include <llvm/IR/Attributes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <xenia/cpu/sdb.h>
#include <xenia/cpu/ppc/instr.h>


namespace xe {
namespace cpu {
namespace codegen {


class FunctionGenerator {
public:
  FunctionGenerator(xe_memory_ref memory, sdb::FunctionSymbol* fn,
                    llvm::LLVMContext* context, llvm::Module* gen_module,
                    llvm::Function* gen_fn);
  ~FunctionGenerator();

  sdb::FunctionSymbol* fn();
  llvm::LLVMContext* context();
  llvm::Module* gen_module();
  llvm::Function* gen_fn();

  void GenerateBasicBlocks();
  llvm::BasicBlock* GetBasicBlock(uint32_t address);

  llvm::Function* GetFunction(uint32_t addr);

  llvm::Value* cia_value();

  void FlushRegisters();

  llvm::Value* xer_value();
  void update_xer_value(llvm::Value* value);
  llvm::Value* lr_value();
  void update_lr_value(llvm::Value* value);
  llvm::Value* ctr_value();
  void update_ctr_value(llvm::Value* value);

  llvm::Value* cr_value(uint32_t n);
  void update_cr_value(uint32_t n, llvm::Value* value);

  llvm::Value* gpr_value(uint32_t n);
  void update_gpr_value(uint32_t n, llvm::Value* value);

  llvm::Value* memory_addr(uint32_t addr);
  llvm::Value* read_memory(llvm::Value* addr, uint32_t size, bool extend);
  void write_memory(llvm::Value* addr, uint32_t size, llvm::Value* value);

private:
  void GenerateBasicBlock(sdb::FunctionBlock* block, llvm::BasicBlock* bb);

  xe_memory_ref       memory_;
  sdb::FunctionSymbol* fn_;
  llvm::LLVMContext*  context_;
  llvm::Module*       gen_module_;
  llvm::Function*     gen_fn_;
  llvm::IRBuilder<>*    builder_;

  std::map<uint32_t, llvm::BasicBlock*> bbs_;

  // Address of the instruction being generated.
  uint32_t      cia_;

  struct {
    llvm::Value*  xer;
    llvm::Value*  lr;
    llvm::Value*  ctr;

    llvm::Value*  cr[4];

    llvm::Value*  gpr[32];
  } values_;
};


}  // namespace codegen
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_CODEGEN_FUNCTION_GENERATOR_H_

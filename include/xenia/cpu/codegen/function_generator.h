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
  FunctionGenerator(
      xe_memory_ref memory, sdb::SymbolDatabase* sdb, sdb::FunctionSymbol* fn,
      llvm::LLVMContext* context, llvm::Module* gen_module,
      llvm::Function* gen_fn);
  ~FunctionGenerator();

  sdb::SymbolDatabase* sdb();
  sdb::FunctionSymbol* fn();
  llvm::LLVMContext* context();
  llvm::Module* gen_module();
  llvm::Function* gen_fn();
  sdb::FunctionBlock* fn_block();

  void PushInsertPoint();
  void PopInsertPoint();

  void GenerateBasicBlocks();
  llvm::BasicBlock* GetBasicBlock(uint32_t address);
  llvm::BasicBlock* GetNextBasicBlock();
  llvm::BasicBlock* GetReturnBasicBlock();

  llvm::Function* GetFunction(sdb::FunctionSymbol* fn);

  int GenerateIndirectionBranch(uint32_t cia, llvm::Value* target,
                                bool lk, bool likely_local);

  llvm::Value* LoadStateValue(uint32_t offset, llvm::Type* type,
                              const char* name = "");
  void StoreStateValue(uint32_t offset, llvm::Type* type, llvm::Value* value);

  llvm::Value* cia_value();

  llvm::Value* SetupLocal(llvm::Type* type, const char* name);
  void FillRegisters();
  void SpillRegisters();

  llvm::Value* xer_value();
  void update_xer_value(llvm::Value* value);
  void update_xer_with_overflow(llvm::Value* value);
  void update_xer_with_carry(llvm::Value* value);
  void update_xer_with_overflow_and_carry(llvm::Value* value);

  llvm::Value* lr_value();
  void update_lr_value(llvm::Value* value);

  llvm::Value* ctr_value();
  void update_ctr_value(llvm::Value* value);

  llvm::Value* cr_value(uint32_t n);
  void update_cr_value(uint32_t n, llvm::Value* value);
  void update_cr_with_cond(uint32_t n, llvm::Value* lhs, llvm::Value* rhs,
                           bool is_signed);

  llvm::Value* gpr_value(uint32_t n);
  void update_gpr_value(uint32_t n, llvm::Value* value);

  llvm::Value* GetMembase();
  llvm::Value* GetMemoryAddress(llvm::Value* addr);
  llvm::Value* ReadMemory(llvm::Value* addr, uint32_t size, bool extend);
  void WriteMemory(llvm::Value* addr, uint32_t size, llvm::Value* value);

private:
  void GenerateSharedBlocks();
  void GenerateBasicBlock(sdb::FunctionBlock* block, llvm::BasicBlock* bb);

  void setup_xer();
  void setup_lr();
  void setup_ctr();
  void setup_cr(uint32_t n);
  void setup_gpr(uint32_t n);

  xe_memory_ref         memory_;
  sdb::SymbolDatabase*  sdb_;
  sdb::FunctionSymbol*  fn_;
  llvm::LLVMContext*    context_;
  llvm::Module*         gen_module_;
  llvm::Function*       gen_fn_;
  sdb::FunctionBlock*   fn_block_;
  llvm::BasicBlock*     return_block_;
  llvm::BasicBlock*     internal_indirection_block_;
  llvm::BasicBlock*     external_indirection_block_;
  llvm::BasicBlock*     bb_;
  llvm::IRBuilder<>*    builder_;

  std::vector<std::pair<llvm::BasicBlock*, llvm::BasicBlock::iterator> >
      insert_points_;

  std::map<uint32_t, llvm::BasicBlock*> bbs_;

  // Address of the instruction being generated.
  uint32_t      cia_;

  struct {
    llvm::Value*  indirection_target;
    llvm::Value*  indirection_cia;

    llvm::Value*  xer;
    llvm::Value*  lr;
    llvm::Value*  ctr;
    llvm::Value*  cr[8];
    llvm::Value*  gpr[32];
  } locals_;
};


}  // namespace codegen
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_CODEGEN_FUNCTION_GENERATOR_H_

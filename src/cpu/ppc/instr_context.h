/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_INSTR_CONTEXT_H_
#define XENIA_CPU_PPC_INSTR_CONTEXT_H_

#include <xenia/cpu/ppc/instr.h>

#include <xenia/cpu/sdb.h>


namespace llvm {
  class LLVMContext;
  class Module;
  class Function;
  class BasicBlock;
  class Value;
}


namespace xe {
namespace cpu {
namespace ppc {


class InstrContext {
public:
  InstrContext(sdb::FunctionSymbol* fn, llvm::LLVMContext* context,
               llvm::Module* gen_module, llvm::Function* gen_fn);
  ~InstrContext();

  sdb::FunctionSymbol* fn();
  llvm::LLVMContext* context();
  llvm::Module* gen_module();
  llvm::Function* gen_fn();

  void AddBasicBlock();
  void GenerateBasicBlocks();
  llvm::BasicBlock* GetBasicBlock(uint32_t address);

  llvm::Function* GetFunction(uint32_t addr);

  llvm::Value* cia_value();
  llvm::Value* nia_value();
  void update_nia_value(llvm::Value* value);
  llvm::Value* spr_value(uint32_t n);
  void update_spr_value(uint32_t n, llvm::Value* value);

  llvm::Value* cr_value();
  void update_cr_value(llvm::Value* value);

  llvm::Value* gpr_value(uint32_t n);
  void update_gpr_value(uint32_t n, llvm::Value* value);

  llvm::Value* memory_addr(uint32_t addr);
  llvm::Value* read_memory(llvm::Value* addr, uint32_t size, bool extend);
  void write_memory(llvm::Value* addr, uint32_t size, llvm::Value* value);

private:
  sdb::FunctionSymbol* fn_;
  llvm::LLVMContext*  context_;
  llvm::Module*       gen_module_;
  llvm::Function*     gen_fn_;
  // TODO(benvanik): IRBuilder/etc

  // Address of the instruction being generated.
  uint32_t      cia_;
};


}  // namespace ppc
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PPC_INSTR_CONTEXT_H_

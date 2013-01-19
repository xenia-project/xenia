/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_INSTR_CTX_H_
#define XENIA_CPU_PPC_INSTR_CTX_H_

#include <xenia/cpu/ppc/instr.h>


typedef struct {
  llvm::LLVMContext   *context;
  llvm::Module        *module;

  // Address of the instruction being generated.
  uint32_t            cia;

  llvm::Value *get_cia();
  void set_nia(llvm::Value *value);
  llvm::Value *get_spr(uint32_t n);
  void set_spr(uint32_t n, llvm::Value *value);

  llvm::Value *get_cr();
  void set_cr(llvm::Value *value);

  llvm::Value *get_gpr(uint32_t n);
  void set_gpr(uint32_t n, llvm::Value *value);

  llvm::Value *get_memory_addr(uint32_t addr);
  llvm::Value *read_memory(llvm::Value *addr, uint32_t size, bool extend);
  void write_memory(llvm::Value *addr, uint32_t size, llvm::Value *value);
} xe_ppc_instr_ctx_t;


#endif  // XENIA_CPU_PPC_INSTR_CTX_H_

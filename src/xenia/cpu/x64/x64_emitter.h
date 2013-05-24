/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_X64_X64_EMITTER_H_
#define XENIA_CPU_X64_X64_EMITTER_H_

#include <xenia/cpu/global_exports.h>
#include <xenia/cpu/sdb.h>
#include <xenia/cpu/ppc/instr.h>

#include <asmjit/asmjit.h>


namespace xe {
namespace cpu {
namespace x64 {


// Typedef for all generated functions.
typedef void (*x64_function_t)(xe_ppc_state_t* ppc_state, uint64_t lr);


class X64Emitter {
public:
  X64Emitter(xe_memory_ref memory);
  ~X64Emitter();

  void Lock();
  void Unlock();

  int PrepareFunction(sdb::FunctionSymbol* symbol);
  int MakeFunction(sdb::FunctionSymbol* symbol);

  AsmJit::X86Compiler& compiler();
  sdb::FunctionSymbol* symbol();
  sdb::FunctionBlock* fn_block();

  // jit_value_t get_int32(int32_t value);
  // jit_value_t get_uint32(uint32_t value);
  // jit_value_t get_int64(int64_t value);
  // jit_value_t get_uint64(uint64_t value);
  // jit_value_t make_signed(jit_value_t value);
  // jit_value_t make_unsigned(jit_value_t value);
  // jit_value_t sign_extend(jit_value_t value, jit_type_t target_type);
  // jit_value_t zero_extend(jit_value_t value, jit_type_t target_type);
  // jit_value_t trunc_to_sbyte(jit_value_t value);
  // jit_value_t trunc_to_ubyte(jit_value_t value);
  // jit_value_t trunc_to_short(jit_value_t value);
  // jit_value_t trunc_to_int(jit_value_t value);

  // int branch_to_block(uint32_t address);
  // int branch_to_block_if(uint32_t address, jit_value_t value);
  // int branch_to_block_if_not(uint32_t address, jit_value_t value);
  // int branch_to_return();
  // int branch_to_return_if(jit_value_t value);
  // int branch_to_return_if_not(jit_value_t value);
  // int call_function(sdb::FunctionSymbol* target_symbol, jit_value_t lr,
  //                   bool tail);

  void TraceKernelCall();
  void TraceUserCall();
  void TraceInstruction(ppc::InstrData& i);
  void TraceInvalidInstruction(ppc::InstrData& i);
  void TraceBranch(uint32_t cia);

  // int GenerateIndirectionBranch(uint32_t cia, jit_value_t target,
  //                               bool lk, bool likely_local);

  // jit_value_t LoadStateValue(size_t offset, jit_type_t type,
  //                            const char* name = "");
  // void StoreStateValue(size_t offset, jit_type_t type, jit_value_t value);

  // jit_value_t SetupLocal(jit_type_t type, const char* name);
  void FillRegisters();
  void SpillRegisters();

  // jit_value_t xer_value();
  // void update_xer_value(jit_value_t value);
  // void update_xer_with_overflow(jit_value_t value);
  // void update_xer_with_carry(jit_value_t value);
  // void update_xer_with_overflow_and_carry(jit_value_t value);

  // jit_value_t lr_value();
  // void update_lr_value(jit_value_t value);

  // jit_value_t ctr_value();
  // void update_ctr_value(jit_value_t value);

  // jit_value_t cr_value(uint32_t n);
  // void update_cr_value(uint32_t n, jit_value_t value);
  // void update_cr_with_cond(uint32_t n, jit_value_t lhs, jit_value_t rhs,
  //                          bool is_signed);

  // jit_value_t gpr_value(uint32_t n);
  // void update_gpr_value(uint32_t n, jit_value_t value);
  // jit_value_t fpr_value(uint32_t n);
  // void update_fpr_value(uint32_t n, jit_value_t value);

  // jit_value_t TouchMemoryAddress(uint32_t cia, jit_value_t addr);
  // jit_value_t ReadMemory(
  //     uint32_t cia, jit_value_t addr, uint32_t size, bool acquire = false);
  // void WriteMemory(
  //     uint32_t cia, jit_value_t addr, uint32_t size, jit_value_t value,
  //     bool release = false);

private:
  static void* OnDemandCompileTrampoline(
      X64Emitter* emitter, sdb::FunctionSymbol* symbol);
  void* OnDemandCompile(sdb::FunctionSymbol* symbol);
  int MakeUserFunction();
  int MakePresentImportFunction();
  int MakeMissingImportFunction();

  void GenerateSharedBlocks();
  int PrepareBasicBlock(sdb::FunctionBlock* block);
  void GenerateBasicBlock(sdb::FunctionBlock* block);
  void SetupLocals();

  xe_memory_ref         memory_;
  GlobalExports         global_exports_;
  xe_mutex_t*           lock_;

  AsmJit::Logger*       logger_;
  AsmJit::X86Assembler  assembler_;
  AsmJit::X86Compiler   compiler_;

  sdb::FunctionSymbol*  symbol_;
  sdb::FunctionBlock*   fn_block_;
  AsmJit::Label         return_block_;
  // jit_label_t           internal_indirection_block_;
  // jit_label_t           external_indirection_block_;

  std::map<uint32_t, AsmJit::Label> bbs_;

  ppc::InstrAccessBits access_bits_;
  // struct {
  //   jit_value_t  indirection_target;
  //   jit_value_t  indirection_cia;

  //   jit_value_t  xer;
  //   jit_value_t  lr;
  //   jit_value_t  ctr;
  //   jit_value_t  cr[8];
  //   jit_value_t  gpr[32];
  //   jit_value_t  fpr[32];
  // } locals_;
};


}  // namespace x64
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_X64_X64_EMITTER_H_

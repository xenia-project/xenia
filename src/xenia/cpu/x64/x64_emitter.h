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

  void SetupGpuPointers(void* gpu_this, void* gpu_read, void* gpu_write);

  void Lock();
  void Unlock();

  uint32_t cpu_feature_mask() const { return cpu_feature_mask_; }

  int PrepareFunction(sdb::FunctionSymbol* symbol);
  int MakeFunction(sdb::FunctionSymbol* symbol);

  AsmJit::X86Compiler& compiler();
  sdb::FunctionSymbol* symbol();
  sdb::FunctionBlock* fn_block();

  AsmJit::Label& GetReturnLabel();
  AsmJit::Label& GetBlockLabel(uint32_t address);
  int CallFunction(sdb::FunctionSymbol* target_symbol, AsmJit::GpVar& lr,
                   bool tail);

  void TraceKernelCall();
  void TraceUserCall();
  void TraceInstruction(ppc::InstrData& i);
  void TraceInvalidInstruction(ppc::InstrData& i);
  void TraceBranch(uint32_t cia);
  void TraceFPR(uint32_t fpr0, uint32_t fpr1 = UINT_MAX,
                uint32_t fpr2 = UINT_MAX, uint32_t fpr3 = UINT_MAX,
                uint32_t fpr4 = UINT_MAX);
  void TraceVR(uint32_t vr0, uint32_t vr1 = UINT_MAX, uint32_t vr2 = UINT_MAX,
               uint32_t vr3 = UINT_MAX, uint32_t vr4 = UINT_MAX);

  int GenerateIndirectionBranch(uint32_t cia, AsmJit::GpVar& target,
                                bool lk, bool likely_local);

  AsmJit::GpVar read_gpu_register(uint32_t r);
  void write_gpu_register(uint32_t r, AsmJit::GpVar& v);

  void FillRegisters();
  void SpillRegisters();

  bool get_constant_gpr_value(uint32_t n, uint64_t* value);
  void set_constant_gpr_value(uint32_t n, uint64_t value);
  void clear_constant_gpr_value(uint32_t n);
  void clear_all_constant_gpr_values();

  AsmJit::GpVar xer_value();
  void update_xer_value(AsmJit::GpVar& value);
  void update_xer_with_overflow(AsmJit::GpVar& value);
  void update_xer_with_carry(AsmJit::GpVar& value);
  void update_xer_with_overflow_and_carry(AsmJit::GpVar& value);

  AsmJit::GpVar lr_value();
  void update_lr_value(AsmJit::GpVar& value);
  void update_lr_value(AsmJit::Imm& imm);

  AsmJit::GpVar ctr_value();
  void update_ctr_value(AsmJit::GpVar& value);

  AsmJit::GpVar cr_value(uint32_t n);
  void update_cr_value(uint32_t n, AsmJit::GpVar& value);
  void update_cr_with_cond(uint32_t n, AsmJit::GpVar& lhs,
                           bool is_signed = true);
  void update_cr_with_cond(uint32_t n, AsmJit::GpVar& lhs, AsmJit::GpVar& rhs,
                           bool is_signed = true);

  AsmJit::GpVar gpr_value(uint32_t n);
  void update_gpr_value(uint32_t n, AsmJit::GpVar& value);
  AsmJit::XmmVar fpr_value(uint32_t n);
  void update_fpr_value(uint32_t n, AsmJit::XmmVar& value);
  AsmJit::XmmVar vr_value(uint32_t n);
  void update_vr_value(uint32_t n, AsmJit::XmmVar& value);

  AsmJit::GpVar TouchMemoryAddress(uint32_t cia, AsmJit::GpVar& addr);
  AsmJit::GpVar ReadMemory(
      uint32_t cia, AsmJit::GpVar& addr, uint32_t size, bool acquire = false);
  void WriteMemory(
      uint32_t cia, AsmJit::GpVar& addr, uint32_t size, AsmJit::GpVar& value,
      bool release = false);
  void ByteSwapXmm(AsmJit::XmmVar& value);
  AsmJit::XmmVar ReadMemoryXmm(
      uint32_t cia, AsmJit::GpVar& addr, uint32_t alignment);
  void WriteMemoryXmm(
      uint32_t cia, AsmJit::GpVar& addr, uint32_t alignment,
      AsmJit::XmmVar& value);

  AsmJit::GpVar get_uint64(uint64_t value);
  AsmJit::GpVar sign_extend(AsmJit::GpVar& value, int from_size, int to_size);
  AsmJit::GpVar zero_extend(AsmJit::GpVar& value, int from_size, int to_size);
  AsmJit::GpVar trunc(AsmJit::GpVar& value, int size);

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
  uint32_t              cpu_feature_mask_;

  void*                 gpu_this_;
  void*                 gpu_read_;
  void*                 gpu_write_;

  AsmJit::Logger*       logger_;
  AsmJit::X86Assembler  assembler_;
  AsmJit::X86Compiler   compiler_;

  sdb::FunctionSymbol*  symbol_;
  sdb::FunctionBlock*   fn_block_;
  AsmJit::Label         return_block_;
  AsmJit::Label         internal_indirection_block_;
  AsmJit::Label         external_indirection_block_;

  std::map<uint32_t, AsmJit::Label> bbs_;

  ppc::InstrAccessBits  access_bits_;
  struct {
    bool      is_constant;
    uint64_t  value;
  } gpr_values_[32];
  struct {
    AsmJit::GpVar   indirection_target;
    AsmJit::GpVar   indirection_cia;

    AsmJit::GpVar   xer;
    AsmJit::GpVar   lr;
    AsmJit::GpVar   ctr;
    AsmJit::GpVar   cr[8];
    AsmJit::GpVar   gpr[32];
    AsmJit::XmmVar  fpr[32];
    AsmJit::XmmVar  vr[128];
  } locals_;
};


}  // namespace x64
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_X64_X64_EMITTER_H_

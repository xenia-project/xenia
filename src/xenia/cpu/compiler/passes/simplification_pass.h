/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_SIMPLIFICATION_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_SIMPLIFICATION_PASS_H_

#include "xenia/cpu/compiler/passes/conditional_group_subpass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {
using ScalarNZM = uint64_t;
class SimplificationPass : public ConditionalGroupSubpass {
 public:
  SimplificationPass();
  ~SimplificationPass() override;

  bool Run(hir::HIRBuilder* builder, bool& result) override;

 private:
  bool EliminateConversions(hir::HIRBuilder* builder);
  bool CheckTruncate(hir::Instr* i);
  bool CheckByteSwap(hir::Instr* i);

  bool SimplifyAssignments(hir::HIRBuilder* builder);
  hir::Value* CheckValue(hir::Value* value, bool& result);
  bool SimplifyBitArith(hir::HIRBuilder* builder);

  // handles simple multiplication/addition rules
  bool SimplifyBasicArith(hir::HIRBuilder* builder);

bool SimplifyVectorOps(hir::HIRBuilder* builder);
  bool SimplifyVectorOps(hir::Instr* i, hir::HIRBuilder* builder);
  bool SimplifyBasicArith(hir::Instr* i, hir::HIRBuilder* builder);
  bool SimplifyAddWithSHL(hir::Instr* i, hir::HIRBuilder* builder);
  bool SimplifyAddToSelf(hir::Instr* i, hir::HIRBuilder* builder);
  bool SimplifyAddArith(hir::Instr* i, hir::HIRBuilder* builder);
  bool SimplifySubArith(hir::Instr* i, hir::HIRBuilder* builder);
  bool SimplifySHLArith(hir::Instr* i, hir::HIRBuilder* builder);
  // handle either or or xor with 0
  bool CheckOrXorZero(hir::Instr* i);
  bool CheckOr(hir::Instr* i, hir::HIRBuilder* builder);
  bool CheckXor(hir::Instr* i, hir::HIRBuilder* builder);
  bool CheckAnd(hir::Instr* i, hir::HIRBuilder* builder);
  bool CheckAdd(hir::Instr* i, hir::HIRBuilder* builder);
  bool CheckSelect(hir::Instr* i, hir::HIRBuilder* builder,
                   hir::Value* condition, hir::Value* iftrue,
                   hir::Value* iffalse);
  bool CheckSelect(hir::Instr* i, hir::HIRBuilder* builder);
  bool CheckScalarConstCmp(hir::Instr* i, hir::HIRBuilder* builder);
  bool CheckIsTrueIsFalse(hir::Instr* i, hir::HIRBuilder* builder);
  bool CheckSHRByConst(hir::Instr* i, hir::HIRBuilder* builder,
                       hir::Value* variable, unsigned int shift);

  bool CheckSHR(hir::Instr* i, hir::HIRBuilder* builder);
  bool CheckSAR(hir::Instr* i, hir::HIRBuilder* builder);
  // called by CheckXor, handles transforming a 1 bit value xored against 1
  bool CheckBooleanXor1(hir::Instr* i, hir::HIRBuilder* builder,
                        hir::Value* xored);
  bool CheckXorOfTwoBools(hir::Instr* i, hir::HIRBuilder* builder,
                          hir::Value* b1, hir::Value* b2);

  // for rlwinm
  bool TryHandleANDROLORSHLSeq(hir::Instr* i, hir::HIRBuilder* builder);
  bool TransformANDROLORSHLSeq(
      hir::Instr* i,  // the final instr in the rlwinm sequence (the AND)
      hir::HIRBuilder* builder,
      ScalarNZM input_nzm,  // the NZM of the value being rot'ed/masked (input)
      uint64_t mask,        // mask after rotation
      uint64_t rotation,    // rot amount prior to masking
      hir::Instr* input_def,  // the defining instr of the input var
      hir::Value* input       // the input variable to the rlwinm
  );
  static bool Is1BitOpcode(hir::Opcode def_opcode);

  /* we currently have no caching, and although it is rare some games
             may have massive (in the hundreds) chains of ands + ors + shifts in
             a single unbroken basic block (exsploderman web of shadows) */
  static constexpr unsigned MAX_NZM_DEPTH = 16;
  // todo: use valuemask
  // returns maybenonzeromask for value (mask of bits that may possibly hold
  // information)
  static ScalarNZM GetScalarNZM(const hir::Value* value, unsigned depth = 0);
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_SIMPLIFICATION_PASS_H_

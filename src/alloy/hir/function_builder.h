/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_HIR_FUNCTION_BUILDER_H_
#define ALLOY_HIR_FUNCTION_BUILDER_H_

#include <alloy/core.h>
#include <alloy/hir/block.h>
#include <alloy/hir/instr.h>
#include <alloy/hir/opcodes.h>
#include <alloy/hir/value.h>


namespace alloy {
namespace hir {

enum FunctionAttributes {
  FUNCTION_ATTRIB_INLINE    = (1 << 1),
};


class FunctionBuilder {
public:
  FunctionBuilder();
  virtual ~FunctionBuilder();

  virtual void Reset();

  void Dump(StringBuffer* str);

  uint32_t attributes() const { return attributes_; }
  void set_attributes(uint32_t value) { attributes_ = value; }

  Block* first_block() const { return block_head_; }
  Block* current_block() const;
  Instr* last_instr() const;

  Label* NewLabel();
  void MarkLabel(Label* label);
  void InsertLabel(Label* label, Instr* prev_instr);

  // static allocations:
  // Value* AllocStatic(size_t length);

  // stack allocations:
  // Value* AllocLocal(TypeName type);

  void Comment(const char* format, ...);

  void Nop();

  // trace info/etc
  void DebugBreak();
  void DebugBreakTrue(Value* cond);

  void Trap();
  void TrapTrue(Value* cond);

  void Call(runtime::FunctionInfo* symbol_info, uint32_t call_flags = 0);
  void CallTrue(Value* cond, runtime::FunctionInfo* symbol_info,
                uint32_t call_flags = 0);
  void CallIndirect(Value* value, uint32_t call_flags = 0);
  void CallIndirectTrue(Value* cond, Value* value, uint32_t call_flags = 0);
  void Return();

  void Branch(Label* label, uint32_t branch_flags = 0);
  void BranchIf(Value* cond, Label* true_label, Label* false_label,
                uint32_t branch_flags = 0);
  void BranchTrue(Value* cond, Label* label,
                  uint32_t branch_flags = 0);
  void BranchFalse(Value* cond, Label* label,
                   uint32_t branch_flags = 0);

  // phi type_name, Block* b1, Value* v1, Block* b2, Value* v2, etc

  Value* Assign(Value* value);
  Value* Cast(Value* value, TypeName target_type);
  Value* ZeroExtend(Value* value, TypeName target_type);
  Value* SignExtend(Value* value, TypeName target_type);
  Value* Truncate(Value* value, TypeName target_type);
  Value* Convert(Value* value, TypeName target_type,
                 RoundMode round_mode = ROUND_TO_ZERO);
  Value* Round(Value* value, RoundMode round_mode);

  // TODO(benvanik): make this cleaner -- not happy with it.
  //     It'd be nice if Convert() supported this, however then we'd need a
  //     VEC128_INT32_TYPE or something.
  Value* VectorConvertI2F(Value* value);
  Value* VectorConvertF2I(Value* value, RoundMode round_mode = ROUND_TO_ZERO);

  Value* LoadZero(TypeName type);
  Value* LoadConstant(int8_t value);
  Value* LoadConstant(uint8_t value);
  Value* LoadConstant(int16_t value);
  Value* LoadConstant(uint16_t value);
  Value* LoadConstant(int32_t value);
  Value* LoadConstant(uint32_t value);
  Value* LoadConstant(int64_t value);
  Value* LoadConstant(uint64_t value);
  Value* LoadConstant(float value);
  Value* LoadConstant(double value);
  Value* LoadConstant(const vec128_t& value);

  Value* LoadContext(size_t offset, TypeName type);
  void StoreContext(size_t offset, Value* value);

  Value* Load(Value* address, TypeName type, uint32_t load_flags = 0);
  Value* LoadAcquire(Value* address, TypeName type, uint32_t load_flags = 0);
  void Store(Value* address, Value* value, uint32_t store_flags = 0);
  Value* StoreRelease(Value* address, Value* value, uint32_t store_flags = 0);
  void Prefetch(Value* address, size_t length, uint32_t prefetch_flags = 0);

  Value* Max(Value* value1, Value* value2);
  Value* Min(Value* value1, Value* value2);
  Value* Select(Value* cond, Value* value1, Value* value2);
  Value* IsTrue(Value* value);
  Value* IsFalse(Value* value);
  Value* CompareEQ(Value* value1, Value* value2);
  Value* CompareNE(Value* value1, Value* value2);
  Value* CompareSLT(Value* value1, Value* value2);
  Value* CompareSLE(Value* value1, Value* value2);
  Value* CompareSGT(Value* value1, Value* value2);
  Value* CompareSGE(Value* value1, Value* value2);
  Value* CompareULT(Value* value1, Value* value2);
  Value* CompareULE(Value* value1, Value* value2);
  Value* CompareUGT(Value* value1, Value* value2);
  Value* CompareUGE(Value* value1, Value* value2);
  Value* DidCarry(Value* value);
  Value* DidOverflow(Value* value);
  Value* VectorCompareEQ(Value* value1, Value* value2, TypeName part_type);
  Value* VectorCompareSGT(Value* value1, Value* value2, TypeName part_type);
  Value* VectorCompareSGE(Value* value1, Value* value2, TypeName part_type);
  Value* VectorCompareUGT(Value* value1, Value* value2, TypeName part_type);
  Value* VectorCompareUGE(Value* value1, Value* value2, TypeName part_type);

  Value* Add(Value* value1, Value* value2, uint32_t arithmetic_flags = 0);
  Value* AddWithCarry(Value* value1, Value* value2, Value* value3,
                      uint32_t arithmetic_flags = 0);
  Value* Sub(Value* value1, Value* value2,
             uint32_t arithmetic_flags = 0);
  Value* Mul(Value* value1, Value* value2);
  Value* Div(Value* value1, Value* value2);
  Value* Rem(Value* value1, Value* value2);
  Value* MulAdd(Value* value1, Value* value2, Value* value3); // (1 * 2) + 3
  Value* MulSub(Value* value1, Value* value2, Value* value3); // (1 * 2) - 3
  Value* Neg(Value* value);
  Value* Abs(Value* value);
  Value* Sqrt(Value* value);
  Value* RSqrt(Value* value);
  Value* DotProduct3(Value* value1, Value* value2);
  Value* DotProduct4(Value* value1, Value* value2);

  Value* And(Value* value1, Value* value2);
  Value* Or(Value* value1, Value* value2);
  Value* Xor(Value* value1, Value* value2);
  Value* Not(Value* value);
  Value* Shl(Value* value1, Value* value2);
  Value* VectorShl(Value* value1, Value* value2, TypeName part_type);
  Value* Shl(Value* value1, int8_t value2);
  Value* Shr(Value* value1, Value* value2);
  Value* Shr(Value* value1, int8_t value2);
  Value* Sha(Value* value1, Value* value2);
  Value* Sha(Value* value1, int8_t value2);
  Value* RotateLeft(Value* value1, Value* value2);
  Value* ByteSwap(Value* value);
  Value* CountLeadingZeros(Value* value);
  Value* Insert(Value* value, uint32_t index, Value* part);
  Value* Extract(Value* value, uint32_t index, TypeName target_type);
  // i8->i16/i32/... (i8|i8 / i8|i8|i8|i8 / ...)
  // i8/i16/i32 -> vec128
  Value* Splat(Value* value, TypeName target_type);
  Value* Permute(Value* control, Value* value1, Value* value2,
                 TypeName part_type);
  Value* Swizzle(Value* value, TypeName part_type, uint32_t swizzle_mask);
  // SelectBits(cond, value1, value2)
  // pack/unpack/etc

  Value* CompareExchange(Value* address,
                         Value* compare_value, Value* exchange_value);
  Value* AtomicAdd(Value* address, Value* value);
  Value* AtomicSub(Value* address, Value* value);

protected:
  void DumpValue(StringBuffer* str, Value* value);
  void DumpOp(
      StringBuffer* str, OpcodeSignatureType sig_type, Instr::Op* op);

  Value* AllocValue(TypeName type = INT64_TYPE);
  Value* CloneValue(Value* source);

private:
  Block* AppendBlock();
  void EndBlock();
  Instr* AppendInstr(const OpcodeInfo& opcode, uint16_t flags,
                     Value* dest = 0);
  Value* CompareXX(const OpcodeInfo& opcode, Value* value1, Value* value2);
  Value* VectorCompareXX(
      const OpcodeInfo& opcode, Value* value1, Value* value2, TypeName part_type);

protected:
  Arena*    arena_;

private:
  uint32_t  attributes_;

  uint32_t  next_label_id_;
  uint32_t  next_value_ordinal_;

  Block*    block_head_;
  Block*    block_tail_;
  Block*    current_block_;
};


}  // namespace hir
}  // namespace alloy


#endif  // ALLOY_HIR_FUNCTION_BUILDER_H_

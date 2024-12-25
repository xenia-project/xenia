/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_HIR_HIR_BUILDER_H_
#define XENIA_CPU_HIR_HIR_BUILDER_H_

#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/arena.h"
#include "xenia/base/string_buffer.h"

#include "xenia/base/simple_freelist.h"
#include "xenia/cpu/hir/block.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/hir/label.h"
#include "xenia/cpu/hir/opcodes.h"
#include "xenia/cpu/hir/value.h"
#include "xenia/cpu/mmio_handler.h"

namespace xe {
namespace cpu {
namespace hir {

enum FunctionAttributes {
  FUNCTION_ATTRIB_INLINE = (1 << 1),
};

class HIRBuilder {
  SimpleFreelist<Instr> free_instrs_;
  SimpleFreelist<Value> free_values_;
  SimpleFreelist<Value::Use> free_uses_;

 public:
  HIRBuilder();
  virtual ~HIRBuilder();
  static HIRBuilder* GetCurrent();

  void MakeCurrent();
  void RemoveCurrent();

  virtual void Reset();

  virtual bool Finalize();

  void Dump(StringBuffer* str);
  void AssertNoCycles();

  Arena* arena() const { return arena_; }

  uint32_t attributes() const { return attributes_; }
  void set_attributes(uint32_t value) { attributes_ = value; }

  std::vector<Value*>& locals() { return locals_; }

  uint32_t max_value_ordinal() const { return next_value_ordinal_; }

  Block* first_block() const { return block_head_; }
  Block* last_block() const { return block_tail_; }
  Block* current_block() const;
  Instr* last_instr() const;

  Label* NewLabel();
  void MarkLabel(Label* label, Block* block = 0);
  void InsertLabel(Label* label, Instr* prev_instr);
  void ResetLabelTags();

  void AddEdge(Block* src, Block* dest, uint32_t flags);
  void RemoveEdge(Block* src, Block* dest);
  void RemoveEdge(Edge* edge);
  void RemoveBlock(Block* block);
  void MergeAdjacentBlocks(Block* left, Block* right);

  Instr* AllocateInstruction();

  Value* AllocateValue();
  Value::Use* AllocateUse();
  void DeallocateInstruction(Instr* instr);
  void DeallocateValue(Value* value);
  void DeallocateUse(Value::Use* use);
  void ResetPools() {
    free_instrs_.Reset();
    free_uses_.Reset();
    free_values_.Reset();
  }
  // static allocations:
  // Value* AllocStatic(size_t length);

  void Comment(const std::string_view value);
  void Comment(const StringBuffer& value);

  template <typename... Args>
  void CommentFormat(const std::string_view format, const Args&... args) {
    static const uint32_t kMaxCommentSize = 1024;
    char* p = reinterpret_cast<char*>(arena_->Alloc(kMaxCommentSize, 1));
    auto result =
        fmt::format_to_n(p, kMaxCommentSize - 1, fmt::runtime(format), args...);
    p[result.size] = '\0';
    size_t rewind = kMaxCommentSize - 1 - result.size;
    arena_->Rewind(rewind);
    CommentBuffer(p);
  }

  void Nop();

  void SourceOffset(uint32_t offset);

  // trace info/etc
  void DebugBreak();
  void DebugBreakTrue(Value* cond);

  void Trap(uint16_t trap_code = 0);
  void TrapTrue(Value* cond, uint16_t trap_code = 0);

  void Call(Function* symbol, uint16_t call_flags = 0);
  void CallTrue(Value* cond, Function* symbol, uint16_t call_flags = 0);
  void CallIndirect(Value* value, uint16_t call_flags = 0);
  void CallIndirectTrue(Value* cond, Value* value, uint16_t call_flags = 0);
  void CallExtern(Function* symbol);
  void Return();
  void ReturnTrue(Value* cond);
  void SetReturnAddress(Value* value);

  void Branch(Label* label, uint16_t branch_flags = 0);
  void Branch(Block* block, uint16_t branch_flags = 0);
  void BranchTrue(Value* cond, Label* label, uint16_t branch_flags = 0);
  void BranchFalse(Value* cond, Label* label, uint16_t branch_flags = 0);

  Value* AllocValue(TypeName type = INT64_TYPE);
  Value* CloneValue(Value* source);

  // phi type_name, Block* b1, Value* v1, Block* b2, Value* v2, etc
  Value* Assign(Value* value);
  Value* Cast(Value* value, TypeName target_type);
  Value* ZeroExtend(Value* value, TypeName target_type);
  Value* SignExtend(Value* value, TypeName target_type);
  Value* Truncate(Value* value, TypeName target_type);
  Value* Convert(Value* value, TypeName target_type,
                 RoundMode round_mode = ROUND_TO_ZERO);
  Value* Round(Value* value, RoundMode round_mode);
  Value* VectorConvertI2F(Value* value, uint32_t arithmetic_flags = 0);
  Value* VectorConvertF2I(Value* value, uint32_t arithmetic_flags = 0);

  Value* LoadZero(TypeName type);
  Value* LoadZeroInt8() { return LoadZero(INT8_TYPE); }
  Value* LoadZeroInt16() { return LoadZero(INT16_TYPE); }
  Value* LoadZeroInt32() { return LoadZero(INT32_TYPE); }
  Value* LoadZeroInt64() { return LoadZero(INT64_TYPE); }
  Value* LoadZeroFloat32() { return LoadZero(FLOAT32_TYPE); }
  Value* LoadZeroFloat64() { return LoadZero(FLOAT64_TYPE); }
  Value* LoadZeroVec128() { return LoadZero(VEC128_TYPE); }

  Value* LoadConstantInt8(int8_t value);
  Value* LoadConstantUint8(uint8_t value);
  Value* LoadConstantInt16(int16_t value);
  Value* LoadConstantUint16(uint16_t value);
  Value* LoadConstantInt32(int32_t value);
  Value* LoadConstantUint32(uint32_t value);
  Value* LoadConstantInt64(int64_t value);
  Value* LoadConstantUint64(uint64_t value);
  Value* LoadConstantFloat32(float value);
  Value* LoadConstantFloat64(double value);
  Value* LoadConstantVec128(const vec128_t& value);

  Value* LoadVectorShl(Value* sh);
  Value* LoadVectorShr(Value* sh);

  Value* LoadClock();

  Value* AllocLocal(TypeName type);
  Value* LoadLocal(Value* slot);
  void StoreLocal(Value* slot, Value* value);

  Value* LoadContext(size_t offset, TypeName type);
  void StoreContext(size_t offset, Value* value);
  void ContextBarrier();

  Value* LoadMmio(cpu::MMIORange* mmio_range, uint32_t address, TypeName type);
  void StoreMmio(cpu::MMIORange* mmio_range, uint32_t address, Value* value);

  Value* LoadOffset(Value* address, Value* offset, TypeName type,
                    uint32_t load_flags = 0);
  void StoreOffset(Value* address, Value* offset, Value* value,
                   uint32_t store_flags = 0);

  Value* Load(Value* address, TypeName type, uint32_t load_flags = 0);
  // create a reserve on an address,
  Value* LoadWithReserve(Value* address, TypeName type);
  Value* StoreWithReserve(Value* address, Value* value, TypeName type);

  Value* LoadVectorLeft(Value* address);
  Value* LoadVectorRight(Value* address);
  void StoreVectorLeft(Value* address, Value* value);
  void StoreVectorRight(Value* address, Value* value);
  void Store(Value* address, Value* value, uint32_t store_flags = 0);
  void Memset(Value* address, Value* value, Value* length);
  void CacheControl(Value* address, size_t cache_line_size,
                    CacheControlType type);
  void MemoryBarrier();
  void DelayExecution();
  void SetRoundingMode(Value* value);
  Value* Max(Value* value1, Value* value2);
  Value* VectorMax(Value* value1, Value* value2, TypeName part_type,
                   uint32_t arithmetic_flags = 0);
  Value* Min(Value* value1, Value* value2);
  Value* VectorMin(Value* value1, Value* value2, TypeName part_type,
                   uint32_t arithmetic_flags = 0);
  Value* Select(Value* cond, Value* value1, Value* value2);
  Value* IsTrue(Value* value);
  Value* IsFalse(Value* value);
  Value* IsNan(Value* value);
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
  Value* DidSaturate(Value* value);
  Value* VectorCompareEQ(Value* value1, Value* value2, TypeName part_type);
  Value* VectorCompareSGT(Value* value1, Value* value2, TypeName part_type);
  Value* VectorCompareSGE(Value* value1, Value* value2, TypeName part_type);
  Value* VectorCompareUGT(Value* value1, Value* value2, TypeName part_type);
  Value* VectorCompareUGE(Value* value1, Value* value2, TypeName part_type);
  Value* VectorDenormFlush(Value* value1);
  Value* ToSingle(Value* value);
  Value* Add(Value* value1, Value* value2, uint32_t arithmetic_flags = 0);
  Value* AddWithCarry(Value* value1, Value* value2, Value* value3,
                      uint32_t arithmetic_flags = 0);
  Value* VectorAdd(Value* value1, Value* value2, TypeName part_type,
                   uint32_t arithmetic_flags = 0);
  Value* Sub(Value* value1, Value* value2, uint32_t arithmetic_flags = 0);
  Value* VectorSub(Value* value1, Value* value2, TypeName part_type,
                   uint32_t arithmetic_flags = 0);
  Value* Mul(Value* value1, Value* value2, uint32_t arithmetic_flags = 0);
  Value* MulHi(Value* value1, Value* value2, uint32_t arithmetic_flags = 0);
  Value* Div(Value* value1, Value* value2, uint32_t arithmetic_flags = 0);
  Value* MulAdd(Value* value1, Value* value2, Value* value3);  // (1 * 2) + 3
  Value* MulSub(Value* value1, Value* value2, Value* value3);  // (1 * 2) - 3

  Value* Neg(Value* value);
  Value* Abs(Value* value);
  Value* Sqrt(Value* value);
  Value* RSqrt(Value* value);
  Value* Recip(Value* value);
  Value* Pow2(Value* value);
  Value* Log2(Value* value);
  Value* DotProduct3(Value* value1, Value* value2);
  Value* DotProduct4(Value* value1, Value* value2);

  Value* And(Value* value1, Value* value2);
  Value* AndNot(Value* value1, Value* value2);
  Value* Or(Value* value1, Value* value2);
  Value* Xor(Value* value1, Value* value2);
  Value* Not(Value* value);
  Value* Shl(Value* value1, Value* value2);
  Value* Shl(Value* value1, int8_t value2);
  Value* VectorShl(Value* value1, Value* value2, TypeName part_type);
  Value* Shr(Value* value1, Value* value2);
  Value* Shr(Value* value1, int8_t value2);
  Value* VectorShr(Value* value1, Value* value2, TypeName part_type);
  Value* Sha(Value* value1, Value* value2);
  Value* Sha(Value* value1, int8_t value2);
  Value* VectorSha(Value* value1, Value* value2, TypeName part_type);
  Value* RotateLeft(Value* value1, Value* value2);
  Value* VectorRotateLeft(Value* value1, Value* value2, TypeName part_type);
  Value* VectorAverage(Value* value1, Value* value2, TypeName part_type,
                       uint32_t arithmetic_flags);
  Value* ByteSwap(Value* value);
  Value* CountLeadingZeros(Value* value);
  Value* Insert(Value* value, Value* index, Value* part);
  Value* Insert(Value* value, uint64_t index, Value* part);
  Value* Extract(Value* value, Value* index, TypeName target_type);
  Value* Extract(Value* value, uint8_t index, TypeName target_type);
  // i8->i16/i32/... (i8|i8 / i8|i8|i8|i8 / ...)
  // i8/i16/i32 -> vec128
  Value* Splat(Value* value, TypeName target_type);
  Value* Permute(Value* control, Value* value1, Value* value2,
                 TypeName part_type);
  Value* Swizzle(Value* value, TypeName part_type, uint32_t swizzle_mask);
  // SelectBits(cond, value1, value2)
  Value* Pack(Value* value, uint32_t pack_flags = 0);
  Value* Pack(Value* value1, Value* value2, uint32_t pack_flags = 0);
  Value* Unpack(Value* value, uint32_t pack_flags = 0);

  Value* AtomicExchange(Value* address, Value* new_value);
  Value* AtomicCompareExchange(Value* address, Value* old_value,
                               Value* new_value);
  Value* AtomicAdd(Value* address, Value* value);
  Value* AtomicSub(Value* address, Value* value);

  void SetNJM(Value* value);

 protected:
  void DumpValue(StringBuffer* str, Value* value);
  void DumpOp(StringBuffer* str, OpcodeSignatureType sig_type, Instr::Op* op);

 private:
  Block* AppendBlock();
  void EndBlock();
  bool IsUnconditionalJump(Instr* instr);
  Instr* AppendInstr(const OpcodeInfo& opcode, uint16_t flags, Value* dest = 0);
  void CommentBuffer(const char* p);
  Value* CompareXX(const OpcodeInfo& opcode, Value* value1, Value* value2);
  Value* VectorCompareXX(const OpcodeInfo& opcode, Value* value1, Value* value2,
                         TypeName part_type);

 protected:
  Arena* arena_;

  uint32_t attributes_;

  uint32_t next_label_id_;
  uint32_t next_value_ordinal_;

  std::vector<Value*> locals_;

  Block* block_head_;
  Block* block_tail_;
  Block* current_block_;
};

}  // namespace hir
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_HIR_HIR_BUILDER_H_

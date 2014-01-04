/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_LIR_LIR_BUILDER_H_
#define ALLOY_BACKEND_X64_LIR_LIR_BUILDER_H_

#include <alloy/core.h>
#include <alloy/backend/x64/lir/lir_block.h>
#include <alloy/backend/x64/lir/lir_instr.h>
#include <alloy/backend/x64/lir/lir_label.h>
#include <alloy/backend/x64/lir/lir_opcodes.h>
#include <alloy/hir/value.h>


namespace alloy {
namespace backend {
namespace x64 {
class X64Backend;
namespace lir {


class LIRBuilder {
public:
  LIRBuilder(X64Backend* backend);
  ~LIRBuilder();

  void Reset();
  int Finalize();

  void Dump(StringBuffer* str);

  Arena* arena() const { return arena_; }

  LIRBlock* first_block() const { return block_head_; }
  LIRBlock* current_block() const;
  LIRInstr* last_instr() const;

  LIRLabel* NewLabel(const char* name = 0, bool local = false);
  LIRLabel* NewLocalLabel() { return NewLabel(0, true); }
  void MarkLabel(LIRLabel* label, LIRBlock* block = 0);

  // TODO(benvanik): allocations

  LIRBlock* AppendBlock();
  void EndBlock();
  
  void Nop() { AppendInstr(LIR_OPCODE_NOP_info, 0); }

  void Comment(const char* format, ...);
  void SourceOffset(uintptr_t offset) { AppendInstr(LIR_OPCODE_SOURCE_OFFSET_info, 0, offset); }
  void DebugBreak() { AppendInstr(LIR_OPCODE_DEBUG_BREAK_info, 0); }
  void Trap() { AppendInstr(LIR_OPCODE_TRAP_info, 0); }

  template<typename A0, typename A1>
  void Mov(A0& a, A1& b) { AppendInstr(LIR_OPCODE_MOV_info, 0, a, b); }
  template<typename A0, typename A1, typename A2>
  void Mov(A0& a, A1& b, A2& c) { AppendInstr(LIR_OPCODE_MOV_info, 0, a, b, c); }
  template<typename A0, typename A1>
  void MovZX(A0& a, A1& b) { AppendInstr(LIR_OPCODE_MOV_ZX_info, 0, a, b); }
  template<typename A0, typename A1>
  void MovSX(A0& a, A1& b) { AppendInstr(LIR_OPCODE_MOV_ZX_info, 0, a, b); }

  template<typename A0, typename A1>
  void Test(A0& a, A1& b) { AppendInstr(LIR_OPCODE_TEST_info, 0, a, b); }

  template<typename A0>
  void Jump(A0& target) { AppendInstr(LIR_OPCODE_JUMP_info, 0, target); }
  void JumpEQ(LIRLabel* target) { AppendInstr(LIR_OPCODE_JUMP_EQ_info, 0, target); }
  void JumpNE(LIRLabel* target) { AppendInstr(LIR_OPCODE_JUMP_NE_info, 0, target); }

private:
  void DumpArg(StringBuffer* str, LIROperandType type, intptr_t ptr);

  LIRInstr* AppendInstr(const LIROpcodeInfo& opcode, uint16_t flags = 0);

  XEFORCEINLINE LIROperandType GetOperandType(runtime::FunctionInfo*) { return LIROperandType::FUNCTION; }
  XEFORCEINLINE LIROperandType GetOperandType(LIRLabel*) { return LIROperandType::LABEL; }
  XEFORCEINLINE LIROperandType GetOperandType(uintptr_t) { return LIROperandType::OFFSET; }
  XEFORCEINLINE LIROperandType GetOperandType(const char*) { return LIROperandType::STRING; }
  XEFORCEINLINE LIROperandType GetOperandType(LIRRegister&) { return LIROperandType::REGISTER; }
  XEFORCEINLINE LIROperandType GetOperandType(int8_t) { return LIROperandType::INT8_CONSTANT; }
  XEFORCEINLINE LIROperandType GetOperandType(int16_t) { return LIROperandType::INT16_CONSTANT; }
  XEFORCEINLINE LIROperandType GetOperandType(int32_t) { return LIROperandType::INT32_CONSTANT; }
  XEFORCEINLINE LIROperandType GetOperandType(int64_t) { return LIROperandType::INT64_CONSTANT; }
  XEFORCEINLINE LIROperandType GetOperandType(float) { return LIROperandType::FLOAT32_CONSTANT; }
  XEFORCEINLINE LIROperandType GetOperandType(double) { return LIROperandType::FLOAT64_CONSTANT; }
  XEFORCEINLINE LIROperandType GetOperandType(vec128_t&) { return LIROperandType::VEC128_CONSTANT; }
  LIROperandType GetOperandType(hir::Value* value) {
    if (value->IsConstant()) {
      switch (value->type) {
      case hir::INT8_TYPE:
        return LIROperandType::INT8_CONSTANT;
      case hir::INT16_TYPE:
        return LIROperandType::INT16_CONSTANT;
      case hir::INT32_TYPE:
        return LIROperandType::INT32_CONSTANT;
      case hir::INT64_TYPE:
        return LIROperandType::INT64_CONSTANT;
      case hir::FLOAT32_TYPE:
        return LIROperandType::FLOAT32_CONSTANT;
      case hir::FLOAT64_TYPE:
        return LIROperandType::FLOAT64_CONSTANT;
      case hir::VEC128_TYPE:
        return LIROperandType::VEC128_CONSTANT;
      default:
        XEASSERTALWAYS();
        return LIROperandType::INT8_CONSTANT;
      }
    } else {
      return LIROperandType::REGISTER;
    }
  }

  template<typename T>
  uint8_t AppendOperand(LIRInstr* instr, LIROperandType& type, T& value) {
    type = GetOperandType(value);
    auto ptr = arena_->Alloc<T>();
    *ptr = value;
    return (uint8_t)((intptr_t)ptr - (intptr_t)instr);
  }
  uint8_t AppendOperand(LIRInstr* instr, LIROperandType& type, const char* value);
  uint8_t AppendOperand(LIRInstr* instr, LIROperandType& type, hir::Value* value);

  template<typename A0>
  LIRInstr* AppendInstr(const LIROpcodeInfo& opcode, uint16_t flags, A0& arg0) {
    auto instr = AppendInstr(opcode, flags);
    instr->arg_offsets.arg0 =
        AppendOperand(instr, instr->arg_types.arg0, arg0);
    return instr;
  }
  template<typename A0, typename A1>
  LIRInstr* AppendInstr(const LIROpcodeInfo& opcode, uint16_t flags, A0& arg0, A1& arg1) {
    auto instr = AppendInstr(opcode, flags);
    instr->arg_offsets.arg0 =
        AppendOperand(instr, instr->arg_types.arg0, arg0);
    instr->arg_offsets.arg1 =
        AppendOperand(instr, instr->arg_types.arg1, arg1);
    return instr;
  }
  template<typename A0, typename A1, typename A2>
  LIRInstr* AppendInstr(const LIROpcodeInfo& opcode, uint16_t flags, A0& arg0, A1& arg1, A2& arg2) {
    auto instr = AppendInstr(opcode, flags);
    instr->arg_offsets.arg0 =
        AppendOperand(instr, instr->arg_types.arg0, arg0);
    instr->arg_offsets.arg1 =
        AppendOperand(instr, instr->arg_types.arg1, arg1);
    instr->arg_offsets.arg2 =
        AppendOperand(instr, instr->arg_types.arg2, arg2);
    return instr;
  }
  template<typename A0, typename A1, typename A2, typename A3>
  LIRInstr* AppendInstr(const LIROpcodeInfo& opcode, uint16_t flags, A0& arg0, A1& arg1, A2& arg2, A3& arg3) {
    auto instr = AppendInstr(opcode, flags);
    instr->arg_offsets.arg0 =
        AppendOperand(instr, instr->arg_types.arg0, arg0);
    instr->arg_offsets.arg1 =
        AppendOperand(instr, instr->arg_types.arg1, arg1);
    instr->arg_offsets.arg2 =
        AppendOperand(instr, instr->arg_types.arg2, arg2);
    instr->arg_offsets.arg3 =
        AppendOperand(instr, instr->arg_types.arg3, arg3);
    return instr;
  }

private:
  X64Backend* backend_;
  Arena*      arena_;

  uint32_t    next_label_id_;

  LIRBlock*   block_head_;
  LIRBlock*   block_tail_;
  LIRBlock*   current_block_;
};


}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_LIR_LIR_BUILDER_H_

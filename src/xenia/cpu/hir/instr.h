/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_HIR_INSTR_H_
#define XENIA_CPU_HIR_INSTR_H_

#include "xenia/cpu/hir/opcodes.h"
#include "xenia/cpu/hir/value.h"

namespace xe {
namespace cpu {
class Function;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace hir {

class Block;
class Label;

class Instr {
 public:
  Block* block;
  Instr* next;
  Instr* prev;

  const OpcodeInfo* opcode;
  uint16_t flags;
  uint32_t ordinal;

  typedef union {
    Function* symbol;
    Label* label;
    Value* value;
    uint64_t offset;
  } Op;

  Value* dest;
  Op src1;
  Op src2;
  Op src3;

  Value::Use* src1_use;
  Value::Use* src2_use;
  Value::Use* src3_use;

  void set_src1(Value* value);
  void set_src2(Value* value);
  void set_src3(Value* value);

  void MoveBefore(Instr* other);
  void Replace(const OpcodeInfo* new_opcode, uint16_t new_flags);
  void Remove();

  template <typename TPredicate>
  std::pair<Value*, Value*> BinaryValueArrangeByPredicateExclusive(
      TPredicate&& pred) {
    auto src1_value = src1.value;
    auto src2_value = src2.value;
    if (!src1_value || !src2_value) return {nullptr, nullptr};

    if (!opcode) return {nullptr, nullptr};  // impossible!

    // check if binary opcode taking two values. we dont care if the dest is a
    // value

    if (!IsOpcodeBinaryValue(opcode->signature)) return {nullptr, nullptr};

    if (pred(src1_value)) {
      if (pred(src2_value)) {
        return {nullptr, nullptr};
      } else {
        return {src1_value, src2_value};
      }
    } else if (pred(src2_value)) {
      return {src2_value, src1_value};
    } else {
      return {nullptr, nullptr};
    }
  }

  /*
if src1 is constant, and src2 is not, return [src1, src2]
if src2 is constant, and src1 is not, return [src2, src1]
if neither is constant, return nullptr, nullptr
if both are constant, return nullptr, nullptr
*/
  std::pair<Value*, Value*> BinaryValueArrangeAsConstAndVar() {
    return BinaryValueArrangeByPredicateExclusive(
        [](Value* value) { return value->IsConstant(); });
  }
  std::pair<Value*, Value*> BinaryValueArrangeByDefiningOpcode(
      const OpcodeInfo* op_ptr) {
    return BinaryValueArrangeByPredicateExclusive([op_ptr](Value* value) {
      return value->def && value->def->opcode == op_ptr;
    });
  }

  Instr* GetDestDefSkipAssigns();
};

}  // namespace hir
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_HIR_INSTR_H_

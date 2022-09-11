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
// todo: better name
enum MovTunnel {
  MOVTUNNEL_ASSIGNS = 1,
  MOVTUNNEL_MOVZX = 2,
  MOVTUNNEL_MOVSX = 4,
  MOVTUNNEL_TRUNCATE = 8,
  MOVTUNNEL_AND32FF = 16,  // tunnel through and with 0xFFFFFFFF
};

class Instr {
 public:
  Block* block;
  Instr* next;
  Instr* prev;

  const OpcodeInfo* opcode;
  uint16_t flags;
  uint16_t backend_flags;  // backends may do whatever they wish with this
  uint32_t ordinal;

  typedef union {
    Function* symbol;
    Label* label;
    Value* value;
    uint64_t offset;
  } Op;

  Value* dest;
  union {
    struct {
      Op src1;
      Op src2;
      Op src3;
    };
    Op srcs[3];
  };
  union {
    struct {
      Value::Use* src1_use;
      Value::Use* src2_use;
      Value::Use* src3_use;
    };
    Value::Use* srcs_use[3];
  };
  void set_srcN(Value* value, uint32_t idx);
  void set_src1(Value* value) { set_srcN(value, 0); }

  void set_src2(Value* value) { set_srcN(value, 1); }

  void set_src3(Value* value) { set_srcN(value, 2); }

  void MoveBefore(Instr* other);
  void Replace(const OpcodeInfo* new_opcode, uint16_t new_flags);
  void UnlinkAndNOP();
  //chrispy: wanted to change this one to Remove, but i changed Remove's name to UnlinkAndNOP,
  //so if changes happened in master that we wanted to port over, and those changes used Remove, then we would have a lot of issues that the cause of would
  //be difficult to track 
  //^todo: rework this comment, im frazzled
  void Deallocate();
  const OpcodeInfo* GetOpcodeInfo() const { return opcode; }
  // if opcode is null, we have bigger problems
  Opcode GetOpcodeNum() const { return GetOpcodeInfo()->num; }

  template <typename TPredicate>
  std::pair<Value*, Value*> BinaryValueArrangeByPredicateExclusive(
      TPredicate&& pred) {
    auto src1_value = src1.value;
    auto src2_value = src2.value;
    if (!src1_value || !src2_value) return {nullptr, nullptr};

    if (!GetOpcodeInfo()) return {nullptr, nullptr};  // impossible!

    // check if binary opcode taking two values. we dont care if the dest is a
    // value

    if (!IsOpcodeBinaryValue(GetOpcodeInfo()->signature))
      return {nullptr, nullptr};

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
      return value->def && value->def->GetOpcodeInfo() == op_ptr;
    });
  }

  Instr* GetDestDefSkipAssigns();
  Instr* GetDestDefTunnelMovs(unsigned int* tunnel_flags);

  // returns [def op, constant]
  std::pair<Value*, Value*> BinaryValueArrangeByDefOpAndConstant(
      const OpcodeInfo* op_ptr) {
    auto result = BinaryValueArrangeByDefiningOpcode(op_ptr);

    if (!result.first) return result;
    if (!result.second->IsConstant()) {
      return {nullptr, nullptr};
    }
    return result;
  }
  /*
  Invokes the provided lambda callback on each operand that is a Value. Callback
  is invoked with Value*, uint32_t index
*/
  template <typename TCallable>
  void VisitValueOperands(TCallable&& call_for_values) {
    uint32_t signature = GetOpcodeInfo()->signature;

    OpcodeSignatureType t_dest, t_src1, t_src2, t_src3;

    UnpackOpcodeSig(signature, t_dest, t_src1, t_src2, t_src3);

    if (t_src1 == OPCODE_SIG_TYPE_V) {
      call_for_values(src1.value, 0);
    }
    if (t_src2 == OPCODE_SIG_TYPE_V) {
      call_for_values(src2.value, 1);
    }
    if (t_src3 == OPCODE_SIG_TYPE_V) {
      call_for_values(src3.value, 2);
    }
  }
  bool IsFake() const;

  // gets previous instr, skipping instrs like COMMENT, OPCODE_CONTEXT_BARRIER,
  // OPCODE_SOURCE_OFFSET
  const hir::Instr* GetNonFakePrev() const;

  uint32_t GuestAddressFor() const;

  bool AllScalarIntegral();  // dest and all srcs are scalar integral
};

}  // namespace hir
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_HIR_INSTR_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_LIR_LIR_INSTR_H_
#define ALLOY_BACKEND_X64_LIR_LIR_INSTR_H_

#include <alloy/core.h>
#include <alloy/backend/x64/lir/lir_block.h>
#include <alloy/backend/x64/lir/lir_label.h>
#include <alloy/backend/x64/lir/lir_opcodes.h>


namespace alloy { namespace runtime { class FunctionInfo; } }


namespace alloy {
namespace backend {
namespace x64 {
namespace lir {


enum class LIRRegisterName : uint32_t {
  AL,   AX,   EAX,  RAX,
  BL,   BX,   EBX,  RBX,
  CL,   CX,   ECX,  RCX,
  DL,   DX,   EDX,  RDX,
  R8B,  R8W,  R8D,   R8,
  R9B,  R9W,  R9D,   R9,
  R10B, R10W, R10D, R10,
  R11B, R11W, R11D, R11,
  R12B, R12W, R12D, R12,
  R13B, R13W, R13D, R13,
  R14B, R14W, R14D, R14,
  R15B, R15W, R15D, R15,

  SIL,  SI,   ESI,  RSI,
  DIL,  DI,   EDI,  RDI,

  RBP,
  RSP,

  XMM0,
  XMM1,
  XMM2,
  XMM3,
  XMM4,
  XMM5,
  XMM6,
  XMM7,
  XMM8,
  XMM9,
  XMM10,
  XMM11,
  XMM12,
  XMM13,
  XMM14,
  XMM15,

  VREG0 = 0x10000000,
};
extern const char* register_names[74];

enum class LIRRegisterType : uint32_t {
  REG8,
  REG16,
  REG32,
  REG64,
  REGXMM,
};

typedef struct LIRRegister_s {
  LIRRegisterType type;
  union {
    uint32_t id;
    LIRRegisterName name;
  };
  struct LIRRegister_s(LIRRegisterType _type, uint32_t _id) :
      type(_type), id((uint32_t)LIRRegisterName::VREG0 + _id) {}
  struct LIRRegister_s(LIRRegisterType _type, LIRRegisterName _name) :
      type(_type), name(_name) {}
  bool is_virtual() const { return id > (uint32_t)LIRRegisterName::VREG0; }
} LIRRegister;

enum class LIROperandType : uint8_t {
  NONE = 0,
  FUNCTION,
  LABEL,
  OFFSET,
  STRING,
  REGISTER,
  INT8_CONSTANT,
  INT16_CONSTANT,
  INT32_CONSTANT,
  INT64_CONSTANT,
  FLOAT32_CONSTANT,
  FLOAT64_CONSTANT,
  VEC128_CONSTANT,
};

class LIRInstr {
public:
  LIRBlock* block;
  LIRInstr* next;
  LIRInstr* prev;

  const LIROpcodeInfo* opcode;
  uint16_t flags;

  struct {
    LIROperandType arg0;
    LIROperandType arg1;
    LIROperandType arg2;
    LIROperandType arg3;
  } arg_types;
  struct {
    uint8_t arg0;
    uint8_t arg1;
    uint8_t arg2;
    uint8_t arg3;
  } arg_offsets;
  LIROperandType arg0_type() const { return arg_types.arg0; }
  LIROperandType arg1_type() const { return arg_types.arg1; }
  LIROperandType arg2_type() const { return arg_types.arg2; }
  LIROperandType arg3_type() const { return arg_types.arg3; }
  template<typename T> T* arg0() { return (T*)(((uint8_t*)this) + arg_offsets.arg0); }
  template<typename T> T* arg1() { return (T*)(((uint8_t*)this) + arg_offsets.arg1); }
  template<typename T> T* arg2() { return (T*)(((uint8_t*)this) + arg_offsets.arg2); }
  template<typename T> T* arg3() { return (T*)(((uint8_t*)this) + arg_offsets.arg3); }

  void Remove();
};


}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_LIR_LIR_INSTR_H_

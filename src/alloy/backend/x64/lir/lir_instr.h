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


enum LIRRegister {
  REG8, REG16, REG32, REG64, REGXMM,

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
};

typedef union {
  runtime::FunctionInfo* symbol_info;
  LIRLabel*   label;
  LIRRegister reg;
  int8_t      i8;
  int16_t     i16;
  int32_t     i32;
  int64_t     i64;
  float       f32;
  double      f64;
  uint64_t    offset;
  char*       string;
} LIROperand;


class LIRInstr {
public:
  LIRBlock* block;
  LIRInstr* next;
  LIRInstr* prev;

  const LIROpcodeInfo* opcode;
  uint16_t flags;

  // TODO(benvanik): make this variable width?
  LIROperand arg[4];
};


}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_LIR_LIR_INSTR_H_

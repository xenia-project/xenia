/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/ivm/ivm_intcode.h>

#include <alloy/hir/label.h>
#include <alloy/runtime/runtime.h>
#include <alloy/runtime/symbol_info.h>
#include <alloy/runtime/thread_state.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::ivm;
using namespace alloy::hir;
using namespace alloy::runtime;


namespace alloy {
namespace backend {
namespace ivm {


//#define DPRINT printf
//#define DFLUSH() fflush(stdout)
#define DPRINT
#define DFLUSH()
//#define IPRINT printf
//#define IFLUSH() fflush(stdout)
#define IPRINT
#define IFLUSH()


uint32_t IntCode_INT_LOAD_CONSTANT(IntCodeState& ics, const IntCode* i) {
  // TODO(benvanik): optimize on type to avoid 16b copy per load.
  ics.rf[i->dest_reg].v128 = i->constant.v128;
  return IA_NEXT;
}

uint32_t AllocConstant(TranslationContext& ctx, uint64_t value,
                       IntCode** out_ic = NULL) {
  ctx.intcode_count++;
  IntCode* ic = ctx.intcode_arena->Alloc<IntCode>();
  ic->intcode_fn = IntCode_INT_LOAD_CONSTANT;
  ic->flags = 0;
  ic->dest_reg = ctx.register_count++;
  ic->constant.u64 = value;
  if (out_ic) {
    *out_ic = ic;
  }
  return ic->dest_reg;
}

uint32_t AllocConstant(TranslationContext& ctx, Value* value) {
  ctx.intcode_count++;
  IntCode* ic = ctx.intcode_arena->Alloc<IntCode>();
  ic->intcode_fn = IntCode_INT_LOAD_CONSTANT;
  ic->flags = 0;
  ic->dest_reg = ctx.register_count++;
  ic->constant.v128 = value->constant.v128;
  return ic->dest_reg;
}

uint32_t AllocLabel(TranslationContext& ctx, Label* label) {
  // If it's a back-branch to an already tagged label avoid setting up
  // a reference.
  uint32_t value = (uint32_t)label->tag;
  if (value & 0x80000000) {
    // Already set.
    return AllocConstant(ctx, value & ~0x80000000);
  }

  // Allocate a constant - it will be updated later.
  IntCode* ic;
  uint32_t reg = AllocConstant(ctx, 0, &ic);

  // Setup a label reference. After assembly is complete this will
  // run through and fix up the constant with the IA.
  LabelRef* label_ref = ctx.scratch_arena->Alloc<LabelRef>();
  label_ref->next = ctx.label_ref_head;
  ctx.label_ref_head = label_ref;
  label_ref->label = label;
  label_ref->instr = ic;

  return reg;
}

uint32_t AllocDynamicRegister(TranslationContext& ctx, Value* value) {
  int32_t reg = (int32_t)value->tag - 1;
  if (reg == -1) {
    reg = ctx.register_count++;
    value->tag = (void*)(reg + 1);
  }
  return (uint32_t)reg;
}

uint32_t AllocOpRegister(
    TranslationContext& ctx, OpcodeSignatureType sig_type, Instr::Op* op) {
  switch (sig_type) {
  case OPCODE_SIG_TYPE_X:
    // Nothing.
    return 0;
  case OPCODE_SIG_TYPE_L:
    return AllocLabel(ctx, op->label);
  case OPCODE_SIG_TYPE_O:
    return AllocConstant(ctx, (uint64_t)op->offset);
  case OPCODE_SIG_TYPE_S:
    return AllocConstant(ctx, (uint64_t)op->symbol_info);
  case OPCODE_SIG_TYPE_V:
    Value* value = op->value;
    if (value->IsConstant()) {
      return AllocConstant(ctx, value);
    } else {
      return AllocDynamicRegister(ctx, value);
    }
  }
  return 0;
}

uint32_t IntCode_INVALID(IntCodeState& ics, const IntCode* i);
uint32_t IntCode_INVALID_TYPE(IntCodeState& ics, const IntCode* i);
int DispatchToC(TranslationContext& ctx, Instr* i, IntCodeFn fn) {
  XEASSERT(fn != IntCode_INVALID);
  XEASSERT(fn != IntCode_INVALID_TYPE);

  const OpcodeInfo* op = i->opcode;
  uint32_t sig = op->signature;
  OpcodeSignatureType dest_type = GET_OPCODE_SIG_TYPE_DEST(sig);
  OpcodeSignatureType src1_type = GET_OPCODE_SIG_TYPE_SRC1(sig);
  OpcodeSignatureType src2_type = GET_OPCODE_SIG_TYPE_SRC2(sig);
  OpcodeSignatureType src3_type = GET_OPCODE_SIG_TYPE_SRC3(sig);

  // Setup arguments.
  uint32_t dest_reg = 0;
  if (dest_type == OPCODE_SIG_TYPE_V) {
    // Allocate dest register.
    dest_reg = AllocDynamicRegister(ctx, i->dest);
  }
  uint32_t src1_reg = AllocOpRegister(ctx, src1_type, &i->src1);
  uint32_t src2_reg = AllocOpRegister(ctx, src2_type, &i->src2);
  uint32_t src3_reg = AllocOpRegister(ctx, src3_type, &i->src3);

  // Allocate last (in case we had any setup instructions for args).
  ctx.intcode_count++;
  IntCode* ic = ctx.intcode_arena->Alloc<IntCode>();
  ic->intcode_fn = fn;
  ic->flags = i->flags;
  ic->dest_reg = dest_reg;
  ic->src1_reg = src1_reg;
  ic->src2_reg = src2_reg;
  ic->src3_reg = src3_reg;

  return 0;
}


uint32_t IntCode_INVALID(IntCodeState& ics, const IntCode* i) {
  XEASSERTALWAYS();
  return IA_NEXT;
}
uint32_t IntCode_INVALID_TYPE(IntCodeState& ics, const IntCode* i) {
  XEASSERTALWAYS();
  return IA_NEXT;
}
int TranslateInvalid(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_INVALID);
}

uint32_t IntCode_COMMENT(IntCodeState& ics, const IntCode* i) {
  char* value = (char*)(i->src1_reg | ((uint64_t)i->src2_reg << 32));
  IPRINT("%s\n", value);
  IFLUSH();
  return IA_NEXT;
}
int Translate_COMMENT(TranslationContext& ctx, Instr* i) {
  ctx.intcode_count++;
  IntCode* ic = ctx.intcode_arena->Alloc<IntCode>();
  ic->intcode_fn = IntCode_COMMENT;
  ic->flags = i->flags;
  // HACK HACK HACK
  char* src = xestrdupa((char*)i->src1.offset);
  uint64_t src_p = (uint64_t)src;
  ic->src1_reg = (uint32_t)src_p;
  ic->src2_reg = (uint32_t)(src_p >> 32);
  return 0;
}

uint32_t IntCode_NOP(IntCodeState& ics, const IntCode* i) {
  return IA_NEXT;
}
int Translate_NOP(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_NOP);
}

uint32_t IntCode_DEBUG_BREAK(IntCodeState& ics, const IntCode* i) {
  DFLUSH();
  __debugbreak();
  return IA_NEXT;
}
int Translate_DEBUG_BREAK(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_DEBUG_BREAK);
}

uint32_t IntCode_DEBUG_BREAK_TRUE_I8(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u8) {
    return IntCode_DEBUG_BREAK(ics, i);
  }
  return IA_NEXT;
}
uint32_t IntCode_DEBUG_BREAK_TRUE_I16(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u16) {
    return IntCode_DEBUG_BREAK(ics, i);
  }
  return IA_NEXT;
}
uint32_t IntCode_DEBUG_BREAK_TRUE_I32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u32) {
    return IntCode_DEBUG_BREAK(ics, i);
  }
  return IA_NEXT;
}
uint32_t IntCode_DEBUG_BREAK_TRUE_I64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u64) {
    return IntCode_DEBUG_BREAK(ics, i);
  }
  return IA_NEXT;
}
uint32_t IntCode_DEBUG_BREAK_TRUE_F32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f32) {
    return IntCode_DEBUG_BREAK(ics, i);
  }
  return IA_NEXT;
}
uint32_t IntCode_DEBUG_BREAK_TRUE_F64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f64) {
    return IntCode_DEBUG_BREAK(ics, i);
  }
  return IA_NEXT;
}
int Translate_DEBUG_BREAK_TRUE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_DEBUG_BREAK_TRUE_I8,
    IntCode_DEBUG_BREAK_TRUE_I16,
    IntCode_DEBUG_BREAK_TRUE_I32,
    IntCode_DEBUG_BREAK_TRUE_I64,
    IntCode_DEBUG_BREAK_TRUE_F32,
    IntCode_DEBUG_BREAK_TRUE_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_TRAP(IntCodeState& ics, const IntCode* i) {
  __debugbreak();
  return IA_NEXT;
}
int Translate_TRAP(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_TRAP);
}

uint32_t IntCode_TRAP_TRUE_I8(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u8) {
    return IntCode_TRAP(ics, i);
  }
  return IA_NEXT;
}
uint32_t IntCode_TRAP_TRUE_I16(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u16) {
    return IntCode_TRAP(ics, i);
  }
  return IA_NEXT;
}
uint32_t IntCode_TRAP_TRUE_I32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u32) {
    return IntCode_TRAP(ics, i);
  }
  return IA_NEXT;
}
uint32_t IntCode_TRAP_TRUE_I64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u64) {
    return IntCode_TRAP(ics, i);
  }
  return IA_NEXT;
}
uint32_t IntCode_TRAP_TRUE_F32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f32) {
    return IntCode_TRAP(ics, i);
  }
  return IA_NEXT;
}
uint32_t IntCode_TRAP_TRUE_F64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f64) {
    return IntCode_TRAP(ics, i);
  }
  return IA_NEXT;
}
int Translate_TRAP_TRUE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_TRAP_TRUE_I8,
    IntCode_TRAP_TRUE_I16,
    IntCode_TRAP_TRUE_I32,
    IntCode_TRAP_TRUE_I64,
    IntCode_TRAP_TRUE_F32,
    IntCode_TRAP_TRUE_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_CALL_XX(IntCodeState& ics, const IntCode* i, uint32_t reg) {
  FunctionInfo* symbol_info = (FunctionInfo*)ics.rf[reg].u64;
  Function* fn;
  ics.thread_state->runtime()->ResolveFunction(symbol_info->address(), &fn);
  // TODO(benvanik): proper tail call support, somehow.
  fn->Call(ics.thread_state);
  if (i->flags & CALL_TAIL) {
    return IA_RETURN;
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL(IntCodeState& ics, const IntCode* i) {
  return IntCode_CALL_XX(ics, i, i->src1_reg);
}
int Translate_CALL(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_CALL);
}

uint32_t IntCode_CALL_TRUE_I8(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u8) {
    return IntCode_CALL_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_TRUE_I16(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u16) {
    return IntCode_CALL_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_TRUE_I32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u32) {
    return IntCode_CALL_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_TRUE_I64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u64) {
    return IntCode_CALL_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_TRUE_F32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f32) {
    return IntCode_CALL_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_TRUE_F64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f64) {
    return IntCode_CALL_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
int Translate_CALL_TRUE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_CALL_TRUE_I8,
    IntCode_CALL_TRUE_I16,
    IntCode_CALL_TRUE_I32,
    IntCode_CALL_TRUE_I64,
    IntCode_CALL_TRUE_F32,
    IntCode_CALL_TRUE_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_CALL_INDIRECT_XX(IntCodeState& ics, const IntCode* i, uint32_t reg) {
  // Check if return address - if so, return.
  /*if (ics.rf[reg].u64 == ics.return_address) {
    return IA_RETURN;
  }*/

  // Real call.
  Function* fn;
  ics.thread_state->runtime()->ResolveFunction(ics.rf[reg].u64, &fn);
  // TODO(benvanik): proper tail call support, somehow.
  fn->Call(ics.thread_state);
  if (i->flags & CALL_TAIL) {
    return IA_RETURN;
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_INDIRECT(IntCodeState& ics, const IntCode* i) {
  return IntCode_CALL_INDIRECT_XX(ics, i, i->src1_reg);
}
int Translate_CALL_INDIRECT(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_CALL_INDIRECT);
}

uint32_t IntCode_CALL_INDIRECT_TRUE_I8(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u8) {
    return IntCode_CALL_INDIRECT_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_INDIRECT_TRUE_I16(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u16) {
    return IntCode_CALL_INDIRECT_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_INDIRECT_TRUE_I32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u32) {
    return IntCode_CALL_INDIRECT_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_INDIRECT_TRUE_I64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u64) {
    return IntCode_CALL_INDIRECT_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_INDIRECT_TRUE_F32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f32) {
    return IntCode_CALL_INDIRECT_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_CALL_INDIRECT_TRUE_F64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f64) {
    return IntCode_CALL_INDIRECT_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
int Translate_CALL_INDIRECT_TRUE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_CALL_INDIRECT_TRUE_I8,
    IntCode_CALL_INDIRECT_TRUE_I16,
    IntCode_CALL_INDIRECT_TRUE_I32,
    IntCode_CALL_INDIRECT_TRUE_I64,
    IntCode_CALL_INDIRECT_TRUE_F32,
    IntCode_CALL_INDIRECT_TRUE_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_RETURN(IntCodeState& ics, const IntCode* i) {
  return IA_RETURN;
}
int Translate_RETURN(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_RETURN);
}

uint32_t IntCode_BRANCH_XX(IntCodeState& ics, const IntCode* i, uint32_t reg) {
  return ics.rf[reg].u32;
}
uint32_t IntCode_BRANCH(IntCodeState& ics, const IntCode* i) {
  return IntCode_BRANCH_XX(ics, i, i->src1_reg);
}
int Translate_BRANCH(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_BRANCH);
}

uint32_t IntCode_BRANCH_IF_I8(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u8) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  } else {
    return IntCode_BRANCH_XX(ics, i, i->src3_reg);
  }
}
uint32_t IntCode_BRANCH_IF_I16(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u16) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  } else {
    return IntCode_BRANCH_XX(ics, i, i->src3_reg);
  }
}
uint32_t IntCode_BRANCH_IF_I32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u32) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  } else {
    return IntCode_BRANCH_XX(ics, i, i->src3_reg);
  }
}
uint32_t IntCode_BRANCH_IF_I64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u64) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  } else {
    return IntCode_BRANCH_XX(ics, i, i->src3_reg);
  }
}
uint32_t IntCode_BRANCH_IF_F32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f32) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  } else {
    return IntCode_BRANCH_XX(ics, i, i->src3_reg);
  }
}
uint32_t IntCode_BRANCH_IF_F64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f64) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  } else {
    return IntCode_BRANCH_XX(ics, i, i->src3_reg);
  }
}
int Translate_BRANCH_IF(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_BRANCH_IF_I8,
    IntCode_BRANCH_IF_I16,
    IntCode_BRANCH_IF_I32,
    IntCode_BRANCH_IF_I64,
    IntCode_BRANCH_IF_F32,
    IntCode_BRANCH_IF_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_BRANCH_TRUE_I8(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u8) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_BRANCH_TRUE_I16(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u16) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_BRANCH_TRUE_I32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u32) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_BRANCH_TRUE_I64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u64) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_BRANCH_TRUE_F32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f32) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_BRANCH_TRUE_F64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f64) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
int Translate_BRANCH_TRUE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_BRANCH_TRUE_I8,
    IntCode_BRANCH_TRUE_I16,
    IntCode_BRANCH_TRUE_I32,
    IntCode_BRANCH_TRUE_I64,
    IntCode_BRANCH_TRUE_F32,
    IntCode_BRANCH_TRUE_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_BRANCH_FALSE_I8(IntCodeState& ics, const IntCode* i) {
  if (!ics.rf[i->src1_reg].u8) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_BRANCH_FALSE_I16(IntCodeState& ics, const IntCode* i) {
  if (!ics.rf[i->src1_reg].u16) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_BRANCH_FALSE_I32(IntCodeState& ics, const IntCode* i) {
  if (!ics.rf[i->src1_reg].u32) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_BRANCH_FALSE_I64(IntCodeState& ics, const IntCode* i) {
  if (!ics.rf[i->src1_reg].u64) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_BRANCH_FALSE_F32(IntCodeState& ics, const IntCode* i) {
  if (!ics.rf[i->src1_reg].f32) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
uint32_t IntCode_BRANCH_FALSE_F64(IntCodeState& ics, const IntCode* i) {
  if (!ics.rf[i->src1_reg].f64) {
    return IntCode_BRANCH_XX(ics, i, i->src2_reg);
  }
  return IA_NEXT;
}
int Translate_BRANCH_FALSE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_BRANCH_FALSE_I8,
    IntCode_BRANCH_FALSE_I16,
    IntCode_BRANCH_FALSE_I32,
    IntCode_BRANCH_FALSE_I64,
    IntCode_BRANCH_FALSE_F32,
    IntCode_BRANCH_FALSE_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_ASSIGN_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_ASSIGN_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_ASSIGN_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_ASSIGN_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_ASSIGN_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_ASSIGN_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_ASSIGN_V128(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].v128 = ics.rf[i->src1_reg].v128;
  return IA_NEXT;
}
int Translate_ASSIGN(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_ASSIGN_I8,
    IntCode_ASSIGN_I16,
    IntCode_ASSIGN_I32,
    IntCode_ASSIGN_I64,
    IntCode_ASSIGN_F32,
    IntCode_ASSIGN_F64,
    IntCode_ASSIGN_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_CAST(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].v128 = ics.rf[i->src1_reg].v128;
  return IA_NEXT;
}
int Translate_CAST(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_CAST);
}

uint32_t IntCode_ZERO_EXTEND_I8_TO_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = (uint8_t)ics.rf[i->src1_reg].u8;
  return IA_NEXT;
}
uint32_t IntCode_ZERO_EXTEND_I8_TO_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = (uint16_t)ics.rf[i->src1_reg].u8;
  return IA_NEXT;
}
uint32_t IntCode_ZERO_EXTEND_I8_TO_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = (uint64_t)ics.rf[i->src1_reg].u8;
  return IA_NEXT;
}
uint32_t IntCode_ZERO_EXTEND_I16_TO_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = (uint32_t)ics.rf[i->src1_reg].u16;
  return IA_NEXT;
}
uint32_t IntCode_ZERO_EXTEND_I16_TO_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = (uint64_t)ics.rf[i->src1_reg].u16;
  return IA_NEXT;
}
uint32_t IntCode_ZERO_EXTEND_I32_TO_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = (uint64_t)ics.rf[i->src1_reg].u32;
  return IA_NEXT;
}
int Translate_ZERO_EXTEND(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_ASSIGN_I8, IntCode_ZERO_EXTEND_I8_TO_I16, IntCode_ZERO_EXTEND_I8_TO_I32, IntCode_ZERO_EXTEND_I8_TO_I64, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_ASSIGN_I16, IntCode_ZERO_EXTEND_I16_TO_I32, IntCode_ZERO_EXTEND_I16_TO_I64, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_ASSIGN_I32, IntCode_ZERO_EXTEND_I32_TO_I64, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_ASSIGN_I64, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
  };
  IntCodeFn fn = fns[i->src1.value->type * MAX_TYPENAME + i->dest->type];
  return DispatchToC(ctx, i, fn);
}

uint32_t IntCode_SIGN_EXTEND_I8_TO_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = (int8_t)ics.rf[i->src1_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SIGN_EXTEND_I8_TO_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = (int16_t)ics.rf[i->src1_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SIGN_EXTEND_I8_TO_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = (int64_t)ics.rf[i->src1_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SIGN_EXTEND_I16_TO_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = (int32_t)ics.rf[i->src1_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_SIGN_EXTEND_I16_TO_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = (int64_t)ics.rf[i->src1_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_SIGN_EXTEND_I32_TO_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = (int64_t)ics.rf[i->src1_reg].i32;
  return IA_NEXT;
}
int Translate_SIGN_EXTEND(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_ASSIGN_I8, IntCode_SIGN_EXTEND_I8_TO_I16, IntCode_SIGN_EXTEND_I8_TO_I32, IntCode_SIGN_EXTEND_I8_TO_I64, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_ASSIGN_I16, IntCode_SIGN_EXTEND_I16_TO_I32, IntCode_SIGN_EXTEND_I16_TO_I64, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_ASSIGN_I32, IntCode_SIGN_EXTEND_I32_TO_I64, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_ASSIGN_I64, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
  };
  IntCodeFn fn = fns[i->src1.value->type * MAX_TYPENAME + i->dest->type];
  return DispatchToC(ctx, i, fn);
}

uint32_t IntCode_TRUNCATE_I16_TO_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = (int8_t)ics.rf[i->src1_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_TRUNCATE_I32_TO_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = (int8_t)ics.rf[i->src1_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_TRUNCATE_I32_TO_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = (int16_t)ics.rf[i->src1_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_TRUNCATE_I64_TO_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = (int8_t)ics.rf[i->src1_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_TRUNCATE_I64_TO_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = (int16_t)ics.rf[i->src1_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_TRUNCATE_I64_TO_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = (int32_t)ics.rf[i->src1_reg].i64;
  return IA_NEXT;
}
int Translate_TRUNCATE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_ASSIGN_I8, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_TRUNCATE_I16_TO_I8, IntCode_ASSIGN_I16, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_TRUNCATE_I32_TO_I8, IntCode_TRUNCATE_I32_TO_I16, IntCode_ASSIGN_I32, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_TRUNCATE_I64_TO_I8, IntCode_TRUNCATE_I64_TO_I16, IntCode_TRUNCATE_I64_TO_I32, IntCode_ASSIGN_I64, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
  };
  IntCodeFn fn = fns[i->src1.value->type * MAX_TYPENAME + i->dest->type];
  return DispatchToC(ctx, i, fn);
}

uint32_t IntCode_CONVERT_I32_TO_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = (float)ics.rf[i->src1_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_CONVERT_I64_TO_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = (double)ics.rf[i->src1_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_CONVERT_F32_TO_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = (int32_t)ics.rf[i->src1_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_CONVERT_F32_TO_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = (double)ics.rf[i->src1_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_CONVERT_F64_TO_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = (int64_t)ics.rf[i->src1_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_CONVERT_F64_TO_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = (float)ics.rf[i->src1_reg].f64;
  return IA_NEXT;
}
int Translate_CONVERT(TranslationContext& ctx, Instr* i) {
  // Can do more as needed.
  static IntCodeFn fns[] = {
    IntCode_ASSIGN_I8, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_ASSIGN_I16, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_ASSIGN_I32, IntCode_INVALID_TYPE, IntCode_CONVERT_I32_TO_F32, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_ASSIGN_I64, IntCode_INVALID_TYPE, IntCode_CONVERT_I64_TO_F64, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_CONVERT_F32_TO_I32, IntCode_INVALID_TYPE, IntCode_ASSIGN_F32, IntCode_CONVERT_F32_TO_F64, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_CONVERT_F64_TO_I64, IntCode_CONVERT_F64_TO_F32, IntCode_ASSIGN_F64, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_ASSIGN_V128,
  };
  IntCodeFn fn = fns[i->src1.value->type * MAX_TYPENAME + i->dest->type];
  return DispatchToC(ctx, i, fn);
}


uint32_t IntCode_VECTOR_CONVERT_I2F(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  dest.f4[0] = (float)(int32_t)dest.i4[0];
  dest.f4[1] = (float)(int32_t)dest.i4[1];
  dest.f4[2] = (float)(int32_t)dest.i4[2];
  dest.f4[3] = (float)(int32_t)dest.i4[3];
  return IA_NEXT;
}
int Translate_VECTOR_CONVERT_I2F(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_VECTOR_CONVERT_I2F);
}

uint32_t IntCode_LOAD_CONTEXT_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = *((int8_t*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("%d (%.X) = ctx i8 +%d\n", ics.rf[i->dest_reg].i8, ics.rf[i->dest_reg].u8, ics.rf[i->src1_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = *((int16_t*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("%d (%.X) = ctx i16 +%d\n", ics.rf[i->dest_reg].i16, ics.rf[i->dest_reg].u16, ics.rf[i->src1_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = *((int32_t*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("%d (%.X) = ctx i32 +%d\n", ics.rf[i->dest_reg].i32, ics.rf[i->dest_reg].u32, ics.rf[i->src1_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = *((int64_t*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("%d (%.X) = ctx i64 +%d\n", ics.rf[i->dest_reg].i64, ics.rf[i->dest_reg].u64, ics.rf[i->src1_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = *((float*)(ics.context + ics.rf[i->src1_reg].u64));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = *((double*)(ics.context + ics.rf[i->src1_reg].u64));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_V128(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].v128 = *((vec128_t*)(ics.context + ics.rf[i->src1_reg].u64));
  return IA_NEXT;
}
int Translate_LOAD_CONTEXT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_LOAD_CONTEXT_I8,
    IntCode_LOAD_CONTEXT_I16,
    IntCode_LOAD_CONTEXT_I32,
    IntCode_LOAD_CONTEXT_I64,
    IntCode_LOAD_CONTEXT_F32,
    IntCode_LOAD_CONTEXT_F64,
    IntCode_LOAD_CONTEXT_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_STORE_CONTEXT_I8(IntCodeState& ics, const IntCode* i) {
  *((int8_t*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].i8;
  DPRINT("ctx i8 +%d = %d (%.X)\n", ics.rf[i->src1_reg].u64, ics.rf[i->src2_reg].i8, ics.rf[i->src2_reg].u8);
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_I16(IntCodeState& ics, const IntCode* i) {
  *((int16_t*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].i16;
  DPRINT("ctx i16 +%d = %d (%.X)\n", ics.rf[i->src1_reg].u64, ics.rf[i->src2_reg].i16, ics.rf[i->src2_reg].u16);
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_I32(IntCodeState& ics, const IntCode* i) {
  *((int32_t*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].i32;
  DPRINT("ctx i32 +%d = %d (%.X)\n", ics.rf[i->src1_reg].u64, ics.rf[i->src2_reg].i32, ics.rf[i->src2_reg].u32);
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_I64(IntCodeState& ics, const IntCode* i) {
  *((int64_t*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].i64;
  DPRINT("ctx i64 +%d = %d (%.X)\n", ics.rf[i->src1_reg].u64, ics.rf[i->src2_reg].i64, ics.rf[i->src2_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_F32(IntCodeState& ics, const IntCode* i) {
  *((float*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_F64(IntCodeState& ics, const IntCode* i) {
  *((double*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_V128(IntCodeState& ics, const IntCode* i) {
  *((vec128_t*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].v128;
  return IA_NEXT;
}
int Translate_STORE_CONTEXT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_STORE_CONTEXT_I8,
    IntCode_STORE_CONTEXT_I16,
    IntCode_STORE_CONTEXT_I32,
    IntCode_STORE_CONTEXT_I64,
    IntCode_STORE_CONTEXT_F32,
    IntCode_STORE_CONTEXT_F64,
    IntCode_STORE_CONTEXT_V128,
  };
  return DispatchToC(ctx, i, fns[i->src2.value->type]);
}

uint32_t IntCode_LOAD_I8(IntCodeState& ics, const IntCode* i) {
  DPRINT("%d (%X) = load.i8 %.8X\n",
         *((int8_t*)(ics.membase + ics.rf[i->src1_reg].u32)),
         *((uint8_t*)(ics.membase + ics.rf[i->src1_reg].u32)),
         ics.rf[i->src1_reg].u32);
  DFLUSH();
  ics.rf[i->dest_reg].i8 = *((int8_t*)(ics.membase + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_I16(IntCodeState& ics, const IntCode* i) {
  DPRINT("%d (%X) = load.i16 %.8X\n",
         *((int16_t*)(ics.membase + ics.rf[i->src1_reg].u32)),
         *((uint16_t*)(ics.membase + ics.rf[i->src1_reg].u32)),
         ics.rf[i->src1_reg].u32);
  DFLUSH();
  ics.rf[i->dest_reg].i16 = *((int16_t*)(ics.membase + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_I32(IntCodeState& ics, const IntCode* i) {
  DFLUSH();
  DPRINT("%d (%X) = load.i32 %.8X\n",
         *((int32_t*)(ics.membase + ics.rf[i->src1_reg].u32)),
         *((uint32_t*)(ics.membase + ics.rf[i->src1_reg].u32)),
         ics.rf[i->src1_reg].u32);
  DFLUSH();
  ics.rf[i->dest_reg].i32 = *((int32_t*)(ics.membase + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_I64(IntCodeState& ics, const IntCode* i) {
  DPRINT("%d (%X) = load.i64 %.8X\n",
         *((int64_t*)(ics.membase + ics.rf[i->src1_reg].u32)),
         *((uint64_t*)(ics.membase + ics.rf[i->src1_reg].u32)),
         ics.rf[i->src1_reg].u32);
  DFLUSH();
  ics.rf[i->dest_reg].i64 = *((int64_t*)(ics.membase + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = *((float*)(ics.membase + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = *((double*)(ics.membase + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_V128(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].v128 = *((vec128_t*)(ics.membase + (ics.rf[i->src1_reg].u32 & ~0xF)));
  return IA_NEXT;
}
int Translate_LOAD(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_LOAD_I8,
    IntCode_LOAD_I16,
    IntCode_LOAD_I32,
    IntCode_LOAD_I64,
    IntCode_LOAD_F32,
    IntCode_LOAD_F64,
    IntCode_LOAD_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_STORE_I8(IntCodeState& ics, const IntCode* i) {
  DPRINT("store.i8 %.8X = %d (%X)\n",
         ics.rf[i->src1_reg].u32, ics.rf[i->src2_reg].i8, ics.rf[i->src2_reg].i8);
  DFLUSH();
  *((int8_t*)(ics.membase + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_STORE_I16(IntCodeState& ics, const IntCode* i) {
  DPRINT("store.i16 %.8X = %d (%X)\n",
         ics.rf[i->src1_reg].u32, ics.rf[i->src2_reg].i16, ics.rf[i->src2_reg].i16);
  DFLUSH();
  *((int16_t*)(ics.membase + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_STORE_I32(IntCodeState& ics, const IntCode* i) {
  DPRINT("store.i32 %.8X = %d (%X)\n",
         ics.rf[i->src1_reg].u32, ics.rf[i->src2_reg].i32, ics.rf[i->src2_reg].i32);
  DFLUSH();
  *((int32_t*)(ics.membase + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_STORE_I64(IntCodeState& ics, const IntCode* i) {
  DPRINT("store.i64 %.8X = %d (%X)\n",
         ics.rf[i->src1_reg].u32, ics.rf[i->src2_reg].i64, ics.rf[i->src2_reg].i64);
  DFLUSH();
  *((int64_t*)(ics.membase + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_STORE_F32(IntCodeState& ics, const IntCode* i) {
  *((float*)(ics.membase + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_STORE_F64(IntCodeState& ics, const IntCode* i) {
  *((double*)(ics.membase + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_STORE_V128(IntCodeState& ics, const IntCode* i) {
  *((vec128_t*)(ics.membase + (ics.rf[i->src1_reg].u32 & ~0xF))) = ics.rf[i->src2_reg].v128;
  return IA_NEXT;
}
int Translate_STORE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_STORE_I8,
    IntCode_STORE_I16,
    IntCode_STORE_I32,
    IntCode_STORE_I64,
    IntCode_STORE_F32,
    IntCode_STORE_F64,
    IntCode_STORE_V128,
  };
  return DispatchToC(ctx, i, fns[i->src2.value->type]);
}

uint32_t IntCode_PREFETCH(IntCodeState& ics, const IntCode* i) {
  return IA_NEXT;
}
int Translate_PREFETCH(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_PREFETCH);
}

uint32_t IntCode_SELECT_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 ?
      ics.rf[i->src2_reg].i8 : ics.rf[i->src3_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SELECT_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i8 ?
      ics.rf[i->src2_reg].i16 : ics.rf[i->src3_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_SELECT_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i8 ?
      ics.rf[i->src2_reg].i32 : ics.rf[i->src3_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_SELECT_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i8 ?
      ics.rf[i->src2_reg].i64 : ics.rf[i->src3_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_SELECT_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].i8 ?
      ics.rf[i->src2_reg].f32 : ics.rf[i->src3_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_SELECT_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].i8 ?
      ics.rf[i->src2_reg].f64 : ics.rf[i->src3_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_SELECT_V128(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].v128 = ics.rf[i->src1_reg].i8 ?
      ics.rf[i->src2_reg].v128 : ics.rf[i->src3_reg].v128;
  return IA_NEXT;
}
int Translate_SELECT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_SELECT_I8,
    IntCode_SELECT_I16,
    IntCode_SELECT_I32,
    IntCode_SELECT_I64,
    IntCode_SELECT_F32,
    IntCode_SELECT_F64,
    IntCode_SELECT_V128,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_IS_TRUE_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !!ics.rf[i->src1_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_IS_TRUE_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !!ics.rf[i->src1_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_IS_TRUE_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !!ics.rf[i->src1_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_IS_TRUE_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !!ics.rf[i->src1_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_IS_TRUE_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !!ics.rf[i->src1_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_IS_TRUE_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !!ics.rf[i->src1_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_IS_TRUE_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  ics.rf[i->dest_reg].i8 = src1.high && src1.low;
  return IA_NEXT;
}
int Translate_IS_TRUE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_IS_TRUE_I8,
    IntCode_IS_TRUE_I16,
    IntCode_IS_TRUE_I32,
    IntCode_IS_TRUE_I64,
    IntCode_IS_TRUE_F32,
    IntCode_IS_TRUE_F64,
    IntCode_IS_TRUE_V128,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_IS_FALSE_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !ics.rf[i->src1_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_IS_FALSE_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !ics.rf[i->src1_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_IS_FALSE_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !ics.rf[i->src1_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_IS_FALSE_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !ics.rf[i->src1_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_IS_FALSE_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !ics.rf[i->src1_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_IS_FALSE_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = !ics.rf[i->src1_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_IS_FALSE_V128(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 =
      !(ics.rf[i->src1_reg].v128.high && ics.rf[i->src1_reg].v128.low);
  return IA_NEXT;
}
int Translate_IS_FALSE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_IS_FALSE_I8,
    IntCode_IS_FALSE_I16,
    IntCode_IS_FALSE_I32,
    IntCode_IS_FALSE_I64,
    IntCode_IS_FALSE_F32,
    IntCode_IS_FALSE_F64,
    IntCode_IS_FALSE_V128,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_COMPARE_EQ_I8_I8(IntCodeState& ics, const IntCode* i) {    ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8  == ics.rf[i->src2_reg].i8; return IA_NEXT; }
uint32_t IntCode_COMPARE_EQ_I16_I16(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i16 == ics.rf[i->src2_reg].i16; return IA_NEXT; }
uint32_t IntCode_COMPARE_EQ_I32_I32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i32 == ics.rf[i->src2_reg].i32; return IA_NEXT; }
uint32_t IntCode_COMPARE_EQ_I64_I64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i64 == ics.rf[i->src2_reg].i64; return IA_NEXT; }
uint32_t IntCode_COMPARE_EQ_F32_F32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f32 == ics.rf[i->src2_reg].f32; return IA_NEXT; }
uint32_t IntCode_COMPARE_EQ_F64_F64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f64 == ics.rf[i->src2_reg].f64; return IA_NEXT; }
int Translate_COMPARE_EQ(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_COMPARE_EQ_I8_I8,
    IntCode_COMPARE_EQ_I16_I16,
    IntCode_COMPARE_EQ_I32_I32,
    IntCode_COMPARE_EQ_I64_I64,
    IntCode_COMPARE_EQ_F32_F32,
    IntCode_COMPARE_EQ_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_COMPARE_NE_I8_I8(IntCodeState& ics, const IntCode* i) {    ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8  != ics.rf[i->src2_reg].i8; return IA_NEXT; }
uint32_t IntCode_COMPARE_NE_I16_I16(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i16 != ics.rf[i->src2_reg].i16; return IA_NEXT; }
uint32_t IntCode_COMPARE_NE_I32_I32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i32 != ics.rf[i->src2_reg].i32; return IA_NEXT; }
uint32_t IntCode_COMPARE_NE_I64_I64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i64 != ics.rf[i->src2_reg].i64; return IA_NEXT; }
uint32_t IntCode_COMPARE_NE_F32_F32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f32 != ics.rf[i->src2_reg].f32; return IA_NEXT; }
uint32_t IntCode_COMPARE_NE_F64_F64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f64 != ics.rf[i->src2_reg].f64; return IA_NEXT; }
int Translate_COMPARE_NE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_COMPARE_NE_I8_I8,
    IntCode_COMPARE_NE_I16_I16,
    IntCode_COMPARE_NE_I32_I32,
    IntCode_COMPARE_NE_I64_I64,
    IntCode_COMPARE_NE_F32_F32,
    IntCode_COMPARE_NE_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_COMPARE_SLT_I8_I8(IntCodeState& ics, const IntCode* i) {    ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8  < ics.rf[i->src2_reg].i8; return IA_NEXT; }
uint32_t IntCode_COMPARE_SLT_I16_I16(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i16 < ics.rf[i->src2_reg].i16; return IA_NEXT; }
uint32_t IntCode_COMPARE_SLT_I32_I32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i32 < ics.rf[i->src2_reg].i32; return IA_NEXT; }
uint32_t IntCode_COMPARE_SLT_I64_I64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i64 < ics.rf[i->src2_reg].i64; return IA_NEXT; }
uint32_t IntCode_COMPARE_SLT_F32_F32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f32 < ics.rf[i->src2_reg].f32; return IA_NEXT; }
uint32_t IntCode_COMPARE_SLT_F64_F64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f64 < ics.rf[i->src2_reg].f64; return IA_NEXT; }
int Translate_COMPARE_SLT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_COMPARE_SLT_I8_I8,
    IntCode_COMPARE_SLT_I16_I16,
    IntCode_COMPARE_SLT_I32_I32,
    IntCode_COMPARE_SLT_I64_I64,
    IntCode_COMPARE_SLT_F32_F32,
    IntCode_COMPARE_SLT_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_COMPARE_SLE_I8_I8(IntCodeState& ics, const IntCode* i) {    ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8  <= ics.rf[i->src2_reg].i8; return IA_NEXT; }
uint32_t IntCode_COMPARE_SLE_I16_I16(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i16 <= ics.rf[i->src2_reg].i16; return IA_NEXT; }
uint32_t IntCode_COMPARE_SLE_I32_I32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i32 <= ics.rf[i->src2_reg].i32; return IA_NEXT; }
uint32_t IntCode_COMPARE_SLE_I64_I64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i64 <= ics.rf[i->src2_reg].i64; return IA_NEXT; }
uint32_t IntCode_COMPARE_SLE_F32_F32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f32 <= ics.rf[i->src2_reg].f32; return IA_NEXT; }
uint32_t IntCode_COMPARE_SLE_F64_F64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f64 <= ics.rf[i->src2_reg].f64; return IA_NEXT; }
int Translate_COMPARE_SLE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_COMPARE_SLE_I8_I8,
    IntCode_COMPARE_SLE_I16_I16,
    IntCode_COMPARE_SLE_I32_I32,
    IntCode_COMPARE_SLE_I64_I64,
    IntCode_COMPARE_SLE_F32_F32,
    IntCode_COMPARE_SLE_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_COMPARE_SGT_I8_I8(IntCodeState& ics, const IntCode* i) {    ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8  > ics.rf[i->src2_reg].i8; return IA_NEXT; }
uint32_t IntCode_COMPARE_SGT_I16_I16(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i16 > ics.rf[i->src2_reg].i16; return IA_NEXT; }
uint32_t IntCode_COMPARE_SGT_I32_I32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i32 > ics.rf[i->src2_reg].i32; return IA_NEXT; }
uint32_t IntCode_COMPARE_SGT_I64_I64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i64 > ics.rf[i->src2_reg].i64; return IA_NEXT; }
uint32_t IntCode_COMPARE_SGT_F32_F32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f32 > ics.rf[i->src2_reg].f32; return IA_NEXT; }
uint32_t IntCode_COMPARE_SGT_F64_F64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f64 > ics.rf[i->src2_reg].f64; return IA_NEXT; }
int Translate_COMPARE_SGT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_COMPARE_SGT_I8_I8,
    IntCode_COMPARE_SGT_I16_I16,
    IntCode_COMPARE_SGT_I32_I32,
    IntCode_COMPARE_SGT_I64_I64,
    IntCode_COMPARE_SGT_F32_F32,
    IntCode_COMPARE_SGT_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_COMPARE_SGE_I8_I8(IntCodeState& ics, const IntCode* i) {    ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8  >= ics.rf[i->src2_reg].i8; return IA_NEXT; }
uint32_t IntCode_COMPARE_SGE_I16_I16(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i16 >= ics.rf[i->src2_reg].i16; return IA_NEXT; }
uint32_t IntCode_COMPARE_SGE_I32_I32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i32 >= ics.rf[i->src2_reg].i32; return IA_NEXT; }
uint32_t IntCode_COMPARE_SGE_I64_I64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i64 >= ics.rf[i->src2_reg].i64; return IA_NEXT; }
uint32_t IntCode_COMPARE_SGE_F32_F32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f32 >= ics.rf[i->src2_reg].f32; return IA_NEXT; }
uint32_t IntCode_COMPARE_SGE_F64_F64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f64 >= ics.rf[i->src2_reg].f64; return IA_NEXT; }
int Translate_COMPARE_SGE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_COMPARE_SGE_I8_I8,
    IntCode_COMPARE_SGE_I16_I16,
    IntCode_COMPARE_SGE_I32_I32,
    IntCode_COMPARE_SGE_I64_I64,
    IntCode_COMPARE_SGE_F32_F32,
    IntCode_COMPARE_SGE_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_COMPARE_ULT_I8_I8(IntCodeState& ics, const IntCode* i) {    ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u8  < ics.rf[i->src2_reg].u8; return IA_NEXT; }
uint32_t IntCode_COMPARE_ULT_I16_I16(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u16 < ics.rf[i->src2_reg].u16; return IA_NEXT; }
uint32_t IntCode_COMPARE_ULT_I32_I32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u32 < ics.rf[i->src2_reg].u32; return IA_NEXT; }
uint32_t IntCode_COMPARE_ULT_I64_I64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u64 < ics.rf[i->src2_reg].u64; return IA_NEXT; }
uint32_t IntCode_COMPARE_ULT_F32_F32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f32 < ics.rf[i->src2_reg].f32; return IA_NEXT; }
uint32_t IntCode_COMPARE_ULT_F64_F64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f64 < ics.rf[i->src2_reg].f64; return IA_NEXT; }
int Translate_COMPARE_ULT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_COMPARE_ULT_I8_I8,
    IntCode_COMPARE_ULT_I16_I16,
    IntCode_COMPARE_ULT_I32_I32,
    IntCode_COMPARE_ULT_I64_I64,
    IntCode_COMPARE_ULT_F32_F32,
    IntCode_COMPARE_ULT_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_COMPARE_ULE_I8_I8(IntCodeState& ics, const IntCode* i) {    ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u8  <= ics.rf[i->src2_reg].u8; return IA_NEXT; }
uint32_t IntCode_COMPARE_ULE_I16_I16(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u16 <= ics.rf[i->src2_reg].u16; return IA_NEXT; }
uint32_t IntCode_COMPARE_ULE_I32_I32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u32 <= ics.rf[i->src2_reg].u32; return IA_NEXT; }
uint32_t IntCode_COMPARE_ULE_I64_I64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u64 <= ics.rf[i->src2_reg].u64; return IA_NEXT; }
uint32_t IntCode_COMPARE_ULE_F32_F32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f32 <= ics.rf[i->src2_reg].f32; return IA_NEXT; }
uint32_t IntCode_COMPARE_ULE_F64_F64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f64 <= ics.rf[i->src2_reg].f64; return IA_NEXT; }
int Translate_COMPARE_ULE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_COMPARE_ULE_I8_I8,
    IntCode_COMPARE_ULE_I16_I16,
    IntCode_COMPARE_ULE_I32_I32,
    IntCode_COMPARE_ULE_I64_I64,
    IntCode_COMPARE_ULE_F32_F32,
    IntCode_COMPARE_ULE_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_COMPARE_UGT_I8_I8(IntCodeState& ics, const IntCode* i) {    ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u8  > ics.rf[i->src2_reg].u8; return IA_NEXT; }
uint32_t IntCode_COMPARE_UGT_I16_I16(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u16 > ics.rf[i->src2_reg].u16; return IA_NEXT; }
uint32_t IntCode_COMPARE_UGT_I32_I32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u32 > ics.rf[i->src2_reg].u32; return IA_NEXT; }
uint32_t IntCode_COMPARE_UGT_I64_I64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u64 > ics.rf[i->src2_reg].u64; return IA_NEXT; }
uint32_t IntCode_COMPARE_UGT_F32_F32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f32 > ics.rf[i->src2_reg].f32; return IA_NEXT; }
uint32_t IntCode_COMPARE_UGT_F64_F64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f64 > ics.rf[i->src2_reg].f64; return IA_NEXT; }
int Translate_COMPARE_UGT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_COMPARE_UGT_I8_I8,
    IntCode_COMPARE_UGT_I16_I16,
    IntCode_COMPARE_UGT_I32_I32,
    IntCode_COMPARE_UGT_I64_I64,
    IntCode_COMPARE_UGT_F32_F32,
    IntCode_COMPARE_UGT_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_COMPARE_UGE_I8_I8(IntCodeState& ics, const IntCode* i) {    ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u8  >= ics.rf[i->src2_reg].u8; return IA_NEXT; }
uint32_t IntCode_COMPARE_UGE_I16_I16(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u16 >= ics.rf[i->src2_reg].u16; return IA_NEXT; }
uint32_t IntCode_COMPARE_UGE_I32_I32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u32 >= ics.rf[i->src2_reg].u32; return IA_NEXT; }
uint32_t IntCode_COMPARE_UGE_I64_I64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u64 >= ics.rf[i->src2_reg].u64; return IA_NEXT; }
uint32_t IntCode_COMPARE_UGE_F32_F32(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f32 >= ics.rf[i->src2_reg].f32; return IA_NEXT; }
uint32_t IntCode_COMPARE_UGE_F64_F64(IntCodeState& ics, const IntCode* i) {  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].f64 >= ics.rf[i->src2_reg].f64; return IA_NEXT; }
int Translate_COMPARE_UGE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_COMPARE_UGE_I8_I8,
    IntCode_COMPARE_UGE_I16_I16,
    IntCode_COMPARE_UGE_I32_I32,
    IntCode_COMPARE_UGE_I64_I64,
    IntCode_COMPARE_UGE_F32_F32,
    IntCode_COMPARE_UGE_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_DID_CARRY(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.did_carry;
  return IA_NEXT;
}
int Translate_DID_CARRY(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_DID_CARRY);
}

#define VECTOR_COMPARER(type, value, count, op) \
  const vec128_t& src1 = ics.rf[i->src1_reg].v128; \
  const vec128_t& src2 = ics.rf[i->src2_reg].v128; \
  vec128_t& dest = ics.rf[i->dest_reg].v128; \
  for (int n = 0; n < count; n++) { \
    dest.value[n] = (type)src1.value[n] op (type)src1.value[n]; \
  } \
  return IA_NEXT;

uint32_t IntCode_VECTOR_COMPARE_EQ_I8(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint8_t, b16, 16, ==) };
uint32_t IntCode_VECTOR_COMPARE_EQ_I16(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint16_t, s8, 8, ==) };
uint32_t IntCode_VECTOR_COMPARE_EQ_I32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint32_t, i4, 4, ==) };
uint32_t IntCode_VECTOR_COMPARE_EQ_F32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(float, f4, 4, ==) };
int Translate_VECTOR_COMPARE_EQ(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_VECTOR_COMPARE_EQ_I8,
    IntCode_VECTOR_COMPARE_EQ_I16,
    IntCode_VECTOR_COMPARE_EQ_I32,
    IntCode_INVALID_TYPE,
    IntCode_VECTOR_COMPARE_EQ_F32,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->flags]);
}

uint32_t IntCode_VECTOR_COMPARE_SGT_I8(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int8_t, b16, 16, >) };
uint32_t IntCode_VECTOR_COMPARE_SGT_I16(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int16_t, s8, 8, >) };
uint32_t IntCode_VECTOR_COMPARE_SGT_I32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int32_t, i4, 4, >) };
uint32_t IntCode_VECTOR_COMPARE_SGT_F32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(float, f4, 4, >) };
int Translate_VECTOR_COMPARE_SGT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_VECTOR_COMPARE_SGT_I8,
    IntCode_VECTOR_COMPARE_SGT_I16,
    IntCode_VECTOR_COMPARE_SGT_I32,
    IntCode_INVALID_TYPE,
    IntCode_VECTOR_COMPARE_SGT_F32,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->flags]);
}

uint32_t IntCode_VECTOR_COMPARE_SGE_I8(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int8_t, b16, 16, >=) };
uint32_t IntCode_VECTOR_COMPARE_SGE_I16(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int16_t, s8, 8, >=) };
uint32_t IntCode_VECTOR_COMPARE_SGE_I32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int32_t, i4, 4, >=) };
uint32_t IntCode_VECTOR_COMPARE_SGE_F32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(float, f4, 4, >=) };
int Translate_VECTOR_COMPARE_SGE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_VECTOR_COMPARE_SGE_I8,
    IntCode_VECTOR_COMPARE_SGE_I16,
    IntCode_VECTOR_COMPARE_SGE_I32,
    IntCode_INVALID_TYPE,
    IntCode_VECTOR_COMPARE_SGE_F32,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->flags]);
}

uint32_t IntCode_VECTOR_COMPARE_UGT_I8(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint8_t, b16, 16, >) };
uint32_t IntCode_VECTOR_COMPARE_UGT_I16(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint16_t, s8, 8, >) };
uint32_t IntCode_VECTOR_COMPARE_UGT_I32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint32_t, i4, 4, >) };
uint32_t IntCode_VECTOR_COMPARE_UGT_F32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(float, f4, 4, >) };
int Translate_VECTOR_COMPARE_UGT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_VECTOR_COMPARE_UGT_I8,
    IntCode_VECTOR_COMPARE_UGT_I16,
    IntCode_VECTOR_COMPARE_UGT_I32,
    IntCode_INVALID_TYPE,
    IntCode_VECTOR_COMPARE_UGT_F32,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->flags]);
}

uint32_t IntCode_VECTOR_COMPARE_UGE_I8(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint8_t, b16, 16, >=) };
uint32_t IntCode_VECTOR_COMPARE_UGE_I16(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint16_t, s8, 8, >=) };
uint32_t IntCode_VECTOR_COMPARE_UGE_I32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint32_t, i4, 4, >=) };
uint32_t IntCode_VECTOR_COMPARE_UGE_F32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(float, f4, 4, >=) };
int Translate_VECTOR_COMPARE_UGE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_VECTOR_COMPARE_UGE_I8,
    IntCode_VECTOR_COMPARE_UGE_I16,
    IntCode_VECTOR_COMPARE_UGE_I32,
    IntCode_INVALID_TYPE,
    IntCode_VECTOR_COMPARE_UGE_F32,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->flags]);
}

#define CHECK_DID_CARRY(v1, v2) ((v2) > ~(v1))
#define ADD_DID_CARRY(a, b) CHECK_DID_CARRY(a, b)
uint32_t IntCode_ADD_I8_I8(IntCodeState& ics, const IntCode* i) {
  int8_t a = ics.rf[i->src1_reg].i8; int8_t b = ics.rf[i->src2_reg].i8;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = ADD_DID_CARRY(a, b);
  }
  ics.rf[i->dest_reg].i8 = a + b;
  return IA_NEXT;
}
uint32_t IntCode_ADD_I16_I16(IntCodeState& ics, const IntCode* i) {
  int16_t a = ics.rf[i->src1_reg].i16; int16_t b = ics.rf[i->src2_reg].i16;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = ADD_DID_CARRY(a, b);
  }
  ics.rf[i->dest_reg].i16 = a + b;
  return IA_NEXT;
}
uint32_t IntCode_ADD_I32_I32(IntCodeState& ics, const IntCode* i) {
  int32_t a = ics.rf[i->src1_reg].i32; int32_t b = ics.rf[i->src2_reg].i32;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = ADD_DID_CARRY(a, b);
  }
  ics.rf[i->dest_reg].i32 = a + b;
  return IA_NEXT;
}
uint32_t IntCode_ADD_I64_I64(IntCodeState& ics, const IntCode* i) {
  int64_t a = ics.rf[i->src1_reg].i64; int64_t b = ics.rf[i->src2_reg].i64;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = ADD_DID_CARRY(a, b);
  }
  ics.rf[i->dest_reg].i64 = a + b;
  return IA_NEXT;
}
uint32_t IntCode_ADD_F32_F32(IntCodeState& ics, const IntCode* i) {
  XEASSERT(!i->flags);
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32 + ics.rf[i->src2_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_ADD_F64_F64(IntCodeState& ics, const IntCode* i) {
  XEASSERT(!i->flags);
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64 + ics.rf[i->src2_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_ADD_V128_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = src1.f4[n] + src2.f4[n];
  }
  return IA_NEXT;
}
int Translate_ADD(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_ADD_I8_I8,
    IntCode_ADD_I16_I16,
    IntCode_ADD_I32_I32,
    IntCode_ADD_I64_I64,
    IntCode_ADD_F32_F32,
    IntCode_ADD_F64_F64,
    IntCode_ADD_V128_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

#define ADD_CARRY_DID_CARRY(a, b, c) \
    (CHECK_DID_CARRY(a, b) || ((c) != 0) && CHECK_DID_CARRY((a) + (b), c))
uint32_t IntCode_ADD_CARRY_I8_I8(IntCodeState& ics, const IntCode* i) {
  int8_t a = ics.rf[i->src1_reg].i8; int8_t b = ics.rf[i->src2_reg].i8; uint8_t c = ics.rf[i->src3_reg].u8;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = ADD_CARRY_DID_CARRY(a, b, c);
  }
  ics.rf[i->dest_reg].i8 = a + b + c;
  return IA_NEXT;
}
uint32_t IntCode_ADD_CARRY_I16_I16(IntCodeState& ics, const IntCode* i) {
  int16_t a = ics.rf[i->src1_reg].i16; int16_t b = ics.rf[i->src2_reg].i16; uint8_t c = ics.rf[i->src3_reg].u8;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = ADD_CARRY_DID_CARRY(a, b, c);
  }
  ics.rf[i->dest_reg].i16 = a + b + c;
  return IA_NEXT;
}
uint32_t IntCode_ADD_CARRY_I32_I32(IntCodeState& ics, const IntCode* i) {
  int32_t a = ics.rf[i->src1_reg].i32; int32_t b = ics.rf[i->src2_reg].i32; uint8_t c = ics.rf[i->src3_reg].u8;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = ADD_CARRY_DID_CARRY(a, b, c);
  }
  ics.rf[i->dest_reg].i32 = a + b + c;
  return IA_NEXT;
}
uint32_t IntCode_ADD_CARRY_I64_I64(IntCodeState& ics, const IntCode* i) {
  int64_t a = ics.rf[i->src1_reg].i64; int64_t b = ics.rf[i->src2_reg].i64; uint8_t c = ics.rf[i->src3_reg].u8;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = ADD_CARRY_DID_CARRY(a, b, c);
  }
  ics.rf[i->dest_reg].i64 = a + b + c;
  return IA_NEXT;
}
uint32_t IntCode_ADD_CARRY_F32_F32(IntCodeState& ics, const IntCode* i) {
  XEASSERT(!i->flags);
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32 + ics.rf[i->src2_reg].f32 + ics.rf[i->src3_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_ADD_CARRY_F64_F64(IntCodeState& ics, const IntCode* i) {
  XEASSERT(!i->flags);
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64 + ics.rf[i->src2_reg].f64 + ics.rf[i->src3_reg].i8;
  return IA_NEXT;
}
int Translate_ADD_CARRY(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_ADD_CARRY_I8_I8,
    IntCode_ADD_CARRY_I16_I16,
    IntCode_ADD_CARRY_I32_I32,
    IntCode_ADD_CARRY_I64_I64,
    IntCode_ADD_CARRY_F32_F32,
    IntCode_ADD_CARRY_F64_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

#define SUB_DID_CARRY(a, b) \
    ((b) == 0) || CHECK_DID_CARRY(a, 0 - b)
uint32_t IntCode_SUB_I8_I8(IntCodeState& ics, const IntCode* i) {
  int8_t a = ics.rf[i->src1_reg].i8; int8_t b = ics.rf[i->src2_reg].i8;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = SUB_DID_CARRY(a, b);
  }
  ics.rf[i->dest_reg].i8 = a - b;
  return IA_NEXT;
}
uint32_t IntCode_SUB_I16_I16(IntCodeState& ics, const IntCode* i) {
  int16_t a = ics.rf[i->src1_reg].i16; int16_t b = ics.rf[i->src2_reg].i16;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = SUB_DID_CARRY(a, b);
  }
  ics.rf[i->dest_reg].i16 = a - b;
  return IA_NEXT;
}
uint32_t IntCode_SUB_I32_I32(IntCodeState& ics, const IntCode* i) {
  int32_t a = ics.rf[i->src1_reg].i32; int32_t b = ics.rf[i->src2_reg].i32;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = a < ~b;
  }
  ics.did_carry = SUB_DID_CARRY(a, b);
  return IA_NEXT;
}
uint32_t IntCode_SUB_I64_I64(IntCodeState& ics, const IntCode* i) {
  int64_t a = ics.rf[i->src1_reg].i64; int64_t b = ics.rf[i->src2_reg].i64;
  if (i->flags == ARITHMETIC_SET_CARRY) {
    ics.did_carry = SUB_DID_CARRY(a, b);
  }
  ics.rf[i->dest_reg].i64 = a - b;
  return IA_NEXT;
}
uint32_t IntCode_SUB_F32_F32(IntCodeState& ics, const IntCode* i) {
  XEASSERT(!i->flags);
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32 - ics.rf[i->src2_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_SUB_F64_F64(IntCodeState& ics, const IntCode* i) {
  XEASSERT(!i->flags);
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64 - ics.rf[i->src2_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_SUB_V128_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = src1.f4[n] - src2.f4[n];
  }
  return IA_NEXT;
}
int Translate_SUB(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_SUB_I8_I8,
    IntCode_SUB_I16_I16,
    IntCode_SUB_I32_I32,
    IntCode_SUB_I64_I64,
    IntCode_SUB_F32_F32,
    IntCode_SUB_F64_F64,
    IntCode_SUB_V128_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_MUL_I8_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 * ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_MUL_I16_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 * ics.rf[i->src2_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_MUL_I32_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 * ics.rf[i->src2_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_MUL_I64_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 * ics.rf[i->src2_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_MUL_F32_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32 * ics.rf[i->src2_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_MUL_F64_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64 * ics.rf[i->src2_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_MUL_V128_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = src1.f4[n] * src2.f4[n];
  }
  return IA_NEXT;
}
int Translate_MUL(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_MUL_I8_I8,
    IntCode_MUL_I16_I16,
    IntCode_MUL_I32_I32,
    IntCode_MUL_I64_I64,
    IntCode_MUL_F32_F32,
    IntCode_MUL_F64_F64,
    IntCode_MUL_V128_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_DIV_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 / ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_DIV_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 / ics.rf[i->src2_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_DIV_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 / ics.rf[i->src2_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_DIV_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 / ics.rf[i->src2_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_DIV_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32 / ics.rf[i->src2_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_DIV_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64 / ics.rf[i->src2_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_DIV_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = src1.f4[n] / src2.f4[n];
  }
  return IA_NEXT;
}
int Translate_DIV(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_DIV_I8,
    IntCode_DIV_I16,
    IntCode_DIV_I32,
    IntCode_DIV_I64,
    IntCode_DIV_F32,
    IntCode_DIV_F64,
    IntCode_DIV_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

// TODO(benvanik): use intrinsics or something
uint32_t IntCode_MULADD_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 * ics.rf[i->src2_reg].i8 + ics.rf[i->src3_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_MULADD_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 * ics.rf[i->src2_reg].i16 + ics.rf[i->src3_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_MULADD_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 * ics.rf[i->src2_reg].i32 + ics.rf[i->src3_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_MULADD_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 * ics.rf[i->src2_reg].i64 + ics.rf[i->src3_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_MULADD_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32 * ics.rf[i->src2_reg].f32 + ics.rf[i->src3_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_MULADD_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64 * ics.rf[i->src2_reg].f64 + ics.rf[i->src3_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_MULADD_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  const vec128_t& src3 = ics.rf[i->src3_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = src1.f4[n] * src2.f4[n] + src3.f4[n];
  }
  return IA_NEXT;
}
int Translate_MULADD(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_MULADD_I8,
    IntCode_MULADD_I16,
    IntCode_MULADD_I32,
    IntCode_MULADD_I64,
    IntCode_MULADD_F32,
    IntCode_MULADD_F64,
    IntCode_MULADD_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

// TODO(benvanik): use intrinsics or something
uint32_t IntCode_MULSUB_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 * ics.rf[i->src2_reg].i8 - ics.rf[i->src3_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_MULSUB_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 * ics.rf[i->src2_reg].i16 - ics.rf[i->src3_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_MULSUB_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 * ics.rf[i->src2_reg].i32 - ics.rf[i->src3_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_MULSUB_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 * ics.rf[i->src2_reg].i64 - ics.rf[i->src3_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_MULSUB_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32 * ics.rf[i->src2_reg].f32 - ics.rf[i->src3_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_MULSUB_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64 * ics.rf[i->src2_reg].f64 - ics.rf[i->src3_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_MULSUB_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  const vec128_t& src3 = ics.rf[i->src3_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = src1.f4[n] * src2.f4[n] - src3.f4[n];
  }
  return IA_NEXT;
}
int Translate_MULSUB(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_MULSUB_I8,
    IntCode_MULSUB_I16,
    IntCode_MULSUB_I32,
    IntCode_MULSUB_I64,
    IntCode_MULSUB_F32,
    IntCode_MULSUB_F64,
    IntCode_MULSUB_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_NEG_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = -ics.rf[i->src1_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_NEG_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = -ics.rf[i->src1_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_NEG_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = -ics.rf[i->src1_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_NEG_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = -ics.rf[i->src1_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_NEG_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = -ics.rf[i->src1_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_NEG_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = -ics.rf[i->src1_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_NEG_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 4; i++) {
    dest.f4[i] = -src1.f4[i];
  }
  return IA_NEXT;
}
int Translate_NEG(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_NEG_I8,
    IntCode_NEG_I16,
    IntCode_NEG_I32,
    IntCode_NEG_I64,
    IntCode_NEG_F32,
    IntCode_NEG_F64,
    IntCode_NEG_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_ABS_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = abs(ics.rf[i->src1_reg].i8);
  return IA_NEXT;
}
uint32_t IntCode_ABS_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = abs(ics.rf[i->src1_reg].i16);
  return IA_NEXT;
}
uint32_t IntCode_ABS_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = abs(ics.rf[i->src1_reg].i32);
  return IA_NEXT;
}
uint32_t IntCode_ABS_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = abs(ics.rf[i->src1_reg].i64);
  return IA_NEXT;
}
uint32_t IntCode_ABS_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = abs(ics.rf[i->src1_reg].f32);
  return IA_NEXT;
}
uint32_t IntCode_ABS_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = abs(ics.rf[i->src1_reg].f64);
  return IA_NEXT;
}
uint32_t IntCode_ABS_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 4; i++) {
    dest.f4[i] = abs(src1.f4[i]);
  }
  return IA_NEXT;
}
int Translate_ABS(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_ABS_I8,
    IntCode_ABS_I16,
    IntCode_ABS_I32,
    IntCode_ABS_I64,
    IntCode_ABS_F32,
    IntCode_ABS_F64,
    IntCode_ABS_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_DOT_PRODUCT_3_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src1_reg].v128;
  ics.rf[i->dest_reg].f32 =
      (src1.x * src2.x) + (src1.y * src2.y) + (src1.z * src2.z);
  return IA_NEXT;
}
int Translate_DOT_PRODUCT_3(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_DOT_PRODUCT_3_V128,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_DOT_PRODUCT_4_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src1_reg].v128;
  ics.rf[i->dest_reg].f32 =
      (src1.x * src2.x) + (src1.y * src2.y) + (src1.z * src2.z) + (src1.w * src2.w);
  return IA_NEXT;
}
int Translate_DOT_PRODUCT_4(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_DOT_PRODUCT_4_V128,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_RSQRT_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = 1 / sqrtf(src1.f4[n]);
  }
  return IA_NEXT;
}
int Translate_RSQRT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_RSQRT_V128,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_AND_I8_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 & ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_AND_I16_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 & ics.rf[i->src2_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_AND_I32_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 & ics.rf[i->src2_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_AND_I64_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 & ics.rf[i->src2_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_AND_V128_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.i4[n] = src1.i4[n] & src2.i4[n];
  }
  return IA_NEXT;
}
int Translate_AND(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_AND_I8_I8,
    IntCode_AND_I16_I16,
    IntCode_AND_I32_I32,
    IntCode_AND_I64_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_AND_V128_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_OR_I8_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 | ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_OR_I16_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 | ics.rf[i->src2_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_OR_I32_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 | ics.rf[i->src2_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_OR_I64_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 | ics.rf[i->src2_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_OR_V128_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.i4[n] = src1.i4[n] | src2.i4[n];
  }
  return IA_NEXT;
}
int Translate_OR(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_OR_I8_I8,
    IntCode_OR_I16_I16,
    IntCode_OR_I32_I32,
    IntCode_OR_I64_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_OR_V128_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_XOR_I8_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 ^ ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_XOR_I16_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 ^ ics.rf[i->src2_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_XOR_I32_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 ^ ics.rf[i->src2_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_XOR_I64_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 ^ ics.rf[i->src2_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_XOR_V128_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.i4[n] = src1.i4[n] ^ src2.i4[n];
  }
  return IA_NEXT;
}
int Translate_XOR(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_XOR_I8_I8,
    IntCode_XOR_I16_I16,
    IntCode_XOR_I32_I32,
    IntCode_XOR_I64_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_XOR_V128_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_NOT_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ~ics.rf[i->src1_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_NOT_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ~ics.rf[i->src1_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_NOT_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ~ics.rf[i->src1_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_NOT_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ~ics.rf[i->src1_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_NOT_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.i4[n] = ~src1.i4[n];
  }
  return IA_NEXT;
}
int Translate_NOT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_NOT_I8,
    IntCode_NOT_I16,
    IntCode_NOT_I32,
    IntCode_NOT_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_NOT_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_SHL_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 << ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SHL_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 << ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SHL_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 << ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SHL_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 << ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
int Translate_SHL(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_SHL_I8,
    IntCode_SHL_I16,
    IntCode_SHL_I32,
    IntCode_SHL_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_VECTOR_SHL_I8(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 16; n++) {
    dest.b16[n] = src1.b16[n] << src2.b16[n] & 0x7;
  }
  return IA_NEXT;
}
uint32_t IntCode_VECTOR_SHL_I16(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 8; n++) {
    dest.s8[n] = src1.s8[n] << src2.s8[n] & 0xF;
  }
  return IA_NEXT;
}
uint32_t IntCode_VECTOR_SHL_I32(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.i4[n] = src1.i4[n] << src2.i4[n] & 0x1F;
  }
  return IA_NEXT;
}
int Translate_VECTOR_SHL(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_VECTOR_SHL_I8,
    IntCode_VECTOR_SHL_I16,
    IntCode_VECTOR_SHL_I32,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->flags]);
}

uint32_t IntCode_SHR_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].u8 >> ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SHR_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].u16 >> ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SHR_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].u32 >> ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SHR_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].u64 >> ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
int Translate_SHR(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_SHR_I8,
    IntCode_SHR_I16,
    IntCode_SHR_I32,
    IntCode_SHR_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_SHA_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 >> ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SHA_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 >> ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SHA_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 >> ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_SHA_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 >> ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
int Translate_SHA(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_SHA_I8,
    IntCode_SHA_I16,
    IntCode_SHA_I32,
    IntCode_SHA_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

#define ROTL8(v, n) \
  (uint8_t((v) << (n)) | ((v) >> (8 - (n))))
#define ROTL16(v, n) \
  (uint16_t((v) << (n)) | ((v) >> (16 - (n))))
#define ROTL32(v, n) \
  (uint32_t((v) << (n)) | ((v) >> (32 - (n))))
#define ROTL64(v, n) \
  (uint64_t((v) << (n)) | ((v) >> (64 - (n))))
uint32_t IntCode_ROTATE_LEFT_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ROTL8(ics.rf[i->src1_reg].i8, ics.rf[i->src2_reg].i8);
  return IA_NEXT;
}
uint32_t IntCode_ROTATE_LEFT_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ROTL16(ics.rf[i->src1_reg].i16, ics.rf[i->src2_reg].i8);
  return IA_NEXT;
}
uint32_t IntCode_ROTATE_LEFT_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ROTL32(ics.rf[i->src1_reg].i32, ics.rf[i->src2_reg].i8);
  return IA_NEXT;
}
uint32_t IntCode_ROTATE_LEFT_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ROTL64(ics.rf[i->src1_reg].i64, ics.rf[i->src2_reg].i8);
  return IA_NEXT;
}
int Translate_ROTATE_LEFT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_ROTATE_LEFT_I8,
    IntCode_ROTATE_LEFT_I16,
    IntCode_ROTATE_LEFT_I32,
    IntCode_ROTATE_LEFT_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_BYTE_SWAP_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = XESWAP16(ics.rf[i->src1_reg].i16);
  return IA_NEXT;
}
uint32_t IntCode_BYTE_SWAP_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = XESWAP32(ics.rf[i->src1_reg].i32);
  return IA_NEXT;
}
uint32_t IntCode_BYTE_SWAP_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = XESWAP64(ics.rf[i->src1_reg].i64);
  return IA_NEXT;
}
uint32_t IntCode_BYTE_SWAP_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  dest.low = XESWAP64(src1.high);
  dest.high = XESWAP64(src1.low);
  return IA_NEXT;
}
int Translate_BYTE_SWAP(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE,
    IntCode_BYTE_SWAP_I16,
    IntCode_BYTE_SWAP_I32,
    IntCode_BYTE_SWAP_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_BYTE_SWAP_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_CNTLZ_I8(IntCodeState& ics, const IntCode* i) {
  // CHECK
  XEASSERTALWAYS();
  DWORD index;
  DWORD mask = ics.rf[i->src1_reg].i16;
  BOOLEAN is_nonzero = _BitScanReverse(&index, mask);
  ics.rf[i->dest_reg].i8 = is_nonzero ? (int8_t)(index - 24) ^ 0x7 : 0x7;
  return IA_NEXT;
}
uint32_t IntCode_CNTLZ_I16(IntCodeState& ics, const IntCode* i) {
  // CHECK
  XEASSERTALWAYS();
  DWORD index;
  DWORD mask = ics.rf[i->src1_reg].i16;
  BOOLEAN is_nonzero = _BitScanReverse(&index, mask);
  ics.rf[i->dest_reg].i8 = is_nonzero ? (int8_t)(index - 16) ^ 0xF : 0xF;
  return IA_NEXT;
}
uint32_t IntCode_CNTLZ_I32(IntCodeState& ics, const IntCode* i) {
  DWORD index;
  DWORD mask = ics.rf[i->src1_reg].i32;
  BOOLEAN is_nonzero = _BitScanReverse(&index, mask);
  ics.rf[i->dest_reg].i8 = is_nonzero ? (int8_t)index ^ 0x1F : 0x1F;
  return IA_NEXT;
}
uint32_t IntCode_CNTLZ_I64(IntCodeState& ics, const IntCode* i) {
  DWORD index;
  DWORD64 mask = ics.rf[i->src1_reg].i64;
  BOOLEAN is_nonzero = _BitScanReverse64(&index, mask);
  ics.rf[i->dest_reg].i8 = is_nonzero ? (int8_t)index ^ 0x3F : 0x3F;
  return IA_NEXT;
}
int Translate_CNTLZ(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_CNTLZ_I8,
    IntCode_CNTLZ_I16,
    IntCode_CNTLZ_I32,
    IntCode_CNTLZ_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_EXTRACT_INT8_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  ics.rf[i->dest_reg].i8 = src1.b16[ics.rf[i->src2_reg].i64];
  return IA_NEXT;
}
uint32_t IntCode_EXTRACT_INT16_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  ics.rf[i->dest_reg].i16 = src1.s8[ics.rf[i->src2_reg].i64];
  return IA_NEXT;
}
uint32_t IntCode_EXTRACT_INT32_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  ics.rf[i->dest_reg].i32 = src1.i4[ics.rf[i->src2_reg].i64];
  return IA_NEXT;
}
int Translate_EXTRACT(TranslationContext& ctx, Instr* i) {
  // Can do more as needed.
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_EXTRACT_INT8_V128, IntCode_EXTRACT_INT16_V128, IntCode_EXTRACT_INT32_V128, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
  };
  IntCodeFn fn = fns[i->src1.value->type * MAX_TYPENAME + i->dest->type];
  return DispatchToC(ctx, i, fn);
}

uint32_t IntCode_SPLAT_V128_INT8(IntCodeState& ics, const IntCode* i) {
  int8_t src1 = ics.rf[i->src1_reg].i8;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 16; i++) {
    dest.b16[i] = src1;
  }
  return IA_NEXT;
}
uint32_t IntCode_SPLAT_V128_INT16(IntCodeState& ics, const IntCode* i) {
  int16_t src1 = ics.rf[i->src1_reg].i16;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 8; i++) {
    dest.s8[i] = src1;
  }
  return IA_NEXT;
}
uint32_t IntCode_SPLAT_V128_INT32(IntCodeState& ics, const IntCode* i) {
  int32_t src1 = ics.rf[i->src1_reg].i32;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 4; i++) {
    dest.i4[i] = src1;
  }
  return IA_NEXT;
}
uint32_t IntCode_SPLAT_V128_FLOAT32(IntCodeState& ics, const IntCode* i) {
  float src1 = ics.rf[i->src1_reg].f32;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 4; i++) {
    dest.f4[i] = src1;
  }
  return IA_NEXT;
}
int Translate_SPLAT(TranslationContext& ctx, Instr* i) {
  // Can do more as needed.
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_SPLAT_V128_INT8,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_SPLAT_V128_INT16,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_SPLAT_V128_INT32,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_SPLAT_V128_FLOAT32,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
  };
  IntCodeFn fn = fns[i->src1.value->type * MAX_TYPENAME + i->dest->type];
  return DispatchToC(ctx, i, fn);
}

uint32_t IntCode_PERMUTE_V128_BY_INT32(IntCodeState& ics, const IntCode* i) {
  uint32_t src1 = ics.rf[i->src1_reg].i32;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  const vec128_t& src3 = ics.rf[i->src3_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 4; i++) {
#define SWAP_INLINE(x) (((x) & ~0x3) + (3 - ((x) % 4)))
    size_t m = SWAP_INLINE(i);
    size_t b = (src1 >> (m * 8)) & 0x3;
    dest.i4[m] = b < 4 ?
        src2.i4[SWAP_INLINE(b)] :
        src3.i4[SWAP_INLINE(b - 4)];
  }
  return IA_NEXT;
}
uint32_t IntCode_PERMUTE_V128_BY_VEC128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  const vec128_t& src3 = ics.rf[i->src3_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 16; i++) {
#define SWAP_INLINE(x) (((x) & ~0x3) + (3 - ((x) % 4)))
    size_t m = SWAP_INLINE(i);
    size_t b = src1.b16[m] & 0x1F;
    dest.b16[m] = b < 16 ?
        src2.b16[SWAP_INLINE(b)] :
        src3.b16[SWAP_INLINE(b - 16)];
  }
  return IA_NEXT;
}
int Translate_PERMUTE(TranslationContext& ctx, Instr* i) {
  // Can do more as needed.
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_PERMUTE_V128_BY_INT32,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_PERMUTE_V128_BY_VEC128,
  };
  IntCodeFn fn = fns[i->src1.value->type * MAX_TYPENAME + i->dest->type];
  return DispatchToC(ctx, i, fn);
}

uint32_t IntCode_SWIZZLE_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  uint32_t swizzle_mask = i->flags;
  dest.i4[0] = src1.i4[swizzle_mask & 0x3];
  dest.i4[1] = src1.i4[(swizzle_mask >> 2) & 0x3];
  dest.i4[2] = src1.i4[(swizzle_mask >> 4) & 0x3];
  dest.i4[3] = src1.i4[(swizzle_mask >> 6) & 0x3];
  return IA_NEXT;
}
int Translate_SWIZZLE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_SWIZZLE_V128,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

typedef int (*TranslateFn)(TranslationContext& ctx, Instr* i);
static const TranslateFn dispatch_table[] = {
  Translate_COMMENT,

  Translate_NOP,

  Translate_DEBUG_BREAK,
  Translate_DEBUG_BREAK_TRUE,

  Translate_TRAP,
  Translate_TRAP_TRUE,

  Translate_CALL,
  Translate_CALL_TRUE,
  Translate_CALL_INDIRECT,
  Translate_CALL_INDIRECT_TRUE,
  Translate_RETURN,

  Translate_BRANCH,
  Translate_BRANCH_IF,
  Translate_BRANCH_TRUE,
  Translate_BRANCH_FALSE,

  Translate_ASSIGN,
  Translate_CAST,
  Translate_ZERO_EXTEND,
  Translate_SIGN_EXTEND,
  Translate_TRUNCATE,
  Translate_CONVERT,
  TranslateInvalid, //Translate_ROUND,
  Translate_VECTOR_CONVERT_I2F,
  TranslateInvalid, //Translate_VECTOR_CONVERT_F2I,

  Translate_LOAD_CONTEXT,
  Translate_STORE_CONTEXT,

  Translate_LOAD,
  TranslateInvalid, //Translate_LOAD_ACQUIRE,
  Translate_STORE,
  TranslateInvalid, //Translate_STORE_RELEASE,
  Translate_PREFETCH,

  TranslateInvalid, //Translate_MAX,
  TranslateInvalid, //Translate_MIN,
  Translate_SELECT,
  Translate_IS_TRUE,
  Translate_IS_FALSE,
  Translate_COMPARE_EQ,
  Translate_COMPARE_NE,
  Translate_COMPARE_SLT,
  Translate_COMPARE_SLE,
  Translate_COMPARE_SGT,
  Translate_COMPARE_SGE,
  Translate_COMPARE_ULT,
  Translate_COMPARE_ULE,
  Translate_COMPARE_UGT,
  Translate_COMPARE_UGE,
  Translate_DID_CARRY,
  TranslateInvalid, //Translate_DID_OVERFLOW,
  Translate_VECTOR_COMPARE_EQ,
  Translate_VECTOR_COMPARE_SGT,
  Translate_VECTOR_COMPARE_SGE,
  Translate_VECTOR_COMPARE_UGT,
  Translate_VECTOR_COMPARE_UGE,

  Translate_ADD,
  Translate_ADD_CARRY,
  Translate_SUB,
  Translate_MUL,
  Translate_DIV,
  TranslateInvalid, //Translate_REM,
  Translate_MULADD,
  Translate_MULSUB,
  Translate_NEG,
  Translate_ABS,
  TranslateInvalid, //Translate_SQRT,
  Translate_RSQRT,
  Translate_DOT_PRODUCT_3,
  Translate_DOT_PRODUCT_4,

  Translate_AND,
  Translate_OR,
  Translate_XOR,
  Translate_NOT,
  Translate_SHL,
  Translate_VECTOR_SHL,
  Translate_SHR,
  Translate_SHA,
  Translate_ROTATE_LEFT,
  Translate_BYTE_SWAP,
  Translate_CNTLZ,
  TranslateInvalid, //Translate_INSERT
  Translate_EXTRACT,
  Translate_SPLAT,
  Translate_PERMUTE,
  Translate_SWIZZLE,

  TranslateInvalid, //Translate_COMPARE_EXCHANGE,
  TranslateInvalid, //Translate_ATOMIC_ADD,
  TranslateInvalid, //Translate_ATOMIC_SUB,
};

int TranslateIntCodes(TranslationContext& ctx, Instr* i) {
  TranslateFn fn = dispatch_table[i->opcode->num];
  return fn(ctx, i);
}


}  // namespace ivm
}  // namespace backend
}  // namespace alloy

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


// TODO(benvanik): reimplement packing functions
#include <DirectXPackedVector.h>


// TODO(benvanik): make a compile time flag?
//#define DYNAMIC_REGISTER_ACCESS_CHECK(address) false
#define DYNAMIC_REGISTER_ACCESS_CHECK(address) ((address & 0xFF000000) == 0x7F000000)


namespace alloy {
namespace backend {
namespace ivm {


#define IPRINT
#define IFLUSH()
#define DPRINT
#define DFLUSH()

//#define IPRINT if (ics.thread_state->thread_id() == 1) printf
//#define IFLUSH() fflush(stdout)
//#define DPRINT if (ics.thread_state->thread_id() == 1) printf
//#define DFLUSH() fflush(stdout)

#if XE_CPU_BIGENDIAN
#define VECB16(v,n) (v.b16[n])
#define VECS8(v,n) (v.s8[n])
#define VECI4(v,n) (v.i4[n])
#define VECF4(v,n) (v.f4[n])
#else
static const uint8_t __vector_b16_table[16] = {
  3,  2,  1,  0,
  7,  6,  5,  4,
  11, 10, 9,  8,
  15, 14, 13, 12,
};
static const uint8_t __vector_s8_table[8] = {
  1, 0,
  3, 2,
  5, 4,
  7, 6,
};
#define VECB16(v,n) (v.b16[__vector_b16_table[(n)]])
#define VECS8(v,n) (v.s8[__vector_s8_table[(n)]])
#define VECI4(v,n) (v.i4[(n)])
#define VECF4(v,n) (v.f4[(n)])
#endif

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
  ic->debug_flags = 0;
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
  ic->debug_flags = 0;
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
  if (value->flags & VALUE_IS_ALLOCATED) {
    return (uint32_t)value->tag;
  } else {
    value->flags |= VALUE_IS_ALLOCATED;
    auto reg = ctx.register_count++;
    value->tag = (void*)reg;
    return (uint32_t)reg;
  }
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
  ic->debug_flags = 0;
  ic->dest_reg = dest_reg;
  ic->src1_reg = src1_reg;
  ic->src2_reg = src2_reg;
  ic->src3_reg = src3_reg;

  return 0;
}

uint32_t IntCode_LOAD_REGISTER_I8(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = (RegisterAccessCallbacks*)
      (i->src2_reg | ((uint64_t)i->src3_reg << 32));
  ics.rf[i->dest_reg].i8 = (int8_t)cbs->read(cbs->context, address);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_REGISTER_I16(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = (RegisterAccessCallbacks*)
      (i->src2_reg | ((uint64_t)i->src3_reg << 32));
  ics.rf[i->dest_reg].i16 = XESWAP16((int16_t)cbs->read(cbs->context, address));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_REGISTER_I32(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = (RegisterAccessCallbacks*)
      (i->src2_reg | ((uint64_t)i->src3_reg << 32));
  ics.rf[i->dest_reg].i32 = XESWAP32((int32_t)cbs->read(cbs->context, address));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_REGISTER_I64(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = (RegisterAccessCallbacks*)
      (i->src2_reg | ((uint64_t)i->src3_reg << 32));
  ics.rf[i->dest_reg].i64 = XESWAP64((int64_t)cbs->read(cbs->context, address));
  return IA_NEXT;
}
int DispatchRegisterRead(
    TranslationContext& ctx, Instr* i, RegisterAccessCallbacks* cbs) {
  static IntCodeFn fns[] = {
    IntCode_LOAD_REGISTER_I8,
    IntCode_LOAD_REGISTER_I16,
    IntCode_LOAD_REGISTER_I32,
    IntCode_LOAD_REGISTER_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  IntCodeFn fn = fns[i->dest->type];
  XEASSERT(fn != IntCode_INVALID_TYPE);
  uint32_t dest_reg = AllocDynamicRegister(ctx, i->dest);
  uint32_t src1_reg = AllocOpRegister(ctx, OPCODE_SIG_TYPE_V, &i->src1);
  ctx.intcode_count++;
  IntCode* ic = ctx.intcode_arena->Alloc<IntCode>();
  ic->intcode_fn = fn;
  ic->flags = i->flags;
  ic->debug_flags = 0;
  ic->dest_reg = dest_reg;
  ic->src1_reg = src1_reg;
  ic->src2_reg = (uint32_t)((uint64_t)cbs);
  ic->src3_reg = (uint32_t)(((uint64_t)cbs) >> 32);
  return 0;
}
uint32_t IntCode_LOAD_REGISTER_I8_DYNAMIC(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = ics.access_callbacks;
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      ics.rf[i->dest_reg].i8 = (int8_t)cbs->read(cbs->context, address);
      return IA_NEXT;
    }
    cbs = cbs->next;
  }
  return IA_NEXT;
}
uint32_t IntCode_LOAD_REGISTER_I16_DYNAMIC(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = ics.access_callbacks;
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      ics.rf[i->dest_reg].i16 = XESWAP16((int16_t)cbs->read(cbs->context, address));
      return IA_NEXT;
    }
    cbs = cbs->next;
  }
  return IA_NEXT;
}
uint32_t IntCode_LOAD_REGISTER_I32_DYNAMIC(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = ics.access_callbacks;
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      ics.rf[i->dest_reg].i32 = XESWAP32((int32_t)cbs->read(cbs->context, address));
      return IA_NEXT;
    }
    cbs = cbs->next;
  }
  return IA_NEXT;
}
uint32_t IntCode_LOAD_REGISTER_I64_DYNAMIC(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = ics.access_callbacks;
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      ics.rf[i->dest_reg].i64 = XESWAP64((int64_t)cbs->read(cbs->context, address));
      return IA_NEXT;
    }
    cbs = cbs->next;
  }
  return IA_NEXT;
}

uint32_t IntCode_STORE_REGISTER_I8(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = (RegisterAccessCallbacks*)
      (i->src3_reg | ((uint64_t)i->dest_reg << 32));
  cbs->write(cbs->context, address, ics.rf[i->src2_reg].i8);
  return IA_NEXT;
}
uint32_t IntCode_STORE_REGISTER_I16(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = (RegisterAccessCallbacks*)
      (i->src3_reg | ((uint64_t)i->dest_reg << 32));
  cbs->write(cbs->context, address, XESWAP16(ics.rf[i->src2_reg].i16));
  return IA_NEXT;
}
uint32_t IntCode_STORE_REGISTER_I32(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = (RegisterAccessCallbacks*)
      (i->src3_reg | ((uint64_t)i->dest_reg << 32));
  cbs->write(cbs->context, address, XESWAP32(ics.rf[i->src2_reg].i32));
  return IA_NEXT;
}
uint32_t IntCode_STORE_REGISTER_I64(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = (RegisterAccessCallbacks*)
      (i->src3_reg | ((uint64_t)i->dest_reg << 32));
  cbs->write(cbs->context, address, XESWAP64(ics.rf[i->src2_reg].i64));
  return IA_NEXT;
}
int DispatchRegisterWrite(
    TranslationContext& ctx, Instr* i, RegisterAccessCallbacks* cbs) {
  static IntCodeFn fns[] = {
    IntCode_STORE_REGISTER_I8,
    IntCode_STORE_REGISTER_I16,
    IntCode_STORE_REGISTER_I32,
    IntCode_STORE_REGISTER_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  IntCodeFn fn = fns[i->src2.value->type];
  XEASSERT(fn != IntCode_INVALID_TYPE);
  uint32_t src1_reg = AllocOpRegister(ctx, OPCODE_SIG_TYPE_V, &i->src1);
  uint32_t src2_reg = AllocOpRegister(ctx, OPCODE_SIG_TYPE_V, &i->src2);
  ctx.intcode_count++;
  IntCode* ic = ctx.intcode_arena->Alloc<IntCode>();
  ic->intcode_fn = fn;
  ic->flags = i->flags;
  ic->debug_flags = 0;
  ic->dest_reg = (uint32_t)(((uint64_t)cbs) >> 32);
  ic->src1_reg = src1_reg;
  ic->src2_reg = src2_reg;
  ic->src3_reg = (uint32_t)((uint64_t)cbs);
  return 0;
}
uint32_t IntCode_STORE_REGISTER_I8_DYNAMIC(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = ics.access_callbacks;
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      cbs->write(cbs->context, address, ics.rf[i->src2_reg].i8);
      return IA_NEXT;
    }
    cbs = cbs->next;
  }
  return IA_NEXT;
}
uint32_t IntCode_STORE_REGISTER_I16_DYNAMIC(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = ics.access_callbacks;
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      cbs->write(cbs->context, address, XESWAP16(ics.rf[i->src2_reg].i16));
      return IA_NEXT;
    }
    cbs = cbs->next;
  }
  return IA_NEXT;
}
uint32_t IntCode_STORE_REGISTER_I32_DYNAMIC(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = ics.access_callbacks;
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      cbs->write(cbs->context, address, XESWAP32(ics.rf[i->src2_reg].i32));
      return IA_NEXT;
    }
    cbs = cbs->next;
  }
  return IA_NEXT;
}
uint32_t IntCode_STORE_REGISTER_I64_DYNAMIC(IntCodeState& ics, const IntCode* i) {
  uint64_t address = ics.rf[i->src1_reg].u32;
  RegisterAccessCallbacks* cbs = ics.access_callbacks;
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      cbs->write(cbs->context, address, XESWAP64(ics.rf[i->src2_reg].i64));
      return IA_NEXT;
    }
    cbs = cbs->next;
  }
  return IA_NEXT;
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
  IPRINT("XE[t] :%d: %s\n", ics.thread_state->thread_id(), value);
  IFLUSH();
  return IA_NEXT;
}
int Translate_COMMENT(TranslationContext& ctx, Instr* i) {
  ctx.intcode_count++;
  IntCode* ic = ctx.intcode_arena->Alloc<IntCode>();
  ic->intcode_fn = IntCode_COMMENT;
  ic->flags = i->flags;
  ic->debug_flags = 0;
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

uint32_t IntCode_SOURCE_OFFSET(IntCodeState& ics, const IntCode* i) {
  return IA_NEXT;
}
int Translate_SOURCE_OFFSET(TranslationContext& ctx, Instr* i) {
  int result = DispatchToC(ctx, i, IntCode_SOURCE_OFFSET);
  if (result) {
    return result;
  }
  auto entry = ctx.source_map_arena->Alloc<SourceMapEntry>();
  entry->intcode_index = ctx.intcode_count - 1;
  entry->source_offset = i->src1.offset;
  ctx.source_map_count++;
  return 0;
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
  // 0x0FE00014 is a 'debug print' where r3 = buffer r4 = length
  // TODO(benvanik): post software interrupt to debugger.
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
  Function* fn = symbol_info->function();
  if (!fn) {
    ics.thread_state->runtime()->ResolveFunction(symbol_info->address(), &fn);
  }
  XEASSERTNOTNULL(fn);
  // TODO(benvanik): proper tail call support, somehow.
  uint64_t return_address =
      (i->flags & CALL_TAIL) ? ics.return_address : ics.call_return_address;
  fn->Call(ics.thread_state, return_address);
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
  uint64_t target = ics.rf[reg].u32;

  // Check if return address - if so, return.
  if (i->flags & CALL_POSSIBLE_RETURN) {
    if (target == ics.return_address) {
      return IA_RETURN;
    }
  }

  // Real call.
  Function* fn = NULL;
  ics.thread_state->runtime()->ResolveFunction(target, &fn);
  XEASSERTNOTNULL(fn);
  // TODO(benvanik): proper tail call support, somehow.
  uint64_t return_address =
      (i->flags & CALL_TAIL) ? ics.return_address : ics.call_return_address;
  fn->Call(ics.thread_state, return_address);
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

uint32_t IntCode_CALL_EXTERN(IntCodeState& ics, const IntCode* i) {
  return IntCode_CALL_XX(ics, i, i->src1_reg);
}
int Translate_CALL_EXTERN(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_CALL_EXTERN);
}

uint32_t IntCode_RETURN(IntCodeState& ics, const IntCode* i) {
  return IA_RETURN;
}
int Translate_RETURN(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_RETURN);
}

uint32_t IntCode_RETURN_TRUE_I8(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u8) {
    return IA_RETURN;
  }
  return IA_NEXT;
}
uint32_t IntCode_RETURN_TRUE_I16(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u16) {
    return IA_RETURN;
  }
  return IA_NEXT;
}
uint32_t IntCode_RETURN_TRUE_I32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u32) {
    return IA_RETURN;
  }
  return IA_NEXT;
}
uint32_t IntCode_RETURN_TRUE_I64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].u64) {
    return IA_RETURN;
  }
  return IA_NEXT;
}
uint32_t IntCode_RETURN_TRUE_F32(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f32) {
    return IA_RETURN;
  }
  return IA_NEXT;
}
uint32_t IntCode_RETURN_TRUE_F64(IntCodeState& ics, const IntCode* i) {
  if (ics.rf[i->src1_reg].f64) {
    return IA_RETURN;
  }
  return IA_NEXT;
}
int Translate_RETURN_TRUE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_RETURN_TRUE_I8,
    IntCode_RETURN_TRUE_I16,
    IntCode_RETURN_TRUE_I32,
    IntCode_RETURN_TRUE_I64,
    IntCode_RETURN_TRUE_F32,
    IntCode_RETURN_TRUE_F64,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src1.value->type]);
}

uint32_t IntCode_SET_RETURN_ADDRESS(IntCodeState& ics, const IntCode* i) {
  ics.call_return_address = ics.rf[i->src1_reg].u32;
  return IA_NEXT;
}
int Translate_SET_RETURN_ADDRESS(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_SET_RETURN_ADDRESS);
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
uint32_t IntCode_CONVERT_F64_TO_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = (int32_t)ics.rf[i->src1_reg].f64;
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
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_CONVERT_F64_TO_I32, IntCode_CONVERT_F64_TO_I64, IntCode_CONVERT_F64_TO_F32, IntCode_ASSIGN_F64, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_ASSIGN_V128,
  };
  IntCodeFn fn = fns[i->src1.value->type * MAX_TYPENAME + i->dest->type];
  return DispatchToC(ctx, i, fn);
}

uint32_t IntCode_ROUND_F32(IntCodeState& ics, const IntCode* i) {
  float src1 = ics.rf[i->src1_reg].f32;
  float dest = src1;
  switch (i->flags) {
  case ROUND_TO_ZERO:
    dest = truncf(src1);
    break;
  case ROUND_TO_NEAREST:
    dest = roundf(src1);
    break;
  case ROUND_TO_MINUS_INFINITY:
    dest = floorf(src1);
    break;
  case ROUND_TO_POSITIVE_INFINITY:
    dest = ceilf(src1);
    break;
  }
  ics.rf[i->dest_reg].f32 = dest;
  return IA_NEXT;
}
uint32_t IntCode_ROUND_F64(IntCodeState& ics, const IntCode* i) {
  double src1 = ics.rf[i->src1_reg].f64;
  double dest = src1;
  switch (i->flags) {
  case ROUND_TO_ZERO:
    dest = trunc(src1);
    break;
  case ROUND_TO_NEAREST:
    dest = round(src1);
    break;
  case ROUND_TO_MINUS_INFINITY:
    dest = floor(src1);
    break;
  case ROUND_TO_POSITIVE_INFINITY:
    dest = ceil(src1);
    break;
  }
  ics.rf[i->dest_reg].f64 = dest;
  return IA_NEXT;
}
uint32_t IntCode_ROUND_V128_ZERO(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t n = 0; n < 4; n++) {
    dest.f4[n] = truncf(src1.f4[n]);
  }
  return IA_NEXT;
}
uint32_t IntCode_ROUND_V128_NEAREST(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t n = 0; n < 4; n++) {
    dest.f4[n] = roundf(src1.f4[n]);
  }
  return IA_NEXT;
}
uint32_t IntCode_ROUND_V128_MINUS_INFINITY(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t n = 0; n < 4; n++) {
    dest.f4[n] = floorf(src1.f4[n]);
  }
  return IA_NEXT;
}
uint32_t IntCode_ROUND_V128_POSITIVE_INFINTIY(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t n = 0; n < 4; n++) {
    dest.f4[n] = ceilf(src1.f4[n]);
  }
  return IA_NEXT;
}
int Translate_ROUND(TranslationContext& ctx, Instr* i) {
  if (i->dest->type == VEC128_TYPE) {
    static IntCodeFn fns[] = {
      IntCode_ROUND_V128_ZERO,
      IntCode_ROUND_V128_NEAREST,
      IntCode_ROUND_V128_MINUS_INFINITY,
      IntCode_ROUND_V128_POSITIVE_INFINTIY,
    };
    return DispatchToC(ctx, i, fns[i->flags]);
  } else {
    static IntCodeFn fns[] = {
      IntCode_INVALID_TYPE,
      IntCode_INVALID_TYPE,
      IntCode_INVALID_TYPE,
      IntCode_INVALID_TYPE,
      IntCode_ROUND_F32,
      IntCode_ROUND_F64,
      IntCode_INVALID_TYPE,
    };
    return DispatchToC(ctx, i, fns[i->dest->type]);
  }
}

uint32_t IntCode_VECTOR_CONVERT_I2F_S(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  VECF4(dest,0) = (float)(int32_t)VECI4(src1,0);
  VECF4(dest,1) = (float)(int32_t)VECI4(src1,1);
  VECF4(dest,2) = (float)(int32_t)VECI4(src1,2);
  VECF4(dest,3) = (float)(int32_t)VECI4(src1,3);
  return IA_NEXT;
}
uint32_t IntCode_VECTOR_CONVERT_I2F_U(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  VECF4(dest,0) = (float)(uint32_t)VECI4(src1,0);
  VECF4(dest,1) = (float)(uint32_t)VECI4(src1,1);
  VECF4(dest,2) = (float)(uint32_t)VECI4(src1,2);
  VECF4(dest,3) = (float)(uint32_t)VECI4(src1,3);
  return IA_NEXT;
}
int Translate_VECTOR_CONVERT_I2F(TranslationContext& ctx, Instr* i) {
  if (i->flags & ARITHMETIC_UNSIGNED) {
    return DispatchToC(ctx, i, IntCode_VECTOR_CONVERT_I2F_U);
  } else {
    return DispatchToC(ctx, i, IntCode_VECTOR_CONVERT_I2F_S);
  }
}

uint32_t IntCode_VECTOR_CONVERT_F2I(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  if (i->flags & ARITHMETIC_UNSIGNED) {
    VECI4(dest,0) = (uint32_t)VECF4(src1,0);
    VECI4(dest,1) = (uint32_t)VECF4(src1,1);
    VECI4(dest,2) = (uint32_t)VECF4(src1,2);
    VECI4(dest,3) = (uint32_t)VECF4(src1,3);
  } else {
    VECI4(dest,0) = (int32_t)VECF4(src1,0);
    VECI4(dest,1) = (int32_t)VECF4(src1,1);
    VECI4(dest,2) = (int32_t)VECF4(src1,2);
    VECI4(dest,3) = (int32_t)VECF4(src1,3);
  }
  return IA_NEXT;
}
uint32_t IntCode_VECTOR_CONVERT_F2I_SAT(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  if (i->flags & ARITHMETIC_UNSIGNED) {
    for (int n = 0; n < 4; n++) {
      float src = src1.f4[n];
      if (src < 0) {
        VECI4(dest,n) = 0;
        ics.did_saturate = 1;
      } else if (src > UINT_MAX) {
        VECI4(dest,n) = UINT_MAX;
        ics.did_saturate = 1;
      } else {
        VECI4(dest,n) = (uint32_t)src;
      }
    }
  } else {
    for (int n = 0; n < 4; n++) {
      float src = src1.f4[n];
      if (src < INT_MIN) {
        VECI4(dest,n) = INT_MIN;
        ics.did_saturate = 1;
      } else if (src > INT_MAX) {
        VECI4(dest,n) = INT_MAX;
        ics.did_saturate = 1;
      } else {
        VECI4(dest,n) = (int32_t)src;
      }
    }
  }
  return IA_NEXT;
}
int Translate_VECTOR_CONVERT_F2I(TranslationContext& ctx, Instr* i) {
  if (i->flags & ARITHMETIC_SATURATE) {
    return DispatchToC(ctx, i, IntCode_VECTOR_CONVERT_F2I_SAT);
  } else {
    return DispatchToC(ctx, i, IntCode_VECTOR_CONVERT_F2I);
  }
}

static uint8_t __lvsl_table[17][16] = {
  { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15},
  { 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16},
  { 2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17},
  { 3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18},
  { 4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19},
  { 5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
  { 6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21},
  { 7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22},
  { 8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
  { 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24},
  {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25},
  {11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26},
  {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
  {13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28},
  {14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29},
  {15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30},
  {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31},
};
static uint8_t __lvsr_table[17][16] = {
  {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31},
  {15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30},
  {14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29},
  {13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28},
  {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
  {11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26},
  {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25},
  { 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24},
  { 8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
  { 7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22},
  { 6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21},
  { 5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
  { 4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19},
  { 3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18},
  { 2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17},
  { 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16},
  { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15},
};

uint32_t IntCode_LOAD_VECTOR_SHL(IntCodeState& ics, const IntCode* i) {
  int8_t sh = MIN(16, ics.rf[i->src1_reg].i8);
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 16; n++) {
    VECB16(dest,n) = __lvsl_table[sh][n];
  }
  return IA_NEXT;
}
int Translate_LOAD_VECTOR_SHL(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_LOAD_VECTOR_SHL);
}

uint32_t IntCode_LOAD_VECTOR_SHR(IntCodeState& ics, const IntCode* i) {
  int8_t sh = MIN(16, ics.rf[i->src1_reg].i8);
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 16; n++) {
    VECB16(dest,n) = __lvsr_table[sh][n];
  }
  return IA_NEXT;
}
int Translate_LOAD_VECTOR_SHR(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_LOAD_VECTOR_SHR);
}

uint32_t IntCode_LOAD_CLOCK(IntCodeState& ics, const IntCode* i) {
  LARGE_INTEGER counter;
  uint64_t time = 0;
  if (QueryPerformanceCounter(&counter)) {
    time = counter.QuadPart;
  }
  ics.rf[i->dest_reg].i64 = time;
  return IA_NEXT;
}
int Translate_LOAD_CLOCK(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_LOAD_CLOCK);
}

uint32_t IntCode_LOAD_LOCAL_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = *((int8_t*)(ics.locals + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_LOCAL_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = *((int16_t*)(ics.locals + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_LOCAL_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = *((int32_t*)(ics.locals + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_LOCAL_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = *((int64_t*)(ics.locals + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_LOCAL_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = *((float*)(ics.locals + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_LOCAL_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = *((double*)(ics.locals + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_LOCAL_V128(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].v128 = *((vec128_t*)(ics.locals + ics.rf[i->src1_reg].u32));
  return IA_NEXT;
}
int Translate_LOAD_LOCAL(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_LOAD_LOCAL_I8,
    IntCode_LOAD_LOCAL_I16,
    IntCode_LOAD_LOCAL_I32,
    IntCode_LOAD_LOCAL_I64,
    IntCode_LOAD_LOCAL_F32,
    IntCode_LOAD_LOCAL_F64,
    IntCode_LOAD_LOCAL_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_STORE_LOCAL_I8(IntCodeState& ics, const IntCode* i) {
  *((int8_t*)(ics.locals + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_STORE_LOCAL_I16(IntCodeState& ics, const IntCode* i) {
  *((int16_t*)(ics.locals + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_STORE_LOCAL_I32(IntCodeState& ics, const IntCode* i) {
  *((int32_t*)(ics.locals + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_STORE_LOCAL_I64(IntCodeState& ics, const IntCode* i) {
  *((int64_t*)(ics.locals + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_STORE_LOCAL_F32(IntCodeState& ics, const IntCode* i) {
  *((float*)(ics.locals + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_STORE_LOCAL_F64(IntCodeState& ics, const IntCode* i) {
  *((double*)(ics.locals + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_STORE_LOCAL_V128(IntCodeState& ics, const IntCode* i) {
  *((vec128_t*)(ics.locals + ics.rf[i->src1_reg].u32)) = ics.rf[i->src2_reg].v128;
  return IA_NEXT;
}
int Translate_STORE_LOCAL(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_STORE_LOCAL_I8,
    IntCode_STORE_LOCAL_I16,
    IntCode_STORE_LOCAL_I32,
    IntCode_STORE_LOCAL_I64,
    IntCode_STORE_LOCAL_F32,
    IntCode_STORE_LOCAL_F64,
    IntCode_STORE_LOCAL_V128,
  };
  return DispatchToC(ctx, i, fns[i->src2.value->type]);
}

uint32_t IntCode_LOAD_CONTEXT_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = *((int8_t*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("%d (%X) = ctx i8 +%d\n", ics.rf[i->dest_reg].i8, ics.rf[i->dest_reg].u8, ics.rf[i->src1_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = *((int16_t*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("%d (%X) = ctx i16 +%d\n", ics.rf[i->dest_reg].i16, ics.rf[i->dest_reg].u16, ics.rf[i->src1_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = *((int32_t*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("%d (%X) = ctx i32 +%d\n", ics.rf[i->dest_reg].i32, ics.rf[i->dest_reg].u32, ics.rf[i->src1_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = *((int64_t*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("%lld (%llX) = ctx i64 +%d\n", ics.rf[i->dest_reg].i64, ics.rf[i->dest_reg].u64, ics.rf[i->src1_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = *((float*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("%e (%X) = ctx f32 +%d\n", ics.rf[i->dest_reg].f32, ics.rf[i->dest_reg].u32, ics.rf[i->src1_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = *((double*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("%lle (%llX) = ctx f64 +%d\n", ics.rf[i->dest_reg].f64, ics.rf[i->dest_reg].u64, ics.rf[i->src1_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_LOAD_CONTEXT_V128(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].v128 = *((vec128_t*)(ics.context + ics.rf[i->src1_reg].u64));
  DPRINT("[%e, %e, %e, %e] [%.8X, %.8X, %.8X, %.8X] = ctx v128 +%d\n",
         VECF4(ics.rf[i->dest_reg].v128,0), VECF4(ics.rf[i->dest_reg].v128,1), VECF4(ics.rf[i->dest_reg].v128,2), VECF4(ics.rf[i->dest_reg].v128,3),
         VECI4(ics.rf[i->dest_reg].v128,0), VECI4(ics.rf[i->dest_reg].v128,1), VECI4(ics.rf[i->dest_reg].v128,2), VECI4(ics.rf[i->dest_reg].v128,3),
         ics.rf[i->src1_reg].u64);
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
  DPRINT("ctx i8 +%d = %d (%X)\n", ics.rf[i->src1_reg].u64, ics.rf[i->src2_reg].i8, ics.rf[i->src2_reg].u8);
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_I16(IntCodeState& ics, const IntCode* i) {
  *((int16_t*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].i16;
  DPRINT("ctx i16 +%d = %d (%X)\n", ics.rf[i->src1_reg].u64, ics.rf[i->src2_reg].i16, ics.rf[i->src2_reg].u16);
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_I32(IntCodeState& ics, const IntCode* i) {
  *((int32_t*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].i32;
  DPRINT("ctx i32 +%d = %d (%X)\n", ics.rf[i->src1_reg].u64, ics.rf[i->src2_reg].i32, ics.rf[i->src2_reg].u32);
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_I64(IntCodeState& ics, const IntCode* i) {
  *((int64_t*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].i64;
  DPRINT("ctx i64 +%d = %lld (%llX)\n", ics.rf[i->src1_reg].u64, ics.rf[i->src2_reg].i64, ics.rf[i->src2_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_F32(IntCodeState& ics, const IntCode* i) {
  *((float*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].f32;
  DPRINT("ctx f32 +%d = %e (%X)\n", ics.rf[i->src1_reg].u64, ics.rf[i->src2_reg].f32, ics.rf[i->src2_reg].u32);
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_F64(IntCodeState& ics, const IntCode* i) {
  *((double*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].f64;
  DPRINT("ctx f64 +%d = %lle (%llX)\n", ics.rf[i->src1_reg].u64, ics.rf[i->src2_reg].f64, ics.rf[i->src2_reg].u64);
  return IA_NEXT;
}
uint32_t IntCode_STORE_CONTEXT_V128(IntCodeState& ics, const IntCode* i) {
  *((vec128_t*)(ics.context + ics.rf[i->src1_reg].u64)) = ics.rf[i->src2_reg].v128;
  DPRINT("ctx v128 +%d = [%e, %e, %e, %e] [%.8X, %.8X, %.8X, %.8X]\n", ics.rf[i->src1_reg].u64,
         VECF4(ics.rf[i->src2_reg].v128,0), VECF4(ics.rf[i->src2_reg].v128,1), VECF4(ics.rf[i->src2_reg].v128,2), VECF4(ics.rf[i->src2_reg].v128,3),
         VECI4(ics.rf[i->src2_reg].v128,0), VECI4(ics.rf[i->src2_reg].v128,1), VECI4(ics.rf[i->src2_reg].v128,2), VECI4(ics.rf[i->src2_reg].v128,3));
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
  uint32_t address = ics.rf[i->src1_reg].u32;
  if (DYNAMIC_REGISTER_ACCESS_CHECK(address)) {
    return IntCode_LOAD_REGISTER_I8_DYNAMIC(ics, i);
  }
  DPRINT("%d (%X) = load.i8 %.8X\n",
         *((int8_t*)(ics.membase + address)),
         *((uint8_t*)(ics.membase + address)),
         address);
  DFLUSH();
  ics.rf[i->dest_reg].i8 = *((int8_t*)(ics.membase + address));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_I16(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  if (DYNAMIC_REGISTER_ACCESS_CHECK(address)) {
    return IntCode_LOAD_REGISTER_I16_DYNAMIC(ics, i);
  }
  DPRINT("%d (%X) = load.i16 %.8X\n",
         *((int16_t*)(ics.membase + address)),
         *((uint16_t*)(ics.membase + address)),
         address);
  DFLUSH();
  ics.rf[i->dest_reg].i16 = *((int16_t*)(ics.membase + address));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_I32(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  if (DYNAMIC_REGISTER_ACCESS_CHECK(address)) {
    return IntCode_LOAD_REGISTER_I32_DYNAMIC(ics, i);
  }
  DFLUSH();
  DPRINT("%d (%X) = load.i32 %.8X\n",
         *((int32_t*)(ics.membase + address)),
         *((uint32_t*)(ics.membase + address)),
         address);
  DFLUSH();
  ics.rf[i->dest_reg].i32 = *((int32_t*)(ics.membase + address));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_I64(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  if (DYNAMIC_REGISTER_ACCESS_CHECK(address)) {
    return IntCode_LOAD_REGISTER_I64(ics, i);
  }
  DPRINT("%lld (%llX) = load.i64 %.8X\n",
         *((int64_t*)(ics.membase + address)),
         *((uint64_t*)(ics.membase + address)),
         address);
  DFLUSH();
  ics.rf[i->dest_reg].i64 = *((int64_t*)(ics.membase + address));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_F32(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  DPRINT("%e (%X) = load.f32 %.8X\n",
         *((float*)(ics.membase + address)),
         *((uint64_t*)(ics.membase + address)),
         address);
  DFLUSH();
  ics.rf[i->dest_reg].f32 = *((float*)(ics.membase + address));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_F64(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  DPRINT("%lle (%llX) = load.f64 %.8X\n",
         *((double*)(ics.membase + address)),
         *((uint64_t*)(ics.membase + address)),
         address);
  DFLUSH();
  ics.rf[i->dest_reg].f64 = *((double*)(ics.membase + address));
  return IA_NEXT;
}
uint32_t IntCode_LOAD_V128(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    VECI4(dest,n) = *((uint32_t*)(ics.membase + address + n * 4));
  }
  DPRINT("[%e, %e, %e, %e] [%.8X, %.8X, %.8X, %.8X] = load.v128 %.8X\n",
         VECF4(dest,0), VECF4(dest,1), VECF4(dest,2), VECF4(dest,3),
         VECI4(dest,0), VECI4(dest,1), VECI4(dest,2), VECI4(dest,3),
         address);
  DFLUSH();
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
  if (i->src1.value->IsConstant()) {
    // Constant address - check register access callbacks.
    // NOTE: we still will likely want to check on access in debug mode, as
    //       constant propagation may not have happened.
    uint64_t address = i->src1.value->AsUint64();
    RegisterAccessCallbacks* cbs = ctx.access_callbacks;
    while (cbs) {
      if (cbs->handles(cbs->context, address)) {
        return DispatchRegisterRead(ctx, i, cbs);
      }
      cbs = cbs->next;
    }
  }
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_STORE_I8(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  if (DYNAMIC_REGISTER_ACCESS_CHECK(address)) {
    return IntCode_STORE_REGISTER_I8_DYNAMIC(ics, i);
  }
  DPRINT("store.i8 %.8X = %d (%X)\n",
         address, ics.rf[i->src2_reg].i8, ics.rf[i->src2_reg].u8);
  DFLUSH();
  *((int8_t*)(ics.membase + address)) = ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_STORE_I16(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  if (DYNAMIC_REGISTER_ACCESS_CHECK(address)) {
    return IntCode_STORE_REGISTER_I16_DYNAMIC(ics, i);
  }
  DPRINT("store.i16 %.8X = %d (%X)\n",
         address, ics.rf[i->src2_reg].i16, ics.rf[i->src2_reg].u16);
  DFLUSH();
  *((int16_t*)(ics.membase + address)) = ics.rf[i->src2_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_STORE_I32(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  if (DYNAMIC_REGISTER_ACCESS_CHECK(address)) {
    return IntCode_STORE_REGISTER_I32_DYNAMIC(ics, i);
  }
  DPRINT("store.i32 %.8X = %d (%X)\n",
         address, ics.rf[i->src2_reg].i32, ics.rf[i->src2_reg].u32);
  DFLUSH();
  *((int32_t*)(ics.membase + address)) = ics.rf[i->src2_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_STORE_I64(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  if (DYNAMIC_REGISTER_ACCESS_CHECK(address)) {
    return IntCode_STORE_REGISTER_I64_DYNAMIC(ics, i);
  }
  DPRINT("store.i64 %.8X = %lld (%llX)\n",
         address, ics.rf[i->src2_reg].i64, ics.rf[i->src2_reg].u64);
  DFLUSH();
  *((int64_t*)(ics.membase + address)) = ics.rf[i->src2_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_STORE_F32(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  DPRINT("store.f32 %.8X = %e (%X)\n",
         address, ics.rf[i->src2_reg].f32, ics.rf[i->src2_reg].u32);
  DFLUSH();
  *((float*)(ics.membase + address)) = ics.rf[i->src2_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_STORE_F64(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  DPRINT("store.f64 %.8X = %lle (%llX)\n",
         address, ics.rf[i->src2_reg].f64, ics.rf[i->src2_reg].u64);
  DFLUSH();
  *((double*)(ics.membase + address)) = ics.rf[i->src2_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_STORE_V128(IntCodeState& ics, const IntCode* i) {
  uint32_t address = ics.rf[i->src1_reg].u32;
  DPRINT("store.v128 %.8X = [%e, %e, %e, %e] [%.8X, %.8X, %.8X, %.8X]\n",
         address,
         VECF4(ics.rf[i->src2_reg].v128,0), VECF4(ics.rf[i->src2_reg].v128,1), VECF4(ics.rf[i->src2_reg].v128,2), VECF4(ics.rf[i->src2_reg].v128,3),
         VECI4(ics.rf[i->src2_reg].v128,0), VECI4(ics.rf[i->src2_reg].v128,1), VECI4(ics.rf[i->src2_reg].v128,2), VECI4(ics.rf[i->src2_reg].v128,3));
  DFLUSH();
  *((vec128_t*)(ics.membase + address)) = ics.rf[i->src2_reg].v128;
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
  if (i->src1.value->IsConstant()) {
    // Constant address - check register access callbacks.
    // NOTE: we still will likely want to check on access in debug mode, as
    //       constant propagation may not have happened.
    uint64_t address = i->src1.value->AsUint64();
    RegisterAccessCallbacks* cbs = ctx.access_callbacks;
    while (cbs) {
      if (cbs->handles(cbs->context, address)) {
        return DispatchRegisterWrite(ctx, i, cbs);
      }
      cbs = cbs->next;
    }
  }
  return DispatchToC(ctx, i, fns[i->src2.value->type]);
}

uint32_t IntCode_PREFETCH(IntCodeState& ics, const IntCode* i) {
  return IA_NEXT;
}
int Translate_PREFETCH(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_PREFETCH);
}

uint32_t IntCode_MAX_I8_I8(IntCodeState& ics, const IntCode* i) {
  int8_t a = ics.rf[i->src1_reg].i8; int8_t b = ics.rf[i->src2_reg].i8;
  ics.rf[i->dest_reg].i8 = MAX(a, b);
  return IA_NEXT;
}
uint32_t IntCode_MAX_I16_I16(IntCodeState& ics, const IntCode* i) {
  int16_t a = ics.rf[i->src1_reg].i16; int16_t b = ics.rf[i->src2_reg].i16;
  ics.rf[i->dest_reg].i16 = MAX(a, b);
  return IA_NEXT;
}
uint32_t IntCode_MAX_I32_I32(IntCodeState& ics, const IntCode* i) {
  int32_t a = ics.rf[i->src1_reg].i32; int32_t b = ics.rf[i->src2_reg].i32;
  ics.rf[i->dest_reg].i32 = MAX(a, b);
  return IA_NEXT;
}
uint32_t IntCode_MAX_I64_I64(IntCodeState& ics, const IntCode* i) {
  int64_t a = ics.rf[i->src1_reg].i64; int64_t b = ics.rf[i->src2_reg].i64;
  ics.rf[i->dest_reg].i64 = MAX(a, b);
  return IA_NEXT;
}
uint32_t IntCode_MAX_F32_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 =
      MAX(ics.rf[i->src1_reg].f32, ics.rf[i->src2_reg].f32);
  return IA_NEXT;
}
uint32_t IntCode_MAX_F64_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 =
      MAX(ics.rf[i->src1_reg].f64, ics.rf[i->src2_reg].f64);
  return IA_NEXT;
}
uint32_t IntCode_MAX_V128_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = MAX(src1.f4[n], src2.f4[n]);
  }
  return IA_NEXT;
}
int Translate_MAX(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_MAX_I8_I8,
    IntCode_MAX_I16_I16,
    IntCode_MAX_I32_I32,
    IntCode_MAX_I64_I64,
    IntCode_MAX_F32_F32,
    IntCode_MAX_F64_F64,
    IntCode_MAX_V128_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_MIN_I8_I8(IntCodeState& ics, const IntCode* i) {
  int8_t a = ics.rf[i->src1_reg].i8; int8_t b = ics.rf[i->src2_reg].i8;
  ics.rf[i->dest_reg].i8 = MIN(a, b);
  return IA_NEXT;
}
uint32_t IntCode_MIN_I16_I16(IntCodeState& ics, const IntCode* i) {
  int16_t a = ics.rf[i->src1_reg].i16; int16_t b = ics.rf[i->src2_reg].i16;
  ics.rf[i->dest_reg].i16 = MIN(a, b);
  return IA_NEXT;
}
uint32_t IntCode_MIN_I32_I32(IntCodeState& ics, const IntCode* i) {
  int32_t a = ics.rf[i->src1_reg].i32; int32_t b = ics.rf[i->src2_reg].i32;
  ics.rf[i->dest_reg].i32 = MIN(a, b);
  return IA_NEXT;
}
uint32_t IntCode_MIN_I64_I64(IntCodeState& ics, const IntCode* i) {
  int64_t a = ics.rf[i->src1_reg].i64; int64_t b = ics.rf[i->src2_reg].i64;
  ics.rf[i->dest_reg].i64 = MIN(a, b);
  return IA_NEXT;
}
uint32_t IntCode_MIN_F32_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 =
      MIN(ics.rf[i->src1_reg].f32, ics.rf[i->src2_reg].f32);
  return IA_NEXT;
}
uint32_t IntCode_MIN_F64_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 =
      MIN(ics.rf[i->src1_reg].f64, ics.rf[i->src2_reg].f64);
  return IA_NEXT;
}
uint32_t IntCode_MIN_V128_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = MIN(src1.f4[n], src2.f4[n]);
  }
  return IA_NEXT;
}
int Translate_MIN(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_MIN_I8_I8,
    IntCode_MIN_I16_I16,
    IntCode_MIN_I32_I32,
    IntCode_MIN_I64_I64,
    IntCode_MIN_F32_F32,
    IntCode_MIN_F64_F64,
    IntCode_MIN_V128_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
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
  return DispatchToC(ctx, i, fns[i->dest->type]);
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

uint32_t IntCode_DID_SATURATE(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.did_saturate;
  return IA_NEXT;
}
int Translate_DID_SATURATE(TranslationContext& ctx, Instr* i) {
  return DispatchToC(ctx, i, IntCode_DID_SATURATE);
}

#define VECTOR_COMPARER(type, value, dest_value, count, op) \
  const vec128_t& src1 = ics.rf[i->src1_reg].v128; \
  const vec128_t& src2 = ics.rf[i->src2_reg].v128; \
  vec128_t& dest = ics.rf[i->dest_reg].v128; \
  for (int n = 0; n < count; n++) { \
    dest.dest_value[n] = ((type)src1.value[n] op (type)src2.value[n]) ? 0xFFFFFFFF : 0; \
  } \
  return IA_NEXT;

uint32_t IntCode_VECTOR_COMPARE_EQ_I8(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint8_t, b16, b16, 16, ==) };
uint32_t IntCode_VECTOR_COMPARE_EQ_I16(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint16_t, s8, s8, 8, ==) };
uint32_t IntCode_VECTOR_COMPARE_EQ_I32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint32_t, i4, i4, 4, ==) };
uint32_t IntCode_VECTOR_COMPARE_EQ_F32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(float, f4, i4, 4, ==) };
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

uint32_t IntCode_VECTOR_COMPARE_SGT_I8(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int8_t, b16, b16, 16, >) };
uint32_t IntCode_VECTOR_COMPARE_SGT_I16(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int16_t, s8, s8, 8, >) };
uint32_t IntCode_VECTOR_COMPARE_SGT_I32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int32_t, i4, i4, 4, >) };
uint32_t IntCode_VECTOR_COMPARE_SGT_F32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(float, f4, i4, 4, >) };
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

uint32_t IntCode_VECTOR_COMPARE_SGE_I8(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int8_t, b16, b16, 16, >=) };
uint32_t IntCode_VECTOR_COMPARE_SGE_I16(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int16_t, s8, s8, 8, >=) };
uint32_t IntCode_VECTOR_COMPARE_SGE_I32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(int32_t, i4, i4, 4, >=) };
uint32_t IntCode_VECTOR_COMPARE_SGE_F32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(float, f4, i4, 4, >=) };
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

uint32_t IntCode_VECTOR_COMPARE_UGT_I8(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint8_t, b16, b16, 16, >) };
uint32_t IntCode_VECTOR_COMPARE_UGT_I16(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint16_t, s8, s8, 8, >) };
uint32_t IntCode_VECTOR_COMPARE_UGT_I32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint32_t, i4, i4, 4, >) };
uint32_t IntCode_VECTOR_COMPARE_UGT_F32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(float, f4, i4, 4, >) };
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

uint32_t IntCode_VECTOR_COMPARE_UGE_I8(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint8_t, b16, b16, 16, >=) };
uint32_t IntCode_VECTOR_COMPARE_UGE_I16(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint16_t, s8, s8, 8, >=) };
uint32_t IntCode_VECTOR_COMPARE_UGE_I32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(uint32_t, i4, i4, 4, >=) };
uint32_t IntCode_VECTOR_COMPARE_UGE_F32(IntCodeState& ics, const IntCode* i) { VECTOR_COMPARER(float, f4, i4, 4, >=) };
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

#define CHECK_DID_CARRY(v1, v2) (((uint64_t)v2) > ~((uint64_t)v1))
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
  XEASSERT(!i->flags);
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

uint32_t Translate_VECTOR_ADD_I8(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  const uint32_t arithmetic_flags = i->flags >> 8;
  if (arithmetic_flags & ARITHMETIC_SATURATE) {
    if (arithmetic_flags & ARITHMETIC_UNSIGNED) {
      for (int n = 0; n < 16; n++) {
        uint16_t v = VECB16(src1,n) + VECB16(src2,n);
        if (v > 0xFF) {
          VECB16(dest,n) = 0xFF;
          ics.did_saturate = 1;
        } else {
          VECB16(dest,n) = (uint8_t)v;
        }
      }
    } else {
      for (int n = 0; n < 16; n++) {
        int16_t v = (int8_t)VECB16(src1,n) + (int8_t)VECB16(src2,n);
        if (v > 0x7F) {
          VECB16(dest,n) = 0x7F;
          ics.did_saturate = 1;
        } else if (v < -0x80) {
          VECB16(dest,n) = -0x80;
          ics.did_saturate = 1;
        } else {
          VECB16(dest,n) = (uint8_t)v;
        }
      }
    }
  } else {
    for (int n = 0; n < 16; n++) {
      VECB16(dest,n) = VECB16(src1,n) + VECB16(src2,n);
    }
  }
  return IA_NEXT;
}
uint32_t Translate_VECTOR_ADD_I16(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  const uint32_t arithmetic_flags = i->flags >> 8;
  if (arithmetic_flags & ARITHMETIC_SATURATE) {
    if (arithmetic_flags & ARITHMETIC_UNSIGNED) {
      for (int n = 0; n < 8; n++) {
        uint32_t v = VECS8(src1,n) + VECS8(src2,n);
        if (v > 0xFFFF) {
          VECS8(dest,n) = 0xFFFF;
          ics.did_saturate = 1;
        } else {
          VECS8(dest,n) = (uint16_t)v;
        }
      }
    } else {
      for (int n = 0; n < 8; n++) {
        int32_t v = (int16_t)VECS8(src1,n) + (int16_t)VECS8(src2,n);
        if (v > 0x7FFF) {
          VECS8(dest,n) = 0x7FFF;
          ics.did_saturate = 1;
        } else if (v < -0x8000) {
          VECS8(dest,n) = -0x8000;
          ics.did_saturate = 1;
        } else {
          VECS8(dest,n) = (uint16_t)v;
        }
      }
    }
  } else {
    for (int n = 0; n < 8; n++) {
      VECS8(dest,n) = VECS8(src1,n) + VECS8(src2,n);
    }
  }
  return IA_NEXT;
}
uint32_t Translate_VECTOR_ADD_I32(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  const uint32_t arithmetic_flags = i->flags >> 8;
  if (arithmetic_flags & ARITHMETIC_SATURATE) {
    if (arithmetic_flags & ARITHMETIC_UNSIGNED) {
      for (int n = 0; n < 4; n++) {
        uint64_t v = VECI4(src1,n) + VECI4(src2,n);
        if (v > 0xFFFFFFFF) {
          VECI4(dest,n) = 0xFFFFFFFF;
          ics.did_saturate = 1;
        } else {
          VECI4(dest,n) = (uint32_t)v;
        }
      }
    } else {
      for (int n = 0; n < 4; n++) {
        int64_t v = (int32_t)VECI4(src1,n) + (int32_t)VECI4(src2,n);
        if (v > 0x7FFFFFFF) {
          VECI4(dest,n) = 0x7FFFFFFF;
          ics.did_saturate = 1;
        } else if (v < -0x80000000ll) {
          VECI4(dest,n) = 0x80000000;
          ics.did_saturate = 1;
        } else {
          VECI4(dest,n) = (uint32_t)v;
        }
      }
    }
  } else {
    for (int n = 0; n < 4; n++) {
      VECI4(dest,n) = VECI4(src1,n) + VECI4(src2,n);
    }
  }
  return IA_NEXT;
}
uint32_t Translate_VECTOR_ADD_F32(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = src1.f4[n] + src2.f4[n];
  }
  return IA_NEXT;
}
int Translate_VECTOR_ADD(TranslationContext& ctx, Instr* i) {
  TypeName part_type = (TypeName)(i->flags & 0xFF);
  static IntCodeFn fns[] = {
    Translate_VECTOR_ADD_I8,
    Translate_VECTOR_ADD_I16,
    Translate_VECTOR_ADD_I32,
    IntCode_INVALID_TYPE,
    Translate_VECTOR_ADD_F32,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[part_type]);
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
    ics.did_carry = SUB_DID_CARRY(a, b);
  }
  ics.rf[i->dest_reg].i32 = a - b;
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
uint32_t IntCode_MUL_I8_I8_U(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].u8 = ics.rf[i->src1_reg].u8 * ics.rf[i->src2_reg].u8;
  return IA_NEXT;
}
uint32_t IntCode_MUL_I16_I16_U(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].u16 = ics.rf[i->src1_reg].u16 * ics.rf[i->src2_reg].u16;
  return IA_NEXT;
}
uint32_t IntCode_MUL_I32_I32_U(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].u32 = ics.rf[i->src1_reg].u32 * ics.rf[i->src2_reg].u32;
  return IA_NEXT;
}
uint32_t IntCode_MUL_I64_I64_U(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].u64 = ics.rf[i->src1_reg].u64 * ics.rf[i->src2_reg].u64;
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
  static IntCodeFn fns_unsigned[] = {
    IntCode_MUL_I8_I8_U,
    IntCode_MUL_I16_I16_U,
    IntCode_MUL_I32_I32_U,
    IntCode_MUL_I64_I64_U,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  if (i->flags & ARITHMETIC_UNSIGNED) {
    return DispatchToC(ctx, i, fns_unsigned[i->dest->type]);
  } else {
    return DispatchToC(ctx, i, fns[i->dest->type]);
  }
}

namespace {
uint64_t Mul128(uint64_t xi_low, uint64_t xi_high,
                uint64_t yi_low, uint64_t yi_high) {
  // 128bit multiply, simplified for two input 64bit integers.
  // http://mrob.com/pub/math/int128.c.txt
#define HI_WORD 0xFFFFFFFF00000000LL
#define LO_WORD 0x00000000FFFFFFFFLL
  uint64_t d = xi_low & LO_WORD;
  uint64_t c = (xi_low & HI_WORD) >> 32LL;
  uint64_t b = xi_high & LO_WORD;
  uint64_t a = (xi_high & HI_WORD) >> 32LL;
  uint64_t h = yi_low & LO_WORD;
  uint64_t g = (yi_low & HI_WORD) >> 32LL;
  uint64_t f = yi_high & LO_WORD;
  uint64_t e = (yi_high & HI_WORD) >> 32LL;
  uint64_t acc = d * h;
  uint64_t o1 = acc & LO_WORD;
  acc >>= 32LL;
  uint64_t carry = 0;

  uint64_t ac2 = acc + c * h; if (ac2 < acc) { carry++; }
  acc = ac2 + d * g; if (acc < ac2) { carry++; }
  uint64_t rv2_lo = o1 | (acc << 32LL);
  ac2 = (acc >> 32LL) | (carry << 32LL); carry = 0;

  acc = ac2 + b * h; if (acc < ac2) { carry++; }
  ac2 = acc + c * g; if (ac2 < acc) { carry++; }
  acc = ac2 + d * f; if (acc < ac2) { carry++; }
  uint64_t o2 = acc & LO_WORD;
  ac2 = (acc >> 32LL) | (carry << 32LL);

  acc = ac2 + a * h;
  ac2 = acc + b * g;
  acc = ac2 + c * f;
  ac2 = acc + d * e;
  uint64_t rv2_hi = (ac2 << 32LL) | o2;

  return rv2_hi;
}
}

uint32_t IntCode_MUL_HI_I8_I8(IntCodeState& ics, const IntCode* i) {
  int16_t v =
      (int16_t)ics.rf[i->src1_reg].i8 * (int16_t)ics.rf[i->src2_reg].i8;
  ics.rf[i->dest_reg].i8 = (v >> 8);
  return IA_NEXT;
}
uint32_t IntCode_MUL_HI_I16_I16(IntCodeState& ics, const IntCode* i) {
  int32_t v =
      (int32_t)ics.rf[i->src1_reg].i16 * (int32_t)ics.rf[i->src2_reg].i16;
  ics.rf[i->dest_reg].i16 = (v >> 16);
  return IA_NEXT;
}
uint32_t IntCode_MUL_HI_I32_I32(IntCodeState& ics, const IntCode* i) {
  int64_t v =
      (int64_t)ics.rf[i->src1_reg].i32 * (int64_t)ics.rf[i->src2_reg].i32;
  ics.rf[i->dest_reg].i32 = (v >> 32);
  return IA_NEXT;
}
uint32_t IntCode_MUL_HI_I64_I64(IntCodeState& ics, const IntCode* i) {
#if !XE_COMPILER_MSVC
  // GCC can, in theory, do this:
  __int128 v =
      (__int128)ics.rf[i->src1_reg].i64 * (__int128)ics.rf[i->src2_reg].i64;
  ics.rf[i->dest_reg].i64 = (v >> 64);
#else
  // 128bit multiply, simplified for two input 64bit integers.
  // http://mrob.com/pub/math/int128.c.txt
  int64_t xi_low = ics.rf[i->src1_reg].i64;
  int64_t xi_high = xi_low < 0 ? -1 : 0;
  int64_t yi_low = ics.rf[i->src2_reg].i64;
  int64_t yi_high = yi_low < 0 ? -1 : 0;
  ics.rf[i->dest_reg].i64 = Mul128(xi_low, xi_high, yi_low, yi_high);
#endif  // !MSVC
  return IA_NEXT;
}
uint32_t IntCode_MUL_HI_I8_I8_U(IntCodeState& ics, const IntCode* i) {
  uint16_t v =
      (uint16_t)ics.rf[i->src1_reg].u8 * (uint16_t)ics.rf[i->src2_reg].u8;
  ics.rf[i->dest_reg].u8 = (v >> 8);
  return IA_NEXT;
}
uint32_t IntCode_MUL_HI_I16_I16_U(IntCodeState& ics, const IntCode* i) {
  uint32_t v =
      (uint32_t)ics.rf[i->src1_reg].u16 * (uint32_t)ics.rf[i->src2_reg].u16;
  ics.rf[i->dest_reg].u16 = (v >> 16);
  return IA_NEXT;
}
uint32_t IntCode_MUL_HI_I32_I32_U(IntCodeState& ics, const IntCode* i) {
  uint64_t v =
      (uint64_t)ics.rf[i->src1_reg].u32 * (uint64_t)ics.rf[i->src2_reg].u32;
  ics.rf[i->dest_reg].u32 = (v >> 32);
  return IA_NEXT;
}
uint32_t IntCode_MUL_HI_I64_I64_U(IntCodeState& ics, const IntCode* i) {
#if !XE_COMPILER_MSVC
  // GCC can, in theory, do this:
  __int128 v =
      (__int128)ics.rf[i->src1_reg].i64 * (__int128)ics.rf[i->src2_reg].i64;
  ics.rf[i->dest_reg].i64 = (v >> 64);
#else
  // 128bit multiply, simplified for two input 64bit integers.
  // http://mrob.com/pub/math/int128.c.txt
  int64_t xi_low = ics.rf[i->src1_reg].i64;
  int64_t xi_high = 0;
  int64_t yi_low = ics.rf[i->src2_reg].i64;
  int64_t yi_high = 0;
  ics.rf[i->dest_reg].i64 = Mul128(xi_low, xi_high, yi_low, yi_high);
#endif  // !MSVC
  return IA_NEXT;
}
int Translate_MUL_HI(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_MUL_HI_I8_I8,
    IntCode_MUL_HI_I16_I16,
    IntCode_MUL_HI_I32_I32,
    IntCode_MUL_HI_I64_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  static IntCodeFn fns_unsigned[] = {
    IntCode_MUL_HI_I8_I8_U,
    IntCode_MUL_HI_I16_I16_U,
    IntCode_MUL_HI_I32_I32_U,
    IntCode_MUL_HI_I64_I64_U,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  if (i->flags & ARITHMETIC_UNSIGNED) {
    return DispatchToC(ctx, i, fns_unsigned[i->dest->type]);
  } else {
    return DispatchToC(ctx, i, fns[i->dest->type]);
  }
}

uint32_t IntCode_DIV_I8_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 / ics.rf[i->src2_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_DIV_I16_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 / ics.rf[i->src2_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_DIV_I32_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 / ics.rf[i->src2_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_DIV_I64_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 / ics.rf[i->src2_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_DIV_F32_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32 / ics.rf[i->src2_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_DIV_F64_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64 / ics.rf[i->src2_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_DIV_V128_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = src1.f4[n] / src2.f4[n];
  }
  return IA_NEXT;
}
uint32_t IntCode_DIV_I8_I8_U(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].u8 = ics.rf[i->src1_reg].u8 / ics.rf[i->src2_reg].u8;
  return IA_NEXT;
}
uint32_t IntCode_DIV_I16_I16_U(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].u16 = ics.rf[i->src1_reg].u16 / ics.rf[i->src2_reg].u16;
  return IA_NEXT;
}
uint32_t IntCode_DIV_I32_I32_U(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].u32 = ics.rf[i->src1_reg].u32 / ics.rf[i->src2_reg].u32;
  return IA_NEXT;
}
uint32_t IntCode_DIV_I64_I64_U(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].u64 = ics.rf[i->src1_reg].u64 / ics.rf[i->src2_reg].u64;
  return IA_NEXT;
}
int Translate_DIV(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_DIV_I8_I8,
    IntCode_DIV_I16_I16,
    IntCode_DIV_I32_I32,
    IntCode_DIV_I64_I64,
    IntCode_DIV_F32_F32,
    IntCode_DIV_F64_F64,
    IntCode_DIV_V128_V128,
  };
  static IntCodeFn fns_unsigned[] = {
    IntCode_DIV_I8_I8_U,
    IntCode_DIV_I16_I16_U,
    IntCode_DIV_I32_I32_U,
    IntCode_DIV_I64_I64_U,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  if (i->flags & ARITHMETIC_UNSIGNED) {
    return DispatchToC(ctx, i, fns_unsigned[i->dest->type]);
  } else {
    return DispatchToC(ctx, i, fns[i->dest->type]);
  }
}

// TODO(benvanik): use intrinsics or something
uint32_t IntCode_MUL_ADD_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 * ics.rf[i->src2_reg].i8 + ics.rf[i->src3_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_MUL_ADD_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 * ics.rf[i->src2_reg].i16 + ics.rf[i->src3_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_MUL_ADD_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 * ics.rf[i->src2_reg].i32 + ics.rf[i->src3_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_MUL_ADD_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 * ics.rf[i->src2_reg].i64 + ics.rf[i->src3_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_MUL_ADD_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32 * ics.rf[i->src2_reg].f32 + ics.rf[i->src3_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_MUL_ADD_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64 * ics.rf[i->src2_reg].f64 + ics.rf[i->src3_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_MUL_ADD_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  const vec128_t& src3 = ics.rf[i->src3_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = src1.f4[n] * src2.f4[n] + src3.f4[n];
  }
  return IA_NEXT;
}
int Translate_MUL_ADD(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_MUL_ADD_I8,
    IntCode_MUL_ADD_I16,
    IntCode_MUL_ADD_I32,
    IntCode_MUL_ADD_I64,
    IntCode_MUL_ADD_F32,
    IntCode_MUL_ADD_F64,
    IntCode_MUL_ADD_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

// TODO(benvanik): use intrinsics or something
uint32_t IntCode_MUL_SUB_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ics.rf[i->src1_reg].i8 * ics.rf[i->src2_reg].i8 - ics.rf[i->src3_reg].i8;
  return IA_NEXT;
}
uint32_t IntCode_MUL_SUB_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ics.rf[i->src1_reg].i16 * ics.rf[i->src2_reg].i16 - ics.rf[i->src3_reg].i16;
  return IA_NEXT;
}
uint32_t IntCode_MUL_SUB_I32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i32 = ics.rf[i->src1_reg].i32 * ics.rf[i->src2_reg].i32 - ics.rf[i->src3_reg].i32;
  return IA_NEXT;
}
uint32_t IntCode_MUL_SUB_I64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i64 = ics.rf[i->src1_reg].i64 * ics.rf[i->src2_reg].i64 - ics.rf[i->src3_reg].i64;
  return IA_NEXT;
}
uint32_t IntCode_MUL_SUB_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = ics.rf[i->src1_reg].f32 * ics.rf[i->src2_reg].f32 - ics.rf[i->src3_reg].f32;
  return IA_NEXT;
}
uint32_t IntCode_MUL_SUB_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = ics.rf[i->src1_reg].f64 * ics.rf[i->src2_reg].f64 - ics.rf[i->src3_reg].f64;
  return IA_NEXT;
}
uint32_t IntCode_MUL_SUB_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  const vec128_t& src3 = ics.rf[i->src3_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = src1.f4[n] * src2.f4[n] - src3.f4[n];
  }
  return IA_NEXT;
}
int Translate_MUL_SUB(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_MUL_SUB_I8,
    IntCode_MUL_SUB_I16,
    IntCode_MUL_SUB_I32,
    IntCode_MUL_SUB_I64,
    IntCode_MUL_SUB_F32,
    IntCode_MUL_SUB_F64,
    IntCode_MUL_SUB_V128,
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
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
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
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
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

uint32_t IntCode_SQRT_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = sqrt(ics.rf[i->src1_reg].f32);
  return IA_NEXT;
}
uint32_t IntCode_SQRT_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = sqrt(ics.rf[i->src1_reg].f64);
  return IA_NEXT;
}
uint32_t IntCode_SQRT_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 4; i++) {
    dest.f4[i] = sqrt(src1.f4[i]);
  }
  return IA_NEXT;
}
int Translate_SQRT(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_SQRT_F32,
    IntCode_SQRT_F64,
    IntCode_SQRT_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
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

uint32_t IntCode_POW2_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = (float)pow(2, ics.rf[i->src1_reg].f32);
  return IA_NEXT;
}
uint32_t IntCode_POW2_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = pow(2, ics.rf[i->src1_reg].f64);
  return IA_NEXT;
}
uint32_t IntCode_POW2_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 4; i++) {
    dest.f4[i] = (float)pow(2, src1.f4[i]);
  }
  return IA_NEXT;
}
int Translate_POW2(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_POW2_F32,
    IntCode_POW2_F64,
    IntCode_POW2_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
}

uint32_t IntCode_LOG2_F32(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f32 = log2(ics.rf[i->src1_reg].f32);
  return IA_NEXT;
}
uint32_t IntCode_LOG2_F64(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].f64 = log2(ics.rf[i->src1_reg].f64);
  return IA_NEXT;
}
uint32_t IntCode_LOG2_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 4; i++) {
    dest.f4[i] = log2(src1.f4[i]);
  }
  return IA_NEXT;
}
int Translate_LOG2(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_LOG2_F32,
    IntCode_LOG2_F64,
    IntCode_LOG2_V128,
  };
  return DispatchToC(ctx, i, fns[i->dest->type]);
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
    VECI4(dest,n) = VECI4(src1,n) & VECI4(src2,n);
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
    VECI4(dest,n) = VECI4(src1,n) | VECI4(src2,n);
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
    VECI4(dest,n) = VECI4(src1,n) ^ VECI4(src2,n);
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
    VECI4(dest,n) = ~VECI4(src1,n);
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
    VECB16(dest,n) = VECB16(src1,n) << (VECB16(src2,n) & 0x7);
  }
  return IA_NEXT;
}
uint32_t IntCode_VECTOR_SHL_I16(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 8; n++) {
    VECS8(dest,n) = VECS8(src1,n) << (VECS8(src2,n) & 0xF);
  }
  return IA_NEXT;
}
uint32_t IntCode_VECTOR_SHL_I32(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    VECI4(dest,n) = VECI4(src1,n) << (VECI4(src2,n) & 0x1F);
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

uint32_t IntCode_VECTOR_SHR_I8(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 16; n++) {
    VECB16(dest,n) = VECB16(src1,n) >> (VECB16(src2,n) & 0x7);
  }
  return IA_NEXT;
}
uint32_t IntCode_VECTOR_SHR_I16(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 8; n++) {
    VECS8(dest,n) = VECS8(src1,n) >> (VECS8(src2,n) & 0xF);
  }
  return IA_NEXT;
}
uint32_t IntCode_VECTOR_SHR_I32(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    VECI4(dest,n) = VECI4(src1,n) >> (VECI4(src2,n) & 0x1F);
  }
  return IA_NEXT;
}
int Translate_VECTOR_SHR(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_VECTOR_SHR_I8,
    IntCode_VECTOR_SHR_I16,
    IntCode_VECTOR_SHR_I32,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->flags]);
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

uint32_t IntCode_VECTOR_SHA_I8(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 16; n++) {
    VECB16(dest,n) = int8_t(VECB16(src1,n)) >> (VECB16(src2,n) & 0x7);
  }
  return IA_NEXT;
}
uint32_t IntCode_VECTOR_SHA_I16(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 8; n++) {
    VECS8(dest,n) = int16_t(VECS8(src1,n)) >> (VECS8(src2,n) & 0xF);
  }
  return IA_NEXT;
}
uint32_t IntCode_VECTOR_SHA_I32(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (int n = 0; n < 4; n++) {
    VECI4(dest,n) = int32_t(VECI4(src1,n)) >> (VECI4(src2,n) & 0x1F);
  }
  return IA_NEXT;
}
int Translate_VECTOR_SHA(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_VECTOR_SHA_I8,
    IntCode_VECTOR_SHA_I16,
    IntCode_VECTOR_SHA_I32,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->flags]);
}

template<typename T>
T ROTL(T v, int8_t sh) {
  return (T(v) << sh) | (T(v) >> ((sizeof(T) * 8) - sh));
}
uint32_t IntCode_ROTATE_LEFT_I8(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i8 = ROTL<uint8_t>(ics.rf[i->src1_reg].i8, ics.rf[i->src2_reg].i8);
  return IA_NEXT;
}
uint32_t IntCode_ROTATE_LEFT_I16(IntCodeState& ics, const IntCode* i) {
  ics.rf[i->dest_reg].i16 = ROTL<uint16_t>(ics.rf[i->src1_reg].i16, ics.rf[i->src2_reg].i8);
  return IA_NEXT;
}
uint32_t IntCode_ROTATE_LEFT_I32(IntCodeState& ics, const IntCode* i) {
  // TODO(benvanik): use _rtol on vc++
  ics.rf[i->dest_reg].i32 = ROTL<uint32_t>(ics.rf[i->src1_reg].i32, ics.rf[i->src2_reg].i8);
  return IA_NEXT;
}
uint32_t IntCode_ROTATE_LEFT_I64(IntCodeState& ics, const IntCode* i) {
  // TODO(benvanik): use _rtol64 on vc++
  ics.rf[i->dest_reg].i64 = ROTL<uint64_t>(ics.rf[i->src1_reg].i64, ics.rf[i->src2_reg].i8);
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
  for (int n = 0; n < 4; n++) {
    VECI4(dest,n) = XESWAP32(VECI4(src1,n));
  }
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
  DWORD mask = ics.rf[i->src1_reg].i8;
  BOOLEAN is_nonzero = _BitScanReverse(&index, mask);
  ics.rf[i->dest_reg].i8 = is_nonzero ? (int8_t)(index - 24) ^ 0x7 : 8;
  return IA_NEXT;
}
uint32_t IntCode_CNTLZ_I16(IntCodeState& ics, const IntCode* i) {
  // CHECK
  XEASSERTALWAYS();
  DWORD index;
  DWORD mask = ics.rf[i->src1_reg].i16;
  BOOLEAN is_nonzero = _BitScanReverse(&index, mask);
  ics.rf[i->dest_reg].i8 = is_nonzero ? (int8_t)(index - 16) ^ 0xF : 16;
  return IA_NEXT;
}
uint32_t IntCode_CNTLZ_I32(IntCodeState& ics, const IntCode* i) {
  DWORD index;
  DWORD mask = ics.rf[i->src1_reg].i32;
  BOOLEAN is_nonzero = _BitScanReverse(&index, mask);
  ics.rf[i->dest_reg].i8 = is_nonzero ? (int8_t)index ^ 0x1F : 32;
  return IA_NEXT;
}
uint32_t IntCode_CNTLZ_I64(IntCodeState& ics, const IntCode* i) {
  DWORD index;
  DWORD64 mask = ics.rf[i->src1_reg].i64;
  BOOLEAN is_nonzero = _BitScanReverse64(&index, mask);
  ics.rf[i->dest_reg].i8 = is_nonzero ? (int8_t)index ^ 0x3F : 64;
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
  ics.rf[i->dest_reg].i8 = VECB16(src1,ics.rf[i->src2_reg].i8);
  return IA_NEXT;
}
uint32_t IntCode_EXTRACT_INT16_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  ics.rf[i->dest_reg].i16 = VECS8(src1,ics.rf[i->src2_reg].i8);
  return IA_NEXT;
}
uint32_t IntCode_EXTRACT_INT32_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  ics.rf[i->dest_reg].i32 = VECI4(src1,ics.rf[i->src2_reg].i8);
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

uint32_t IntCode_INSERT_INT8_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const size_t offset = ics.rf[i->src2_reg].i64;
  const uint8_t part = ics.rf[i->src3_reg].i8;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t n = 0; n < 16; n++) {
    VECB16(dest,n) = (n == offset) ? part : VECB16(src1,n);
  }
  return IA_NEXT;
}
uint32_t IntCode_INSERT_INT16_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const size_t offset = ics.rf[i->src2_reg].i64;
  const uint16_t part = ics.rf[i->src3_reg].i16;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t n = 0; n < 8; n++) {
    VECS8(dest,n) = (n == offset) ? part : VECS8(src1,n);
  }
  return IA_NEXT;
}
uint32_t IntCode_INSERT_INT32_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  const size_t offset = ics.rf[i->src2_reg].i64;
  const uint32_t part = ics.rf[i->src3_reg].i32;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t n = 0; n < 4; n++) {
    VECI4(dest,n) = (n == offset) ? part : VECI4(src1,n);
  }
  return IA_NEXT;
}
int Translate_INSERT(TranslationContext& ctx, Instr* i) {
  // Can do more as needed.
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
    IntCode_INSERT_INT8_V128, IntCode_INSERT_INT16_V128, IntCode_INSERT_INT32_V128, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE,
  };
  IntCodeFn fn = fns[i->src1.value->type * MAX_TYPENAME + i->src3.value->type];
  return DispatchToC(ctx, i, fn);
}

uint32_t IntCode_SPLAT_V128_INT8(IntCodeState& ics, const IntCode* i) {
  int8_t src1 = ics.rf[i->src1_reg].i8;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 16; i++) {
    VECB16(dest,i) = src1;
  }
  return IA_NEXT;
}
uint32_t IntCode_SPLAT_V128_INT16(IntCodeState& ics, const IntCode* i) {
  int16_t src1 = ics.rf[i->src1_reg].i16;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 8; i++) {
    VECS8(dest,i) = src1;
  }
  return IA_NEXT;
}
uint32_t IntCode_SPLAT_V128_INT32(IntCodeState& ics, const IntCode* i) {
  int32_t src1 = ics.rf[i->src1_reg].i32;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 4; i++) {
    VECI4(dest,i) = src1;
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
  uint32_t table = ics.rf[i->src1_reg].i32;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  const vec128_t& src3 = ics.rf[i->src3_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  for (size_t i = 0; i < 4; i++) {
    size_t b = (table >> ((3 - i) * 8)) & 0x7;
    VECI4(dest,i) = b < 4 ?
        VECI4(src2,b) :
        VECI4(src3,b-4);
  }
  return IA_NEXT;
}
uint32_t IntCode_PERMUTE_V128_BY_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& table = ics.rf[i->src1_reg].v128;
  const vec128_t& src2 = ics.rf[i->src2_reg].v128;
  const vec128_t& src3 = ics.rf[i->src3_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  dest.low = dest.high = 0;
  for (size_t n = 0; n < 16; n++) {
    uint8_t index = VECB16(table,n) & 0x1F;
    VECB16(dest,n) = index < 16
        ? VECB16(src2,index)
        : VECB16(src3,index-16);
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
    IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_INVALID_TYPE, IntCode_PERMUTE_V128_BY_V128,
  };
  IntCodeFn fn = fns[i->src1.value->type * MAX_TYPENAME + i->dest->type];
  return DispatchToC(ctx, i, fn);
}

uint32_t IntCode_SWIZZLE_V128(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  uint32_t swizzle_mask = ics.rf[i->src2_reg].u32;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  VECI4(dest,0) = VECI4(src1,(swizzle_mask >> 6) & 0x3);
  VECI4(dest,1) = VECI4(src1,(swizzle_mask >> 4) & 0x3);
  VECI4(dest,2) = VECI4(src1,(swizzle_mask >> 2) & 0x3);
  VECI4(dest,3) = VECI4(src1,(swizzle_mask) & 0x3);
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

uint32_t IntCode_PACK_D3DCOLOR(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  // RGBA (XYZW) -> ARGB (WXYZ)
  dest.ix = dest.iy = dest.iz = 0;
  float r = roundf(((src1.x < 0) ? 0 : ((1 < src1.x) ? 1 : src1.x)) * 255);
  float g = roundf(((src1.y < 0) ? 0 : ((1 < src1.y) ? 1 : src1.y)) * 255);
  float b = roundf(((src1.z < 0) ? 0 : ((1 < src1.z) ? 1 : src1.z)) * 255);
  float a = roundf(((src1.w < 0) ? 0 : ((1 < src1.w) ? 1 : src1.w)) * 255);
  dest.iw = ((uint32_t)a << 24) |
            ((uint32_t)r << 16) |
            ((uint32_t)g << 8) |
            ((uint32_t)b);
  return IA_NEXT;
}
uint32_t IntCode_PACK_FLOAT16_2(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  dest.ix = dest.iy = dest.iz = 0;
  dest.iw =
      ((uint32_t)DirectX::PackedVector::XMConvertFloatToHalf(src1.x) << 16) |
      DirectX::PackedVector::XMConvertFloatToHalf(src1.y);
  return IA_NEXT;
}
uint32_t IntCode_PACK_FLOAT16_4(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  dest.ix = dest.iy = 0;
  dest.iz =
      ((uint32_t)DirectX::PackedVector::XMConvertFloatToHalf(src1.x) << 16) |
      DirectX::PackedVector::XMConvertFloatToHalf(src1.y);
  dest.iw =
      ((uint32_t)DirectX::PackedVector::XMConvertFloatToHalf(src1.z) << 16) |
      DirectX::PackedVector::XMConvertFloatToHalf(src1.w);
  return IA_NEXT;
}
uint32_t IntCode_PACK_SHORT_2(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  // sx = 3 + (x / 1<<22)
  // x = (sx - 3) * 1<<22
  float sx = src1.x;
  float sy = src1.y;
  union {
    int16_t dx;
    int16_t dy;
  };
  dx = (int16_t)((sx - 3.0f) * (float)(1 << 22));
  dy = (int16_t)((sy - 3.0f) * (float)(1 << 22));
  dest.ix = dest.iy = dest.iz = 0;
  dest.iw = ((uint32_t)dx << 16) | dy;
  return IA_NEXT;
}
int Translate_PACK(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_PACK_D3DCOLOR,
    IntCode_PACK_FLOAT16_2,
    IntCode_PACK_FLOAT16_4,
    IntCode_PACK_SHORT_2,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->flags]);
}

uint32_t IntCode_UNPACK_D3DCOLOR(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  // ARGB (WXYZ) -> RGBA (XYZW)
  // XMLoadColor
  int32_t src = (int32_t)src1.iw;
  dest.f4[0] = (float)((src >> 16) & 0xFF) * (1.0f / 255.0f);
  dest.f4[1] = (float)((src >> 8) & 0xFF) * (1.0f / 255.0f);
  dest.f4[2] = (float)(src & 0xFF) * (1.0f / 255.0f);
  dest.f4[3] = (float)((src >> 24) & 0xFF) * (1.0f / 255.0f);
  return IA_NEXT;
}
uint32_t IntCode_UNPACK_FLOAT16_2(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  uint32_t src = src1.iw;
  for (int n = 0; n < 2; n++) {
    dest.f4[n] = DirectX::PackedVector::XMConvertHalfToFloat((uint16_t)src);
    src >>= 16;
  }
  dest.f4[2] = 0.0f;
  dest.f4[3] = 1.0f;
  return IA_NEXT;
}
uint32_t IntCode_UNPACK_FLOAT16_4(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  uint64_t src = src1.iz | ((uint64_t)src1.iw << 32);
  for (int n = 0; n < 4; n++) {
    dest.f4[n] = DirectX::PackedVector::XMConvertHalfToFloat((uint16_t)src);
    src >>= 16;
  }
  return IA_NEXT;
}
uint32_t IntCode_UNPACK_SHORT_2(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  // XMLoadShortN2
  union {
    int16_t sx;
    int16_t sy;
  };
  sx = (int16_t)(src1.iw >> 16);
  sy = (int16_t)src1.iw;
  dest.f4[0] = 3.0f + ((float)sx / (float)(1 << 22));
  dest.f4[1] = 3.0f + ((float)sy / (float)(1 << 22));
  dest.f4[2] = 0.0f;
  dest.f4[3] = 1.0f; // 3?
  return IA_NEXT;
}
uint32_t IntCode_UNPACK_S8_IN_16_LO(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  VECS8(dest,0) = (int16_t)(int8_t)VECB16(src1,8+0);
  VECS8(dest,1) = (int16_t)(int8_t)VECB16(src1,8+1);
  VECS8(dest,2) = (int16_t)(int8_t)VECB16(src1,8+2);
  VECS8(dest,3) = (int16_t)(int8_t)VECB16(src1,8+3);
  VECS8(dest,4) = (int16_t)(int8_t)VECB16(src1,8+4);
  VECS8(dest,5) = (int16_t)(int8_t)VECB16(src1,8+5);
  VECS8(dest,6) = (int16_t)(int8_t)VECB16(src1,8+6);
  VECS8(dest,7) = (int16_t)(int8_t)VECB16(src1,8+7);
  return IA_NEXT;
}
uint32_t IntCode_UNPACK_S8_IN_16_HI(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  VECS8(dest,0) = (int16_t)(int8_t)VECB16(src1,0);
  VECS8(dest,1) = (int16_t)(int8_t)VECB16(src1,1);
  VECS8(dest,2) = (int16_t)(int8_t)VECB16(src1,2);
  VECS8(dest,3) = (int16_t)(int8_t)VECB16(src1,3);
  VECS8(dest,4) = (int16_t)(int8_t)VECB16(src1,4);
  VECS8(dest,5) = (int16_t)(int8_t)VECB16(src1,5);
  VECS8(dest,6) = (int16_t)(int8_t)VECB16(src1,6);
  VECS8(dest,7) = (int16_t)(int8_t)VECB16(src1,7);
  return IA_NEXT;
}
uint32_t IntCode_UNPACK_S16_IN_32_LO(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  VECI4(dest,0) = (int32_t)(int16_t)VECS8(src1,4+0);
  VECI4(dest,1) = (int32_t)(int16_t)VECS8(src1,4+1);
  VECI4(dest,2) = (int32_t)(int16_t)VECS8(src1,4+2);
  VECI4(dest,3) = (int32_t)(int16_t)VECS8(src1,4+3);
  return IA_NEXT;
}
uint32_t IntCode_UNPACK_S16_IN_32_HI(IntCodeState& ics, const IntCode* i) {
  const vec128_t& src1 = ics.rf[i->src1_reg].v128;
  vec128_t& dest = ics.rf[i->dest_reg].v128;
  VECI4(dest,0) = (int32_t)(int16_t)VECS8(src1,0);
  VECI4(dest,1) = (int32_t)(int16_t)VECS8(src1,1);
  VECI4(dest,2) = (int32_t)(int16_t)VECS8(src1,2);
  VECI4(dest,3) = (int32_t)(int16_t)VECS8(src1,3);
  return IA_NEXT;
}
int Translate_UNPACK(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_UNPACK_D3DCOLOR,
    IntCode_UNPACK_FLOAT16_2,
    IntCode_UNPACK_FLOAT16_4,
    IntCode_UNPACK_SHORT_2,
    IntCode_UNPACK_S8_IN_16_LO,
    IntCode_UNPACK_S8_IN_16_HI,
    IntCode_UNPACK_S16_IN_32_LO,
    IntCode_UNPACK_S16_IN_32_HI,
  };
  return DispatchToC(ctx, i, fns[i->flags]);
}

uint32_t IntCode_ATOMIC_EXCHANGE_I32(IntCodeState& ics, const IntCode* i) {
  auto address = (uint8_t*)ics.rf[i->src1_reg].u64;
  auto new_value = ics.rf[i->src2_reg].u32;
  auto old_value = xe_atomic_exchange_32(new_value, address);
  ics.rf[i->dest_reg].u32 = old_value;
  return IA_NEXT;
}
uint32_t IntCode_ATOMIC_EXCHANGE_I64(IntCodeState& ics, const IntCode* i) {
  auto address = (uint8_t*)ics.rf[i->src1_reg].u64;
  auto new_value = ics.rf[i->src2_reg].u64;
  auto old_value = xe_atomic_exchange_64(new_value, address);
  ics.rf[i->dest_reg].u64 = old_value;
  return IA_NEXT;
}
int Translate_ATOMIC_EXCHANGE(TranslationContext& ctx, Instr* i) {
  static IntCodeFn fns[] = {
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_ATOMIC_EXCHANGE_I32,
    IntCode_ATOMIC_EXCHANGE_I64,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
    IntCode_INVALID_TYPE,
  };
  return DispatchToC(ctx, i, fns[i->src2.value->type]);
}

typedef int (*TranslateFn)(TranslationContext& ctx, Instr* i);
static const TranslateFn dispatch_table[] = {
  Translate_COMMENT,

  Translate_NOP,

  Translate_SOURCE_OFFSET,

  Translate_DEBUG_BREAK,
  Translate_DEBUG_BREAK_TRUE,

  Translate_TRAP,
  Translate_TRAP_TRUE,

  Translate_CALL,
  Translate_CALL_TRUE,
  Translate_CALL_INDIRECT,
  Translate_CALL_INDIRECT_TRUE,
  Translate_CALL_EXTERN,
  Translate_RETURN,
  Translate_RETURN_TRUE,
  Translate_SET_RETURN_ADDRESS,

  Translate_BRANCH,
  Translate_BRANCH_TRUE,
  Translate_BRANCH_FALSE,

  Translate_ASSIGN,
  Translate_CAST,
  Translate_ZERO_EXTEND,
  Translate_SIGN_EXTEND,
  Translate_TRUNCATE,
  Translate_CONVERT,
  Translate_ROUND,
  Translate_VECTOR_CONVERT_I2F,
  Translate_VECTOR_CONVERT_F2I,

  Translate_LOAD_VECTOR_SHL,
  Translate_LOAD_VECTOR_SHR,

  Translate_LOAD_CLOCK,

  Translate_LOAD_LOCAL,
  Translate_STORE_LOCAL,

  Translate_LOAD_CONTEXT,
  Translate_STORE_CONTEXT,

  Translate_LOAD,
  Translate_STORE,
  Translate_PREFETCH,

  Translate_MAX,
  Translate_MIN,
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
  Translate_DID_SATURATE,
  Translate_VECTOR_COMPARE_EQ,
  Translate_VECTOR_COMPARE_SGT,
  Translate_VECTOR_COMPARE_SGE,
  Translate_VECTOR_COMPARE_UGT,
  Translate_VECTOR_COMPARE_UGE,

  Translate_ADD,
  Translate_ADD_CARRY,
  Translate_VECTOR_ADD,
  Translate_SUB,
  Translate_MUL,
  Translate_MUL_HI,
  Translate_DIV,
  Translate_MUL_ADD,
  Translate_MUL_SUB,
  Translate_NEG,
  Translate_ABS,
  Translate_SQRT,
  Translate_RSQRT,
  Translate_POW2,
  Translate_LOG2,
  Translate_DOT_PRODUCT_3,
  Translate_DOT_PRODUCT_4,

  Translate_AND,
  Translate_OR,
  Translate_XOR,
  Translate_NOT,
  Translate_SHL,
  Translate_VECTOR_SHL,
  Translate_SHR,
  Translate_VECTOR_SHR,
  Translate_SHA,
  Translate_VECTOR_SHA,
  Translate_ROTATE_LEFT,
  Translate_BYTE_SWAP,
  Translate_CNTLZ,
  Translate_INSERT,
  Translate_EXTRACT,
  Translate_SPLAT,
  Translate_PERMUTE,
  Translate_SWIZZLE,
  Translate_PACK,
  Translate_UNPACK,

  TranslateInvalid, //Translate_COMPARE_EXCHANGE,
  Translate_ATOMIC_EXCHANGE,
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

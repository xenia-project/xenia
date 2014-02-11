/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lowering/lowering_sequences.h>

#include <alloy/backend/x64/x64_backend.h>
#include <alloy/backend/x64/x64_emitter.h>
#include <alloy/backend/x64/x64_function.h>
#include <alloy/backend/x64/x64_thunk_emitter.h>
#include <alloy/backend/x64/lowering/lowering_table.h>
#include <alloy/backend/x64/lowering/tracers.h>
#include <alloy/runtime/symbol_info.h>
#include <alloy/runtime/runtime.h>
#include <alloy/runtime/thread_state.h>

// TODO(benvanik): reimplement packing functions
#include <DirectXPackedVector.h>

using namespace alloy;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lowering;
using namespace alloy::hir;
using namespace alloy::runtime;

using namespace Xbyak;

namespace {

// Make loads/stores to ints check to see if they are doing a register value.
// This is slow, and with proper constant propagation we may be able to always
// avoid it.
// TODO(benvanik): make a compile time flag?
#define DYNAMIC_REGISTER_ACCESS_CHECK 1

#define UNIMPLEMENTED_SEQ() __debugbreak()
#define ASSERT_INVALID_TYPE() XEASSERTALWAYS()

#define ITRACE 1
#define DTRACE 1

#define SHUFPS_SWAP_DWORDS 0x1B


// Major templating foo lives in here.
#include <alloy/backend/x64/lowering/op_utils.inl>


enum XmmConst {
  XMMZero               = 0,
  XMMOne                = 1,
  XMMNegativeOne        = 2,
  XMMMaskX16Y16         = 3,
  XMMFlipX16Y16         = 4,
  XMMFixX16Y16          = 5,
  XMMNormalizeX16Y16    = 6,
  XMM3301               = 7,
  XMMSignMaskPS         = 8,
  XMMSignMaskPD         = 9,
  XMMByteSwapMask       = 10,
  XMMPermuteControl15   = 11,
  XMMUnpackD3DCOLOR     = 12,
  XMMOneOver255         = 13,
};
static const vec128_t xmm_consts[] = {
  /* XMMZero                */ vec128f(0.0f, 0.0f, 0.0f, 0.0f),
  /* XMMOne                 */ vec128f(1.0f, 1.0f, 1.0f, 1.0f),
  /* XMMNegativeOne         */ vec128f(-1.0f, -1.0f, -1.0f, -1.0f),
  /* XMMMaskX16Y16          */ vec128i(0x0000FFFF, 0xFFFF0000, 0x00000000, 0x00000000),
  /* XMMFlipX16Y16          */ vec128i(0x00008000, 0x00000000, 0x00000000, 0x00000000),
  /* XMMFixX16Y16           */ vec128f(-32768.0f, 0.0f, 0.0f, 0.0f),
  /* XMMNormalizeX16Y16     */ vec128f(1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 0.0f, 0.0f),
  /* XMM3301                */ vec128f(3.0f, 3.0f, 0.0f, 1.0f),
  /* XMMSignMaskPS          */ vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
  /* XMMSignMaskPD          */ vec128i(0x00000000u, 0x80000000u, 0x00000000u, 0x80000000u),
  /* XMMByteSwapMask        */ vec128i(0x00010203u, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu),
  /* XMMPermuteControl15    */ vec128b(15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15),
  /* XMMUnpackD3DCOLOR      */ vec128i(0xFFFFFF02, 0xFFFFFF01, 0xFFFFFF00, 0xFFFFFF02),
  /* XMMOneOver255          */ vec128f(1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f),
};
// Use consts by first loading the base register then accessing memory:
// e.mov(e.rax, XMMCONSTBASE)
// e.andps(reg, XMMCONST(XMM3303))
// TODO(benvanik): find a way to do this without the base register.
#define XMMCONSTBASE (uint64_t)&xmm_consts[0]
#define XMMCONST(base_reg, name) e.ptr[base_reg + name * 16]

static vec128_t lvsl_table[17] = {
  vec128b( 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15),
  vec128b( 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16),
  vec128b( 2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17),
  vec128b( 3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
  vec128b( 4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
  vec128b( 5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
  vec128b( 6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
  vec128b( 7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
  vec128b( 8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
  vec128b( 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
  vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
  vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
  vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
  vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
  vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
  vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
  vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
};
static vec128_t lvsr_table[17] = {
  vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
  vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
  vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
  vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
  vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
  vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
  vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
  vec128b( 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
  vec128b( 8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
  vec128b( 7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
  vec128b( 6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
  vec128b( 5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
  vec128b( 4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
  vec128b( 3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
  vec128b( 2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17),
  vec128b( 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16),
  vec128b( 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15),
};
static vec128_t extract_table_32[4] = {
  vec128b( 3,  2,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0),
  vec128b( 7,  6,  5,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0),
  vec128b(11, 10,  9,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0),
  vec128b(15, 14, 13, 12,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0),
};

// A note about vectors:
// Alloy represents vectors as xyzw pairs, with indices 0123.
// XMM registers are xyzw pairs with indices 3210, making them more like wzyx.
// This makes things somewhat confusing. It'd be nice to just shuffle the
// registers around on load/store, however certain operations require that
// data be in the right offset.
// Basically, this identity must hold:
//   shuffle(vec, b00011011) -> {x,y,z,w} => {x,y,z,w}
// All indices and operations must respect that.
//
// Memory (big endian):
// [00 01 02 03] [04 05 06 07] [08 09 0A 0B] [0C 0D 0E 0F] (x, y, z, w)
// load into xmm register:
// [0F 0E 0D 0C] [0B 0A 09 08] [07 06 05 04] [03 02 01 00] (w, z, y, x)

void Dummy() {
  //
}

void UndefinedCallExtern(void* raw_context, FunctionInfo* symbol_info) {
  XELOGW("undefined extern call to %.8X %s",
             symbol_info->address(),
             symbol_info->name());
}

uint64_t DynamicRegisterLoad(void* raw_context, uint32_t address) {
  auto thread_state = *((ThreadState**)raw_context);
  auto cbs = thread_state->runtime()->access_callbacks();
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      return cbs->read(cbs->context, address);
    }
  }
  return 0;
}

void DynamicRegisterStore(void* raw_context, uint32_t address, uint64_t value) {
  auto thread_state = *((ThreadState**)raw_context);
  auto cbs = thread_state->runtime()->access_callbacks();
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      cbs->write(cbs->context, address, value);
      return;
    }
  }
}

void Unpack_FLOAT16_2(void* raw_context, __m128& v) {
  uint32_t src = v.m128_i32[3];
  v.m128_f32[0] = DirectX::PackedVector::XMConvertHalfToFloat((uint16_t)src);
  v.m128_f32[1] = DirectX::PackedVector::XMConvertHalfToFloat((uint16_t)(src >> 16));
  v.m128_f32[2] = 0.0f;
  v.m128_f32[3] = 1.0f;
}

uint64_t LoadClock(void* raw_context) {
  LARGE_INTEGER counter;
  uint64_t time = 0;
  if (QueryPerformanceCounter(&counter)) {
    time = counter.QuadPart;
  }
  return time;
}

// TODO(benvanik): fancy stuff.
void* ResolveFunctionSymbol(void* raw_context, FunctionInfo* symbol_info) {
  // TODO(benvanik): generate this thunk at runtime? or a shim?
  auto thread_state = *((ThreadState**)raw_context);

  Function* fn = NULL;
  thread_state->runtime()->ResolveFunction(symbol_info->address(), &fn);
  XEASSERTNOTNULL(fn);
  auto x64_fn = (X64Function*)fn;
  return x64_fn->machine_code();
}
void* ResolveFunctionAddress(void* raw_context, uint32_t target_address) {
  // TODO(benvanik): generate this thunk at runtime? or a shim?
  auto thread_state = *((ThreadState**)raw_context);

  Function* fn = NULL;
  thread_state->runtime()->ResolveFunction(target_address, &fn);
  XEASSERTNOTNULL(fn);
  auto x64_fn = (X64Function*)fn;
  return x64_fn->machine_code();
}
void TransitionToHost(X64Emitter& e) {
  // Expects:
  //   rcx = context
  //   rdx = target host function
  //   r8  = arg0
  //   r9  = arg1
  // Returns:
  //   rax = host return
  auto thunk = e.backend()->guest_to_host_thunk();
  e.mov(e.rax, (uint64_t)thunk);
  e.call(e.rax);
}
void IssueCall(X64Emitter& e, FunctionInfo* symbol_info, uint32_t flags) {
  auto fn = (X64Function*)symbol_info->function();
  // Resolve address to the function to call and store in rax.
  // TODO(benvanik): caching/etc. For now this makes debugging easier.
  if (fn) {
    e.mov(e.rax, (uint64_t)fn->machine_code());
  } else {
    e.mov(e.rdx, (uint64_t)symbol_info);
    CallNative(e, ResolveFunctionSymbol);
  }

  // Actually jump/call to rax.
  if (flags & CALL_TAIL) {
    // Pass the callers return address over.
    e.mov(e.rdx, e.qword[e.rsp + StackLayout::GUEST_RET_ADDR]);

    e.add(e.rsp, (uint32_t)e.stack_size());
    e.jmp(e.rax);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    e.mov(e.rdx, e.qword[e.rsp + StackLayout::GUEST_CALL_RET_ADDR]);

    e.call(e.rax);
  }
}
void IssueCallIndirect(X64Emitter& e, Value* target, uint32_t flags) {
  Reg64 r;
  e.BeginOp(target, r, 0);

  // Check if return.
  if (flags & CALL_POSSIBLE_RETURN) {
    e.cmp(r.cvt32(), e.dword[e.rsp + StackLayout::GUEST_RET_ADDR]);
    e.je("epilog", CodeGenerator::T_NEAR);
  }

  // Resolve address to the function to call and store in rax.
  // TODO(benvanik): caching/etc. For now this makes debugging easier.
  if (r != e.rdx) {
    e.mov(e.rdx, r);
  }
  e.EndOp(r);
  CallNative(e, ResolveFunctionAddress);

  // Actually jump/call to rax.
  if (flags & CALL_TAIL) {
    // Pass the callers return address over.
    e.mov(e.rdx, e.qword[e.rsp + StackLayout::GUEST_RET_ADDR]);

    e.add(e.rsp, (uint32_t)e.stack_size());
    e.jmp(e.rax);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    e.mov(e.rdx, e.qword[e.rsp + StackLayout::GUEST_CALL_RET_ADDR]);

    e.call(e.rax);
  }
}

}  // namespace


void alloy::backend::x64::lowering::RegisterSequences(LoweringTable* table) {
// --------------------------------------------------------------------------
// General
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_COMMENT, [](X64Emitter& e, Instr*& i) {
#if ITRACE
  // TODO(benvanik): pass through.
  // TODO(benvanik): don't just leak this memory.
  auto str = (const char*)i->src1.offset;
  auto str_copy = xestrdupa(str);
  e.mov(e.rdx, (uint64_t)str_copy);
  CallNative(e, TraceString);
#endif  // ITRACE
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_NOP, [](X64Emitter& e, Instr*& i) {
  // If we got this, chances are we want it.
  e.nop();
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Debugging
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_SOURCE_OFFSET, [](X64Emitter& e, Instr*& i) {
#if XE_DEBUG
  e.nop();
  e.nop();
  e.mov(e.eax, (uint32_t)i->src1.offset);
  e.nop();
  e.nop();
#endif  // XE_DEBUG

  e.MarkSourceOffset(i);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_DEBUG_BREAK, [](X64Emitter& e, Instr*& i) {
  // TODO(benvanik): insert a call to the debug break function to let the
  //     debugger know.
  e.db(0xCC);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_DEBUG_BREAK_TRUE, [](X64Emitter& e, Instr*& i) {
  e.inLocalLabel();
  CheckBoolean(e, i->src1.value);
  e.jz(".x", e.T_SHORT);
  // TODO(benvanik): insert a call to the debug break function to let the
  //     debugger know.
  e.db(0xCC);
  e.L(".x");
  e.outLocalLabel();
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_TRAP, [](X64Emitter& e, Instr*& i) {
  // TODO(benvanik): insert a call to the trap function to let the
  //     debugger know.
  e.db(0xCC);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_TRAP_TRUE, [](X64Emitter& e, Instr*& i) {
  e.inLocalLabel();
  CheckBoolean(e, i->src1.value);
  e.jz(".x", e.T_SHORT);
  // TODO(benvanik): insert a call to the trap function to let the
  //     debugger know.
  e.db(0xCC);
  e.L(".x");
  e.outLocalLabel();
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Calls
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_CALL, [](X64Emitter& e, Instr*& i) {
  IssueCall(e, i->src1.symbol_info, i->flags);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_CALL_TRUE, [](X64Emitter& e, Instr*& i) {
  e.inLocalLabel();
  CheckBoolean(e, i->src1.value);
  e.jz(".x", e.T_SHORT);
  IssueCall(e, i->src2.symbol_info, i->flags);
  e.L(".x");
  e.outLocalLabel();
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_CALL_INDIRECT, [](X64Emitter& e, Instr*& i) {
  IssueCallIndirect(e, i->src1.value, i->flags);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_CALL_INDIRECT_TRUE, [](X64Emitter& e, Instr*& i) {
  e.inLocalLabel();
  CheckBoolean(e, i->src1.value);
  e.jz(".x", e.T_SHORT);
  IssueCallIndirect(e, i->src2.value, i->flags);
  e.L(".x");
  e.outLocalLabel();
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_CALL_EXTERN, [](X64Emitter& e, Instr*& i) {
  auto symbol_info = i->src1.symbol_info;
  XEASSERT(symbol_info->behavior() == FunctionInfo::BEHAVIOR_EXTERN);
  if (!symbol_info->extern_handler()) {
    e.mov(e.rdx, (uint64_t)symbol_info);
    CallNative(e, UndefinedCallExtern);
  } else {
    // rdx = target host function
    // r8  = arg0
    // r9  = arg1
    e.mov(e.rdx, (uint64_t)symbol_info->extern_handler());
    e.mov(e.r8, (uint64_t)symbol_info->extern_arg0());
    e.mov(e.r9, (uint64_t)symbol_info->extern_arg1());
    TransitionToHost(e);
    ReloadRDX(e);
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_RETURN, [](X64Emitter& e, Instr*& i) {
  // If this is the last instruction in the last block, just let us
  // fall through.
  if (i->next || i->block->next) {
    e.jmp("epilog", CodeGenerator::T_NEAR);
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_RETURN_TRUE, [](X64Emitter& e, Instr*& i) {
  CheckBoolean(e, i->src1.value);
  e.jnz("epilog", CodeGenerator::T_NEAR);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_SET_RETURN_ADDRESS, [](X64Emitter& e, Instr*& i) {
  XEASSERT(i->src1.value->IsConstant());
  e.mov(e.qword[e.rsp + StackLayout::GUEST_CALL_RET_ADDR],
        i->src1.value->AsUint64());
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Branches
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_BRANCH, [](X64Emitter& e, Instr*& i) {
  auto target = i->src1.label;
  e.jmp(target->name, e.T_NEAR);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_BRANCH_TRUE, [](X64Emitter& e, Instr*& i) {
  CheckBoolean(e, i->src1.value);
  auto target = i->src2.label;
  e.jnz(target->name, e.T_NEAR);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_BRANCH_FALSE, [](X64Emitter& e, Instr*& i) {
  CheckBoolean(e, i->src1.value);
  auto target = i->src2.label;
  e.jz(target->name, e.T_NEAR);
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Types
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_ASSIGN, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntUnaryOp(
        e, i,
        [](X64Emitter& e, Instr& i, const Reg& dest_src) {
          // nop - the mov will have happened.
        });
  } else if (IsFloatType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else if (IsVecType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_CAST, [](X64Emitter& e, Instr*& i) {
  if (i->dest->type == INT32_TYPE) {
    if (i->src1.value->type == FLOAT32_TYPE) {
      Reg32 dest;
      Xmm src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.vmovd(dest, src);
      e.EndOp(dest, src);
    } else {
      UNIMPLEMENTED_SEQ();
    }
  } else if (i->dest->type == INT64_TYPE) {
    if (i->src1.value->type == FLOAT64_TYPE) {
      Reg64 dest;
      Xmm src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.vmovq(dest, src);
      e.EndOp(dest, src);
    } else {
      UNIMPLEMENTED_SEQ();
    }
  } else if (i->dest->type == FLOAT32_TYPE) {
    if (i->src1.value->type == INT32_TYPE) {
      Xmm dest;
      Reg32 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.vmovd(dest, src);
      e.EndOp(dest, src);
    } else {
      UNIMPLEMENTED_SEQ();
    }
  } else if (i->dest->type == FLOAT64_TYPE) {
    if (i->src1.value->type == INT64_TYPE) {
      Xmm dest;
      Reg64 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.vmovq(dest, src);
      e.EndOp(dest, src);
    } else {
      UNIMPLEMENTED_SEQ();
    }
  } else if (IsVecType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_ZERO_EXTEND, [](X64Emitter& e, Instr*& i) {
  if (i->Match(SIG_TYPE_I16, SIG_TYPE_I8)) {
    Reg16 dest;
    Reg8 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movzx(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I8)) {
    Reg32 dest;
    Reg8 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movzx(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I16)) {
    Reg32 dest;
    Reg16 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movzx(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I8)) {
    Reg64 dest;
    Reg8 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movzx(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I16)) {
    Reg64 dest;
    Reg16 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movzx(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I32)) {
    Reg64 dest;
    Reg32 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.mov(dest.cvt32(), src.cvt32());
    e.EndOp(dest, src);
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_SIGN_EXTEND, [](X64Emitter& e, Instr*& i) {
  if (i->Match(SIG_TYPE_I16, SIG_TYPE_I8)) {
    Reg16 dest;
    Reg8 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movsx(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I8)) {
    Reg32 dest;
    Reg8 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movsx(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I16)) {
    Reg32 dest;
    Reg16 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movsx(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I8)) {
    Reg64 dest;
    Reg8 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movsx(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I16)) {
    Reg64 dest;
    Reg16 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movsx(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I32)) {
    Reg64 dest;
    Reg32 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.movsxd(dest, src);
    e.EndOp(dest, src);
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_TRUNCATE, [](X64Emitter& e, Instr*& i) {
  if (i->Match(SIG_TYPE_I8, SIG_TYPE_I16)) {
    Reg8 dest;
    Reg16 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.mov(dest, src.cvt8());
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I32)) {
    Reg8 dest;
    Reg16 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.mov(dest, src.cvt8());
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I64)) {
    Reg8 dest;
    Reg64 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.mov(dest, src.cvt8());
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I32)) {
    Reg16 dest;
    Reg32 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.mov(dest, src.cvt16());
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I64)) {
    Reg16 dest;
    Reg64 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.mov(dest, src.cvt16());
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I64)) {
    Reg32 dest;
    Reg64 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.mov(dest, src.cvt32());
    e.EndOp(dest, src);
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_CONVERT, [](X64Emitter& e, Instr*& i) {
  if (i->Match(SIG_TYPE_I32, SIG_TYPE_F32)) {
    Reg32 dest;
    Xmm src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    // TODO(benvanik): additional checks for saturation/etc? cvtt* (trunc?)
    e.cvttss2si(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_F64)) {
    Reg32 dest;
    Xmm src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    // TODO(benvanik): additional checks for saturation/etc? cvtt* (trunc?)
    e.cvtsd2ss(e.xmm0, src);
    e.cvttss2si(dest, e.xmm0);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_F64)) {
    Reg64 dest;
    Xmm src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    // TODO(benvanik): additional checks for saturation/etc? cvtt* (trunc?)
    e.cvttsd2si(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_F32, SIG_TYPE_I32)) {
    Xmm dest;
    Reg32 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    // TODO(benvanik): additional checks for saturation/etc?
    e.cvtsi2ss(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_F32, SIG_TYPE_F64)) {
    Xmm dest, src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    // TODO(benvanik): additional checks for saturation/etc?
    e.cvtsd2ss(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_F64, SIG_TYPE_I64)) {
    Xmm dest;
    Reg64 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    // TODO(benvanik): additional checks for saturation/etc?
    e.cvtsi2sd(dest, src);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_F64, SIG_TYPE_F32)) {
    Xmm dest, src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.cvtss2sd(dest, src);
    e.EndOp(dest, src);
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_ROUND, [](X64Emitter& e, Instr*& i) {
  // flags = ROUND_TO_*
  if (IsFloatType(i->dest->type)) {
    XmmUnaryOp(e, i, 0, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      if (i.src1.value->type == FLOAT32_TYPE) {
        switch (i.flags) {
        case ROUND_TO_ZERO:
          e.roundss(dest, src, B00000011);
          break;
        case ROUND_TO_NEAREST:
          e.roundss(dest, src, B00000000);
          break;
        case ROUND_TO_MINUS_INFINITY:
          e.roundss(dest, src, B00000001);
          break;
        case ROUND_TO_POSITIVE_INFINITY:
          e.roundss(dest, src, B00000010);
          break;
        }
      } else {
        switch (i.flags) {
        case ROUND_TO_ZERO:
          e.roundsd(dest, src, B00000011);
          break;
        case ROUND_TO_NEAREST:
          e.roundsd(dest, src, B00000000);
          break;
        case ROUND_TO_MINUS_INFINITY:
          e.roundsd(dest, src, B00000001);
          break;
        case ROUND_TO_POSITIVE_INFINITY:
          e.roundsd(dest, src, B00000010);
          break;
        }
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmUnaryOp(e, i, 0, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      switch (i.flags) {
      case ROUND_TO_ZERO:
        e.roundps(dest, src, B00000011);
        break;
      case ROUND_TO_NEAREST:
        e.roundps(dest, src, B00000000);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.roundps(dest, src, B00000001);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.roundps(dest, src, B00000010);
        break;
      }
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_CONVERT_I2F, [](X64Emitter& e, Instr*& i) {
  // flags = ARITHMETIC_UNSIGNED
  XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
    // TODO(benvanik): are these really the same? VC++ thinks so.
    if (i.flags & ARITHMETIC_UNSIGNED) {
      e.cvtdq2ps(dest, src);
    } else {
      e.cvtdq2ps(dest, src);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_CONVERT_F2I, [](X64Emitter& e, Instr*& i) {
  // flags = ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED
  XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
    // TODO(benvanik): are these really the same? VC++ thinks so.
    if (i.flags & ARITHMETIC_UNSIGNED) {
      e.cvttps2dq(dest, src);
    } else {
      e.cvttps2dq(dest, src);
    }
    if (i.flags & ARITHMETIC_SATURATE) {
      UNIMPLEMENTED_SEQ();
    }
  });
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Constants
// --------------------------------------------------------------------------

// specials for zeroing/etc (xor/etc)

table->AddSequence(OPCODE_LOAD_VECTOR_SHL, [](X64Emitter& e, Instr*& i) {
  XEASSERT(i->dest->type == VEC128_TYPE);
  if (i->src1.value->IsConstant()) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    auto sh = MIN(16, i->src1.value->AsUint32());
    e.mov(e.rax, (uintptr_t)&lvsl_table[sh]);
    e.movaps(dest, e.ptr[e.rax]);
    e.EndOp(dest);
  } else {
    Xmm dest;
    Reg8 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    // TODO(benvanik): probably a way to do this with addressing.
    e.mov(TEMP_REG, 16);
    e.movzx(e.rax, src);
    e.cmp(src, 16);
    e.cmovb(TEMP_REG, e.rax);
    e.shl(TEMP_REG, 4);
    e.mov(e.rax, (uintptr_t)lvsl_table);
    e.movaps(dest, e.ptr[e.rax + TEMP_REG]);
    e.EndOp(dest, src);
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_LOAD_VECTOR_SHR, [](X64Emitter& e, Instr*& i) {
  XEASSERT(i->dest->type == VEC128_TYPE);
  if (i->src1.value->IsConstant()) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    auto sh = MIN(16, i->src1.value->AsUint32());
    e.mov(e.rax, (uintptr_t)&lvsr_table[sh]);
    e.movaps(dest, e.ptr[e.rax]);
    e.EndOp(dest);
  } else {
    Xmm dest;
    Reg8 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    // TODO(benvanik): probably a way to do this with addressing.
    e.mov(TEMP_REG, 16);
    e.movzx(e.rax, src);
    e.cmp(src, 16);
    e.cmovb(TEMP_REG, e.rax);
    e.shl(TEMP_REG, 4);
    e.mov(e.rax, (uintptr_t)lvsr_table);
    e.movaps(dest, e.ptr[e.rax + TEMP_REG]);
    e.EndOp(dest, src);
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_LOAD_CLOCK, [](X64Emitter& e, Instr*& i) {
  // It'd be cool to call QueryPerformanceCounter directly, but w/e.
  CallNative(e, LoadClock);
  Reg64 dest;
  e.BeginOp(i->dest, dest, REG_DEST);
  e.mov(dest, e.rax);
  e.EndOp(dest);
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Stack Locals
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_LOAD_LOCAL, [](X64Emitter& e, Instr*& i) {
  auto addr = e.rsp + i->src1.value->AsUint32();
  if (i->Match(SIG_TYPE_I8, SIG_TYPE_IGNORE)) {
    Reg8 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.byte[addr]);
    e.EndOp(dest);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_IGNORE)) {
    Reg16 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.word[addr]);
    e.EndOp(dest);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_IGNORE)) {
    Reg32 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.dword[addr]);
    e.EndOp(dest);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_IGNORE)) {
    Reg64 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.qword[addr]);
    e.EndOp(dest);
  } else if (i->Match(SIG_TYPE_F32, SIG_TYPE_IGNORE)) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.movss(dest, e.dword[addr]);
    e.EndOp(dest);
  } else if (i->Match(SIG_TYPE_F64, SIG_TYPE_IGNORE)) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.movsd(dest, e.qword[addr]);
    e.EndOp(dest);
  } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_IGNORE)) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    // NOTE: we always know we are aligned.
    e.movaps(dest, e.ptr[addr]);
    e.EndOp(dest);
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_STORE_LOCAL, [](X64Emitter& e, Instr*& i) {
  auto addr = e.rsp + i->src1.value->AsUint32();
  if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I8)) {
    Reg8 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.byte[addr], src);
    e.EndOp(src);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I8C)) {
    e.mov(e.byte[addr], i->src2.value->constant.i8);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I16)) {
    Reg16 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.word[addr], src);
    e.EndOp(src);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I16C)) {
    e.mov(e.word[addr], i->src2.value->constant.i16);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I32)) {
    Reg32 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.dword[addr], src);
    e.EndOp(src);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I32C)) {
    e.mov(e.dword[addr], i->src2.value->constant.i32);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I64)) {
    Reg64 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.qword[addr], src);
    e.EndOp(src);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I64C)) {
    MovMem64(e, addr, i->src2.value->constant.i64);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F32)) {
    Xmm src;
    e.BeginOp(i->src2.value, src, 0);
    e.movss(e.dword[addr], src);
    e.EndOp(src);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F32C)) {
    e.mov(e.dword[addr], i->src2.value->constant.i32);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F64)) {
    Xmm src;
    e.BeginOp(i->src2.value, src, 0);
    e.movsd(e.qword[addr], src);
    e.EndOp(src);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F64C)) {
    MovMem64(e, addr, i->src2.value->constant.i64);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_V128)) {
    Xmm src;
    e.BeginOp(i->src2.value, src, 0);
    // NOTE: we always know we are aligned.
    e.movaps(e.ptr[addr], src);
    e.EndOp(src);
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_V128C)) {
    // TODO(benvanik): check zero
    // TODO(benvanik): correct order?
    MovMem64(e, addr, i->src2.value->constant.v128.low);
    MovMem64(e, addr + 8, i->src2.value->constant.v128.high);
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Context
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_LOAD_CONTEXT, [](X64Emitter& e, Instr*& i) {
  auto addr = e.rcx + i->src1.offset;
  if (i->Match(SIG_TYPE_I8, SIG_TYPE_IGNORE)) {
    Reg8 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.byte[addr]);
    e.EndOp(dest);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8b, dest);
    CallNative(e, TraceContextLoadI8);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_IGNORE)) {
    Reg16 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.word[addr]);
    e.EndOp(dest);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8w, dest);
    CallNative(e, TraceContextLoadI16);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_IGNORE)) {
    Reg32 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.dword[addr]);
    e.EndOp(dest);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8d, dest);
    CallNative(e, TraceContextLoadI32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_IGNORE)) {
    Reg64 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.qword[addr]);
    e.EndOp(dest);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8, dest);
    CallNative(e, TraceContextLoadI64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_F32, SIG_TYPE_IGNORE)) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.movss(dest, e.dword[addr]);
    e.EndOp(dest);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.lea(e.r8, Stash(e, dest));
    CallNative(e, TraceContextLoadF32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_F64, SIG_TYPE_IGNORE)) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.movsd(dest, e.qword[addr]);
    e.EndOp(dest);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.lea(e.r8, Stash(e, dest));
    CallNative(e, TraceContextLoadF64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_IGNORE)) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    // NOTE: we always know we are aligned.
    e.movaps(dest, e.ptr[addr]);
    e.EndOp(dest);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.lea(e.r8, Stash(e, dest));
    CallNative(e, TraceContextLoadV128);
#endif  // DTRACE
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_STORE_CONTEXT, [](X64Emitter& e, Instr*& i) {
  auto addr = e.rcx + i->src1.offset;
  if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I8)) {
    Reg8 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.byte[addr], src);
    e.EndOp(src);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8b, src);
    CallNative(e, TraceContextStoreI8);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I8C)) {
    e.mov(e.byte[addr], i->src2.value->constant.i8);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8b, i->src2.value->constant.i8);
    CallNative(e, TraceContextStoreI8);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I16)) {
    Reg16 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.word[addr], src);
    e.EndOp(src);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8w, src);
    CallNative(e, TraceContextStoreI16);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I16C)) {
    e.mov(e.word[addr], i->src2.value->constant.i16);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8w, i->src2.value->constant.i16);
    CallNative(e, TraceContextStoreI16);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I32)) {
    Reg32 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.dword[addr], src);
    e.EndOp(src);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8d, src);
    CallNative(e, TraceContextStoreI32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I32C)) {
    e.mov(e.dword[addr], i->src2.value->constant.i32);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8d, i->src2.value->constant.i32);
    CallNative(e, TraceContextStoreI32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I64)) {
    Reg64 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.qword[addr], src);
    e.EndOp(src);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8, src);
    CallNative(e, TraceContextStoreI64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I64C)) {
    MovMem64(e, addr, i->src2.value->constant.i64);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.r8, i->src2.value->constant.i64);
    CallNative(e, TraceContextStoreI64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F32)) {
    Xmm src;
    e.BeginOp(i->src2.value, src, 0);
    e.movss(e.dword[addr], src);
    e.EndOp(src);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.lea(e.r8, Stash(e, src));
    CallNative(e, TraceContextStoreF32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F32C)) {
    e.mov(e.dword[addr], i->src2.value->constant.i32);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.eax, i->src2.value->constant.i32);
    e.vmovd(e.xmm0, e.eax);
    e.lea(e.r8, Stash(e, e.xmm0));
    CallNative(e, TraceContextStoreF32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F64)) {
    Xmm src;
    e.BeginOp(i->src2.value, src, 0);
    e.movsd(e.qword[addr], src);
    e.EndOp(src);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.lea(e.r8, Stash(e, src));
    CallNative(e, TraceContextStoreF64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F64C)) {
    MovMem64(e, addr, i->src2.value->constant.i64);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.mov(e.rax, i->src2.value->constant.i64);
    e.vmovq(e.xmm0, e.rax);
    e.lea(e.r8, Stash(e, e.xmm0));
    CallNative(e, TraceContextStoreF64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_V128)) {
    Xmm src;
    e.BeginOp(i->src2.value, src, 0);
    // NOTE: we always know we are aligned.
    e.movaps(e.ptr[addr], src);
    e.EndOp(src);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.lea(e.r8, Stash(e, src));
    CallNative(e, TraceContextStoreV128);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_V128C)) {
    // TODO(benvanik): check zero
    // TODO(benvanik): correct order?
    MovMem64(e, addr, i->src2.value->constant.v128.low);
    MovMem64(e, addr + 8, i->src2.value->constant.v128.high);
#if DTRACE
    e.mov(e.rdx, i->src1.offset);
    e.lea(e.r8, e.ptr[addr]);
    CallNative(e, TraceContextStoreV128);
#endif  // DTRACE
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Memory
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_LOAD, [](X64Emitter& e, Instr*& i) {
  // If this is a constant address load, check to see if it's in a register
  // range. We'll also probably want a dynamic check for unverified loads.
  // So far, most games use constants.
  if (i->src1.value->IsConstant()) {
    uint64_t address = i->src1.value->AsUint64();
    auto cbs = e.runtime()->access_callbacks();
    while (cbs) {
      if (cbs->handles(cbs->context, address)) {
        // Eh, hacking lambdas.
        i->src3.offset = (uint64_t)cbs;
        IntUnaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src) {
          auto cbs = (RegisterAccessCallbacks*)i.src3.offset;
          e.mov(e.rcx, (uint64_t)cbs->context);
          e.mov(e.rdx, i.src1.value->AsUint64());
          CallNative(e, cbs->read);
          switch (i.dest->type) {
          case INT8_TYPE:
            break;
          case INT16_TYPE:
            e.xchg(e.al, e.ah);
            break;
          case INT32_TYPE:
            e.bswap(e.eax);
            break;
          case INT64_TYPE:
            e.bswap(e.rax);
            break;
          default: ASSERT_INVALID_TYPE(); break;
          }
          e.mov(dest_src, e.rax);
        });
        i = e.Advance(i);
        return true;
      }
      cbs = cbs->next;
    }
  }

  // mov reg, [membase + address.32]
  if (i->src1.value->IsConstant()) {
    e.mov(e.eax, i->src1.value->AsUint32());
  } else {
    Reg64 addr_off;
    e.BeginOp(i->src1.value, addr_off, 0);
    e.mov(e.eax, addr_off.cvt32()); // trunc to 32bits
    e.EndOp(addr_off);
  }
  auto addr = e.rdx + e.rax;

#if DYNAMIC_REGISTER_ACCESS_CHECK
  e.inLocalLabel();
  // if ((address & 0xFF000000) == 0x7F000000) do check;
  e.lea(e.r8d, e.ptr[addr]);
  e.and(e.r8d, 0xFF000000);
  e.cmp(e.r8d, 0x7F000000);
  e.jne(".normal_addr");
  if (IsIntType(i->dest->type)) {
    e.mov(e.rdx, e.rax);
    CallNative(e, DynamicRegisterLoad);
    Reg64 dyn_dest;
    e.BeginOp(i->dest, dyn_dest, REG_DEST);
    switch (i->dest->type) {
    case INT8_TYPE:
      e.movzx(dyn_dest, e.al);
      break;
    case INT16_TYPE:
      e.xchg(e.al, e.ah);
      e.movzx(dyn_dest, e.ax);
      break;
    case INT32_TYPE:
      e.bswap(e.eax);
      e.mov(dyn_dest.cvt32(), e.eax);
      break;
    case INT64_TYPE:
      e.bswap(e.rax);
      e.mov(dyn_dest, e.rax);
      break;
    default:
      e.db(0xCC);
      break;
    }
    e.EndOp(dyn_dest);
  } else {
    e.db(0xCC);
  }
  e.jmp(".skip_access");
  e.L(".normal_addr");
#endif  // DYNAMIC_REGISTER_ACCESS_CHECK

  if (i->Match(SIG_TYPE_I8, SIG_TYPE_IGNORE)) {
    Reg8 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.byte[addr]);
    e.EndOp(dest);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8b, dest);
    CallNative(e, TraceMemoryLoadI8);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_IGNORE)) {
    Reg16 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.word[addr]);
    e.EndOp(dest);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8w, dest);
    CallNative(e, TraceMemoryLoadI16);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_IGNORE)) {
    Reg32 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.dword[addr]);
    e.EndOp(dest);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8d, dest);
    CallNative(e, TraceMemoryLoadI32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_IGNORE)) {
    Reg64 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.mov(dest, e.qword[addr]);
    e.EndOp(dest);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8, dest);
    CallNative(e, TraceMemoryLoadI64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_F32, SIG_TYPE_IGNORE)) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.movss(dest, e.dword[addr]);
    e.EndOp(dest);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.lea(e.r8, Stash(e, dest));
    CallNative(e, TraceMemoryLoadF32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_F64, SIG_TYPE_IGNORE)) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.movsd(dest, e.qword[addr]);
    e.EndOp(dest);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.lea(e.r8, Stash(e, dest));
    CallNative(e, TraceMemoryLoadF64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_IGNORE)) {
    Xmm dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    // TODO(benvanik): we should try to stick to movaps if possible.
    e.movups(dest, e.ptr[addr]);
    e.EndOp(dest);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.lea(e.r8, Stash(e, dest));
    CallNative(e, TraceMemoryLoadV128);
#endif  // DTRACE
  } else {
    ASSERT_INVALID_TYPE();
  }

#if DYNAMIC_REGISTER_ACCESS_CHECK
  e.L(".skip_access");
  e.outLocalLabel();
#endif  // DYNAMIC_REGISTER_ACCESS_CHECK

  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_STORE, [](X64Emitter& e, Instr*& i) {
  // If this is a constant address store, check to see if it's in a
  // register range. We'll also probably want a dynamic check for
  // unverified stores. So far, most games use constants.
  if (i->src1.value->IsConstant()) {
    uint64_t address = i->src1.value->AsUint64();
    auto cbs = e.runtime()->access_callbacks();
    while (cbs) {
      if (cbs->handles(cbs->context, address)) {
        e.mov(e.rcx, (uint64_t)cbs->context);
        e.mov(e.rdx, address);
        if (i->src2.value->IsConstant()) {
          e.mov(e.r8, i->src2.value->AsUint64());
        } else {
          Reg64 src2;
          e.BeginOp(i->src2.value, src2, 0);
          switch (i->src2.value->type) {
          case INT8_TYPE:
            e.movzx(e.r8d, src2.cvt8());
            break;
          case INT16_TYPE:
            e.movzx(e.rax, src2.cvt16());
            e.xchg(e.al, e.ah);
            e.mov(e.r8, e.rax);
            break;
          case INT32_TYPE:
            e.movzx(e.r8, src2.cvt32());
            e.bswap(e.r8d);
            break;
          case INT64_TYPE:
            e.mov(e.r8, src2);
            e.bswap(e.r8);
            break;
          default: ASSERT_INVALID_TYPE(); break;
          }
          e.EndOp(src2);
        }
        CallNative(e, cbs->write);
        i = e.Advance(i);
        return true;
      }
      cbs = cbs->next;
    }
  }

  // mov [membase + address.32], reg
  if (i->src1.value->IsConstant()) {
    e.mov(e.eax, i->src1.value->AsUint32());
  } else {
    Reg64 addr_off;
    e.BeginOp(i->src1.value, addr_off, 0);
    e.mov(e.eax, addr_off.cvt32()); // trunc to 32bits
    e.EndOp(addr_off);
  }
  auto addr = e.rdx + e.rax;

#if DYNAMIC_REGISTER_ACCESS_CHECK
  // if ((address & 0xFF000000) == 0x7F000000) do check;
  e.lea(e.r8d, e.ptr[addr]);
  e.and(e.r8d, 0xFF000000);
  e.cmp(e.r8d, 0x7F000000);
  e.inLocalLabel();
  e.jne(".normal_addr");
  if (IsIntType(i->src2.value->type)) {
    Reg64 dyn_src;
    e.BeginOp(i->src2.value, dyn_src, 0);
    switch (i->src2.value->type) {
    case INT8_TYPE:
      e.movzx(e.r8, dyn_src.cvt8());
      break;
    case INT16_TYPE:
      e.movzx(e.rax, dyn_src.cvt16());
      e.xchg(e.al, e.ah);
      e.mov(e.r8, e.rax);
      break;
    case INT32_TYPE:
      e.mov(e.r8d, dyn_src.cvt32());
      e.bswap(e.r8d);
      break;
    case INT64_TYPE:
      e.mov(e.r8, dyn_src);
      e.bswap(e.r8);
      break;
    default:
      e.db(0xCC);
      break;
    }
    e.EndOp(dyn_src);
    e.mov(e.rdx, e.rax);
    CallNative(e, DynamicRegisterStore);
  } else {
    e.db(0xCC);
  }
  e.jmp(".skip_access");
  e.L(".normal_addr");
#endif  // DYNAMIC_REGISTER_ACCESS_CHECK

  if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I8)) {
    Reg8 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.byte[addr], src);
    e.EndOp(src);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8b, src);
    CallNative(e, TraceMemoryStoreI8);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I8C)) {
    e.mov(e.byte[addr], i->src2.value->constant.i8);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8b, i->src2.value->constant.i8);
    CallNative(e, TraceMemoryStoreI8);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I16)) {
    Reg16 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.word[addr], src);
    e.EndOp(src);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8w, src);
    CallNative(e, TraceMemoryStoreI16);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I16C)) {
    e.mov(e.word[addr], i->src2.value->constant.i16);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8w, i->src2.value->constant.i16);
    CallNative(e, TraceMemoryStoreI16);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I32)) {
    Reg32 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.dword[addr], src);
    e.EndOp(src);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8d, src);
    CallNative(e, TraceMemoryStoreI32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I32C)) {
    e.mov(e.dword[addr], i->src2.value->constant.i32);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8d, i->src2.value->constant.i32);
    CallNative(e, TraceMemoryStoreI32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I64)) {
    Reg64 src;
    e.BeginOp(i->src2.value, src, 0);
    e.mov(e.qword[addr], src);
    e.EndOp(src);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8, src);
    CallNative(e, TraceMemoryStoreI64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I64C)) {
    MovMem64(e, addr, i->src2.value->constant.i64);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.r8, i->src2.value->constant.i64);
    CallNative(e, TraceMemoryStoreI64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F32)) {
    Xmm src;
    e.BeginOp(i->src2.value, src, 0);
    e.movss(e.dword[addr], src);
    e.EndOp(src);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.lea(e.r8, Stash(e, src));
    CallNative(e, TraceMemoryStoreF32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F32C)) {
    e.mov(e.dword[addr], i->src2.value->constant.i32);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.mov(e.eax, i->src2.value->constant.i32);
    e.vmovd(e.xmm0, e.eax);
    e.lea(e.r8, Stash(e, e.xmm0));
    CallNative(e, TraceMemoryStoreF32);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F64)) {
    Xmm src;
    e.BeginOp(i->src2.value, src, 0);
    e.movsd(e.qword[addr], src);
    e.EndOp(src);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.lea(e.r8, Stash(e, src));
    CallNative(e, TraceMemoryStoreF64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F64C)) {
    MovMem64(e, addr, i->src2.value->constant.i64);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.movsd(e.xmm0, e.ptr[addr]);
    CallNative(e, TraceMemoryStoreF64);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_V128)) {
    Xmm src;
    e.BeginOp(i->src2.value, src, 0);
    // TODO(benvanik): we should try to stick to movaps if possible.
    e.movups(e.ptr[addr], src);
    e.EndOp(src);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.lea(e.r8, Stash(e, src));
    CallNative(e, TraceMemoryStoreV128);
#endif  // DTRACE
  } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_V128C)) {
    // TODO(benvanik): check zero
    // TODO(benvanik): correct order?
    MovMem64(e, addr, i->src2.value->constant.v128.low);
    MovMem64(e, addr + 8, i->src2.value->constant.v128.high);
#if DTRACE
    e.lea(e.rdx, e.ptr[addr]);
    e.lea(e.r8, e.ptr[addr]);
    CallNative(e, TraceMemoryStoreV128);
#endif  // DTRACE
  } else {
    ASSERT_INVALID_TYPE();
  }

#if DYNAMIC_REGISTER_ACCESS_CHECK
  e.L(".skip_access");
  e.outLocalLabel();
#endif  // DYNAMIC_REGISTER_ACCESS_CHECK

  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_PREFETCH, [](X64Emitter& e, Instr*& i) {
  UNIMPLEMENTED_SEQ();
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Comparisons
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_MAX, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else if (IsFloatType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      if (i.src1.value->type == FLOAT32_TYPE) {
        e.maxss(dest_src, src);
      } else {
        e.maxsd(dest_src, src);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      e.maxps(dest_src, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_MIN, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else if (IsFloatType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      if (i.src1.value->type == FLOAT32_TYPE) {
        e.minss(dest_src, src);
      } else {
        e.minsd(dest_src, src);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      e.minps(dest_src, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_SELECT, [](X64Emitter& e, Instr*& i) {
  CheckBoolean(e, i->src1.value);
  if (IsIntType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else if (IsFloatType(i->dest->type) || IsVecType(i->dest->type)) {
    Xmm dest, src2, src3;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src2.value, src2, 0,
              i->src3.value, src3, 0);
    // TODO(benvanik): find a way to do this without branches.
    e.inLocalLabel();
    e.movaps(dest, src3);
    e.jz(".skip");
    e.movaps(dest, src2);
    e.L(".skip");
    e.outLocalLabel();
    e.EndOp(dest, src2, src3);
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_IS_TRUE, [](X64Emitter& e, Instr*& i) {
  CheckBoolean(e, i->src1.value);
  Reg8 dest;
  e.BeginOp(i->dest, dest, REG_DEST);
  e.setnz(dest);
  e.EndOp(dest);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_IS_FALSE, [](X64Emitter& e, Instr*& i) {
  CheckBoolean(e, i->src1.value);
  Reg8 dest;
  e.BeginOp(i->dest, dest, REG_DEST);
  e.setz(dest);
  e.EndOp(dest);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_COMPARE_EQ, [](X64Emitter& e, Instr*& i) {
  CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
    if (!invert) {
      e.sete(dest);
    } else {
      e.setne(dest);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_COMPARE_NE, [](X64Emitter& e, Instr*& i) {
  CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
    if (!invert) {
      e.setne(dest);
    } else {
      e.sete(dest);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_COMPARE_SLT, [](X64Emitter& e, Instr*& i) {
  CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
    if (!invert) {
      e.setl(dest);
    } else {
      e.setge(dest);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_COMPARE_SLE, [](X64Emitter& e, Instr*& i) {
  CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
    if (!invert) {
      e.setle(dest);
    } else {
      e.setg(dest);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_COMPARE_SGT, [](X64Emitter& e, Instr*& i) {
  CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
    if (!invert) {
      e.setg(dest);
    } else {
      e.setle(dest);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_COMPARE_SGE, [](X64Emitter& e, Instr*& i) {
  CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
    if (!invert) {
      e.setge(dest);
    } else {
      e.setl(dest);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_COMPARE_ULT, [](X64Emitter& e, Instr*& i) {
  CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
    if (!invert) {
      e.setb(dest);
    } else {
      e.setae(dest);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_COMPARE_ULE, [](X64Emitter& e, Instr*& i) {
  CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
    if (!invert) {
      e.setbe(dest);
    } else {
      e.seta(dest);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_COMPARE_UGT, [](X64Emitter& e, Instr*& i) {
  CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
    if (!invert) {
      e.seta(dest);
    } else {
      e.setbe(dest);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_COMPARE_UGE, [](X64Emitter& e, Instr*& i) {
  CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
    if (!invert) {
      e.setae(dest);
    } else {
      e.setb(dest);
    }
  });
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_DID_CARRY, [](X64Emitter& e, Instr*& i) {
  Reg8 dest;
  e.BeginOp(i->dest, dest, REG_DEST);
  LoadEflags(e);
  e.setc(dest);
  e.EndOp(dest);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_DID_OVERFLOW, [](X64Emitter& e, Instr*& i) {
  Reg8 dest;
  e.BeginOp(i->dest, dest, REG_DEST);
  LoadEflags(e);
  e.seto(dest);
  e.EndOp(dest);
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_DID_SATURATE, [](X64Emitter& e, Instr*& i) {
  UNIMPLEMENTED_SEQ();
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_COMPARE_EQ, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    VectorCompareXX(e, i, VECTOR_CMP_EQ, true);
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_COMPARE_SGT, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    VectorCompareXX(e, i, VECTOR_CMP_GT, true);
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_COMPARE_SGE, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    VectorCompareXX(e, i, VECTOR_CMP_GE, true);
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_COMPARE_UGT, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    VectorCompareXX(e, i, VECTOR_CMP_GT, false);
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_COMPARE_UGE, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    VectorCompareXX(e, i, VECTOR_CMP_GE, false);
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Math
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_ADD, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      e.add(dest_src, src);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      e.add(dest_src, src);
    });
  } else if (IsFloatType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      if (i.src1.value->type == FLOAT32_TYPE) {
        e.addss(dest_src, src);
      } else {
        e.addsd(dest_src, src);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      e.addps(dest_src, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_ADD_CARRY, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    // dest = src1 + src2 + src3.i8
    IntTernaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src2, const Operand& src3) {
      Reg8 src3_8(src3.getIdx());
      if (src3.getIdx() <= 4) {
        e.mov(e.ah, src3_8);
      } else {
        e.mov(e.al, src3_8);
        e.mov(e.ah, e.al);
      }
      e.sahf();
      e.adc(dest_src, src2);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src2, uint32_t src3) {
      e.mov(e.eax, src3);
      e.mov(e.ah, e.al);
      e.sahf();
      e.adc(dest_src, src2);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src2, const Operand& src3) {
      Reg8 src3_8(src3.getIdx());
      if (src3.getIdx() <= 4) {
        e.mov(e.ah, src3_8);
      } else {
        e.mov(e.al, src3_8);
        e.mov(e.ah, e.al);
      }
      e.sahf();
      e.adc(dest_src, src2);
    });
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_ADD, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    if (i->flags == INT8_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->flags == INT16_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->flags == INT32_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->flags == FLOAT32_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_SUB, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      if (i.flags & ARITHMETIC_SET_CARRY) {
        auto Nax = LIKE_REG(e.rax, src);
        e.mov(Nax, src);
        e.not(Nax);
        e.stc();
        e.adc(dest_src, Nax);
      } else {
        e.sub(dest_src, src);
      }
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      if (i.flags & ARITHMETIC_SET_CARRY) {
        auto Nax = LIKE_REG(e.rax, dest_src);
        e.mov(Nax, src);
        e.not(Nax);
        e.stc();
        e.adc(dest_src, Nax);
      } else {
        e.sub(dest_src, src);
      }
    });
  } else if (IsFloatType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      if (i.src1.value->type == FLOAT32_TYPE) {
        e.subss(dest_src, src);
      } else {
        e.subsd(dest_src, src);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      e.subps(dest_src, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_MUL, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      // RAX = value, RDX = clobbered
      // TODO(benvanik): make the register allocator put dest_src in RAX?
      auto Nax = LIKE_REG(e.rax, dest_src);
      e.mov(Nax, dest_src);
      if (i.flags & ARITHMETIC_UNSIGNED) {
        e.mul(src);
      } else {
        e.imul(src);
      }
      e.mov(dest_src, Nax);
      ReloadRDX(e);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      // RAX = value, RDX = clobbered
      // TODO(benvanik): make the register allocator put dest_src in RAX?
      auto Nax = LIKE_REG(e.rax, dest_src);
      auto Ndx = LIKE_REG(e.rdx, dest_src);
      e.mov(Nax, dest_src);
      e.mov(Ndx, src);
      if (i.flags & ARITHMETIC_UNSIGNED) {
        e.mul(Ndx);
      } else {
        e.imul(Ndx);
      }
      e.mov(dest_src, Nax);
      ReloadRDX(e);
    });
  } else if (IsFloatType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      if (i.flags & ARITHMETIC_UNSIGNED) { UNIMPLEMENTED_SEQ(); }
      if (i.src1.value->type == FLOAT32_TYPE) {
        e.mulss(dest_src, src);
      } else {
        e.mulsd(dest_src, src);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      if (i.flags & ARITHMETIC_UNSIGNED) { UNIMPLEMENTED_SEQ(); }
      e.mulps(dest_src, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_MUL_HI, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      // RAX = value, RDX = clobbered
      // TODO(benvanik): make the register allocator put dest_src in RAX?
      auto Nax = LIKE_REG(e.rax, dest_src);
      auto Ndx = LIKE_REG(e.rdx, dest_src);
      e.mov(Nax, dest_src);
      if (i.flags & ARITHMETIC_UNSIGNED) {
        e.mul(src);
      } else {
        e.imul(src);
      }
      e.mov(dest_src, Ndx);
      ReloadRDX(e);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      // RAX = value, RDX = clobbered
      // TODO(benvanik): make the register allocator put dest_src in RAX?
      auto Nax = LIKE_REG(e.rax, dest_src);
      auto Ndx = LIKE_REG(e.rdx, dest_src);
      e.mov(Nax, dest_src);
      e.mov(Ndx, src);
      if (i.flags & ARITHMETIC_UNSIGNED) {
        e.mul(Ndx);
      } else {
        e.imul(Ndx);
      }
      e.mov(dest_src, Ndx);
      ReloadRDX(e);
    });
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_DIV, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      // RAX = value, RDX = clobbered
      // TODO(benvanik): make the register allocator put dest_src in RAX?
      auto Nax = LIKE_REG(e.rax, dest_src);
      auto Ndx = LIKE_REG(e.rdx, dest_src);
      e.mov(Nax, dest_src);
      e.xor(Ndx, Ndx);
      if (i.flags & ARITHMETIC_UNSIGNED) {
        e.div(src);
      } else {
        e.idiv(src);
      }
      e.mov(dest_src, Nax);
      ReloadRDX(e);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      // RAX = value, RDX = clobbered
      // TODO(benvanik): make the register allocator put dest_src in RAX?
      auto Nax = LIKE_REG(e.rax, dest_src);
      auto Ndx = LIKE_REG(e.rdx, dest_src);
      e.mov(Nax, dest_src);
      e.mov(Ndx, src);
      if (i.flags & ARITHMETIC_UNSIGNED) {
        e.div(Ndx);
      } else {
        e.idiv(Ndx);
      }
      e.mov(dest_src, Nax);
      ReloadRDX(e);
    });
  } else if (IsFloatType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      if (i.flags & ARITHMETIC_UNSIGNED) { UNIMPLEMENTED_SEQ(); }
      if (i.src1.value->type == FLOAT32_TYPE) {
        e.divss(dest_src, src);
      } else {
        e.divsd(dest_src, src);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      if (i.flags & ARITHMETIC_UNSIGNED) { UNIMPLEMENTED_SEQ(); }
      e.divps(dest_src, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_MUL_ADD, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else if (IsFloatType(i->dest->type)) {
    XmmTernaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src2, const Xmm& src3) {
      if (i.dest->type == FLOAT32_TYPE) {
        e.vfmadd132ss(dest_src, src3, src2);
      } else {
        e.vfmadd132sd(dest_src, src3, src2);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmTernaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src2, const Xmm& src3) {
      e.vfmadd132ps(dest_src, src3, src2);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_MUL_SUB, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else if (IsFloatType(i->dest->type)) {
    XmmTernaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src2, const Xmm& src3) {
      if (i.dest->type == FLOAT32_TYPE) {
        e.vfmsub132ss(dest_src, src3, src2);
      } else {
        e.vfmsub132sd(dest_src, src3, src2);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmTernaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src2, const Xmm& src3) {
      e.vfmsub132ps(dest_src, src3, src2);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_NEG, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntUnaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src) {
      e.neg(dest_src);
    });
  } else if (IsFloatType(i->dest->type)) {
    XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      if (i.src1.value->type == FLOAT32_TYPE) {
        e.mov(e.rax, XMMCONSTBASE);
        e.vpxor(dest, src, XMMCONST(e.rax, XMMSignMaskPS));
      } else {
        e.mov(e.rax, XMMCONSTBASE);
        e.vpxor(dest, src, XMMCONST(e.rax, XMMSignMaskPD));
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      e.mov(e.rax, XMMCONSTBASE);
      e.vpxor(dest, src, XMMCONST(e.rax, XMMSignMaskPS));
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_ABS, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else if (IsFloatType(i->dest->type)) {
    XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      if (i.src1.value->type == FLOAT32_TYPE) {
        e.mov(e.rax, XMMCONSTBASE);
        e.movaps(e.xmm0, XMMCONST(e.rax, XMMSignMaskPS));
        e.vpandn(dest, e.xmm0, src);
      } else {
        e.mov(e.rax, XMMCONSTBASE);
        e.movaps(e.xmm0, XMMCONST(e.rax, XMMSignMaskPD));;
        e.vpandn(dest, e.xmm0, src);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      e.mov(e.rax, XMMCONSTBASE);
      e.movaps(e.xmm0, XMMCONST(e.rax, XMMSignMaskPS));;
      e.vpandn(dest, e.xmm0, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_SQRT, [](X64Emitter& e, Instr*& i) {
  if (IsFloatType(i->dest->type)) {
    XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      if (i.dest->type == FLOAT32_TYPE) {
        e.sqrtss(dest, src);
      } else {
        e.sqrtsd(dest, src);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      e.sqrtps(dest, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_RSQRT, [](X64Emitter& e, Instr*& i) {
  if (IsFloatType(i->dest->type)) {
    XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      if (i.dest->type == FLOAT32_TYPE) {
        e.rsqrtss(dest, src);
      } else {
        e.cvtsd2ss(dest, src);
        e.rsqrtss(dest, dest);
        e.cvtss2sd(dest, dest);
      }
    });
  } else if (IsVecType(i->dest->type)) {
    XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      e.rsqrtps(dest, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_POW2, [](X64Emitter& e, Instr*& i) {
  if (IsFloatType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else if (IsVecType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_LOG2, [](X64Emitter& e, Instr*& i) {
  if (IsFloatType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else if (IsVecType(i->dest->type)) {
    UNIMPLEMENTED_SEQ();
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_DOT_PRODUCT_3, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->src1.value->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      // http://msdn.microsoft.com/en-us/library/bb514054(v=vs.90).aspx
      // TODO(benvanik): verify ordering
      e.dpps(dest_src, src, B01110001);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_DOT_PRODUCT_4, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->src1.value->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      // http://msdn.microsoft.com/en-us/library/bb514054(v=vs.90).aspx
      // TODO(benvanik): verify ordering
      e.dpps(dest_src, src, B11110001);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_AND, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      e.and(dest_src, src);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      e.and(dest_src, src);
    });
  } else if (IsVecType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      e.pand(dest_src, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_OR, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      e.or(dest_src, src);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      e.or(dest_src, src);
    });
  } else if (IsVecType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      e.por(dest_src, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_XOR, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      e.xor(dest_src, src);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      e.xor(dest_src, src);
    });
  } else if (IsVecType(i->dest->type)) {
    XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
      e.pxor(dest_src, src);
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_NOT, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntUnaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src) {
      e.not(dest_src);
    });
  } else if (IsVecType(i->dest->type)) {
    XmmUnaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      // dest_src ^= 0xFFFF...
      if (dest.getIdx() != src.getIdx()) {
        e.movaps(dest, src);
      }
      e.mov(e.rax, XMMCONSTBASE);
      e.pxor(dest, XMMCONST(e.rax, XMMOne));
    });
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_SHL, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    // TODO(benvanik): use shlx if available.
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      // Can only shl by cl. Eww x86.
      Reg8 shamt(src.getIdx());
      e.mov(e.rax, e.rcx);
      e.mov(e.cl, shamt);
      e.shl(dest_src, e.cl);
      e.mov(e.rcx, e.rax);
      // BeaEngine can't disasm this, boo.
      /*Reg32e dest_src_e(dest_src.getIdx(), MAX(dest_src.getBit(), 32));
      Reg32e src_e(src.getIdx(), MAX(dest_src.getBit(), 32));
      e.and(src_e, 0x3F);
      e.shlx(dest_src_e, dest_src_e, src_e);*/
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      e.shl(dest_src, src);
    });
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_SHR, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    // TODO(benvanik): use shrx if available.
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      // Can only sar by cl. Eww x86.
      Reg8 shamt(src.getIdx());
      e.mov(e.rax, e.rcx);
      e.mov(e.cl, shamt);
      e.shr(dest_src, e.cl);
      e.mov(e.rcx, e.rax);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      e.shr(dest_src, src);
    });
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_SHA, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    // TODO(benvanik): use sarx if available.
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      // Can only sar by cl. Eww x86.
      Reg8 shamt(src.getIdx());
      e.mov(e.rax, e.rcx);
      e.mov(e.cl, shamt);
      e.sar(dest_src, e.cl);
      e.mov(e.rcx, e.rax);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      e.sar(dest_src, src);
    });
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_SHL, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    if (i->flags == INT8_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->flags == INT16_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->flags == INT32_TYPE) {
      XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
        // src shift mask may have values >31, and x86 sets to zero when
        // that happens so we mask.
        e.mov(e.eax, 0x1F);
        e.vmovd(e.xmm0, e.eax);
        e.vpbroadcastd(e.xmm0, e.xmm0);
        e.vandps(e.xmm0, src, e.xmm0);
        e.vpsllvd(dest_src, dest_src, e.xmm0);
      });
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_SHR, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    if (i->flags == INT8_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->flags == INT16_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->flags == INT32_TYPE) {
      XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
        // src shift mask may have values >31, and x86 sets to zero when
        // that happens so we mask.
        e.mov(e.eax, 0x1F);
        e.vmovd(e.xmm0, e.eax);
        e.vpbroadcastd(e.xmm0, e.xmm0);
        e.vandps(e.xmm0, src, e.xmm0);
        e.vpsrlvd(dest_src, dest_src, src);
      });
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_VECTOR_SHA, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    if (i->flags == INT8_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->flags == INT16_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->flags == INT32_TYPE) {
      XmmBinaryOp(e, i, i->flags, [](X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src) {
        // src shift mask may have values >31, and x86 sets to zero when
        // that happens so we mask.
        e.mov(e.eax, 0x1F);
        e.vmovd(e.xmm0, e.eax);
        e.vpbroadcastd(e.xmm0, e.xmm0);
        e.vandps(e.xmm0, src, e.xmm0);
        e.vpsravd(dest_src, dest_src, src);
      });
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_ROTATE_LEFT, [](X64Emitter& e, Instr*& i) {
  if (IsIntType(i->dest->type)) {
    IntBinaryOp(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
      // Can only rol by cl. Eww x86.
      Reg8 shamt(src.getIdx());
      e.mov(e.rax, e.rcx);
      e.mov(e.cl, shamt);
      e.rol(dest_src, e.cl);
      e.mov(e.rcx, e.rax);
    }, [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
      e.rol(dest_src, src);
    });
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_BYTE_SWAP, [](X64Emitter& e, Instr*& i) {
  if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16)) {
    Reg16 dest, src1;
    // TODO(benvanik): fix register allocator to put the value in ABCD
    //e.BeginOp(i->dest, d, REG_DEST | REG_ABCD,
    //          i->src1.value, s1, 0);
    //if (d != s1) {
    //  e.mov(d, s1);
    //  e.xchg(d.cvt8(), Reg8(d.getIdx() + 4));
    //} else {
    //  e.xchg(d.cvt8(), Reg8(d.getIdx() + 4));
    //}
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    e.mov(e.ax, src1);
    e.xchg(e.ah, e.al);
    e.mov(dest, e.ax);
    e.EndOp(dest, src1);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32)) {
    Reg32 dest, src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    if (dest.getIdx() != src1.getIdx()) {
      e.mov(dest, src1);
      e.bswap(dest);
    } else {
      e.bswap(dest);
    }
    e.EndOp(dest, src1);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64)) {
    Reg64 dest, src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    if (dest.getIdx() != src1.getIdx()) {
      e.mov(dest, src1);
      e.bswap(dest);
    } else {
      e.bswap(dest);
    }
    e.EndOp(dest, src1);
  } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_V128)) {
    Xmm dest, src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    // TODO(benvanik): find a way to do this without the memory load.
    e.mov(e.rax, XMMCONSTBASE);
    e.vpshufb(dest, src1, XMMCONST(e.rax, XMMByteSwapMask));
    e.EndOp(dest, src1);
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_CNTLZ, [](X64Emitter& e, Instr*& i) {
  if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I8)) {
    Reg8 dest;
    Reg8 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.bsr(dest.cvt16(), src.cvt16());
    // ZF = 1 if zero
    e.mov(e.eax, 16 ^ 0x7);
    e.cmovz(dest.cvt32(), e.eax);
    e.sub(dest, 8);
    e.xor(dest, 0x7);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16)) {
    Reg8 dest;
    Reg16 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.bsr(dest.cvt16(), src);
    // ZF = 1 if zero
    e.mov(e.eax, 16 ^ 0xF);
    e.cmovz(dest.cvt32(), e.eax);
    e.xor(dest, 0xF);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32)) {
    Reg8 dest;
    Reg32 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.bsr(dest.cvt32(), src);
    // ZF = 1 if zero
    e.mov(e.eax, 32 ^ 0x1F);
    e.cmovz(dest.cvt32(), e.eax);
    e.xor(dest, 0x1F);
    e.EndOp(dest, src);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64)) {
    Reg8 dest;
    Reg64 src;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src, 0);
    e.bsr(dest, src);
    // ZF = 1 if zero
    e.mov(e.eax, 64 ^ 0x3F);
    e.cmovz(dest.cvt32(), e.eax);
    e.xor(dest, 0x3F);
    e.EndOp(dest, src);
  } else {
    UNIMPLEMENTED_SEQ();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_INSERT, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    if (i->src3.value->type == INT8_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->src3.value->type == INT16_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->src3.value->type == INT32_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

// TODO(benvanik): sequence extract/splat:
//  v0.i32 = extract v0.v128, 0
//  v0.v128 = splat v0.i32
// This can be a single broadcast.

table->AddSequence(OPCODE_EXTRACT, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->src1.value->type)) {
    if (i->dest->type == INT8_TYPE) {
      Reg8 dest;
      Xmm src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      if (i->src2.value->IsConstant()) {
        e.pextrb(dest, src, i->src2.value->constant.i8);
      } else {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src);
    } else if (i->dest->type == INT16_TYPE) {
      Reg16 dest;
      Xmm src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      if (i->src2.value->IsConstant()) {
        e.pextrw(dest, src, i->src2.value->constant.i8);
      } else {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src);
    } else if (i->dest->type == INT32_TYPE) {
      if (i->src2.value->IsConstant()) {
        Reg32 dest;
        Xmm src;
        e.BeginOp(i->dest, dest, REG_DEST,
                  i->src1.value, src, 0);
        e.pextrd(dest, src, i->src2.value->constant.i8);
        e.EndOp(dest, src);
      } else {
        Reg32 dest;
        Xmm src;
        Reg8 sel;
        e.BeginOp(i->dest, dest, REG_DEST,
                  i->src1.value, src, 0,
                  i->src2.value, sel, 0);
        // Get the desired word in xmm0, then extract that.
        e.mov(TEMP_REG, sel);
        e.and(TEMP_REG, 0x03);
        e.shl(TEMP_REG, 4);
        e.mov(e.rax, (uintptr_t)extract_table_32);
        e.movaps(e.xmm0, e.ptr[e.rax + TEMP_REG]);
        e.vpshufb(e.xmm0, src, e.xmm0);
        e.pextrd(dest, e.xmm0, 0);
        e.EndOp(dest, src, sel);
      }
    } else if (i->dest->type == FLOAT32_TYPE) {
      Reg32 dest;
      Xmm src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      if (i->src2.value->IsConstant()) {
        e.extractps(dest, src, i->src2.value->constant.i8);
      } else {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src);
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_SPLAT, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    if (i->Match(SIG_TYPE_V128, SIG_TYPE_I8)) {
      Xmm dest;
      Reg8 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.vmovd(e.xmm0, src.cvt32());
      e.vpbroadcastb(dest, e.xmm0);
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_I8C)) {
      Xmm dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i->src1.value->constant.i8);
      e.vmovd(e.xmm0, e.eax);
      e.vpbroadcastb(dest, e.xmm0);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_I16)) {
      Xmm dest;
      Reg16 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.vmovd(e.xmm0, src.cvt32());
      e.vpbroadcastw(dest, e.xmm0);
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_I16C)) {
      Xmm dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i->src1.value->constant.i16);
      e.vmovd(e.xmm0, e.eax);
      e.vpbroadcastw(dest, e.xmm0);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_I32)) {
      Xmm dest;
      Reg32 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.vmovd(e.xmm0, src);
      e.vpbroadcastd(dest, e.xmm0);
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_I32C)) {
      Xmm dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i->src1.value->constant.i32);
      e.vmovd(e.xmm0, e.eax);
      e.vpbroadcastd(dest, e.xmm0);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_F32)) {
      Xmm dest, src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.vbroadcastss(dest, src);
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_F32C)) {
      Xmm dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.mov(e.eax, i->src1.value->constant.i32);
      e.vmovd(e.xmm0, e.eax);
      e.vbroadcastss(dest, e.xmm0);
      e.EndOp(dest);
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_PERMUTE, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    if (i->src1.value->type == INT32_TYPE) {
      // Permute words between src2 and src3.
      // TODO(benvanik): check src3 for zero. if 0, we can use pshufb.
      if (i->src1.value->IsConstant()) {
        uint32_t control = i->src1.value->AsUint32();
        Xmm dest, src2, src3;
        e.BeginOp(i->dest, dest, REG_DEST,
                  i->src2.value, src2, 0,
                  i->src3.value, src3, 0);
        // Shuffle things into the right places in dest & xmm0,
        // then we blend them together.
        uint32_t src_control =
            (((control >> 24) & 0x3) << 0) |
            (((control >> 16) & 0x3) << 2) |
            (((control >> 8) & 0x3) << 4) |
            (((control >> 0) & 0x3) << 6);
        uint32_t blend_control =
            (((control >> 26) & 0x1) << 0) |
            (((control >> 18) & 0x1) << 1) |
            (((control >> 10) & 0x1) << 2) |
            (((control >> 2) & 0x1) << 3);
        if (dest.getIdx() != src3.getIdx()) {
          e.pshufd(dest, src2, src_control);
          e.pshufd(e.xmm0, src3, src_control);
          e.blendps(dest, e.xmm0, blend_control);
        } else {
          e.movaps(e.xmm0, src3);
          e.pshufd(dest, src2, src_control);
          e.pshufd(e.xmm0, e.xmm0, src_control);
          e.blendps(dest, e.xmm0, blend_control);
        }
        e.EndOp(dest, src2, src3);
      } else {
        Reg32 control;
        Xmm dest, src2, src3;
        e.BeginOp(i->dest, dest, REG_DEST,
                  i->src1.value, control, 0,
                  i->src2.value, src2, 0,
                  i->src3.value, src3, 0);
        UNIMPLEMENTED_SEQ();
        e.EndOp(dest, control, src2, src3);
      }
    } else if (i->src1.value->type == VEC128_TYPE) {
      // Permute bytes between src2 and src3.
      if (i->src3.value->IsConstantZero()) {
        // Permuting with src2/zero, so just shuffle/mask.
        Xmm dest, control, src2;
        e.BeginOp(i->dest, dest, REG_DEST,
                  i->src1.value, control, 0,
                  i->src2.value, src2, 0);
        if (i->src2.value->IsConstantZero()) {
          e.vpxor(dest, src2, src2);
        } else {
          if (i->src2.value->IsConstant()) {
            LoadXmmConstant(e, src2, i->src2.value->constant.v128);
          }
          // Control mask needs to be shuffled.
          e.mov(e.rax, XMMCONSTBASE);
          e.vpshufb(e.xmm0, control, XMMCONST(e.rax, XMMByteSwapMask));
          e.vpshufb(dest, src2, e.xmm0);
          // Build a mask with values in src2 having 0 and values in src3 having 1.
          e.vpcmpgtb(e.xmm0, e.xmm0, XMMCONST(e.rax, XMMPermuteControl15));
          e.vpandn(dest, e.xmm0, dest);
        }
        e.EndOp(dest, control, src2);
      } else {
        // General permute.
        Xmm dest, control, src2, src3;
        e.BeginOp(i->dest, dest, REG_DEST,
                  i->src1.value, control, 0,
                  i->src2.value, src2, 0,
                  i->src3.value, src3, 0);
        e.mov(e.rax, XMMCONSTBASE);
        // Control mask needs to be shuffled.
        e.vpshufb(e.xmm1, control, XMMCONST(e.rax, XMMByteSwapMask));
        // Build a mask with values in src2 having 0 and values in src3 having 1.
        e.vpcmpgtb(dest, e.xmm1, XMMCONST(e.rax, XMMPermuteControl15));
        Xmm src2_shuf, src3_shuf;
        if (i->src2.value->IsConstantZero()) {
          e.vpxor(src2, src2);
          src2_shuf = src2;
        } else {
          if (i->src2.value->IsConstant()) {
            LoadXmmConstant(e, src2, i->src2.value->constant.v128);
          }
          src2_shuf = e.xmm0;
          e.vpshufb(src2_shuf, src2, e.xmm1);
        }
        if (i->src3.value->IsConstantZero()) {
          e.vpxor(src3, src3);
          src3_shuf = src3;
        } else {
          if (i->src3.value->IsConstant()) {
            LoadXmmConstant(e, src3, i->src3.value->constant.v128);
          }
          // NOTE: reusing xmm1 here.
          src3_shuf = e.xmm1;
          e.vpshufb(src3_shuf, src3, e.xmm1);
        }
        e.vpblendvb(dest, src2_shuf, src3_shuf, dest);
        e.EndOp(dest, control, src2, src3);
      }
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_SWIZZLE, [](X64Emitter& e, Instr*& i) {
  if (IsVecType(i->dest->type)) {
    // Defined by SWIZZLE_MASK()
    if (i->flags == INT32_TYPE || i->flags == FLOAT32_TYPE) {
      uint8_t swizzle_mask = (uint8_t)i->src2.offset;
      swizzle_mask =
          (((swizzle_mask >> 6) & 0x3) << 0) |
          (((swizzle_mask >> 4) & 0x3) << 2) |
          (((swizzle_mask >> 2) & 0x3) << 4) |
          (((swizzle_mask >> 0) & 0x3) << 6);
      Xmm dest, src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      e.pshufd(dest, src1, swizzle_mask);
      e.EndOp(dest, src1);
    } else {
      UNIMPLEMENTED_SEQ();
    }
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_PACK, [](X64Emitter& e, Instr*& i) {
  if (i->flags == PACK_TYPE_D3DCOLOR) {
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_FLOAT16_2) {
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_FLOAT16_4) {
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_SHORT_2) {
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_S8_IN_16_LO) {
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_S8_IN_16_HI) {
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_S16_IN_32_LO) {
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_S16_IN_32_HI) {
    UNIMPLEMENTED_SEQ();
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_UNPACK, [](X64Emitter& e, Instr*& i) {
  if (i->flags == PACK_TYPE_D3DCOLOR) {
    // ARGB (WXYZ) -> RGBA (XYZW)
    // XMLoadColor
    // int32_t src = (int32_t)src1.iw;
    // dest.f4[0] = (float)((src >> 16) & 0xFF) * (1.0f / 255.0f);
    // dest.f4[1] = (float)((src >> 8) & 0xFF) * (1.0f / 255.0f);
    // dest.f4[2] = (float)(src & 0xFF) * (1.0f / 255.0f);
    // dest.f4[3] = (float)((src >> 24) & 0xFF) * (1.0f / 255.0f);
    XmmUnaryOp(e, i, 0, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      // src = ZZYYXXWW
      // unpack to 000000ZZ,000000YY,000000XX,000000WW
      e.mov(e.rax, XMMCONSTBASE);
      e.vpshufb(dest, src, XMMCONST(e.rax, XMMUnpackD3DCOLOR));
      // mult by 1/255
      e.vmulps(dest, XMMCONST(e.rax, XMMOneOver255));
    });
  } else if (i->flags == PACK_TYPE_FLOAT16_2) {
    // 1 bit sign, 5 bit exponent, 10 bit mantissa
    // D3D10 half float format
    // TODO(benvanik): http://blogs.msdn.com/b/chuckw/archive/2012/09/11/directxmath-f16c-and-fma.aspx
    // Use _mm_cvtph_ps -- requires very modern processors (SSE5+)
    // Unpacking half floats: http://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/
    // Packing half floats: https://gist.github.com/rygorous/2156668
    // Load source, move from tight pack of X16Y16.... to X16...Y16...
    // Also zero out the high end.
    // TODO(benvanik): special case constant unpacks that just get 0/1/etc.
    XmmUnaryOp(e, i, 0, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      // sx = src.iw >> 16;
      // sy = src.iw & 0xFFFF;
      // dest = { XMConvertHalfToFloat(sx),
      //          XMConvertHalfToFloat(sy),
      //          0.0,
      //          1.0 };
      auto addr = Stash(e, src);
      e.lea(e.rdx, addr);
      CallNative(e, Unpack_FLOAT16_2);
      e.movaps(dest, addr);
    });
  } else if (i->flags == PACK_TYPE_FLOAT16_4) {
    // Could be shared with FLOAT16_2.
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_SHORT_2) {
    // (VD.x) = 3.0 + (VB.x>>16)*2^-22
    // (VD.y) = 3.0 + (VB.x)*2^-22
    // (VD.z) = 0.0
    // (VD.w) = 1.0
    XmmUnaryOp(e, i, 0, [](X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src) {
      // XMLoadShortN2 plus 3,3,0,3 (for some reason)
      // src is (xx,xx,xx,VALUE)
      e.mov(e.rax, XMMCONSTBASE);
      // (VALUE,VALUE,VALUE,VALUE)
      e.vbroadcastss(dest, src);
      // (VALUE&0xFFFF,VALUE&0xFFFF0000,0,0)
      e.andps(dest, XMMCONST(e.rax, XMMMaskX16Y16));
      // Sign extend.
      e.xorps(dest, XMMCONST(e.rax, XMMFlipX16Y16));
      // Convert int->float.
      e.cvtpi2ps(dest, Stash(e, dest));
      // 0x8000 to undo sign.
      e.addps(dest, XMMCONST(e.rax, XMMFixX16Y16));
      // Normalize.
      e.mulps(dest, XMMCONST(e.rax, XMMNormalizeX16Y16));
      // Clamp.
      e.maxps(dest, XMMCONST(e.rax, XMMNegativeOne));
      // Add 3,3,0,1.
      e.addps(dest, XMMCONST(e.rax, XMM3301));
    });
  } else if (i->flags == PACK_TYPE_S8_IN_16_LO) {
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_S8_IN_16_HI) {
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_S16_IN_32_LO) {
    UNIMPLEMENTED_SEQ();
  } else if (i->flags == PACK_TYPE_S16_IN_32_HI) {
    UNIMPLEMENTED_SEQ();
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

// --------------------------------------------------------------------------
// Atomic
// --------------------------------------------------------------------------

table->AddSequence(OPCODE_COMPARE_EXCHANGE, [](X64Emitter& e, Instr*& i) {
  UNIMPLEMENTED_SEQ();
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_ATOMIC_EXCHANGE, [](X64Emitter& e, Instr*& i) {
  // dest = old_value = InterlockedExchange(src1 = address, src2 = new_value);
  if (i->Match(SIG_TYPE_I32, SIG_TYPE_I64, SIG_TYPE_I32)) {
    Reg32 dest, src2;
    Reg64 src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0,
              i->src2.value, src2, 0);
    Reg64 real_src1 = src1;
    if (dest.getIdx() == src1.getIdx()) {
      e.mov(TEMP_REG, src1);
      real_src1 = TEMP_REG;
    }
    e.mov(dest, src2);
    e.lock();
    e.xchg(e.dword[real_src1], dest);
    e.EndOp(dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I64, SIG_TYPE_I32C)) {
    Reg32 dest;
    Reg64 src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    Reg64 real_src1 = src1;
    if (dest.getIdx() == src1.getIdx()) {
      e.mov(TEMP_REG, src1);
      real_src1 = TEMP_REG;
    }
    e.mov(dest, i->src2.value->constant.i32);
    e.lock();
    e.xchg(e.dword[real_src1], dest);
    e.EndOp(dest, src1);
  } else {
    ASSERT_INVALID_TYPE();
  }
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_ATOMIC_ADD, [](X64Emitter& e, Instr*& i) {
  UNIMPLEMENTED_SEQ();
  i = e.Advance(i);
  return true;
});

table->AddSequence(OPCODE_ATOMIC_SUB, [](X64Emitter& e, Instr*& i) {
  UNIMPLEMENTED_SEQ();
  i = e.Advance(i);
  return true;
});
}

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_emitter.h>

#include <alloy/backend/x64/x64_backend.h>
#include <alloy/backend/x64/x64_code_cache.h>
#include <alloy/backend/x64/x64_function.h>
#include <alloy/backend/x64/x64_sequences.h>
#include <alloy/backend/x64/x64_thunk_emitter.h>
#include <alloy/hir/hir_builder.h>
#include <alloy/runtime/debug_info.h>
#include <alloy/runtime/runtime.h>
#include <alloy/runtime/symbol_info.h>
#include <alloy/runtime/thread_state.h>

namespace alloy {
namespace backend {
namespace x64 {

// TODO(benvanik): remove when enums redefined.
using namespace alloy::hir;
using namespace alloy::runtime;

using namespace Xbyak;
using alloy::hir::HIRBuilder;
using alloy::hir::Instr;
using alloy::runtime::Function;
using alloy::runtime::FunctionInfo;
using alloy::runtime::SourceMapEntry;
using alloy::runtime::ThreadState;

static const size_t MAX_CODE_SIZE = 1 * 1024 * 1024;

static const size_t STASH_OFFSET = 32;

// If we are running with tracing on we have to store the EFLAGS in the stack,
// otherwise our calls out to C to print will clear it before DID_CARRY/etc
// can get the value.
#define STORE_EFLAGS 1

const uint32_t X64Emitter::gpr_reg_map_[X64Emitter::GPR_COUNT] = {
    Operand::RBX, Operand::R12, Operand::R13, Operand::R14, Operand::R15,
};

const uint32_t X64Emitter::xmm_reg_map_[X64Emitter::XMM_COUNT] = {
    6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

X64Emitter::X64Emitter(X64Backend* backend, XbyakAllocator* allocator)
    : CodeGenerator(MAX_CODE_SIZE, AutoGrow, allocator),
      runtime_(backend->runtime()),
      backend_(backend),
      code_cache_(backend->code_cache()),
      allocator_(allocator),
      current_instr_(0) {}

X64Emitter::~X64Emitter() {}

int X64Emitter::Initialize() { return 0; }

int X64Emitter::Emit(HIRBuilder* builder, uint32_t debug_info_flags,
                     runtime::DebugInfo* debug_info, void*& out_code_address,
                     size_t& out_code_size) {
  SCOPE_profile_cpu_f("alloy");

  // Reset.
  if (debug_info_flags & DEBUG_INFO_SOURCE_MAP) {
    source_map_count_ = 0;
    source_map_arena_.Reset();
  }

  // Fill the generator with code.
  size_t stack_size = 0;
  int result = Emit(builder, stack_size);
  if (result) {
    return result;
  }

  // Copy the final code to the cache and relocate it.
  out_code_size = getSize();
  out_code_address = Emplace(stack_size);

  // Stash source map.
  if (debug_info_flags & DEBUG_INFO_SOURCE_MAP) {
    debug_info->InitializeSourceMap(
        source_map_count_, (SourceMapEntry*)source_map_arena_.CloneContents());
  }

  return 0;
}

void* X64Emitter::Emplace(size_t stack_size) {
  // To avoid changing xbyak, we do a switcharoo here.
  // top_ points to the Xbyak buffer, and since we are in AutoGrow mode
  // it has pending relocations. We copy the top_ to our buffer, swap the
  // pointer, relocate, then return the original scratch pointer for use.
  uint8_t* old_address = top_;
  void* new_address = code_cache_->PlaceCode(top_, size_, stack_size);
  top_ = (uint8_t*)new_address;
  ready();
  top_ = old_address;
  reset();
  return new_address;
}

int X64Emitter::Emit(HIRBuilder* builder, size_t& out_stack_size) {
  // Calculate stack size. We need to align things to their natural sizes.
  // This could be much better (sort by type/etc).
  auto locals = builder->locals();
  size_t stack_offset = StackLayout::GUEST_STACK_SIZE;
  for (auto it = locals.begin(); it != locals.end(); ++it) {
    auto slot = *it;
    size_t type_size = GetTypeSize(slot->type);
    // Align to natural size.
    stack_offset = XEALIGN(stack_offset, type_size);
    slot->set_constant((uint32_t)stack_offset);
    stack_offset += type_size;
  }
  // Ensure 16b alignment.
  stack_offset -= StackLayout::GUEST_STACK_SIZE;
  stack_offset = XEALIGN(stack_offset, 16);

  // Function prolog.
  // Must be 16b aligned.
  // Windows is very strict about the form of this and the epilog:
  // http://msdn.microsoft.com/en-us/library/tawsa7cb.aspx
  // TODO(benvanik): save off non-volatile registers so we can use them:
  //     RBX, RBP, RDI, RSI, RSP, R12, R13, R14, R15
  //     Only want to do this if we actually use them, though, otherwise
  //     it just adds overhead.
  // IMPORTANT: any changes to the prolog must be kept in sync with
  //     X64CodeCache, which dynamically generates exception information.
  //     Adding or changing anything here must be matched!
  const bool emit_prolog = true;
  const size_t stack_size = StackLayout::GUEST_STACK_SIZE + stack_offset;
  assert_true((stack_size + 8) % 16 == 0);
  out_stack_size = stack_size;
  stack_size_ = stack_size;
  if (emit_prolog) {
    sub(rsp, (uint32_t)stack_size);
    mov(qword[rsp + StackLayout::GUEST_RCX_HOME], rcx);
    mov(qword[rsp + StackLayout::GUEST_RET_ADDR], rdx);
    mov(qword[rsp + StackLayout::GUEST_CALL_RET_ADDR], 0);
    mov(rdx, qword[rcx + 8]);  // membase
  }

  // Body.
  auto block = builder->first_block();
  while (block) {
    // Mark block labels.
    auto label = block->label_head;
    while (label) {
      L(label->name);
      label = label->next;
    }

    // Process instructions.
    const Instr* instr = block->instr_head;
    while (instr) {
      const Instr* new_tail = instr;
      if (!SelectSequence(*this, instr, &new_tail)) {
        // No sequence found!
        assert_always();
        XELOGE("Unable to process HIR opcode %s", instr->opcode->name);
        break;
      }
      instr = new_tail;
    }

    block = block->next;
  }

  // Function epilog.
  L("epilog");
  if (emit_prolog) {
    mov(rcx, qword[rsp + StackLayout::GUEST_RCX_HOME]);
    add(rsp, (uint32_t)stack_size);
  }
  ret();

#if XE_DEBUG
  nop();
  nop();
  nop();
  nop();
  nop();
#endif  // XE_DEBUG

  return 0;
}

void X64Emitter::MarkSourceOffset(const Instr* i) {
  auto entry = source_map_arena_.Alloc<SourceMapEntry>();
  entry->source_offset = i->src1.offset;
  entry->hir_offset = uint32_t(i->block->ordinal << 16) | i->ordinal;
  entry->code_offset = getSize();
  source_map_count_++;
}

void X64Emitter::DebugBreak() {
  // TODO(benvanik): notify debugger.
  db(0xCC);
}

void X64Emitter::Trap(uint16_t trap_type) {
  switch (trap_type) {
    case 20:
      // 0x0FE00014 is a 'debug print' where r3 = buffer r4 = length
      // TODO(benvanik): debug print at runtime.
      break;
    case 0:
    case 22:
      // Always trap?
      // TODO(benvanik): post software interrupt to debugger.
      db(0xCC);
      break;
    default:
      XELOGW("Unknown trap type %d", trap_type);
      db(0xCC);
      break;
  }
}

void X64Emitter::UnimplementedInstr(const hir::Instr* i) {
  // TODO(benvanik): notify debugger.
  db(0xCC);
  assert_always();
}

// Total size of ResolveFunctionSymbol call site in bytes.
// Used to overwrite it with nops as needed.
const size_t TOTAL_RESOLVE_SIZE = 27;
const size_t ASM_OFFSET = 2 + 2 + 8 + 2 + 8;

uint64_t ResolveFunctionSymbol(void* raw_context, uint64_t symbol_info_ptr) {
  // TODO(benvanik): generate this thunk at runtime? or a shim?
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);
  auto symbol_info = reinterpret_cast<FunctionInfo*>(symbol_info_ptr);

  // Resolve function. This will demand compile as required.
  Function* fn = NULL;
  thread_state->runtime()->ResolveFunction(symbol_info->address(), &fn);
  assert_not_null(fn);
  auto x64_fn = static_cast<X64Function*>(fn);
  uint64_t addr = reinterpret_cast<uint64_t>(x64_fn->machine_code());

// Overwrite the call site.
// The return address points to ReloadRCX work after the call.
#if XE_LIKE_WIN32
  uint64_t return_address = reinterpret_cast<uint64_t>(_ReturnAddress());
#else
  uint64_t return_address =
      reinterpret_cast<uint64_t>(__builtin_return_address(0));
#endif  // XE_WIN32_LIKE
#pragma pack(push, 1)
  struct Asm {
    uint16_t mov_rax;
    uint64_t rax_constant;
    uint16_t mov_rdx;
    uint64_t rdx_constant;
    uint16_t call_rax;
    uint8_t mov_rcx[5];
  };
#pragma pack(pop)
  static_assert_size(Asm, TOTAL_RESOLVE_SIZE);
  Asm* code = reinterpret_cast<Asm*>(return_address - ASM_OFFSET);
  code->rax_constant = addr;
  code->call_rax = 0x9066;

  // We need to return the target in rax so that it gets called.
  return addr;
}

void X64Emitter::Call(const hir::Instr* instr,
                      runtime::FunctionInfo* symbol_info) {
  auto fn = reinterpret_cast<X64Function*>(symbol_info->function());
  // Resolve address to the function to call and store in rax.
  if (fn) {
    mov(rax, reinterpret_cast<uint64_t>(fn->machine_code()));
  } else {
    size_t start = getSize();
    // 2b + 8b constant
    mov(rax, reinterpret_cast<uint64_t>(ResolveFunctionSymbol));
    // 2b + 8b constant
    mov(rdx, reinterpret_cast<uint64_t>(symbol_info));
    // 2b
    call(rax);
    // 5b
    ReloadECX();
    size_t total_size = getSize() - start;
    assert_true(total_size == TOTAL_RESOLVE_SIZE);
    // EDX overwritten, don't bother reloading.
  }

  // Actually jump/call to rax.
  if (instr->flags & CALL_TAIL) {
    // Pass the callers return address over.
    mov(rdx, qword[rsp + StackLayout::GUEST_RET_ADDR]);

    add(rsp, static_cast<uint32_t>(stack_size()));
    jmp(rax);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    mov(rdx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);
    call(rax);
  }
}

// NOTE: slot count limited by short jump size.
const int kICSlotCount = 4;
const int kICSlotSize = 23;
const uint64_t kICSlotInvalidTargetAddress = 0x0F0F0F0F0F0F0F0F;

uint64_t ResolveFunctionAddress(void* raw_context, uint64_t target_address) {
  // TODO(benvanik): generate this thunk at runtime? or a shim?
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);

  // TODO(benvanik): required?
  target_address &= 0xFFFFFFFF;
  assert_not_zero(target_address);

  Function* fn = NULL;
  thread_state->runtime()->ResolveFunction(target_address, &fn);
  assert_not_null(fn);
  auto x64_fn = static_cast<X64Function*>(fn);
  uint64_t addr = reinterpret_cast<uint64_t>(x64_fn->machine_code());

// Add an IC slot, if there is room.
#if XE_LIKE_WIN32
  uint64_t return_address = reinterpret_cast<uint64_t>(_ReturnAddress());
#else
  uint64_t return_address =
      reinterpret_cast<uint64_t>(__builtin_return_address(0));
#endif  // XE_WIN32_LIKE
#pragma pack(push, 1)
  struct Asm {
    uint16_t cmp_rdx;
    uint32_t address_constant;
    uint16_t jmp_next_slot;
    uint16_t mov_rax;
    uint64_t target_constant;
    uint8_t jmp_skip_resolve[5];
  };
#pragma pack(pop)
  static_assert_size(Asm, kICSlotSize);
  // The return address points to ReloadRCX work after the call.
  // To get the top of the table, look back a ways.
  uint64_t table_start = return_address - 12 - kICSlotSize * kICSlotCount;
  // NOTE: order matters here - we update the address BEFORE we switch the code
  // over to passing the compare.
  Asm* table_slot = reinterpret_cast<Asm*>(table_start);
  bool wrote_ic = false;
  for (int i = 0; i < kICSlotCount; ++i) {
    if (poly::atomic_cas(kICSlotInvalidTargetAddress, addr,
                         &table_slot->target_constant)) {
      // Got slot! Just write the compare and we're done.
      table_slot->address_constant = static_cast<uint32_t>(target_address);
      wrote_ic = true;
      break;
    }
    ++table_slot;
  }
  if (!wrote_ic) {
    // TODO(benvanik): log that IC table is full.
  }

  // We need to return the target in rax so that it gets called.
  return addr;
}

void X64Emitter::CallIndirect(const hir::Instr* instr, const Reg64& reg) {
  // Check if return.
  if (instr->flags & CALL_POSSIBLE_RETURN) {
    cmp(reg.cvt32(), dword[rsp + StackLayout::GUEST_RET_ADDR]);
    je("epilog", CodeGenerator::T_NEAR);
  }

  if (reg.getIdx() != rdx.getIdx()) {
    mov(rdx, reg);
  }

  inLocalLabel();
  Xbyak::Label skip_resolve;

  // TODO(benvanik): make empty tables skippable (cmp, jump right to resolve).

  // IC table, initially empty.
  // This will get filled in as functions are resolved.
  // Note that we only have a limited cache, and once it's full all calls
  // will fall through.
  // TODO(benvanik): check miss rate when full and add a 2nd-level table?
  // 0000000264BD4DC3 81 FA 0F0F0F0F         cmp         edx,0F0F0F0Fh
  // 0000000264BD4DC9 75 0C                  jne         0000000264BD4DD7
  // 0000000264BD4DCB 48 B8 0F0F0F0F0F0F0F0F mov         rax,0F0F0F0F0F0F0F0Fh
  // 0000000264BD4DD5 EB XXXXXXXX            jmp         0000000264BD4E00
  size_t table_start = getSize();
  for (int i = 0; i < kICSlotCount; ++i) {
    // Compare target address with constant, if matches jump there.
    // Otherwise, fall through.
    // 6b
    cmp(edx, 0x0F0F0F0F);
    Xbyak::Label next_slot;
    // 2b
    jne(next_slot, T_SHORT);
    // Match! Load up rax and skip down to the jmp code.
    // 10b
    mov(rax, kICSlotInvalidTargetAddress);
    // 5b
    jmp(skip_resolve, T_NEAR);
    L(next_slot);
  }
  size_t table_size = getSize() - table_start;
  assert_true(table_size == kICSlotSize * kICSlotCount);

  // Resolve address to the function to call and store in rax.
  // We fall through to this when there are no hits in the IC table.
  CallNative(ResolveFunctionAddress);

  // Actually jump/call to rax.
  L(skip_resolve);
  if (instr->flags & CALL_TAIL) {
    // Pass the callers return address over.
    mov(rdx, qword[rsp + StackLayout::GUEST_RET_ADDR]);

    add(rsp, static_cast<uint32_t>(stack_size()));
    jmp(rax);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    mov(rdx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);
    call(rax);
  }

  outLocalLabel();
}

uint64_t UndefinedCallExtern(void* raw_context, uint64_t symbol_info_ptr) {
  auto symbol_info = reinterpret_cast<FunctionInfo*>(symbol_info_ptr);
  XELOGW("undefined extern call to %.8llX %s", symbol_info->address(),
         symbol_info->name().c_str());
  return 0;
}
void X64Emitter::CallExtern(const hir::Instr* instr,
                            const FunctionInfo* symbol_info) {
  assert_true(symbol_info->behavior() == FunctionInfo::BEHAVIOR_EXTERN);
  if (!symbol_info->extern_handler()) {
    CallNative(UndefinedCallExtern, reinterpret_cast<uint64_t>(symbol_info));
  } else {
    // rcx = context
    // rdx = target host function
    // r8  = arg0
    // r9  = arg1
    mov(rdx, reinterpret_cast<uint64_t>(symbol_info->extern_handler()));
    mov(r8, reinterpret_cast<uint64_t>(symbol_info->extern_arg0()));
    mov(r9, reinterpret_cast<uint64_t>(symbol_info->extern_arg1()));
    auto thunk = backend()->guest_to_host_thunk();
    mov(rax, reinterpret_cast<uint64_t>(thunk));
    call(rax);
    ReloadECX();
    ReloadEDX();
    // rax = host return
  }
}

void X64Emitter::CallNative(void* fn) {
  mov(rax, reinterpret_cast<uint64_t>(fn));
  call(rax);
  ReloadECX();
  ReloadEDX();
}

void X64Emitter::CallNative(uint64_t (*fn)(void* raw_context)) {
  mov(rax, reinterpret_cast<uint64_t>(fn));
  call(rax);
  ReloadECX();
  ReloadEDX();
}

void X64Emitter::CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0)) {
  mov(rax, reinterpret_cast<uint64_t>(fn));
  call(rax);
  ReloadECX();
  ReloadEDX();
}

void X64Emitter::CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0),
                            uint64_t arg0) {
  mov(rdx, arg0);
  mov(rax, reinterpret_cast<uint64_t>(fn));
  call(rax);
  ReloadECX();
  ReloadEDX();
}

void X64Emitter::CallNativeSafe(void* fn) {
  // rcx = context
  // rdx = target host function
  // r8  = arg0
  // r9  = arg1
  mov(rdx, reinterpret_cast<uint64_t>(fn));
  auto thunk = backend()->guest_to_host_thunk();
  mov(rax, reinterpret_cast<uint64_t>(thunk));
  call(rax);
  ReloadECX();
  ReloadEDX();
  // rax = host return
}

void X64Emitter::SetReturnAddress(uint64_t value) {
  mov(qword[rsp + StackLayout::GUEST_CALL_RET_ADDR], value);
}

void X64Emitter::ReloadECX() {
  mov(rcx, qword[rsp + StackLayout::GUEST_RCX_HOME]);
}

void X64Emitter::ReloadEDX() {
  mov(rdx, qword[rcx + 8]);  // membase
}

// Len Assembly                                   Byte Sequence
// ============================================================================
// 2b  66 NOP                                     66 90H
// 3b  NOP DWORD ptr [EAX]                        0F 1F 00H
// 4b  NOP DWORD ptr [EAX + 00H]                  0F 1F 40 00H
// 5b  NOP DWORD ptr [EAX + EAX*1 + 00H]          0F 1F 44 00 00H
// 6b  66 NOP DWORD ptr [EAX + EAX*1 + 00H]       66 0F 1F 44 00 00H
// 7b  NOP DWORD ptr [EAX + 00000000H]            0F 1F 80 00 00 00 00H
// 8b  NOP DWORD ptr [EAX + EAX*1 + 00000000H]    0F 1F 84 00 00 00 00 00H
// 9b  66 NOP DWORD ptr [EAX + EAX*1 + 00000000H] 66 0F 1F 84 00 00 00 00 00H
void X64Emitter::nop(size_t length) {
  // TODO(benvanik): fat nop
  for (size_t i = 0; i < length; ++i) {
    db(0x90);
  }
}

void X64Emitter::LoadEflags() {
#if STORE_EFLAGS
  mov(eax, dword[rsp + STASH_OFFSET]);
  push(rax);
  popf();
#else
// EFLAGS already present.
#endif  // STORE_EFLAGS
}

void X64Emitter::StoreEflags() {
#if STORE_EFLAGS
  pushf();
  pop(qword[rsp + STASH_OFFSET]);
#else
// EFLAGS should have CA set?
// (so long as we don't fuck with it)
#endif  // STORE_EFLAGS
}

uint32_t X64Emitter::page_table_address() const {
  uint64_t addr = runtime_->memory()->page_table();
  return static_cast<uint32_t>(addr);
}

bool X64Emitter::ConstantFitsIn32Reg(uint64_t v) {
  if ((v & ~0x7FFFFFFF) == 0) {
    // Fits under 31 bits, so just load using normal mov.
    return true;
  } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
    // Negative number that fits in 32bits.
    return true;
  }
  return false;
}

void X64Emitter::MovMem64(const RegExp& addr, uint64_t v) {
  if ((v & ~0x7FFFFFFF) == 0) {
    // Fits under 31 bits, so just load using normal mov.
    mov(qword[addr], v);
  } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
    // Negative number that fits in 32bits.
    mov(qword[addr], v);
  } else if (!(v >> 32)) {
    // All high bits are zero. It'd be nice if we had a way to load a 32bit
    // immediate without sign extending!
    // TODO(benvanik): this is super common, find a better way.
    mov(dword[addr], static_cast<uint32_t>(v));
    mov(dword[addr + 4], 0);
  } else {
    // 64bit number that needs double movs.
    mov(dword[addr], static_cast<uint32_t>(v));
    mov(dword[addr + 4], static_cast<uint32_t>(v >> 32));
  }
}

Address X64Emitter::GetXmmConstPtr(XmmConst id) {
  static const vec128_t xmm_consts[] = {
      /* XMMZero                */ vec128f(0.0f, 0.0f, 0.0f, 0.0f),
      /* XMMOne                 */ vec128f(1.0f, 1.0f, 1.0f, 1.0f),
      /* XMMNegativeOne         */ vec128f(-1.0f, -1.0f, -1.0f, -1.0f),
      /* XMMMaskX16Y16          */ vec128i(0x0000FFFFu, 0xFFFF0000u,
                                           0x00000000u, 0x00000000u),
      /* XMMFlipX16Y16          */ vec128i(0x00008000u, 0x00000000u,
                                           0x00000000u, 0x00000000u),
      /* XMMFixX16Y16           */ vec128f(-32768.0f, 0.0f, 0.0f, 0.0f),
      /* XMMNormalizeX16Y16     */ vec128f(
          1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 0.0f, 0.0f),
      /* XMM0001                */ vec128f(0.0f, 0.0f, 0.0f, 1.0f),
      /* XMM3301                */ vec128f(3.0f, 3.0f, 0.0f, 1.0f),
      /* XMMSignMaskPS          */ vec128i(0x80000000u, 0x80000000u,
                                           0x80000000u, 0x80000000u),
      /* XMMSignMaskPD          */ vec128i(0x00000000u, 0x80000000u,
                                           0x00000000u, 0x80000000u),
      /* XMMAbsMaskPS           */ vec128i(0x7FFFFFFFu, 0x7FFFFFFFu,
                                           0x7FFFFFFFu, 0x7FFFFFFFu),
      /* XMMAbsMaskPD           */ vec128i(0xFFFFFFFFu, 0x7FFFFFFFu,
                                           0xFFFFFFFFu, 0x7FFFFFFFu),
      /* XMMByteSwapMask        */ vec128i(0x00010203u, 0x04050607u,
                                           0x08090A0Bu, 0x0C0D0E0Fu),
      /* XMMPermuteControl15    */ vec128b(15, 15, 15, 15, 15, 15, 15, 15, 15,
                                           15, 15, 15, 15, 15, 15, 15),
      /* XMMPackD3DCOLOR        */ vec128i(0xFFFFFFFFu, 0xFFFFFFFFu,
                                           0xFFFFFFFFu, 0x0C000408u),
      /* XMMUnpackD3DCOLOR      */ vec128i(0xFFFFFF0Eu, 0xFFFFFF0Du,
                                           0xFFFFFF0Cu, 0xFFFFFF0Fu),
      /* XMMOneOver255          */ vec128f(1.0f / 255.0f, 1.0f / 255.0f,
                                           1.0f / 255.0f, 1.0f / 255.0f),
      /* XMMMaskEvenPI16        */ vec128i(0x0000FFFFu, 0x0000FFFFu,
                                           0x0000FFFFu, 0x0000FFFFu),
      /* XMMShiftMaskEvenPI16   */ vec128i(0x0000000Fu, 0x0000000Fu,
                                           0x0000000Fu, 0x0000000Fu),
      /* XMMShiftMaskPS         */ vec128i(0x0000001Fu, 0x0000001Fu,
                                           0x0000001Fu, 0x0000001Fu),
      /* XMMShiftByteMask       */ vec128i(0x000000FFu, 0x000000FFu,
                                           0x000000FFu, 0x000000FFu),
      /* XMMUnsignedDwordMax    */ vec128i(0xFFFFFFFFu, 0x00000000u,
                                           0xFFFFFFFFu, 0x00000000u),
      /* XMM255                 */ vec128f(255.0f, 255.0f, 255.0f, 255.0f),
      /* XMMPI32                */ vec128i(32, 32, 32, 32),
      /* XMMSignMaskI8          */ vec128i(0x80808080u, 0x80808080u,
                                           0x80808080u, 0x80808080u),
      /* XMMSignMaskI16         */ vec128i(0x80008000u, 0x80008000u,
                                           0x80008000u, 0x80008000u),
      /* XMMSignMaskI32         */ vec128i(0x80000000u, 0x80000000u,
                                           0x80000000u, 0x80000000u),
      /* XMMSignMaskF32         */ vec128i(0x80000000u, 0x80000000u,
                                           0x80000000u, 0x80000000u),
  };
  // TODO(benvanik): cache base pointer somewhere? stack? It'd be nice to
  // prevent this move.
  // TODO(benvanik): move to predictable location in PPCContext? could then
  // just do rcx relative addression with no rax overwriting.
  mov(rax, (uint64_t)&xmm_consts[id]);
  return ptr[rax];
}

void X64Emitter::LoadConstantXmm(Xbyak::Xmm dest, const vec128_t& v) {
  // http://www.agner.org/optimize/optimizing_assembly.pdf
  // 13.4 Generating constants
  if (!v.low && !v.high) {
    // 0000...
    vpxor(dest, dest);
  } else if (v.low == ~0ull && v.high == ~0ull) {
    // 1111...
    vpcmpeqb(dest, dest);
  } else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    MovMem64(rsp + STASH_OFFSET, v.low);
    MovMem64(rsp + STASH_OFFSET + 8, v.high);
    vmovdqa(dest, ptr[rsp + STASH_OFFSET]);
  }
}

void X64Emitter::LoadConstantXmm(Xbyak::Xmm dest, float v) {
  union {
    float f;
    uint32_t i;
  } x = {v};
  if (!v) {
    // 0
    vpxor(dest, dest);
  } else if (x.i == ~0U) {
    // 1111...
    vpcmpeqb(dest, dest);
  } else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    mov(eax, x.i);
    vmovd(dest, eax);
  }
}

void X64Emitter::LoadConstantXmm(Xbyak::Xmm dest, double v) {
  union {
    double d;
    uint64_t i;
  } x = {v};
  if (!v) {
    // 0
    vpxor(dest, dest);
  } else if (x.i == ~0ULL) {
    // 1111...
    vpcmpeqb(dest, dest);
  } else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    mov(rax, x.i);
    vmovq(dest, rax);
  }
}

Address X64Emitter::StashXmm(const Xmm& r) {
  auto addr = ptr[rsp + STASH_OFFSET];
  vmovups(addr, r);
  return addr;
}

Address X64Emitter::StashXmm(const vec128_t& v) {
  auto addr = ptr[rsp + STASH_OFFSET];
  LoadConstantXmm(xmm0, v);
  vmovups(addr, xmm0);
  return addr;
}

}  // namespace x64
}  // namespace backend
}  // namespace alloy

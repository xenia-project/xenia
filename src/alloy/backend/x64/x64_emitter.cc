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
#include <alloy/backend/x64/lowering/lowering_table.h>
#include <alloy/hir/hir_builder.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::x64;
using namespace alloy::hir;
using namespace alloy::runtime;

using namespace Xbyak;


namespace alloy {
namespace backend {
namespace x64 {

static const size_t MAX_CODE_SIZE = 1 * 1024 * 1024;

}  // namespace x64
}  // namespace backend
}  // namespace alloy


X64Emitter::X64Emitter(X64Backend* backend, XbyakAllocator* allocator) :
    backend_(backend),
    code_cache_(backend->code_cache()),
    allocator_(allocator),
    CodeGenerator(MAX_CODE_SIZE, AutoGrow, allocator) {
  xe_zero_struct(&reg_state_, sizeof(reg_state_));
}

X64Emitter::~X64Emitter() {
  delete allocator_;
}

int X64Emitter::Initialize() {
  return 0;
}

int X64Emitter::Emit(
    HIRBuilder* builder, void*& out_code_address, size_t& out_code_size) {
  // Fill the generator with code.
  int result = Emit(builder);
  if (result) {
    return result;
  }

  // Copy the final code to the cache and relocate it.
  out_code_size = getSize();
  out_code_address = Emplace(code_cache_);

  return 0;
}

void* X64Emitter::Emplace(X64CodeCache* code_cache) {
  // To avoid changing xbyak, we do a switcharoo here.
  // top_ points to the Xbyak buffer, and since we are in AutoGrow mode
  // it has pending relocations. We copy the top_ to our buffer, swap the
  // pointer, relocate, then return the original scratch pointer for use.
  uint8_t* old_address = top_;
  void* new_address = code_cache->PlaceCode(top_, size_);
  top_ = (uint8_t*)new_address;
  ready();
  top_ = old_address;
  reset();
  return new_address;
}

int X64Emitter::Emit(HIRBuilder* builder) {
  // These are the registers we will not be using. All others are fare game.
  const uint32_t reserved_regs =
      GetRegBit(rax) |
      GetRegBit(rcx) |
      GetRegBit(rdx) |
      GetRegBit(rsp) |
      GetRegBit(rbp) |
      GetRegBit(rsi) |
      GetRegBit(rdi) |
      GetRegBit(xmm0);

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
  const bool emit_prolog = false;
  const size_t stack_size = 16;
  if (emit_prolog) {
    sub(rsp, stack_size);
    // TODO(benvanik): save registers.
  }

  auto lowering_table = backend_->lowering_table();

  // Body.
  auto block = builder->first_block();
  while (block) {
    // Mark block labels.
    auto label = block->label_head;
    while (label) {
      L(label->name);
      label = label->next;
    }

    // Reset reg allocation state.
    // If we start keeping regs across blocks this needs to change.
    // We mark a few active so that the allocator doesn't use them.
    reg_state_.active_regs = reg_state_.live_regs = reserved_regs;

    // Add instructions.
    // The table will process sequences of instructions to (try to)
    // generate optimal code.
    if (lowering_table->ProcessBlock(*this, block)) {
      return 1;
    }

    block = block->next;
  }

  // Function epilog.
  L("epilog");
  if (emit_prolog) {
    add(rsp, stack_size);
    // TODO(benvanik): restore registers.
  }
  ret();

  return 0;
}

void X64Emitter::FindFreeRegs(
    Value* v0, uint32_t& v0_idx, uint32_t v0_flags) {
  // If the value is already in a register, use it.
  if (v0->reg != -1) {
    // Already in a register. Mark active and return.
    v0_idx = v0->reg;
    reg_state_.active_regs |= 1 << v0_idx;
    return;
  }

  uint32_t avail_regs = 0;
  if (IsIntType(v0->type)) {
    if (v0_flags & REG_ABCD) {
      avail_regs = B00001111;
    } else {
      avail_regs = 0xFFFF;
    }
  } else {
    avail_regs = 0xFFFF0000;
  }
  uint32_t free_regs = avail_regs & ~reg_state_.active_regs;
  if (free_regs) {
    // Just take one.
    _BitScanReverse((DWORD*)&v0_idx, free_regs);
  } else {
    // Need to evict something.
    XEASSERTALWAYS();
  }

  reg_state_.active_regs |= 1 << v0_idx;
  reg_state_.live_regs |= 1 << v0_idx;
  v0->reg = v0_idx;
  reg_state_.reg_values[v0_idx] = v0;
}

void X64Emitter::FindFreeRegs(
    Value* v0, uint32_t& v0_idx, uint32_t v0_flags,
    Value* v1, uint32_t& v1_idx, uint32_t v1_flags) {
  // TODO(benvanik): support REG_DEST reuse/etc.
  FindFreeRegs(v0, v0_idx, v0_flags);
  FindFreeRegs(v1, v1_idx, v1_flags);
}

void X64Emitter::FindFreeRegs(
    Value* v0, uint32_t& v0_idx, uint32_t v0_flags,
    Value* v1, uint32_t& v1_idx, uint32_t v1_flags,
    Value* v2, uint32_t& v2_idx, uint32_t v2_flags) {
  // TODO(benvanik): support REG_DEST reuse/etc.
  FindFreeRegs(v0, v0_idx, v0_flags);
  FindFreeRegs(v1, v1_idx, v1_flags);
  FindFreeRegs(v2, v2_idx, v2_flags);
}

void X64Emitter::FindFreeRegs(
    Value* v0, uint32_t& v0_idx, uint32_t v0_flags,
    Value* v1, uint32_t& v1_idx, uint32_t v1_flags,
    Value* v2, uint32_t& v2_idx, uint32_t v2_flags,
    Value* v3, uint32_t& v3_idx, uint32_t v3_flags) {
  // TODO(benvanik): support REG_DEST reuse/etc.
  FindFreeRegs(v0, v0_idx, v0_flags);
  FindFreeRegs(v1, v1_idx, v1_flags);
  FindFreeRegs(v2, v2_idx, v2_flags);
  FindFreeRegs(v3, v3_idx, v3_flags);
}

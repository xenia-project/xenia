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
#include <alloy/runtime/debug_info.h>

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
    current_instr_(0),
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
    HIRBuilder* builder, 
    uint32_t debug_info_flags, runtime::DebugInfo* debug_info,
    void*& out_code_address, size_t& out_code_size) {
  // Reset.
  if (debug_info_flags & DEBUG_INFO_SOURCE_MAP) {
    source_map_count_ = 0;
    source_map_arena_.Reset();
  }

  // Fill the generator with code.
  int result = Emit(builder);
  if (result) {
    return result;
  }

  // Copy the final code to the cache and relocate it.
  out_code_size = getSize();
  out_code_address = Emplace(code_cache_);

  // Stash source map.
  if (debug_info_flags & DEBUG_INFO_SOURCE_MAP) {
    debug_info->InitializeSourceMap(
        source_map_count_,
        (SourceMapEntry*)source_map_arena_.CloneContents());
  }

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
  const size_t stack_size = 64;
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
    current_instr_ = block->instr_head;
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

#if XE_DEBUG
  nop();
  nop();
  nop();
  nop();
  nop();
#endif  // XE_DEBUG

  return 0;
}

void X64Emitter::EvictStaleRegs() {
  // NOTE: if we are getting called it's because we *need* a register.
  // We must get rid of something.

  uint32_t current_ordinal = current_instr_->ordinal;

  // Remove any register with no more uses.
  uint32_t new_live_regs = 0;
  for (size_t n = 0; n < 32; n++) {
    uint32_t bit = 1 << n;
    if (bit & reg_state_.active_regs) {
      // Register is active and cannot be freed.
      new_live_regs |= bit;
      continue;
    }
    if (!(bit & reg_state_.live_regs)) {
      // Register is not alive - nothing to do.
      continue;
    }

    // Register is live, not active. Check and see if we get rid of it.
    auto v = reg_state_.reg_values[n];
    if (v->last_use->ordinal < current_ordinal) {
      reg_state_.reg_values[n] = NULL;
    }
  }

  // Hrm. We have spilled.
  if (reg_state_.live_regs == new_live_regs) {
    XEASSERTALWAYS();
  }

  reg_state_.live_regs = new_live_regs;
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
  uint32_t free_regs = avail_regs & ~reg_state_.live_regs;
  if (!free_regs) {
    // Need to evict something.
    EvictStaleRegs();
  }

  // Find the first available.
  // We start from the MSB so that we get the non-rNx regs that are often
  // in short supply.
  _BitScanReverse((DWORD*)&v0_idx, free_regs);

  reg_state_.active_regs |= 1 << v0_idx;
  reg_state_.live_regs |= 1 << v0_idx;
  v0->reg = v0_idx;
  reg_state_.reg_values[v0_idx] = v0;
}

void X64Emitter::FindFreeRegs(
    Value* v0, uint32_t& v0_idx, uint32_t v0_flags,
    Value* v1, uint32_t& v1_idx, uint32_t v1_flags) {
  // TODO(benvanik): support REG_DEST reuse/etc.
  // Grab all already-present registers first.
  // This way we won't spill them trying to get new registers.
  bool need_v0 = v0->reg == -1;
  bool need_v1 = v1->reg == -1;
  if (!need_v0) {
    FindFreeRegs(v0, v0_idx, v0_flags);
  }
  if (!need_v1) {
    FindFreeRegs(v1, v1_idx, v1_flags);
  }
  // Grab any registers we still need. These calls may evict.
  if (need_v0) {
    FindFreeRegs(v0, v0_idx, v0_flags);
  }
  if (need_v1) {
    FindFreeRegs(v1, v1_idx, v1_flags);
  }
}

void X64Emitter::FindFreeRegs(
    Value* v0, uint32_t& v0_idx, uint32_t v0_flags,
    Value* v1, uint32_t& v1_idx, uint32_t v1_flags,
    Value* v2, uint32_t& v2_idx, uint32_t v2_flags) {
  // TODO(benvanik): support REG_DEST reuse/etc.
  // Grab all already-present registers first.
  // This way we won't spill them trying to get new registers.
  bool need_v0 = v0->reg == -1;
  bool need_v1 = v1->reg == -1;
  bool need_v2 = v2->reg == -1;
  if (!need_v0) {
    FindFreeRegs(v0, v0_idx, v0_flags);
  }
  if (!need_v1) {
    FindFreeRegs(v1, v1_idx, v1_flags);
  }
  if (!need_v2) {
    FindFreeRegs(v2, v2_idx, v2_flags);
  }
  // Grab any registers we still need. These calls may evict.
  if (need_v0) {
    FindFreeRegs(v0, v0_idx, v0_flags);
  }
  if (need_v1) {
    FindFreeRegs(v1, v1_idx, v1_flags);
  }
  if (need_v2) {
    FindFreeRegs(v2, v2_idx, v2_flags);
  }
}

void X64Emitter::FindFreeRegs(
    Value* v0, uint32_t& v0_idx, uint32_t v0_flags,
    Value* v1, uint32_t& v1_idx, uint32_t v1_flags,
    Value* v2, uint32_t& v2_idx, uint32_t v2_flags,
    Value* v3, uint32_t& v3_idx, uint32_t v3_flags) {
  // TODO(benvanik): support REG_DEST reuse/etc.
  // Grab all already-present registers first.
  // This way we won't spill them trying to get new registers.
  bool need_v0 = v0->reg == -1;
  bool need_v1 = v1->reg == -1;
  bool need_v2 = v2->reg == -1;
  bool need_v3 = v3->reg == -1;
  if (!need_v0) {
    FindFreeRegs(v0, v0_idx, v0_flags);
  }
  if (!need_v1) {
    FindFreeRegs(v1, v1_idx, v1_flags);
  }
  if (!need_v2) {
    FindFreeRegs(v2, v2_idx, v2_flags);
  }
  if (!need_v3) {
    FindFreeRegs(v3, v3_idx, v3_flags);
  }
  // Grab any registers we still need. These calls may evict.
  if (need_v0) {
    FindFreeRegs(v0, v0_idx, v0_flags);
  }
  if (need_v1) {
    FindFreeRegs(v1, v1_idx, v1_flags);
  }
  if (need_v2) {
    FindFreeRegs(v2, v2_idx, v2_flags);
  }
  if (need_v3) {
    FindFreeRegs(v3, v3_idx, v3_flags);
  }
}

Instr* X64Emitter::Advance(Instr* i) {
  auto next = i->next;
  current_instr_ = next;
  return next;
}

void X64Emitter::MarkSourceOffset(Instr* i) {
  auto entry = source_map_arena_.Alloc<SourceMapEntry>();
  entry->source_offset  = i->src1.offset;
  entry->hir_offset     = uint32_t(i->block->ordinal << 16) | i->ordinal;
  entry->code_offset    = getSize();
  source_map_count_++;
}

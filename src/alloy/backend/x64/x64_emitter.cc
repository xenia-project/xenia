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
#include <alloy/backend/x64/x64_thunk_emitter.h>
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


const uint32_t X64Emitter::gpr_reg_map_[X64Emitter::GPR_COUNT] = {
  Operand::RBX,
  Operand::R12, Operand::R13, Operand::R14, Operand::R15,
};

const uint32_t X64Emitter::xmm_reg_map_[X64Emitter::XMM_COUNT] = {
  6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};


X64Emitter::X64Emitter(X64Backend* backend, XbyakAllocator* allocator) :
    runtime_(backend->runtime()),
    backend_(backend),
    code_cache_(backend->code_cache()),
    allocator_(allocator),
    current_instr_(0),
    CodeGenerator(MAX_CODE_SIZE, AutoGrow, allocator) {
}

X64Emitter::~X64Emitter() {
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
        source_map_count_,
        (SourceMapEntry*)source_map_arena_.CloneContents());
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
    slot->set_constant(stack_offset);
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
  XEASSERT((stack_size + 8) % 16 == 0);
  out_stack_size = stack_size;
  stack_size_ = stack_size;
  if (emit_prolog) {
    sub(rsp, (uint32_t)stack_size);
    mov(qword[rsp + StackLayout::GUEST_RCX_HOME], rcx);
    mov(qword[rsp + StackLayout::GUEST_RET_ADDR], rdx);
    mov(qword[rsp + StackLayout::GUEST_CALL_RET_ADDR], 0);
    // ReloadRDX:
    mov(rdx, qword[rcx + 8]); // membase
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

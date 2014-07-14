/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/ivm/ivm_assembler.h>

#include <alloy/reset_scope.h>
#include <alloy/backend/backend.h>
#include <alloy/backend/ivm/ivm_intcode.h>
#include <alloy/backend/ivm/ivm_function.h>
#include <alloy/hir/hir_builder.h>
#include <alloy/hir/label.h>
#include <alloy/runtime/runtime.h>

namespace alloy {
namespace backend {
namespace ivm {

using alloy::hir::HIRBuilder;
using alloy::runtime::DebugInfo;
using alloy::runtime::Function;
using alloy::runtime::FunctionInfo;

IVMAssembler::IVMAssembler(Backend* backend)
    : Assembler(backend), source_map_arena_(128 * 1024) {}

IVMAssembler::~IVMAssembler() = default;

int IVMAssembler::Initialize() {
  int result = Assembler::Initialize();
  if (result) {
    return result;
  }

  return result;
}

void IVMAssembler::Reset() {
  intcode_arena_.Reset();
  source_map_arena_.Reset();
  scratch_arena_.Reset();
  Assembler::Reset();
}

int IVMAssembler::Assemble(FunctionInfo* symbol_info, HIRBuilder* builder,
                           uint32_t debug_info_flags,
                           std::unique_ptr<DebugInfo> debug_info,
                           Function** out_function) {
  SCOPE_profile_cpu_f("alloy");

  // Reset when we leave.
  make_reset_scope(this);

  IVMFunction* fn = new IVMFunction(symbol_info);
  fn->set_debug_info(std::move(debug_info));

  TranslationContext ctx;
  ctx.register_count = 0;
  ctx.intcode_count = 0;
  ctx.intcode_arena = &intcode_arena_;
  ctx.source_map_count = 0;
  ctx.source_map_arena = &source_map_arena_;
  ctx.scratch_arena = &scratch_arena_;
  ctx.label_ref_head = NULL;

  // Reset label tags as we use them.
  builder->ResetLabelTags();

  // Function prologue.
  size_t stack_offset = 0;
  auto locals = builder->locals();
  for (auto it = locals.begin(); it != locals.end(); ++it) {
    auto slot = *it;
    size_t type_size = GetTypeSize(slot->type);
    // Align to natural size.
    stack_offset = XEALIGN(stack_offset, type_size);
    slot->set_constant((uint32_t)stack_offset);
    stack_offset += type_size;
  }
  // Ensure 16b alignment.
  stack_offset = XEALIGN(stack_offset, 16);
  ctx.stack_size = stack_offset;

  auto block = builder->first_block();
  while (block) {
    auto label = block->label_head;
    while (label) {
      label->tag = (void*)(0x80000000 | ctx.intcode_count);
      label = label->next;
    }

    auto i = block->instr_head;
    while (i) {
      TranslateIntCodes(ctx, i);
      i = i->next;
    }
    block = block->next;
  }

  // Function epilogue.

  // Fixup label references.
  LabelRef* label_ref = ctx.label_ref_head;
  while (label_ref) {
    label_ref->instr->src1_reg =
        (uint32_t)(intptr_t)label_ref->label->tag & ~0x80000000;
    label_ref = label_ref->next;
  }

  fn->Setup(ctx);

  *out_function = fn;
  return 0;
}

}  // namespace ivm
}  // namespace backend
}  // namespace alloy

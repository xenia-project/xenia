/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/ivm/ivm_assembler.h>

#include <alloy/backend/tracing.h>
#include <alloy/backend/ivm/ivm_intcode.h>
#include <alloy/backend/ivm/ivm_function.h>
#include <alloy/hir/function_builder.h>
#include <alloy/hir/label.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::ivm;
using namespace alloy::hir;
using namespace alloy::runtime;


IVMAssembler::IVMAssembler(Backend* backend) :
    Assembler(backend) {
}

IVMAssembler::~IVMAssembler() {
  alloy::tracing::WriteEvent(EventType::AssemblerDeinit({
  }));
}

int IVMAssembler::Initialize() {
  int result = Assembler::Initialize();
  if (result) {
    return result;
  }

  alloy::tracing::WriteEvent(EventType::AssemblerInit({
  }));

  return result;
}

void IVMAssembler::Reset() {
  intcode_arena_.Reset();
  scratch_arena_.Reset();
  Assembler::Reset();
}

int IVMAssembler::Assemble(
    FunctionInfo* symbol_info, FunctionBuilder* builder,
    Function** out_function) {
  IVMFunction* fn = new IVMFunction(symbol_info);

  TranslationContext ctx;
  ctx.register_count = 0;
  ctx.intcode_count = 0;
  ctx.intcode_arena = &intcode_arena_;
  ctx.scratch_arena = &scratch_arena_;
  ctx.label_ref_head = NULL;

  // Function prologue.

  Block* block = builder->first_block();
  while (block) {
    Label* label = block->label_head;
    while (label) {
      label->tag = (void*)(0x80000000 | ctx.intcode_count);
      label = label->next;
    }

    Instr* i = block->instr_head;
    while (i) {
      int result = TranslateIntCodes(ctx, i);
      i = i->next;
    }
    block = block->next;
  }

  // Function epilogue.

  // Fixup label references.
  LabelRef* label_ref = ctx.label_ref_head;
  while (label_ref) {
    label_ref->instr->src1_reg = (uint32_t)label_ref->label->tag & ~0x80000000;
    label_ref = label_ref->next;
  }

  fn->Setup(ctx);

  *out_function = fn;
  return 0;
}

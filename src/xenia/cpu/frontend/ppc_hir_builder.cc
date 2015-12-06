/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/frontend/ppc_hir_builder.h"

#include <cstring>

#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/frontend/ppc_context.h"
#include "xenia/cpu/frontend/ppc_disasm.h"
#include "xenia/cpu/frontend/ppc_frontend.h"
#include "xenia/cpu/frontend/ppc_instr.h"
#include "xenia/cpu/hir/label.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {
namespace frontend {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Label;
using xe::cpu::hir::TypeName;
using xe::cpu::hir::Value;

PPCHIRBuilder::PPCHIRBuilder(PPCFrontend* frontend)
    : HIRBuilder(), frontend_(frontend), comment_buffer_(4096) {}

PPCHIRBuilder::~PPCHIRBuilder() = default;

PPCBuiltins* PPCHIRBuilder::builtins() const { return frontend_->builtins(); }

void PPCHIRBuilder::Reset() {
  function_ = nullptr;
  start_address_ = 0;
  instr_count_ = 0;
  instr_offset_list_ = NULL;
  label_list_ = NULL;
  with_debug_info_ = false;
  HIRBuilder::Reset();
}

bool PPCHIRBuilder::Emit(GuestFunction* function, uint32_t flags) {
  SCOPE_profile_cpu_f("cpu");

  Memory* memory = frontend_->memory();

  function_ = function;
  start_address_ = function_->address();
  instr_count_ = (function_->end_address() - function_->address()) / 4 + 1;

  with_debug_info_ = (flags & EMIT_DEBUG_COMMENTS) == EMIT_DEBUG_COMMENTS;
  if (with_debug_info_) {
    CommentFormat("%s fn %.8X-%.8X %s", function_->module()->name().c_str(),
                  function_->address(), function_->end_address(),
                  function_->name().c_str());
  }

  // Allocate offset list.
  // This is used to quickly map labels to instructions.
  // The list is built as the instructions are traversed, with the values
  // being the previous HIR Instr before the given instruction. An
  // instruction may have a label assigned to it if it hasn't been hit
  // yet.
  size_t list_size = instr_count_ * sizeof(void*);
  instr_offset_list_ = (Instr**)arena_->Alloc(list_size);
  label_list_ = (Label**)arena_->Alloc(list_size);
  std::memset(instr_offset_list_, 0, list_size);
  std::memset(label_list_, 0, list_size);

  // Always mark entry with label.
  label_list_[0] = NewLabel();

  uint32_t start_address = function_->address();
  uint32_t end_address = function_->end_address();
  InstrData i;
  for (uint32_t address = start_address, offset = 0; address <= end_address;
       address += 4, offset++) {
    i.address = address;
    i.code = xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
    // TODO(benvanik): find a way to avoid using the opcode tables.
    i.type = GetInstrType(i.code);
    trace_info_.dest_count = 0;

    // Mark label, if we were assigned one earlier on in the walk.
    // We may still get a label, but it'll be inserted by LookupLabel
    // as needed.
    Label* label = label_list_[offset];
    if (label) {
      MarkLabel(label);
    }

    Instr* first_instr = 0;
    if (with_debug_info_) {
      if (label) {
        AnnotateLabel(address, label);
      }
      comment_buffer_.Reset();
      comment_buffer_.AppendFormat("%.8X %.8X ", address, i.code);
      DisasmPPC(&i, &comment_buffer_);
      Comment(comment_buffer_);
      first_instr = last_instr();
    }

    // Mark source offset for debugging.
    // We could omit this if we never wanted to debug.
    SourceOffset(i.address);
    if (!first_instr) {
      first_instr = last_instr();
    }

    // Stash instruction offset. It's either the SOURCE_OFFSET or the COMMENT.
    instr_offset_list_[offset] = first_instr;

    if (!i.type) {
      XELOGE("Invalid instruction %.8llX %.8X", i.address, i.code);
      Comment("INVALID!");
      // TraceInvalidInstruction(i);
      continue;
    }
    ++i.type->translation_count;

    // Synchronize the PPC context as required.
    // This will ensure all registers are saved to the PPC context before this
    // instruction executes.
    if (i.type->type & kXEPPCInstrTypeSynchronizeContext) {
      ContextBarrier();
    }

    typedef int (*InstrEmitter)(PPCHIRBuilder& f, InstrData& i);
    InstrEmitter emit = (InstrEmitter)i.type->emit;

    if (i.address == FLAGS_break_on_instruction) {
      Comment("--break-on-instruction target");

      if (FLAGS_break_condition_gpr < 0) {
        DebugBreak();
      } else {
        auto left = LoadGPR(FLAGS_break_condition_gpr);
        auto right = LoadConstantUint64(FLAGS_break_condition_value);
        if (FLAGS_break_condition_truncate) {
          left = Truncate(left, INT32_TYPE);
          right = Truncate(right, INT32_TYPE);
        }
        TrapTrue(CompareEQ(left, right));
      }
    }

    if (!i.type->emit || emit(*this, i)) {
      XELOGE("Unimplemented instr %.8llX %.8X %s", i.address, i.code,
             i.type->name);
      Comment("UNIMPLEMENTED!");
      // DebugBreak();
      // TraceInvalidInstruction(i);
    }
  }

  return Finalize();
}

void PPCHIRBuilder::AnnotateLabel(uint32_t address, Label* label) {
  char name_buffer[13];
  snprintf(name_buffer, xe::countof(name_buffer), "loc_%.8X", address);
  label->name = (char*)arena_->Alloc(sizeof(name_buffer));
  memcpy(label->name, name_buffer, sizeof(name_buffer));
}

Function* PPCHIRBuilder::LookupFunction(uint32_t address) {
  return frontend_->processor()->LookupFunction(address);
}

Label* PPCHIRBuilder::LookupLabel(uint32_t address) {
  if (address < start_address_) {
    return nullptr;
  }
  size_t offset = (address - start_address_) / 4;
  if (offset >= instr_count_) {
    return nullptr;
  }
  Label* label = label_list_[offset];
  if (label) {
    return label;
  }
  // No label. If we haven't yet hit the instruction in the walk
  // then create a label. Otherwise, we must go back and insert
  // the label.
  label = NewLabel();
  label_list_[offset] = label;
  Instr* instr = instr_offset_list_[offset];
  if (instr) {
    if (instr->prev) {
      // Insert label, breaking up existing instructions.
      InsertLabel(label, instr->prev);
    } else {
      // Instruction is at the head of a block, so just add the label.
      MarkLabel(label, instr->block);
    }

    // Annotate the label, as we won't do it later.
    if (with_debug_info_) {
      AnnotateLabel(address, label);
    }
  }
  return label;
}

// Value* PPCHIRBuilder::LoadXER() {
//}
//
// void PPCHIRBuilder::StoreXER(Value* value) {
//}

Value* PPCHIRBuilder::LoadLR() {
  return LoadContext(offsetof(PPCContext, lr), INT64_TYPE);
}

void PPCHIRBuilder::StoreLR(Value* value) {
  assert_true(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, lr), value);

  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 64;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadCTR() {
  return LoadContext(offsetof(PPCContext, ctr), INT64_TYPE);
}

void PPCHIRBuilder::StoreCTR(Value* value) {
  assert_true(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, ctr), value);

  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 65;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadCR() {
  // All bits. This is expensive, but seems to be less used than the
  // field-specific LoadCR.
  Value* v = LoadCR(0);
  for (int i = 1; i <= 7; ++i) {
    v = Or(v, LoadCR(i));
  }
  return v;
}

Value* PPCHIRBuilder::LoadCR(uint32_t n) {
  // Construct the entire word of just the bits we care about.
  // This makes it easier for the optimizer to exclude things, though
  // we could be even more clever and watch sequences.
  Value* v = Shl(ZeroExtend(LoadContext(offsetof(PPCContext, cr0) + (4 * n) + 0,
                                        INT8_TYPE),
                            INT64_TYPE),
                 4 * (7 - n) + 3);
  v = Or(v, Shl(ZeroExtend(LoadContext(offsetof(PPCContext, cr0) + (4 * n) + 1,
                                       INT8_TYPE),
                           INT64_TYPE),
                4 * (7 - n) + 2));
  v = Or(v, Shl(ZeroExtend(LoadContext(offsetof(PPCContext, cr0) + (4 * n) + 2,
                                       INT8_TYPE),
                           INT64_TYPE),
                4 * (7 - n) + 1));
  v = Or(v, Shl(ZeroExtend(LoadContext(offsetof(PPCContext, cr0) + (4 * n) + 3,
                                       INT8_TYPE),
                           INT64_TYPE),
                4 * (7 - n) + 0));
  return v;
}

Value* PPCHIRBuilder::LoadCRField(uint32_t n, uint32_t bit) {
  return LoadContext(offsetof(PPCContext, cr0) + (4 * n) + bit, INT8_TYPE);
}

void PPCHIRBuilder::StoreCR(Value* value) {
  // All bits. This is expensive, but seems to be less used than the
  // field-specific StoreCR.
  for (int i = 0; i <= 7; ++i) {
    StoreCR(i, value);
  }
}

void PPCHIRBuilder::StoreCR(uint32_t n, Value* value) {
  // Pull out the bits we are interested in.
  // Optimization passes will kill any unneeded stores (mostly).
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 0,
               And(Truncate(Shr(value, 4 * (7 - n) + 3), INT8_TYPE),
                   LoadConstantUint8(1)));
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 1,
               And(Truncate(Shr(value, 4 * (7 - n) + 2), INT8_TYPE),
                   LoadConstantUint8(1)));
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 2,
               And(Truncate(Shr(value, 4 * (7 - n) + 1), INT8_TYPE),
                   LoadConstantUint8(1)));
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 3,
               And(Truncate(Shr(value, 4 * (7 - n) + 0), INT8_TYPE),
                   LoadConstantUint8(1)));
}

void PPCHIRBuilder::StoreCRField(uint32_t n, uint32_t bit, Value* value) {
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + bit, value);

  // TODO(benvanik): trace CR.
}

void PPCHIRBuilder::UpdateCR(uint32_t n, Value* lhs, bool is_signed) {
  UpdateCR(n, Truncate(lhs, INT32_TYPE), LoadZeroInt32(), is_signed);
}

void PPCHIRBuilder::UpdateCR(uint32_t n, Value* lhs, Value* rhs,
                             bool is_signed) {
  if (is_signed) {
    Value* lt = CompareSLT(lhs, rhs);
    StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 0, lt);
    Value* gt = CompareSGT(lhs, rhs);
    StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 1, gt);
  } else {
    Value* lt = CompareULT(lhs, rhs);
    StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 0, lt);
    Value* gt = CompareUGT(lhs, rhs);
    StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 1, gt);
  }
  Value* eq = CompareEQ(lhs, rhs);
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 2, eq);

  // Value* so = AllocValue(UINT8_TYPE);
  // StoreContext(offsetof(PPCContext, cr) + (4 * n) + 3, so);

  // TOOD(benvanik): trace CR.
}

void PPCHIRBuilder::UpdateCR6(Value* src_value) {
  // Testing for all 1's and all 0's.
  // if (Rc) CR6 = all_equal | 0 | none_equal | 0
  // TODO(benvanik): efficient instruction?
  StoreContext(offsetof(PPCContext, cr6.cr6_1), LoadZeroInt8());
  StoreContext(offsetof(PPCContext, cr6.cr6_3), LoadZeroInt8());
  StoreContext(offsetof(PPCContext, cr6.cr6_all_equal),
               IsFalse(Not(src_value)));
  StoreContext(offsetof(PPCContext, cr6.cr6_none_equal), IsFalse(src_value));

  // TOOD(benvanik): trace CR.
}

Value* PPCHIRBuilder::LoadFPSCR() {
  return LoadContext(offsetof(PPCContext, fpscr), INT64_TYPE);
}

void PPCHIRBuilder::StoreFPSCR(Value* value) {
  assert_true(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, fpscr), value);

  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 67;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadXER() {
  assert_always();
  return NULL;
}

void PPCHIRBuilder::StoreXER(Value* value) { assert_always(); }

Value* PPCHIRBuilder::LoadCA() {
  return LoadContext(offsetof(PPCContext, xer_ca), INT8_TYPE);
}

void PPCHIRBuilder::StoreCA(Value* value) {
  assert_true(value->type == INT8_TYPE);
  StoreContext(offsetof(PPCContext, xer_ca), value);

  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 66;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadSAT() {
  return LoadContext(offsetof(PPCContext, vscr_sat), INT8_TYPE);
}

void PPCHIRBuilder::StoreSAT(Value* value) {
  value = Truncate(value, INT8_TYPE);
  StoreContext(offsetof(PPCContext, vscr_sat), value);

  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 44;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadGPR(uint32_t reg) {
  return LoadContext(offsetof(PPCContext, r) + reg * 8, INT64_TYPE);
}

void PPCHIRBuilder::StoreGPR(uint32_t reg, Value* value) {
  assert_true(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, r) + reg * 8, value);

  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = reg;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadFPR(uint32_t reg) {
  return LoadContext(offsetof(PPCContext, f) + reg * 8, FLOAT64_TYPE);
}

void PPCHIRBuilder::StoreFPR(uint32_t reg, Value* value) {
  assert_true(value->type == FLOAT64_TYPE);
  StoreContext(offsetof(PPCContext, f) + reg * 8, value);

  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = reg + 32;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadVR(uint32_t reg) {
  return LoadContext(offsetof(PPCContext, v) + reg * 16, VEC128_TYPE);
}

void PPCHIRBuilder::StoreVR(uint32_t reg, Value* value) {
  assert_true(value->type == VEC128_TYPE);
  StoreContext(offsetof(PPCContext, v) + reg * 16, value);

  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 128 + reg;
  trace_reg.value = value;
}

}  // namespace frontend
}  // namespace cpu
}  // namespace xe

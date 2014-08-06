/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_hir_builder.h>

#include <alloy/alloy-private.h>
#include <alloy/frontend/ppc/ppc_context.h>
#include <alloy/frontend/ppc/ppc_disasm.h>
#include <alloy/frontend/ppc/ppc_frontend.h>
#include <alloy/frontend/ppc/ppc_instr.h>
#include <alloy/hir/label.h>
#include <alloy/runtime/runtime.h>

namespace alloy {
namespace frontend {
namespace ppc {

// TODO(benvanik): remove when enums redefined.
using namespace alloy::hir;

using alloy::hir::Label;
using alloy::hir::TypeName;
using alloy::hir::Value;
using alloy::runtime::Runtime;
using alloy::runtime::FunctionInfo;

PPCHIRBuilder::PPCHIRBuilder(PPCFrontend* frontend)
    : HIRBuilder(), frontend_(frontend), comment_buffer_(4096) {}

PPCHIRBuilder::~PPCHIRBuilder() = default;

void PPCHIRBuilder::Reset() {
  start_address_ = 0;
  instr_offset_list_ = NULL;
  label_list_ = NULL;
  with_debug_info_ = false;
  HIRBuilder::Reset();
}

int PPCHIRBuilder::Emit(FunctionInfo* symbol_info, bool with_debug_info) {
  SCOPE_profile_cpu_f("alloy");

  Memory* memory = frontend_->memory();
  const uint8_t* p = memory->membase();

  symbol_info_ = symbol_info;
  start_address_ = symbol_info->address();
  instr_count_ = (symbol_info->end_address() - symbol_info->address()) / 4 + 1;

  with_debug_info_ = with_debug_info;
  if (with_debug_info_) {
    Comment("%s fn %.8X-%.8X %s", symbol_info->module()->name().c_str(),
            symbol_info->address(), symbol_info->end_address(),
            symbol_info->name().c_str());
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
  xe_zero_struct(instr_offset_list_, list_size);
  xe_zero_struct(label_list_, list_size);

  // Always mark entry with label.
  label_list_[0] = NewLabel();

  uint64_t start_address = symbol_info->address();
  uint64_t end_address = symbol_info->end_address();
  InstrData i;
  for (uint64_t address = start_address, offset = 0; address <= end_address;
       address += 4, offset++) {
    i.address = address;
    i.code = poly::load_and_swap<uint32_t>(p + address);
    // TODO(benvanik): find a way to avoid using the opcode tables.
    i.type = GetInstrType(i.code);

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
      DisasmPPC(i, &comment_buffer_);
      Comment("%.8X %.8X %s", address, i.code, comment_buffer_.GetString());
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
      XELOGCPU("Invalid instruction %.8llX %.8X", i.address, i.code);
      Comment("INVALID!");
      // TraceInvalidInstruction(i);
      continue;
    }

    typedef int (*InstrEmitter)(PPCHIRBuilder& f, InstrData& i);
    InstrEmitter emit = (InstrEmitter)i.type->emit;

    if (i.address == FLAGS_break_on_instruction) {
      Comment("--break-on-instruction target");
      DebugBreak();
    }

    if (!i.type->emit || emit(*this, i)) {
      XELOGCPU("Unimplemented instr %.8llX %.8X %s", i.address, i.code,
               i.type->name);
      Comment("UNIMPLEMENTED!");
      // DebugBreak();
      // TraceInvalidInstruction(i);
    }
  }

  return Finalize();
}

void PPCHIRBuilder::AnnotateLabel(uint64_t address, Label* label) {
  char name_buffer[13];
  xesnprintfa(name_buffer, XECOUNT(name_buffer), "loc_%.8X", (uint32_t)address);
  label->name = (char*)arena_->Alloc(sizeof(name_buffer));
  xe_copy_struct(label->name, name_buffer, sizeof(name_buffer));
}

FunctionInfo* PPCHIRBuilder::LookupFunction(uint64_t address) {
  Runtime* runtime = frontend_->runtime();
  FunctionInfo* symbol_info;
  if (runtime->LookupFunctionInfo(address, &symbol_info)) {
    return NULL;
  }
  return symbol_info;
}

Label* PPCHIRBuilder::LookupLabel(uint64_t address) {
  if (address < start_address_) {
    return NULL;
  }
  size_t offset = (address - start_address_) / 4;
  if (offset >= instr_count_) {
    return NULL;
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
}

Value* PPCHIRBuilder::LoadCTR() {
  return LoadContext(offsetof(PPCContext, ctr), INT64_TYPE);
}

void PPCHIRBuilder::StoreCTR(Value* value) {
  assert_true(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, ctr), value);
}

Value* PPCHIRBuilder::LoadCR(uint32_t n) {
  assert_always();
  return 0;
}

Value* PPCHIRBuilder::LoadCRField(uint32_t n, uint32_t bit) {
  return LoadContext(offsetof(PPCContext, cr0) + (4 * n) + bit, INT8_TYPE);
}

void PPCHIRBuilder::StoreCR(uint32_t n, Value* value) {
  // TODO(benvanik): split bits out and store in values.
  assert_always();
}

void PPCHIRBuilder::StoreCRField(uint32_t n, uint32_t bit, Value* value) {
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + bit, value);
}

void PPCHIRBuilder::UpdateCR(uint32_t n, Value* lhs, bool is_signed) {
  UpdateCR(n, lhs, LoadZero(lhs->type), is_signed);
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
}

void PPCHIRBuilder::UpdateCR6(Value* src_value) {
  // Testing for all 1's and all 0's.
  // if (Rc) CR6 = all_equal | 0 | none_equal | 0
  // TODO(benvanik): efficient instruction?
  StoreContext(offsetof(PPCContext, cr6.cr6_all_equal),
               IsFalse(Not(src_value)));
  StoreContext(offsetof(PPCContext, cr6.cr6_none_equal), IsFalse(src_value));
}

Value* PPCHIRBuilder::LoadFPSCR() {
  return LoadContext(offsetof(PPCContext, fpscr), INT64_TYPE);
}

void PPCHIRBuilder::StoreFPSCR(Value* value) {
  assert_true(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, fpscr), value);
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
}

Value* PPCHIRBuilder::LoadSAT() {
  return LoadContext(offsetof(PPCContext, vscr_sat), INT8_TYPE);
}

void PPCHIRBuilder::StoreSAT(Value* value) {
  value = Truncate(value, INT8_TYPE);
  StoreContext(offsetof(PPCContext, vscr_sat), value);
}

Value* PPCHIRBuilder::LoadGPR(uint32_t reg) {
  return LoadContext(offsetof(PPCContext, r) + reg * 8, INT64_TYPE);
}

void PPCHIRBuilder::StoreGPR(uint32_t reg, Value* value) {
  assert_true(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, r) + reg * 8, value);
}

Value* PPCHIRBuilder::LoadFPR(uint32_t reg) {
  return LoadContext(offsetof(PPCContext, f) + reg * 8, FLOAT64_TYPE);
}

void PPCHIRBuilder::StoreFPR(uint32_t reg, Value* value) {
  assert_true(value->type == FLOAT64_TYPE);
  StoreContext(offsetof(PPCContext, f) + reg * 8, value);
}

Value* PPCHIRBuilder::LoadVR(uint32_t reg) {
  return LoadContext(offsetof(PPCContext, v) + reg * 16, VEC128_TYPE);
}

void PPCHIRBuilder::StoreVR(uint32_t reg, Value* value) {
  assert_true(value->type == VEC128_TYPE);
  StoreContext(offsetof(PPCContext, v) + reg * 16, value);
}

Value* PPCHIRBuilder::LoadAcquire(Value* address, TypeName type,
                                  uint32_t load_flags) {
  AtomicExchange(LoadContext(offsetof(PPCContext, reserve_address), INT64_TYPE),
                 Truncate(address, INT32_TYPE));
  return Load(address, type, load_flags);
}

Value* PPCHIRBuilder::StoreRelease(Value* address, Value* value,
                                   uint32_t store_flags) {
  Value* old_address = AtomicExchange(
      LoadContext(offsetof(PPCContext, reserve_address), INT64_TYPE),
      LoadZero(INT32_TYPE));
  Value* eq = CompareEQ(Truncate(address, INT32_TYPE), old_address);
  StoreContext(offsetof(PPCContext, cr0.cr0_eq), eq);
  auto skip_label = NewLabel();
  BranchFalse(eq, skip_label, BRANCH_UNLIKELY);
  Store(address, value, store_flags);
  MarkLabel(skip_label);
  return eq;
}

}  // namespace ppc
}  // namespace frontend
}  // namespace alloy

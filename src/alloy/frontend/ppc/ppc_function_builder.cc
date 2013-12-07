/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_function_builder.h>

#include <alloy/frontend/tracing.h>
#include <alloy/frontend/ppc/ppc_context.h>
#include <alloy/frontend/ppc/ppc_frontend.h>
#include <alloy/frontend/ppc/ppc_instr.h>
#include <alloy/hir/label.h>
#include <alloy/runtime/runtime.h>

using namespace alloy;
using namespace alloy::frontend;
using namespace alloy::frontend::ppc;
using namespace alloy::hir;
using namespace alloy::runtime;


PPCFunctionBuilder::PPCFunctionBuilder(PPCFrontend* frontend) :
    frontend_(frontend),
    FunctionBuilder() {
}

PPCFunctionBuilder::~PPCFunctionBuilder() {
}

void PPCFunctionBuilder::Reset() {
  start_address_ = 0;
  instr_offset_list_ = NULL;
  label_list_ = NULL;
  FunctionBuilder::Reset();
}

const bool FLAGS_annotate_disassembly = true;

int PPCFunctionBuilder::Emit(FunctionInfo* symbol_info) {
  Memory* memory = frontend_->memory();
  const uint8_t* p = memory->membase();

  symbol_info_ = symbol_info;
  start_address_ = symbol_info->address();
  instr_count_ =
      (symbol_info->end_address() - symbol_info->address()) / 4 + 1;

  // TODO(benvanik): get/make up symbol name.
  Comment("%s fn %.8X-%.8X %s",
          symbol_info->module()->name(),
          symbol_info->address(), symbol_info->end_address(),
          "(symbol name)");

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
    i.code = XEGETUINT32BE(p + address);
    // TODO(benvanik): find a way to avoid using the opcode tables.
    i.type = GetInstrType(i.code);

    // Stash instruction offset.
    instr_offset_list_[offset] = last_instr();

    // Mark label, if we were assigned one earlier on in the walk.
    // We may still get a label, but it'll be inserted by LookupLabel
    // as needed.
    Label* label = label_list_[offset];
    if (label) {
      MarkLabel(label);
    }

    if (FLAGS_annotate_disassembly) {
      if (label) {
        AnnotateLabel(address, label);
      }
      if (!i.type) {
        Comment("%.8X: %.8X ???", address, i.code);
      } else if (i.type->disassemble) {
        ppc::InstrDisasm d;
        i.type->disassemble(i, d);
        std::string disasm;
        d.Dump(disasm);
        Comment("%.8X: %.8X %s", address, i.code, disasm.c_str());
      } else {
        Comment("%.8X: %.8X %s ???", address, i.code, i.type->name);
      }
    }

    if (!i.type) {
      XELOGCPU("Invalid instruction %.8X %.8X", i.address, i.code);
      Comment("INVALID!");
      //TraceInvalidInstruction(i);
      continue;
    }

    typedef int (*InstrEmitter)(PPCFunctionBuilder& f, InstrData& i);
    InstrEmitter emit = (InstrEmitter)i.type->emit;

    /*if (i.address == FLAGS_break_on_instruction) {
      Comment("--break-on-instruction target");
      DebugBreak();
    }*/

    if (!i.type->emit || emit(*this, i)) {
      XELOGCPU("Unimplemented instr %.8X %.8X %s",
               i.address, i.code, i.type->name);
      Comment("UNIMPLEMENTED!");
      DebugBreak();
      //TraceInvalidInstruction(i);

      // This printf is handy for sort/uniquify to find instructions.
      printf("unimplinstr %s\n", i.type->name);
    }
  }

  return 0;
}

void PPCFunctionBuilder::AnnotateLabel(uint64_t address, Label* label) {
  char name_buffer[13];
  xesnprintfa(name_buffer, XECOUNT(name_buffer), "loc_%.8X", address);
  label->name = (char*)arena_->Alloc(sizeof(name_buffer));
  xe_copy_struct(label->name, name_buffer, sizeof(name_buffer));
}

FunctionInfo* PPCFunctionBuilder::LookupFunction(uint64_t address) {
  Runtime* runtime = frontend_->runtime();
  FunctionInfo* symbol_info;
  if (runtime->LookupFunctionInfo(address, &symbol_info)) {
    return NULL;
  }
  return symbol_info;
}

Label* PPCFunctionBuilder::LookupLabel(uint64_t address) {
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
  Instr* prev_instr = instr_offset_list_[offset];
  if (prev_instr) {
    // Insert label, breaking up existing instructions.
    InsertLabel(label, prev_instr);

    // Annotate the label, as we won't do it later.
    if (FLAGS_annotate_disassembly) {
      AnnotateLabel(address, label);
    }
  }
  return label;
}

//Value* PPCFunctionBuilder::LoadXER() {
//}
//
//void PPCFunctionBuilder::StoreXER(Value* value) {
//}

Value* PPCFunctionBuilder::LoadLR() {
  return LoadContext(offsetof(PPCContext, lr), INT64_TYPE);
}

void PPCFunctionBuilder::StoreLR(Value* value) {
  XEASSERT(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, lr), value);
}

Value* PPCFunctionBuilder::LoadCTR() {
  return LoadContext(offsetof(PPCContext, ctr), INT64_TYPE);
}

void PPCFunctionBuilder::StoreCTR(Value* value) {
  XEASSERT(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, ctr), value);
}

Value* PPCFunctionBuilder::LoadCR(uint32_t n) {
  XEASSERTALWAYS();
  return 0;
}

Value* PPCFunctionBuilder::LoadCRField(uint32_t n, uint32_t bit) {
  return LoadContext(offsetof(PPCContext, cr0) + (4 * n) + bit, INT8_TYPE);
}

void PPCFunctionBuilder::StoreCR(uint32_t n, Value* value) {
  // TODO(benvanik): split bits out and store in values.
  XEASSERTALWAYS();
}

void PPCFunctionBuilder::UpdateCR(
    uint32_t n, Value* lhs, bool is_signed) {
  UpdateCR(n, lhs, LoadZero(lhs->type), is_signed);
}

void PPCFunctionBuilder::UpdateCR(
    uint32_t n, Value* lhs, Value* rhs, bool is_signed) {
  Value* lt;
  Value* gt;
  if (is_signed) {
    lt = CompareSLT(lhs, rhs);
    gt = CompareSGT(lhs, rhs);
  } else {
    lt = CompareULT(lhs, rhs);
    gt = CompareUGT(lhs, rhs);
  }
  Value* eq = CompareEQ(lhs, rhs);
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 0, lt);
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 1, gt);
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 2, eq);

  // Value* so = AllocValue(UINT8_TYPE);
  // StoreContext(offsetof(PPCContext, cr) + (4 * n) + 3, so);
}

void PPCFunctionBuilder::UpdateCR6(Value* src_value) {
  // Testing for all 1's and all 0's.
  // if (Rc) CR6 = all_equal | 0 | none_equal | 0
  // TODO(benvanik): efficient instruction?
  StoreContext(offsetof(PPCContext, cr6.cr6_all_equal), IsFalse(Not(src_value)));
  StoreContext(offsetof(PPCContext, cr6.cr6_none_equal), IsFalse(src_value));
}

Value* PPCFunctionBuilder::LoadXER() {
  XEASSERTALWAYS();
  return NULL;
}

void PPCFunctionBuilder::StoreXER(Value* value) {
  XEASSERTALWAYS();
}

Value* PPCFunctionBuilder::LoadCA() {
  return LoadContext(offsetof(PPCContext, xer_ca), INT8_TYPE);
}

void PPCFunctionBuilder::StoreCA(Value* value) {
  StoreContext(offsetof(PPCContext, xer_ca), value);
}

Value* PPCFunctionBuilder::LoadGPR(uint32_t reg) {
  return LoadContext(
      offsetof(PPCContext, r) + reg * 8, INT64_TYPE);
}

void PPCFunctionBuilder::StoreGPR(uint32_t reg, Value* value) {
  XEASSERT(value->type == INT64_TYPE);
  StoreContext(
      offsetof(PPCContext, r) + reg * 8, value);
}

Value* PPCFunctionBuilder::LoadFPR(uint32_t reg) {
  return LoadContext(
      offsetof(PPCContext, f) + reg * 8, FLOAT64_TYPE);
}

void PPCFunctionBuilder::StoreFPR(uint32_t reg, Value* value) {
  XEASSERT(value->type == FLOAT64_TYPE);
  StoreContext(
      offsetof(PPCContext, f) + reg * 8, value);
}

Value* PPCFunctionBuilder::LoadVR(uint32_t reg) {
  return LoadContext(
      offsetof(PPCContext, v) + reg * 16, VEC128_TYPE);
}

void PPCFunctionBuilder::StoreVR(uint32_t reg, Value* value) {
  XEASSERT(value->type == VEC128_TYPE);
  StoreContext(
      offsetof(PPCContext, v) + reg * 16, value);
}

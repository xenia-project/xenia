/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_hir_builder.h>

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


PPCHIRBuilder::PPCHIRBuilder(PPCFrontend* frontend) :
    frontend_(frontend),
    HIRBuilder() {
}

PPCHIRBuilder::~PPCHIRBuilder() {
}

void PPCHIRBuilder::Reset() {
  start_address_ = 0;
  instr_offset_list_ = NULL;
  label_list_ = NULL;
  HIRBuilder::Reset();
}

const bool FLAGS_annotate_disassembly = true;

int PPCHIRBuilder::Emit(FunctionInfo* symbol_info) {
  Memory* memory = frontend_->memory();
  const uint8_t* p = memory->membase();

  symbol_info_ = symbol_info;
  start_address_ = symbol_info->address();
  instr_count_ =
      (symbol_info->end_address() - symbol_info->address()) / 4 + 1;

  Comment("%s fn %.8X-%.8X %s",
          symbol_info->module()->name(),
          symbol_info->address(), symbol_info->end_address(),
          symbol_info->name());

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

    Instr* prev_instr = last_instr();

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
        Comment("%.8X %.8X ???", address, i.code);
      } else if (i.type->disassemble) {
        ppc::InstrDisasm d;
        i.type->disassemble(i, d);
        std::string disasm;
        d.Dump(disasm);
        Comment("%.8X %.8X %s", address, i.code, disasm.c_str());
      } else {
        Comment("%.8X %.8X %s ???", address, i.code, i.type->name);
      }
    }

    // Mark source offset for debugging.
    // We could omit this if we never wanted to debug.
    SourceOffset(i.address);

    if (!i.type) {
      instr_offset_list_[offset] = prev_instr->next;
      XELOGCPU("Invalid instruction %.8X %.8X", i.address, i.code);
      Comment("INVALID!");
      //TraceInvalidInstruction(i);
      continue;
    }

    typedef int (*InstrEmitter)(PPCHIRBuilder& f, InstrData& i);
    InstrEmitter emit = (InstrEmitter)i.type->emit;

    /*if (i.address == FLAGS_break_on_instruction) {
      Comment("--break-on-instruction target");
      DebugBreak();
    }*/

    if (!i.type->emit || emit(*this, i)) {
      XELOGCPU("Unimplemented instr %.8X %.8X %s",
               i.address, i.code, i.type->name);
      Comment("UNIMPLEMENTED!");
      //DebugBreak();
      //TraceInvalidInstruction(i);

      // This printf is handy for sort/uniquify to find instructions.
      printf("unimplinstr %s\n", i.type->name);
    }

    // Stash instruction offset. We do this down here so that if the function
    // splits blocks we don't have weird pointers.
    if (prev_instr && prev_instr->next) {
      instr_offset_list_[offset] = prev_instr->next;
    } else if (current_block_) {
      instr_offset_list_[offset] = current_block_->instr_head;
    } else if (block_tail_) {
      instr_offset_list_[offset] = block_tail_->instr_head;
    } else {
      XEASSERTALWAYS();
    }
    XEASSERT(instr_offset_list_[offset]->next->opcode == &OPCODE_TRAP_TRUE_info ||
             instr_offset_list_[offset]->next->src1.offset == i.address);
  }

  return Finalize();
}

void PPCHIRBuilder::AnnotateLabel(uint64_t address, Label* label) {
  char name_buffer[13];
  xesnprintfa(name_buffer, XECOUNT(name_buffer), "loc_%.8X", address);
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
    if (FLAGS_annotate_disassembly) {
      AnnotateLabel(address, label);
    }
  }
  return label;
}

//Value* PPCHIRBuilder::LoadXER() {
//}
//
//void PPCHIRBuilder::StoreXER(Value* value) {
//}

Value* PPCHIRBuilder::LoadLR() {
  return LoadContext(offsetof(PPCContext, lr), INT64_TYPE);
}

void PPCHIRBuilder::StoreLR(Value* value) {
  XEASSERT(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, lr), value);
}

Value* PPCHIRBuilder::LoadCTR() {
  return LoadContext(offsetof(PPCContext, ctr), INT64_TYPE);
}

void PPCHIRBuilder::StoreCTR(Value* value) {
  XEASSERT(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, ctr), value);
}

Value* PPCHIRBuilder::LoadCR(uint32_t n) {
  XEASSERTALWAYS();
  return 0;
}

Value* PPCHIRBuilder::LoadCRField(uint32_t n, uint32_t bit) {
  return LoadContext(offsetof(PPCContext, cr0) + (4 * n) + bit, INT8_TYPE);
}

void PPCHIRBuilder::StoreCR(uint32_t n, Value* value) {
  // TODO(benvanik): split bits out and store in values.
  XEASSERTALWAYS();
}

void PPCHIRBuilder::UpdateCR(
    uint32_t n, Value* lhs, bool is_signed) {
  UpdateCR(n, lhs, LoadZero(lhs->type), is_signed);
}

void PPCHIRBuilder::UpdateCR(
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

void PPCHIRBuilder::UpdateCR6(Value* src_value) {
  // Testing for all 1's and all 0's.
  // if (Rc) CR6 = all_equal | 0 | none_equal | 0
  // TODO(benvanik): efficient instruction?
  StoreContext(offsetof(PPCContext, cr6.cr6_all_equal), IsFalse(Not(src_value)));
  StoreContext(offsetof(PPCContext, cr6.cr6_none_equal), IsFalse(src_value));
}

Value* PPCHIRBuilder::LoadXER() {
  XEASSERTALWAYS();
  return NULL;
}

void PPCHIRBuilder::StoreXER(Value* value) {
  XEASSERTALWAYS();
}

Value* PPCHIRBuilder::LoadCA() {
  return LoadContext(offsetof(PPCContext, xer_ca), INT8_TYPE);
}

void PPCHIRBuilder::StoreCA(Value* value) {
  StoreContext(offsetof(PPCContext, xer_ca), value);
}

Value* PPCHIRBuilder::LoadGPR(uint32_t reg) {
  return LoadContext(
      offsetof(PPCContext, r) + reg * 8, INT64_TYPE);
}

void PPCHIRBuilder::StoreGPR(uint32_t reg, Value* value) {
  XEASSERT(value->type == INT64_TYPE);
  StoreContext(
      offsetof(PPCContext, r) + reg * 8, value);
}

Value* PPCHIRBuilder::LoadFPR(uint32_t reg) {
  return LoadContext(
      offsetof(PPCContext, f) + reg * 8, FLOAT64_TYPE);
}

void PPCHIRBuilder::StoreFPR(uint32_t reg, Value* value) {
  XEASSERT(value->type == FLOAT64_TYPE);
  StoreContext(
      offsetof(PPCContext, f) + reg * 8, value);
}

Value* PPCHIRBuilder::LoadVR(uint32_t reg) {
  return LoadContext(
      offsetof(PPCContext, v) + reg * 16, VEC128_TYPE);
}

void PPCHIRBuilder::StoreVR(uint32_t reg, Value* value) {
  XEASSERT(value->type == VEC128_TYPE);
  StoreContext(
      offsetof(PPCContext, v) + reg * 16, value);
}

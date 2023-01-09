/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/ppc/ppc_hir_builder.h"

#include <stddef.h>
#include <cstring>

#include "third_party/fmt/include/fmt/format.h"

#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/hir/label.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_decode_data.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/xex_module.h"
DEFINE_bool(
    break_on_unimplemented_instructions, true,
    "Break to the host debugger (or crash if no debugger attached) if an "
    "unimplemented PowerPC instruction is encountered.",
    "CPU");

namespace xe {
namespace cpu {
namespace ppc {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Label;
using xe::cpu::hir::TypeName;
using xe::cpu::hir::Value;

// The number of times each opcode has been translated.
// Accumulated across the entire run.
uint32_t opcode_translation_counts[static_cast<int>(PPCOpcode::kInvalid)] = {0};

void DumpAllOpcodeCounts() {
  StringBuffer sb;
  sb.Append("Instruction translation counts:\n");
  for (size_t i = 0; i < xe::countof(opcode_translation_counts); ++i) {
    auto opcode = static_cast<PPCOpcode>(i);
    auto& opcode_info = GetOpcodeInfo(opcode);
    auto& disasm_info = GetOpcodeDisasmInfo(opcode);
    auto translation_count = opcode_translation_counts[i];
    if (translation_count) {
      sb.AppendFormat("{:8d} : {}\n", translation_count, disasm_info.name);
    }
  }
  fprintf(stdout, "%s", sb.to_string().c_str());
  fflush(stdout);
}

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
  // chrispy: i've seen this one happen, not sure why but i think from trying to
  // precompile twice i've also seen ones with a start and end address that are
  // the same...
  assert_true(function_->address() <= function_->end_address());
  instr_count_ = (function_->end_address() - function_->address()) / 4 + 1;

  with_debug_info_ = (flags & EMIT_DEBUG_COMMENTS) == EMIT_DEBUG_COMMENTS;
  if (with_debug_info_) {
    CommentFormat("{} fn {:08X}-{:08X} {}", function_->module()->name().c_str(),
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
  instr_offset_list_ = (Instr**)arena_->Alloc(list_size, alignof(void*));
  label_list_ = (Label**)arena_->Alloc(list_size, alignof(void*));
  std::memset(instr_offset_list_, 0, list_size);
  std::memset(label_list_, 0, list_size);

  // Always mark entry with label.
  label_list_[0] = NewLabel();

  uint32_t start_address = function_->address();
  uint32_t end_address = function_->end_address();
  for (uint32_t address = start_address, offset = 0; address <= end_address;
       address += 4, offset++) {
    trace_info_.dest_count = 0;
    uint32_t code =
        xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
    auto opcode = LookupOpcode(code);
    auto& opcode_info = GetOpcodeInfo(opcode);

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
      comment_buffer_.AppendFormat("{:08X} {:08X} ", address, code);
      DisasmPPC(address, code, &comment_buffer_);
      Comment(comment_buffer_);
      first_instr = last_instr();
    }

    // Mark source offset for debugging.
    // We could omit this if we never wanted to debug.
    SourceOffset(address);
    if (!first_instr) {
      first_instr = last_instr();
    }

    // Stash instruction offset. It's either the SOURCE_OFFSET or the COMMENT.
    instr_offset_list_[offset] = first_instr;

    if (opcode == PPCOpcode::kInvalid) {
      XELOGE("Invalid instruction {:08X} {:08X}", address, code);
      Comment("INVALID!");
      // TraceInvalidInstruction(i);
      continue;
    }
    ++opcode_translation_counts[static_cast<int>(opcode)];

    // Synchronize the PPC context as required.
    // This will ensure all registers are saved to the PPC context before this
    // instruction executes.
    if (opcode_info.type == PPCOpcodeType::kSync) {
      ContextBarrier();
    }

    MaybeBreakOnInstruction(address);

    InstrData i;
    i.address = address;
    i.code = code;
    i.opcode = opcode;
    i.opcode_info = &opcode_info;
    if (!opcode_info.emit || opcode_info.emit(*this, i)) {
      auto& disasm_info = GetOpcodeDisasmInfo(opcode);
      XELOGE(
          "Unimplemented instr {:08X} {:08X} {} - report the game to Xenia "
          "developers; to skip, disable break_on_unimplemented_instructions",
          address, code, disasm_info.name);
      Comment("UNIMPLEMENTED!");
      if (cvars::break_on_unimplemented_instructions) {
        DebugBreak();
      }
    }
  }

  if (false) {
    DumpAllOpcodeCounts();
  }

  return Finalize();
}

void PPCHIRBuilder::MaybeBreakOnInstruction(uint32_t address) {
  if (address != cvars::break_on_instruction) {
    return;
  }

  Comment("--break-on-instruction target");

  if (cvars::break_condition_gpr < 0) {
    DebugBreak();
    return;
  }

  auto left = LoadGPR(cvars::break_condition_gpr);
  auto right = LoadConstantUint64(cvars::break_condition_value);
  if (cvars::break_condition_truncate) {
    left = Truncate(left, INT32_TYPE);
    right = Truncate(right, INT32_TYPE);
  }

  auto op = cvars::break_condition_op.c_str();
  // TODO(rick): table?
  if (xe_strcasecmp(op, "eq") == 0) {
    TrapTrue(CompareEQ(left, right));
  } else if (xe_strcasecmp(op, "ne") == 0) {
    TrapTrue(CompareNE(left, right));
  } else if (xe_strcasecmp(op, "slt") == 0) {
    TrapTrue(CompareSLT(left, right));
  } else if (xe_strcasecmp(op, "sle") == 0) {
    TrapTrue(CompareSLE(left, right));
  } else if (xe_strcasecmp(op, "sgt") == 0) {
    TrapTrue(CompareSGT(left, right));
  } else if (xe_strcasecmp(op, "sge") == 0) {
    TrapTrue(CompareSGE(left, right));
  } else if (xe_strcasecmp(op, "ult") == 0) {
    TrapTrue(CompareULT(left, right));
  } else if (xe_strcasecmp(op, "ule") == 0) {
    TrapTrue(CompareULE(left, right));
  } else if (xe_strcasecmp(op, "ugt") == 0) {
    TrapTrue(CompareUGT(left, right));
  } else if (xe_strcasecmp(op, "uge") == 0) {
    TrapTrue(CompareUGE(left, right));
  } else {
    assert_always();
  }
}

void PPCHIRBuilder::AnnotateLabel(uint32_t address, Label* label) {
  // chrispy: label->name is unused, it would be nice to be able to remove the
  // field and this code
  char name_buffer[13];
  auto format_result = fmt::format_to_n(name_buffer, 12, "loc_{:08X}", address);
  name_buffer[format_result.size] = '\0';
  label->name = (char*)arena_->Alloc(sizeof(name_buffer), 1);
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

  // chrispy: nothing seems to write cr6_1, figure out if no documented
  // instructions write anything other than 0 to it and remove these stores if
  // so
  StoreContext(offsetof(PPCContext, cr6.cr6_1), LoadZeroInt8());
  StoreContext(offsetof(PPCContext, cr6.cr6_3), LoadZeroInt8());
  StoreContext(offsetof(PPCContext, cr6.cr6_all_equal),
               IsFalse(Not(src_value)));
  StoreContext(offsetof(PPCContext, cr6.cr6_none_equal), IsFalse(src_value));

  // TOOD(benvanik): trace CR.
}

Value* PPCHIRBuilder::LoadFPSCR() {
  return LoadContext(offsetof(PPCContext, fpscr), INT32_TYPE);
}

void PPCHIRBuilder::StoreFPSCR(Value* value) {
  assert_true(value->type == INT32_TYPE);
  StoreContext(offsetof(PPCContext, fpscr), value);

  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 67;
  trace_reg.value = value;
}

void PPCHIRBuilder::UpdateFPSCR(Value* result, bool update_cr1) {
  // TODO(benvanik): detect overflow and nan cases.
  // fx and vx are the most important.
  /*
    chrispy: i stubbed this out at one point because all it does is waste
     memory and CPU time, however, this introduced issues with raiden
    (substitute w/ titleid later) which probably means they stash stuff in the
    fpscr?

  */

  Value* fx = LoadConstantInt8(0);
  Value* fex = LoadConstantInt8(0);
  Value* vx = LoadConstantInt8(0);
  Value* ox = LoadConstantInt8(0);

  if (update_cr1) {
    // Store into the CR1 field.
    // We do this instead of just calling CopyFPSCRToCR1 so that we don't
    // have to read back the bits and do shifting work.
    StoreContext(offsetof(PPCContext, cr1.cr1_fx), fx);
    StoreContext(offsetof(PPCContext, cr1.cr1_fex), fex);
    StoreContext(offsetof(PPCContext, cr1.cr1_vx), vx);
    StoreContext(offsetof(PPCContext, cr1.cr1_ox), ox);
  }

  // Generate our new bits.
  Value* new_bits = Shl(ZeroExtend(fx, INT32_TYPE), 31);
  new_bits = Or(new_bits, Shl(ZeroExtend(fex, INT32_TYPE), 30));
  new_bits = Or(new_bits, Shl(ZeroExtend(vx, INT32_TYPE), 29));
  new_bits = Or(new_bits, Shl(ZeroExtend(ox, INT32_TYPE), 28));

  // Mix into fpscr while preserving sticky bits (FX and OX).
  Value* bits = LoadFPSCR();
  bits = Or(And(bits, LoadConstantUint32(0x9FFFFFFF)), new_bits);
  StoreFPSCR(bits);
}

void PPCHIRBuilder::CopyFPSCRToCR1() {
  // Pull out of FPSCR.
  Value* fpscr = LoadFPSCR();
  StoreContext(offsetof(PPCContext, cr1.cr1_fx),
               And(Truncate(Shr(fpscr, 31), INT8_TYPE), LoadConstantInt8(1)));
  StoreContext(offsetof(PPCContext, cr1.cr1_fex),
               And(Truncate(Shr(fpscr, 30), INT8_TYPE), LoadConstantInt8(1)));
  StoreContext(offsetof(PPCContext, cr1.cr1_vx),
               And(Truncate(Shr(fpscr, 29), INT8_TYPE), LoadConstantInt8(1)));
  StoreContext(offsetof(PPCContext, cr1.cr1_ox),
               And(Truncate(Shr(fpscr, 28), INT8_TYPE), LoadConstantInt8(1)));
}

Value* PPCHIRBuilder::LoadXER() {
  Value* v = Shl(ZeroExtend(LoadCA(), INT64_TYPE), 29);
  // TODO(benvanik): construct with other flags; overflow, etc?
  return v;
}

void PPCHIRBuilder::StoreXER(Value* value) {
  // TODO(benvanik): use other fields? For now, just pull out CA.
  StoreCA(Truncate(And(Shr(value, 29), LoadConstantInt64(1)), INT8_TYPE));
}

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

void PPCHIRBuilder::StoreReserved(Value* val) {
  assert_true(val->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, reserved_val), val);
}

Value* PPCHIRBuilder::LoadReserved() {
  return LoadContext(offsetof(PPCContext, reserved_val), INT64_TYPE);
}
void PPCHIRBuilder::SetReturnAddress(Value* value) {
  /*
     Record the address as being a possible target of a return. This is
     needed for longjmp emulation. See x64_emitter.cc's ResolveFunction
  */
  Module* mod = this->function_->module();
  if (value && value->IsConstant()) {
    if (mod) {
      XexModule* xexmod = dynamic_cast<XexModule*>(mod);
      if (xexmod) {
        auto flags = xexmod->GetInstructionAddressFlags(value->AsUint32());
        if (flags) {
          flags->is_return_site = true;
        }
      }
    }
  }

  HIRBuilder::SetReturnAddress(value);
}
}  // namespace ppc
}  // namespace cpu
}  // namespace xe

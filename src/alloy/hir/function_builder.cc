/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/hir/function_builder.h>

#include <alloy/hir/block.h>
#include <alloy/hir/instr.h>
#include <alloy/hir/label.h>

using namespace alloy;
using namespace alloy::hir;
using namespace alloy::runtime;


#define ASSERT_ADDRESS_TYPE(value)
#define ASSERT_INTEGER_TYPE(value)
#define ASSERT_FLOAT_TYPE(value)
#define ASSERT_NON_VECTOR_TYPE(value)
#define ASSERT_VECTOR_TYPE(value)
#define ASSERT_TYPES_EQUAL(value1, value2)


FunctionBuilder::FunctionBuilder() {
  arena_ = new Arena();
  Reset();
}

FunctionBuilder::~FunctionBuilder() {
  Reset();
  delete arena_;
}

void FunctionBuilder::Reset() {
  attributes_ = 0;
  next_label_id_ = 0;
  next_value_ordinal_ = 0;
  block_head_ = block_tail_ = NULL;
  current_block_ = NULL;
}

void FunctionBuilder::DumpValue(StringBuffer* str, Value* value) {
  if (value->IsConstant()) {
    switch (value->type) {
    case INT8_TYPE:     str->Append("%X", value->constant.i8);  break;
    case INT16_TYPE:    str->Append("%X", value->constant.i16); break;
    case INT32_TYPE:    str->Append("%X", value->constant.i32); break;
    case INT64_TYPE:    str->Append("%X", value->constant.i64); break;
    case FLOAT32_TYPE:  str->Append("%F", value->constant.f32); break;
    case FLOAT64_TYPE:  str->Append("%F", value->constant.f64); break;
    case VEC128_TYPE:   str->Append("(%F,%F,%F,%F)",
                                    value->constant.v128.x,
                                    value->constant.v128.y,
                                    value->constant.v128.z,
                                    value->constant.v128.w); break;
    default: XEASSERTALWAYS(); break;
    }
  } else {
    static const char* type_names[] = {
      "i8", "i16", "i32", "i64", "f32", "f64", "v128",
    };
    str->Append("v%d.%s", value->ordinal, type_names[value->type]);
  }
}

void FunctionBuilder::DumpOp(
    StringBuffer* str, OpcodeSignatureType sig_type, Instr::Op* op) {
  switch (sig_type) {
  case OPCODE_SIG_TYPE_X:
    break;
  case OPCODE_SIG_TYPE_L:
    if (op->label->name) {
      str->Append(op->label->name);
    } else {
      str->Append("label%d", op->label->id);
    }
    break;
  case OPCODE_SIG_TYPE_O:
    str->Append("+%d", op->offset);
    break;
  case OPCODE_SIG_TYPE_S:
    str->Append("<function>");
    break;
  case OPCODE_SIG_TYPE_V:
    DumpValue(str, op->value);
    break;
  }
}

void FunctionBuilder::Dump(StringBuffer* str) {
  if (attributes_) {
    str->Append("; attributes = %.8X\n", attributes_);
  }

  uint32_t block_ordinal = 0;
  Block* block = block_head_;
  while (block) {
    if (block == block_head_) {
      str->Append("<entry>:\n");
    } else {
      str->Append("<block%d>:\n", block_ordinal);
    }
    block_ordinal++;

    Label* label = block->label_head;
    while (label) {
      if (label->name) {
        str->Append("%s:\n", label->name);
      } else {
        str->Append("label%d:\n", label->id);
      }
      label = label->next;
    }

    Instr* i = block->instr_head;
    while (i) {
      if (i->opcode->num == OPCODE_COMMENT) {
        str->Append("  ; %s\n", (char*)i->src1.offset);
        i = i->next;
        continue;
      }

      const OpcodeInfo* info = i->opcode;
      OpcodeSignatureType dest_type = GET_OPCODE_SIG_TYPE_DEST(info->signature);
      OpcodeSignatureType src1_type = GET_OPCODE_SIG_TYPE_SRC1(info->signature);
      OpcodeSignatureType src2_type = GET_OPCODE_SIG_TYPE_SRC2(info->signature);
      OpcodeSignatureType src3_type = GET_OPCODE_SIG_TYPE_SRC3(info->signature);
      str->Append("  ");
      if (dest_type) {
        DumpValue(str, i->dest);
        str->Append(" = ");
      }
      if (i->flags) {
        str->Append("%s.%d", info->name, i->flags);
      } else {
        str->Append("%s", info->name);
      }
      if (src1_type) {
        str->Append(" ");
        DumpOp(str, src1_type, &i->src1);
      }
      if (src2_type) {
        str->Append(", ");
        DumpOp(str, src2_type, &i->src2);
      }
      if (src3_type) {
        str->Append(", ");
        DumpOp(str, src3_type, &i->src3);
      }
      str->Append("\n");
      i = i->next;
    }

    block = block->next;
  }
}

Block* FunctionBuilder::current_block() const {
  return current_block_;
}

Instr* FunctionBuilder::last_instr() const {
  if (current_block_ && current_block_->instr_tail) {
    return current_block_->instr_tail;
  } else if (block_tail_) {
    return block_tail_->instr_tail;
  }
  return NULL;
}

Label* FunctionBuilder::NewLabel() {
  Label* label = arena_->Alloc<Label>();
  label->next = label->prev = NULL;
  label->block = NULL;
  label->id = next_label_id_++;
  label->name = NULL;
  return label;
}

void FunctionBuilder::MarkLabel(Label* label) {
  if (current_block_ && current_block_->instr_tail) {
    EndBlock();
  }
  if (!current_block_) {
    AppendBlock();
  }
  Block* block = current_block_;
  label->block = block;
  label->prev = block->label_tail;
  label->next = NULL;
  if (label->prev) {
    label->prev->next = label;
    block->label_tail = label;
  } else {
    block->label_head = block->label_tail = label;
  }
}

void FunctionBuilder::InsertLabel(Label* label, Instr* prev_instr) {
  // If we are adding to the end just use the normal path.
  if (prev_instr == last_instr()) {
    MarkLabel(label);
    return;
  }

  // If we are adding at the last instruction in a block just mark
  // the following block with a label.
  if (!prev_instr->next) {
    Block* next_block = prev_instr->block->next;
    if (next_block) {
      label->block = next_block;
      label->next = NULL;
      label->prev = next_block->label_tail;
      next_block->label_tail = label;
      if (label->prev) {
        label->prev->next = label;
      } else {
        next_block->label_head = label;
      }
      return;
    } else {
      // No next block, which means we are at the end.
      MarkLabel(label);
      return;
    }
  }

  // In the middle of a block. Split the block in two and insert
  // the new block in the middle.
  // B1.I, B1.I, <insert> B1.I, B1.I
  // B1.I, B1.I, <insert> BN.I, BN.I
  Block* prev_block = prev_instr->block;
  Block* next_block = prev_instr->block->next;

  Block* new_block = arena_->Alloc<Block>();
  new_block->prev = prev_block;
  new_block->next = next_block;
  if (prev_block) {
    prev_block->next = new_block;
  } else {
    block_head_ = new_block;
  }
  if (next_block) {
    next_block->prev = new_block;
  } else {
    block_tail_ = new_block;
  }
  new_block->label_head = new_block->label_tail = label;
  label->block = new_block;
  label->prev = label->next = NULL;

  new_block->instr_head = prev_instr->next;
  if (new_block->instr_head) {
    new_block->instr_head->prev = NULL;
    new_block->instr_tail = prev_block->instr_tail;
  }
  prev_instr->next = NULL;
  prev_block->instr_tail = prev_instr;

  if (current_block_ == prev_block) {
    current_block_ = new_block;
  }
}

Block* FunctionBuilder::AppendBlock() {
  Block* block = arena_->Alloc<Block>();
  block->next = NULL;
  block->prev = block_tail_;
  if (block_tail_) {
    block_tail_->next = block;
  }
  block_tail_ = block;
  if (!block_head_) {
    block_head_ = block;
  }
  current_block_ = block;
  block->label_head = block->label_tail = NULL;
  block->instr_head = block->instr_tail = NULL;
  return block;
}

void FunctionBuilder::EndBlock() {
  if (current_block_ && !current_block_->instr_tail) {
    // Block never had anything added to it. Since it likely has an
    // incoming edge, just keep it around.
    return;
  }
  current_block_ = NULL;
}

Instr* FunctionBuilder::AppendInstr(
    const OpcodeInfo& opcode_info, uint16_t flags, Value* dest) {
  if (!current_block_) {
    AppendBlock();
  }
  Block* block = current_block_;

  Instr* instr = arena_->Alloc<Instr>();
  instr->next = NULL;
  instr->prev = block->instr_tail;
  if (block->instr_tail) {
    block->instr_tail->next = instr;
  }
  block->instr_tail = instr;
  if (!block->instr_head) {
    block->instr_head = instr;
  }
  instr->block = block;
  instr->opcode = &opcode_info;
  instr->flags = flags;
  instr->dest = dest;
  // Rely on callers to set src args.
  // This prevents redundant stores.
  return instr;
}

Value* FunctionBuilder::AllocValue(TypeName type) {
  Value* value = arena_->Alloc<Value>();
  value->ordinal = next_value_ordinal_++;
  value->type = type;
  value->flags = 0;
  value->tag = NULL;
  return value;
}

Value* FunctionBuilder::CloneValue(Value* source) {
  Value* value = arena_->Alloc<Value>();
  value->ordinal = next_value_ordinal_++;
  value->type = source->type;
  value->flags = source->flags;
  value->constant.i64 = source->constant.i64;
  value->tag = NULL;
  return value;
}

#define STATIC_OPCODE(num, name, sig, flags) \
    static const OpcodeInfo opcode = { flags, sig, name, num, };

void FunctionBuilder::Comment(const char* format, ...) {
  STATIC_OPCODE(
      OPCODE_COMMENT,
      "comment",
      OPCODE_SIG_X,
      OPCODE_FLAG_IGNORE);

  char buffer[1024];
  va_list args;
  va_start(args, format);
  xevsnprintfa(buffer, 1024, format, args);
  va_end(args);
  size_t len = xestrlena(buffer);
  if (!len) {
    return;
  }
  void* p = arena_->Alloc(len + 1);
  xe_copy_struct(p, buffer, len + 1);
  Instr* i = AppendInstr(opcode, 0);
  i->src1.offset = (uint64_t)p;
  i->src2.value = i->src3.value = NULL;
}

void FunctionBuilder::Nop() {
  STATIC_OPCODE(
      OPCODE_NOP,
      "nop",
      OPCODE_SIG_X,
      OPCODE_FLAG_IGNORE);

  Instr* i = AppendInstr(opcode, 0);
  i->src1.value = i->src2.value = i->src3.value = NULL;
}

void FunctionBuilder::DebugBreak() {
  STATIC_OPCODE(
      OPCODE_DEBUG_BREAK,
      "debug_break",
      OPCODE_SIG_X,
      OPCODE_FLAG_VOLATILE);

  Instr* i = AppendInstr(opcode, 0);
  i->src1.value = i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::DebugBreakTrue(Value* cond) {
  STATIC_OPCODE(
      OPCODE_DEBUG_BREAK_TRUE,
      "debug_break_true",
      OPCODE_SIG_X_V,
      OPCODE_FLAG_VOLATILE);

  if (cond->IsConstantTrue()) {
    DebugBreak();
    return;
  }

  Instr* i = AppendInstr(opcode, 0);
  i->src1.value = cond;
  i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::Trap() {
  STATIC_OPCODE(
      OPCODE_TRAP,
      "trap",
      OPCODE_SIG_X,
      OPCODE_FLAG_VOLATILE);

  Instr* i = AppendInstr(opcode, 0);
  i->src1.value = i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::TrapTrue(Value* cond) {
  STATIC_OPCODE(
      OPCODE_TRAP_TRUE,
      "trap_true",
      OPCODE_SIG_X_V,
      OPCODE_FLAG_VOLATILE);

  if (cond->IsConstantTrue()) {
    Trap();
    return;
  }

  Instr* i = AppendInstr(opcode, 0);
  i->src1.value = cond;
  i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::Call(
    FunctionInfo* symbol_info, uint32_t call_flags) {
  STATIC_OPCODE(
      OPCODE_CALL,
      "call",
      OPCODE_SIG_X_S,
      OPCODE_FLAG_BRANCH);

  Instr* i = AppendInstr(opcode, call_flags);
  i->src1.symbol_info = symbol_info;
  i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::CallTrue(
    Value* cond, FunctionInfo* symbol_info, uint32_t call_flags) {
  STATIC_OPCODE(
      OPCODE_CALL_TRUE,
      "call_true",
      OPCODE_SIG_X_V_S,
      OPCODE_FLAG_BRANCH);

  if (cond->IsConstantTrue()) {
    Call(symbol_info, call_flags);
    return;
  }

  Instr* i = AppendInstr(opcode, call_flags);
  i->src1.value = cond;
  i->src2.symbol_info = symbol_info;
  i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::CallIndirect(
    Value* value, uint32_t call_flags) {
  STATIC_OPCODE(
      OPCODE_CALL_INDIRECT,
      "call_indirect",
      OPCODE_SIG_X_V,
      OPCODE_FLAG_BRANCH);

  ASSERT_ADDRESS_TYPE(value);
  Instr* i = AppendInstr(opcode, call_flags);
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::CallIndirectTrue(
    Value* cond, Value* value, uint32_t call_flags) {
  STATIC_OPCODE(
      OPCODE_CALL_INDIRECT_TRUE,
      "call_indirect_true",
      OPCODE_SIG_X_V_V,
      OPCODE_FLAG_BRANCH);

  if (cond->IsConstantTrue()) {
    CallIndirect(value, call_flags);
    return;
  }

  ASSERT_ADDRESS_TYPE(value);
  Instr* i = AppendInstr(opcode, call_flags);
  i->src1.value = cond;
  i->src2.value = value;
  i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::Return() {
  STATIC_OPCODE(
    OPCODE_RETURN,
    "return",
    OPCODE_SIG_X,
    OPCODE_FLAG_BRANCH);

  Instr* i = AppendInstr(opcode, 0);
  i->src1.value = i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::Branch(Label* label, uint32_t branch_flags) {
  STATIC_OPCODE(
    OPCODE_BRANCH,
    "branch",
    OPCODE_SIG_X_L,
    OPCODE_FLAG_BRANCH);

  Instr* i = AppendInstr(opcode, branch_flags);
  i->src1.label = label;
  i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::BranchIf(
    Value* cond, Label* true_label, Label* false_label, uint32_t branch_flags) {
  STATIC_OPCODE(
      OPCODE_BRANCH_IF,
      "branch_if",
      OPCODE_SIG_X_V_L_L,
      OPCODE_FLAG_BRANCH);

  if (cond->IsConstantTrue()) {
    Branch(true_label, branch_flags);
    return;
  } else if (cond->IsConstantFalse()) {
    Branch(false_label, branch_flags);
    return;
  }

  Instr* i = AppendInstr(opcode, branch_flags);
  i->src1.value = cond;
  i->src2.label = true_label;
  i->src3.label = false_label;
  EndBlock();
}

void FunctionBuilder::BranchTrue(
    Value* cond, Label* label, uint32_t branch_flags) {
  STATIC_OPCODE(
      OPCODE_BRANCH_TRUE,
      "branch_true",
      OPCODE_SIG_X_V_L,
      OPCODE_FLAG_BRANCH);

  if (cond->IsConstantTrue()) {
    Branch(label, branch_flags);
    return;
  }

  Instr* i = AppendInstr(opcode, branch_flags);
  i->src1.value = cond;
  i->src2.label = label;
  i->src3.value = NULL;
  EndBlock();
}

void FunctionBuilder::BranchFalse(
    Value* cond, Label* label, uint32_t branch_flags) {
  STATIC_OPCODE(
      OPCODE_BRANCH_FALSE,
      "branch_false",
      OPCODE_SIG_X_V_L,
      OPCODE_FLAG_BRANCH);

  if (cond->IsConstantFalse()) {
    Branch(label, branch_flags);
    return;
  }

  Instr* i = AppendInstr(opcode, branch_flags);
  i->src1.value = cond;
  i->src2.label = label;
  i->src3.value = NULL;
  EndBlock();
}

// phi type_name, Block* b1, Value* v1, Block* b2, Value* v2, etc

Value* FunctionBuilder::Assign(Value* value) {
  STATIC_OPCODE(
      OPCODE_ASSIGN,
      "assign",
      OPCODE_SIG_V_V,
      0);

  if (value->IsConstant()) {
    return value;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Cast(Value* value, TypeName target_type) {
  STATIC_OPCODE(
      OPCODE_CAST,
      "cast",
      OPCODE_SIG_V_V,
      0);

  if (value->type == target_type) {
    return value;
  } else if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Cast(target_type);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(target_type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::ZeroExtend(Value* value, TypeName target_type) {
  STATIC_OPCODE(
      OPCODE_ZERO_EXTEND,
      "zero_extend",
      OPCODE_SIG_V_V,
      0);

  if (value->type == target_type) {
    return value;
  } else if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->ZeroExtend(target_type);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(target_type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::SignExtend(Value* value, TypeName target_type) {
  STATIC_OPCODE(
      OPCODE_SIGN_EXTEND,
      "sign_extend",
      OPCODE_SIG_V_V,
      0);

  if (value->type == target_type) {
    return value;
  } else if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->SignExtend(target_type);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(target_type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Truncate(Value* value, TypeName target_type) {
  STATIC_OPCODE(
      OPCODE_TRUNCATE,
      "truncate",
      OPCODE_SIG_V_V,
      0);

  ASSERT_INTEGER_TYPE(value->type);
  ASSERT_INTEGER_TYPE(target_type);

  if (value->type == target_type) {
    return value;
  } else if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Truncate(target_type);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(target_type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Convert(Value* value, TypeName target_type,
                                RoundMode round_mode) {
  STATIC_OPCODE(
      OPCODE_CONVERT,
      "convert",
      OPCODE_SIG_V_V,
      0);

  if (value->type == target_type) {
    return value;
  } else if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Convert(target_type, round_mode);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, round_mode,
      AllocValue(target_type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Round(Value* value, RoundMode round_mode) {
  STATIC_OPCODE(
      OPCODE_ROUND,
      "round",
      OPCODE_SIG_V_V,
      0);

  ASSERT_FLOAT_TYPE(value);

  if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Round(round_mode);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, round_mode,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::VectorConvertI2F(Value* value) {
  STATIC_OPCODE(
      OPCODE_VECTOR_CONVERT_I2F,
      "vector_convert_i2f",
      OPCODE_SIG_V_V,
      0);

  ASSERT_VECTOR_TYPE(value);

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::VectorConvertF2I(Value* value, RoundMode round_mode) {
  STATIC_OPCODE(
      OPCODE_VECTOR_CONVERT_F2I,
      "vector_convert_f2i",
      OPCODE_SIG_V_V,
      0);

  ASSERT_VECTOR_TYPE(value);

  Instr* i = AppendInstr(
      opcode, round_mode,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::LoadZero(TypeName type) {
  // TODO(benvanik): cache zeros per block/fn? Prevents tons of dupes.
  Value* dest = AllocValue();
  dest->set_zero(type);
  return dest;
}

Value* FunctionBuilder::LoadConstant(int8_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadConstant(uint8_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadConstant(int16_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadConstant(uint16_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadConstant(int32_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadConstant(uint32_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadConstant(int64_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadConstant(uint64_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadConstant(float value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadConstant(double value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadConstant(const vec128_t& value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* FunctionBuilder::LoadContext(size_t offset, TypeName type) {
  STATIC_OPCODE(
      OPCODE_LOAD_CONTEXT,
      "load_context",
      OPCODE_SIG_V_O,
      0);

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(type));
  i->src1.offset = offset;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

void FunctionBuilder::StoreContext(size_t offset, Value* value) {
  STATIC_OPCODE(
      OPCODE_STORE_CONTEXT,
      "store_context",
      OPCODE_SIG_X_O_V,
      0);

  Instr* i = AppendInstr(opcode, 0);
  i->src1.offset = offset;
  i->src2.value = value;
  i->src3.value = NULL;
}

Value* FunctionBuilder::Load(
    Value* address, TypeName type, uint32_t load_flags) {
  STATIC_OPCODE(
      OPCODE_LOAD,
      "load",
      OPCODE_SIG_V_V,
      OPCODE_FLAG_MEMORY);

  ASSERT_ADDRESS_TYPE(address);
  Instr* i = AppendInstr(
      opcode, load_flags,
      AllocValue(type));
  i->src1.value = address;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::LoadAcquire(
    Value* address, TypeName type, uint32_t load_flags) {
  STATIC_OPCODE(
      OPCODE_LOAD_ACQUIRE,
      "load_acquire",
      OPCODE_SIG_V_V,
      OPCODE_FLAG_MEMORY | OPCODE_FLAG_VOLATILE);

  ASSERT_ADDRESS_TYPE(address);
  Instr* i = AppendInstr(
      opcode, load_flags,
      AllocValue(type));
  i->src1.value = address;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

void FunctionBuilder::Store(
    Value* address, Value* value, uint32_t store_flags) {
  STATIC_OPCODE(
      OPCODE_STORE,
      "store",
      OPCODE_SIG_X_V_V,
      OPCODE_FLAG_MEMORY);

  ASSERT_ADDRESS_TYPE(address);
  Instr* i = AppendInstr(opcode, store_flags);
  i->src1.value = address;
  i->src2.value = value;
  i->src3.value = NULL;
}

Value* FunctionBuilder::StoreRelease(
    Value* address, Value* value, uint32_t store_flags) {
  STATIC_OPCODE(
      OPCODE_STORE_RELEASE,
      "store_release",
      OPCODE_SIG_V_V_V,
      OPCODE_FLAG_MEMORY | OPCODE_FLAG_VOLATILE);

  ASSERT_ADDRESS_TYPE(address);
  Instr* i = AppendInstr(opcode, store_flags,
      AllocValue(INT8_TYPE));
  i->src1.value = address;
  i->src2.value = value;
  i->src3.value = NULL;
  return i->dest;
}

void FunctionBuilder::Prefetch(
    Value* address, size_t length, uint32_t prefetch_flags) {
  STATIC_OPCODE(
      OPCODE_PREFETCH,
      "prefetch",
      OPCODE_SIG_X_V_O,
      0);

  ASSERT_ADDRESS_TYPE(address);
  Instr* i = AppendInstr(opcode, prefetch_flags);
  i->src1.value = address;
  i->src2.offset = length;
  i->src3.value = NULL;
}

Value* FunctionBuilder::Max(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_MAX,
      "max",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1->type != VEC128_TYPE &&
      value1->IsConstant() && value2->IsConstant()) {
    return value1->Compare(OPCODE_COMPARE_SLT, value2) ? value2 : value1;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Min(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_MIN,
      "min",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1->type != VEC128_TYPE &&
      value1->IsConstant() && value2->IsConstant()) {
    return value1->Compare(OPCODE_COMPARE_SLT, value2) ? value1 : value2;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Select(Value* cond, Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_SELECT,
      "select",
      OPCODE_SIG_V_V_V_V,
      0);

  XEASSERT(cond->type == INT8_TYPE); // for now
  ASSERT_TYPES_EQUAL(value1, value2);

  if (cond->IsConstant()) {
    return cond->IsConstantTrue() ? value1 : value2;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = cond;
  i->src2.value = value1;
  i->src3.value = value2;
  return i->dest;
}

Value* FunctionBuilder::IsTrue(Value* value) {
  STATIC_OPCODE(
      OPCODE_IS_TRUE,
      "is_true",
      OPCODE_SIG_V_V,
      0);

  if (value->IsConstant()) {
    return LoadConstant(value->IsConstantTrue() ? 1 : 0);
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(INT8_TYPE));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::IsFalse(Value* value) {
  STATIC_OPCODE(
      OPCODE_IS_FALSE,
      "is_false",
      OPCODE_SIG_V_V,
      0);

  if (value->IsConstant()) {
    return LoadConstant(value->IsConstantFalse() ? 1 : 0);
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(INT8_TYPE));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::CompareXX(
    const OpcodeInfo& opcode, Value* value1, Value* value2) {
  ASSERT_TYPES_EQUAL(value1, value2);
  if (value1->IsConstant() && value2->IsConstant()) {
    return LoadConstant(value1->Compare(opcode.num, value2) ? 1 : 0);
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(INT8_TYPE));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::CompareEQ(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_COMPARE_EQ,
      "compare_eq",
      OPCODE_SIG_V_V_V,
      OPCODE_FLAG_COMMUNATIVE);
  return CompareXX(opcode, value1, value2);
}

Value* FunctionBuilder::CompareNE(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_COMPARE_NE,
      "compare_ne",
      OPCODE_SIG_V_V_V,
      OPCODE_FLAG_COMMUNATIVE);
  return CompareXX(opcode, value1, value2);
}

Value* FunctionBuilder::CompareSLT(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_COMPARE_SLT,
      "compare_slt",
      OPCODE_SIG_V_V_V,
      0);
  return CompareXX(opcode, value1, value2);
}

Value* FunctionBuilder::CompareSLE(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_COMPARE_SLE,
      "compare_sle",
      OPCODE_SIG_V_V_V,
      0);
  return CompareXX(opcode, value1, value2);
}

Value* FunctionBuilder::CompareSGT(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_COMPARE_SGT,
      "compare_sgt",
      OPCODE_SIG_V_V_V,
      0);
  return CompareXX(opcode, value1, value2);
}

Value* FunctionBuilder::CompareSGE(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_COMPARE_SGE,
      "compare_sge",
      OPCODE_SIG_V_V_V,
      0);
  return CompareXX(opcode, value1, value2);
}

Value* FunctionBuilder::CompareULT(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_COMPARE_ULT,
      "compare_ult",
      OPCODE_SIG_V_V_V,
      0);
  return CompareXX(opcode, value1, value2);
}

Value* FunctionBuilder::CompareULE(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_COMPARE_ULE,
      "compare_ule",
      OPCODE_SIG_V_V_V,
      0);
  return CompareXX(opcode, value1, value2);
}

Value* FunctionBuilder::CompareUGT(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_COMPARE_UGT,
      "compare_ugt",
      OPCODE_SIG_V_V_V,
      0);
  return CompareXX(opcode, value1, value2);
}

Value* FunctionBuilder::CompareUGE(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_COMPARE_UGE,
      "compare_uge",
      OPCODE_SIG_V_V_V,
      0);
  return CompareXX(opcode, value1, value2);
}

Value* FunctionBuilder::DidCarry(Value* value) {
  STATIC_OPCODE(
      OPCODE_DID_CARRY,
      "did_carry",
      OPCODE_SIG_V_V,
      0);

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(INT8_TYPE));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::DidOverflow(Value* value) {
  STATIC_OPCODE(
      OPCODE_DID_OVERFLOW,
      "did_overflow",
      OPCODE_SIG_V_V,
      0);

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(INT8_TYPE));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::VectorCompareXX(
    const OpcodeInfo& opcode, Value* value1, Value* value2,
    TypeName part_type) {
  ASSERT_TYPES_EQUAL(value1, value2);

  // TODO(benvanik): check how this is used - sometimes I think it's used to
  //     load bitmasks and may be worth checking constants on.

  Instr* i = AppendInstr(
      opcode, part_type,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::VectorCompareEQ(
    Value* value1, Value* value2, TypeName part_type) {
  STATIC_OPCODE(
      OPCODE_VECTOR_COMPARE_EQ,
      "vector_compare_eq",
      OPCODE_SIG_V_V_V,
      0);
  return VectorCompareXX(opcode, value1, value2, part_type);
}

Value* FunctionBuilder::VectorCompareSGT(
    Value* value1, Value* value2, TypeName part_type) {
  STATIC_OPCODE(
      OPCODE_VECTOR_COMPARE_SGT,
      "vector_compare_sgt",
      OPCODE_SIG_V_V_V,
      0);
  return VectorCompareXX(opcode, value1, value2, part_type);
}

Value* FunctionBuilder::VectorCompareSGE(
    Value* value1, Value* value2, TypeName part_type) {
  STATIC_OPCODE(
      OPCODE_VECTOR_COMPARE_SGE,
      "vector_compare_sge",
      OPCODE_SIG_V_V_V,
      0);
  return VectorCompareXX(opcode, value1, value2, part_type);
}

Value* FunctionBuilder::VectorCompareUGT(
    Value* value1, Value* value2, TypeName part_type) {
  STATIC_OPCODE(
      OPCODE_VECTOR_COMPARE_UGT,
      "vector_compare_ugt",
      OPCODE_SIG_V_V_V,
      0);
  return VectorCompareXX(opcode, value1, value2, part_type);
}

Value* FunctionBuilder::VectorCompareUGE(
    Value* value1, Value* value2, TypeName part_type) {
  STATIC_OPCODE(
      OPCODE_VECTOR_COMPARE_UGE,
      "vector_compare_uge",
      OPCODE_SIG_V_V_V,
      0);
  return VectorCompareXX(opcode, value1, value2, part_type);
}

Value* FunctionBuilder::Add(
    Value* value1, Value* value2, uint32_t arithmetic_flags) {
  STATIC_OPCODE(
      OPCODE_ADD,
      "add",
      OPCODE_SIG_V_V_V,
      OPCODE_FLAG_COMMUNATIVE);

  ASSERT_TYPES_EQUAL(value1, value2);

  // TODO(benvanik): optimize when flags set.
  if (!arithmetic_flags) {
    if (value1->IsConstantZero()) {
      return value2;
    } else if (value2->IsConstantZero()) {
      return value1;
    } else if (value1->IsConstant() && value2->IsConstant()) {
      Value* dest = CloneValue(value1);
      dest->Add(value2);
      return dest;
    }
  }

  Instr* i = AppendInstr(
      opcode, arithmetic_flags,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::AddWithCarry(
    Value* value1, Value* value2, Value* value3,
    uint32_t arithmetic_flags) {
  STATIC_OPCODE(
      OPCODE_ADD_CARRY,
      "add_carry",
      OPCODE_SIG_V_V_V_V,
      OPCODE_FLAG_COMMUNATIVE);

  ASSERT_TYPES_EQUAL(value1, value2);
  XEASSERT(value3->type == INT8_TYPE);
  
  // TODO(benvanik): optimize when flags set.
  if (!arithmetic_flags) {
    if (value3->IsConstantZero()) {
      return Add(value1, value2);
    } else if (value1->IsConstantZero()) {
      return Add(value2, value3);
    } else if (value2->IsConstantZero()) {
      return Add(value1, value3);
    } else if (value1->IsConstant() && value2->IsConstant()) {
      Value* dest = CloneValue(value1);
      dest->Add(value2);
      return Add(dest, value3);
    }
  }

  Instr* i = AppendInstr(
      opcode, arithmetic_flags,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = value3;
  return i->dest;
}

Value* FunctionBuilder::Sub(
    Value* value1, Value* value2, uint32_t arithmetic_flags) {
  STATIC_OPCODE(
      OPCODE_SUB,
      "sub",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_TYPES_EQUAL(value1, value2);

  // TODO(benvanik): optimize when flags set.
  if (!arithmetic_flags) {
    if (value1->IsConstantZero()) {
      return Neg(value1);
    } else if (value2->IsConstantZero()) {
      return value1;
    } else if (value1->IsConstant() && value2->IsConstant()) {
      Value* dest = CloneValue(value1);
      dest->Sub(value2);
      return dest;
    }
  }

  Instr* i = AppendInstr(
      opcode, arithmetic_flags,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Mul(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_MUL,
      "mul",
      OPCODE_SIG_V_V_V,
      OPCODE_FLAG_COMMUNATIVE);

  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1->IsConstant() && value2->IsConstant()) {
    Value* dest = CloneValue(value1);
    dest->Mul(value2);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Div(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_DIV,
      "div",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1->IsConstant() && value2->IsConstant()) {
    Value* dest = CloneValue(value1);
    dest->Div(value2);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Rem(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_REM,
      "rem",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1->IsConstant() && value2->IsConstant()) {
    Value* dest = CloneValue(value1);
    dest->Rem(value2);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::MulAdd(Value* value1, Value* value2, Value* value3) {
  STATIC_OPCODE(
      OPCODE_MULADD,
      "mul_add",
      OPCODE_SIG_V_V_V_V,
      0);

  ASSERT_TYPES_EQUAL(value1, value2);
  ASSERT_TYPES_EQUAL(value1, value3);

  bool c1 = value1->IsConstant();
  bool c2 = value2->IsConstant();
  bool c3 = value3->IsConstant();
  if (c1 && c2 && c3) {
    Value* dest = AllocValue(value1->type);
    Value::MulAdd(dest, value1, value2, value3);
    return dest;
  } else if (c1 && c2) {
    Value* dest = CloneValue(value1);
    dest->Mul(value2);
    return Add(dest, value3);
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = value3;
  return i->dest;
}

Value* FunctionBuilder::MulSub(Value* value1, Value* value2, Value* value3) {
  STATIC_OPCODE(
      OPCODE_MULSUB,
      "mul_sub",
      OPCODE_SIG_V_V_V_V,
      0);

  ASSERT_TYPES_EQUAL(value1, value2);
  ASSERT_TYPES_EQUAL(value1, value3);

  bool c1 = value1->IsConstant();
  bool c2 = value2->IsConstant();
  bool c3 = value3->IsConstant();
  if (c1 && c2 && c3) {
    Value* dest = AllocValue(value1->type);
    Value::MulSub(dest, value1, value2, value3);
    return dest;
  } else if (c1 && c2) {
    Value* dest = CloneValue(value1);
    dest->Mul(value2);
    return Sub(dest, value3);
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = value3;
  return i->dest;
}

Value* FunctionBuilder::Neg(Value* value) {
  STATIC_OPCODE(
      OPCODE_NEG,
      "neg",
      OPCODE_SIG_V_V,
      0);

  ASSERT_NON_VECTOR_TYPE(value);

  if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Neg();
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Abs(Value* value) {
  STATIC_OPCODE(
      OPCODE_ABS,
      "abs",
      OPCODE_SIG_V_V,
      0);

  ASSERT_NON_VECTOR_TYPE(value);

  if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Abs();
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Sqrt(Value* value) {
  STATIC_OPCODE(
      OPCODE_SQRT,
      "sqrt",
      OPCODE_SIG_V_V,
      0);

  ASSERT_FLOAT_TYPE(value);

  if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Sqrt();
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::RSqrt(Value* value) {
  STATIC_OPCODE(
      OPCODE_RSQRT,
      "rsqrt",
      OPCODE_SIG_V_V,
      0);

  ASSERT_FLOAT_TYPE(value);

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::DotProduct3(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_DOT_PRODUCT_3,
      "dot_product_3",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_VECTOR_TYPE(value1);
  ASSERT_VECTOR_TYPE(value2);
  ASSERT_TYPES_EQUAL(value1, value2);

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(FLOAT32_TYPE));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::DotProduct4(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_DOT_PRODUCT_4,
      "dot_product_4",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_VECTOR_TYPE(value1);
  ASSERT_VECTOR_TYPE(value2);
  ASSERT_TYPES_EQUAL(value1, value2);

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(FLOAT32_TYPE));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::And(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_AND,
      "and",
      OPCODE_SIG_V_V_V,
      OPCODE_FLAG_COMMUNATIVE);

  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);
  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1->IsConstantZero()) {
    return value1;
  } else if (value2->IsConstantZero()) {
    return value2;
  } else if (value1->IsConstant() && value2->IsConstant()) {
    Value* dest = CloneValue(value1);
    dest->And(value2);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Or(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_OR,
      "or",
      OPCODE_SIG_V_V_V,
      OPCODE_FLAG_COMMUNATIVE);

  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);
  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1->IsConstantZero()) {
    return value2;
  } else if (value2->IsConstantZero()) {
    return value1;
  } else if (value1->IsConstant() && value2->IsConstant()) {
    Value* dest = CloneValue(value1);
    dest->Or(value2);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Xor(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_XOR,
      "xor",
      OPCODE_SIG_V_V_V,
      OPCODE_FLAG_COMMUNATIVE);

  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);
  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1->IsConstant() && value2->IsConstant()) {
    Value* dest = CloneValue(value1);
    dest->Xor(value2);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Not(Value* value) {
  STATIC_OPCODE(
      OPCODE_NOT,
      "not",
      OPCODE_SIG_V_V,
      0);

  ASSERT_INTEGER_TYPE(value);

  if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Not();
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Shl(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_SHL,
      "shl",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);

  // NOTE AND value2 with 0x3F for 64bit, 0x1F for 32bit, etc..

  if (value2->IsConstantZero()) {
    return value1;
  }
  if (value2->type != INT8_TYPE) {
    value2 = Truncate(value2, INT8_TYPE);
  }
  if (value1->IsConstant() && value2->IsConstant()) {
    Value* dest = CloneValue(value1);
    dest->Shl(value2);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}
Value* FunctionBuilder::Shl(Value* value1, int8_t value2) {
  return Shl(value1, LoadConstant(value2));
}

Value* FunctionBuilder::VectorShl(Value* value1, Value* value2,
                                  TypeName part_type) {
  STATIC_OPCODE(
      OPCODE_VECTOR_SHL,
      "vector_shl",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_VECTOR_TYPE(value1);
  ASSERT_VECTOR_TYPE(value2);

  Instr* i = AppendInstr(
      opcode, part_type,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Shr(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_SHR,
      "shr",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);

  if (value2->IsConstantZero()) {
    return value1;
  }
  if (value2->type != INT8_TYPE) {
    value2 = Truncate(value2, INT8_TYPE);
  }
  if (value1->IsConstant() && value2->IsConstant()) {
    Value* dest = CloneValue(value1);
    dest->Shr(value2);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}
Value* FunctionBuilder::Shr(Value* value1, int8_t value2) {
  return Shr(value1, LoadConstant(value2));
}

Value* FunctionBuilder::Sha(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_SHA,
      "sha",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);

  if (value2->IsConstantZero()) {
    return value1;
  }
  if (value2->type != INT8_TYPE) {
    value2 = Truncate(value2, INT8_TYPE);
  }
  if (value1->IsConstant() && value2->IsConstant()) {
    Value* dest = CloneValue(value1);
    dest->Sha(value2);
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}
Value* FunctionBuilder::Sha(Value* value1, int8_t value2) {
  return Sha(value1, LoadConstant(value2));
}

Value* FunctionBuilder::RotateLeft(Value* value1, Value* value2) {
  STATIC_OPCODE(
      OPCODE_ROTATE_LEFT,
      "rotate_left",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);

  if (value2->IsConstantZero()) {
    return value1;
  }

  if (value2->type != INT8_TYPE) {
    value2 = Truncate(value2, INT8_TYPE);
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = value1;
  i->src2.value = value2;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::ByteSwap(Value* value) {
  STATIC_OPCODE(
      OPCODE_BYTE_SWAP,
      "byte_swap",
      OPCODE_SIG_V_V,
      0);

  if (value->type == INT8_TYPE) {
    return value;
  }
  if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->ByteSwap();
    return dest;
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::CountLeadingZeros(Value* value) {
  STATIC_OPCODE(
      OPCODE_CNTLZ,
      "cntlz",
      OPCODE_SIG_V_V,
      0);

  ASSERT_INTEGER_TYPE(value);

  if (value->IsConstantZero()) {
    const static uint8_t zeros[] = { 8, 16, 32, 64, };
    return LoadConstant(zeros[value->type]);
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(INT8_TYPE));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Insert(Value* value, uint32_t index, Value* part) {
  STATIC_OPCODE(
      OPCODE_INSERT,
      "insert",
      OPCODE_SIG_V_V_O_V,
      0);

  // TODO(benvanik): could do some of this as constants.

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.offset = index;
  i->src3.value = part;
  return i->dest;
}

Value* FunctionBuilder::Extract(Value* value, uint32_t index, TypeName target_type) {
  STATIC_OPCODE(
      OPCODE_EXTRACT,
      "extract",
      OPCODE_SIG_V_V_O,
      0);

  // TODO(benvanik): could do some of this as constants.

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(target_type));
  i->src1.value = value;
  i->src2.offset = index;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Splat(Value* value, TypeName target_type) {
  STATIC_OPCODE(
      OPCODE_SPLAT,
      "splat",
      OPCODE_SIG_V_V,
      0);

  // TODO(benvanik): could do some of this as constants.

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(target_type));
  i->src1.value = value;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::Permute(
    Value* control, Value* value1, Value* value2, TypeName part_type) {
  STATIC_OPCODE(
      OPCODE_PERMUTE,
      "permute",
      OPCODE_SIG_V_V_V_V,
      part_type);

  ASSERT_TYPES_EQUAL(value1, value2);
  // For now.
  XEASSERT(part_type == INT32_TYPE || part_type == FLOAT32_TYPE);

  // TODO(benvanik): could do some of this as constants.

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value1->type));
  i->src1.value = control;
  i->src2.value = value1;
  i->src3.value = value2;
  return i->dest;
}

Value* FunctionBuilder::Swizzle(
    Value* value, TypeName part_type, uint32_t swizzle_mask) {
  STATIC_OPCODE(
      OPCODE_PERMUTE,
      "permute",
      OPCODE_SIG_V_V_O,
      part_type);

  // For now.
  XEASSERT(part_type == INT32_TYPE || part_type == FLOAT32_TYPE);

  if (swizzle_mask == SWIZZLE_XYZW_TO_XYZW) {
    return Assign(value);
  }

  // TODO(benvanik): could do some of this as constants.

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = value;
  i->src2.offset = swizzle_mask;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::CompareExchange(
    Value* address, Value* compare_value, Value* exchange_value)  {
  STATIC_OPCODE(
      OPCODE_COMPARE_EXCHANGE,
      "compare_exchange",
      OPCODE_SIG_V_V_V_V,
      OPCODE_FLAG_VOLATILE);

  ASSERT_ADDRESS_TYPE(address);
  ASSERT_INTEGER_TYPE(compare_value);
  ASSERT_INTEGER_TYPE(exchange_value);
  ASSERT_TYPES_EQUAL(compare_value, exchange_value);
  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(exchange_value->type));
  i->src1.value = address;
  i->src2.value = compare_value;
  i->src3.value = exchange_value;
  return i->dest;
}

Value* FunctionBuilder::AtomicAdd(Value* address, Value* value) {
  STATIC_OPCODE(
      OPCODE_ATOMIC_ADD,
      "atomic_add",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_ADDRESS_TYPE(address);
  ASSERT_INTEGER_TYPE(value);
  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = address;
  i->src2.value = value;
  i->src3.value = NULL;
  return i->dest;
}

Value* FunctionBuilder::AtomicSub(Value* address, Value* value) {
  STATIC_OPCODE(
      OPCODE_ATOMIC_SUB,
      "atomic_sub",
      OPCODE_SIG_V_V_V,
      0);

  ASSERT_ADDRESS_TYPE(address);
  ASSERT_INTEGER_TYPE(value);
  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(value->type));
  i->src1.value = address;
  i->src2.value = value;
  i->src3.value = NULL;
  return i->dest;
}

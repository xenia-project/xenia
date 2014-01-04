/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lir/lir_builder.h>

#include <alloy/runtime/symbol_info.h>

using namespace alloy;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lir;


LIRBuilder::LIRBuilder(X64Backend* backend) :
    backend_(backend) {
  arena_ = new Arena();
  Reset();
}

LIRBuilder::~LIRBuilder() {
  Reset();
  delete arena_;
}

void LIRBuilder::Reset() {
  next_label_id_ = 0;
  block_head_ = block_tail_ = NULL;
  current_block_ = NULL;
  arena_->Reset();
}

int LIRBuilder::Finalize() {
  return 0;
}

void LIRBuilder::Dump(StringBuffer* str) {
  uint32_t block_ordinal = 0;
  auto block = block_head_;
  while (block) {
    if (block == block_head_) {
      str->Append("<entry>:\n");
    } else if (!block->label_head) {
      str->Append("<block%d>:\n", block_ordinal);
    }
    block_ordinal++;

    auto label = block->label_head;
    while (label) {
      if (label->local) {
        if (label->name) {
          str->Append(".%s:\n", label->name);
        } else {
          str->Append(".label%d:\n", label->id);
        }
      } else {
        if (label->name) {
          str->Append("%s:\n", label->name);
        } else {
          str->Append("label%d:\n", label->id);
        }
      }
      label = label->next;
    }

    auto i = block->instr_head;
    while (i) {
      if (i->opcode->flags & LIR_OPCODE_FLAG_HIDE) {
        i = i->next;
        continue;
      }
      if (i->opcode == &LIR_OPCODE_COMMENT_info) {
        str->Append("  ; %s\n", i->arg0<const char>());
        i = i->next;
        continue;
      }

      const LIROpcodeInfo* info = i->opcode;
      str->Append("  ");
      if (i->flags) {
        str->Append("%s.%d", info->name, i->flags);
      } else {
        str->Append("%s", info->name);
      }
      if (i->arg0_type() != LIROperandType::NONE) {
        str->Append(" ");
        DumpArg(str, i->arg0_type(), (intptr_t)i + i->arg_offsets.arg0);
      }
      if (i->arg1_type() != LIROperandType::NONE) {
        str->Append(", ");
        DumpArg(str, i->arg1_type(), (intptr_t)i + i->arg_offsets.arg1);
      }
      if (i->arg2_type() != LIROperandType::NONE) {
        str->Append(", ");
        DumpArg(str, i->arg2_type(), (intptr_t)i + i->arg_offsets.arg2);
      }
      if (i->arg3_type() != LIROperandType::NONE) {
        str->Append(", ");
        DumpArg(str, i->arg3_type(), (intptr_t)i + i->arg_offsets.arg3);
      }
      str->Append("\n");
      i = i->next;
    }

    block = block->next;
  }
}

void LIRBuilder::DumpArg(StringBuffer* str, LIROperandType type,
                         intptr_t ptr) {
  switch (type) {
  case LIROperandType::NONE:
    break;
  case LIROperandType::FUNCTION:
    if (true) {
      auto target = (*(runtime::FunctionInfo**)ptr);
      str->Append(target->name() ? target->name() : "<fn>");
    }
    break;
  case LIROperandType::LABEL:
    if (true) {
      auto target = (*(LIRLabel**)ptr);
      str->Append(target->name);
    }
    break;
  case LIROperandType::OFFSET:
    if (true) {
      auto offset = (*(intptr_t*)ptr);
      str->Append("+%lld", offset);
    }
    break;
  case LIROperandType::STRING:
    if (true) {
      str->Append((*(char**)ptr));
    }
    break;
  case LIROperandType::REGISTER:
    if (true) {
      LIRRegister* reg = (LIRRegister*)ptr;
      if (reg->is_virtual()) {
        switch (reg->type) {
        case LIRRegisterType::REG8:
          str->Append("v%d.i8", reg->id & 0x00FFFFFF);
          break;
        case LIRRegisterType::REG16:
          str->Append("v%d.i16", reg->id & 0x00FFFFFF);
          break;
        case LIRRegisterType::REG32:
          str->Append("v%d.i32", reg->id & 0x00FFFFFF);
          break;
        case LIRRegisterType::REG64:
          str->Append("v%d.i64", reg->id & 0x00FFFFFF);
          break;
        case LIRRegisterType::REGXMM:
          str->Append("v%d.xmm", reg->id & 0x00FFFFFF);
          break;
        }
      } else {
        str->Append(lir::register_names[(int)reg->name]);
      }
    }
    break;
  case LIROperandType::INT8_CONSTANT:
    str->Append("%X", *(int8_t*)ptr);
    break;
  case LIROperandType::INT16_CONSTANT:
    str->Append("%X", *(int16_t*)ptr);
    break;
  case LIROperandType::INT32_CONSTANT:
    str->Append("%X", *(int32_t*)ptr);
    break;
  case LIROperandType::INT64_CONSTANT:
    str->Append("%X", *(int64_t*)ptr);
    break;
  case LIROperandType::FLOAT32_CONSTANT:
    str->Append("%F", *(float*)ptr);
    break;
  case LIROperandType::FLOAT64_CONSTANT:
    str->Append("%F", *(double*)ptr);
    break;
  case LIROperandType::VEC128_CONSTANT:
    if (true) {
      vec128_t* value = (vec128_t*)ptr;
      str->Append("(%F,%F,%F,%F)",
                  value->x, value->y, value->z, value->w);
    }
    break;
  default: XEASSERTALWAYS(); break;
  }
}

LIRBlock* LIRBuilder::current_block() const {
  return current_block_;
}

LIRInstr* LIRBuilder::last_instr() const {
  if (current_block_ && current_block_->instr_tail) {
    return current_block_->instr_tail;
  } else if (block_tail_) {
    return block_tail_->instr_tail;
  }
  return NULL;
}

LIRLabel* LIRBuilder::NewLabel(const char* name, bool local) {
  LIRLabel* label = arena_->Alloc<LIRLabel>();
  label->next = label->prev = NULL;
  label->block = NULL;
  label->id = next_label_id_++;
  label->local = local;
  label->tag = NULL;
  if (!name) {
    char label_name[32] = "l";
    _itoa(label->id, label_name + 1, 10);
    name = label_name;
  }
  size_t label_length = xestrlena(name);
  label->name = (char*)arena_->Alloc(label_length + 1);
  xe_copy_struct(label->name, name, label_length + 1);
  return label;
}

void LIRBuilder::MarkLabel(LIRLabel* label, LIRBlock* block) {
  if (!block) {
    if (current_block_ && current_block_->instr_tail) {
      EndBlock();
    }
    if (!current_block_) {
      AppendBlock();
    }
    block = current_block_;
  }
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

LIRBlock* LIRBuilder::AppendBlock() {
  LIRBlock* block = arena_->Alloc<LIRBlock>();
  block->arena = arena_;
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

void LIRBuilder::EndBlock() {
  if (current_block_ && !current_block_->instr_tail) {
    // Block never had anything added to it. Since it likely has an
    // incoming edge, just keep it around.
    return;
  }
  current_block_ = NULL;
}

LIRInstr* LIRBuilder::AppendInstr(
    const LIROpcodeInfo& opcode_info, uint16_t flags) {
  if (!current_block_) {
    AppendBlock();
  }
  LIRBlock* block = current_block_;

  LIRInstr* instr = arena_->Alloc<LIRInstr>();
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
  instr->arg_types = {
    LIROperandType::NONE,
    LIROperandType::NONE,
    LIROperandType::NONE,
    LIROperandType::NONE
  };
  return instr;
}

uint8_t LIRBuilder::AppendOperand(
    LIRInstr* instr, LIROperandType& type, const char* value) {
  type = LIROperandType::STRING;
  size_t length = xestrlena(value);
  auto ptr = (char*)arena_->Alloc(length + 1);
  xe_copy_struct(ptr, value, length + 1);
  return (uint8_t)((intptr_t)ptr - (intptr_t)instr);
}

uint8_t LIRBuilder::AppendOperand(
    LIRInstr* instr, LIROperandType& type, hir::Value* value) {
  if (value->IsConstant()) {
    switch (value->type) {
    case hir::INT8_TYPE:
      return AppendOperand(instr, type, value->constant.i8);
    case hir::INT16_TYPE:
      return AppendOperand(instr, type, value->constant.i16);
    case hir::INT32_TYPE:
      return AppendOperand(instr, type, value->constant.i32);
    case hir::INT64_TYPE:
      return AppendOperand(instr, type, value->constant.i64);
    case hir::FLOAT32_TYPE:
      return AppendOperand(instr, type, value->constant.f32);
    case hir::FLOAT64_TYPE:
      return AppendOperand(instr, type, value->constant.f64);
    case hir::VEC128_TYPE:
      return AppendOperand(instr, type, value->constant.v128);
    default:
      XEASSERTALWAYS();
      return 0;
    }
  } else {
    LIRRegisterType reg_type;
    switch (value->type) {
    case hir::INT8_TYPE:
      reg_type = LIRRegisterType::REG8;
      break;
    case hir::INT16_TYPE:
      reg_type = LIRRegisterType::REG16;
      break;
    case hir::INT32_TYPE:
      reg_type = LIRRegisterType::REG32;
      break;
    case hir::INT64_TYPE:
      reg_type = LIRRegisterType::REG64;
      break;
    case hir::FLOAT32_TYPE:
      reg_type = LIRRegisterType::REGXMM;
      break;
    case hir::FLOAT64_TYPE:
      reg_type = LIRRegisterType::REGXMM;
      break;
    case hir::VEC128_TYPE:
      reg_type = LIRRegisterType::REGXMM;
      break;
    default:
      XEASSERTALWAYS();
      return 0;
    }
    // Make it uniqueish by putting it +20000000
    uint32_t reg_id = 0x20000000 + value->ordinal;
    return AppendOperand(instr, type, LIRRegister(reg_type, reg_id));
  }
}

void LIRBuilder::Comment(const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  xevsnprintfa(buffer, 1024, format, args);
  va_end(args);
  auto instr = AppendInstr(LIR_OPCODE_COMMENT_info);
  instr->arg_offsets.arg0 =
      AppendOperand(instr, instr->arg_types.arg0, buffer);
}

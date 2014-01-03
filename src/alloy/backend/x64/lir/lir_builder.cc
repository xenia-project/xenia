/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lir/lir_builder.h>

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
        str->Append("  ; %s\n", i->arg[0].string);
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
      // TODO(benvanik): args based on signature
      str->Append("\n");
      i = i->next;
    }

    block = block->next;
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

LIRLabel* LIRBuilder::NewLabel(bool local) {
  LIRLabel* label = arena_->Alloc<LIRLabel>();
  label->next = label->prev = NULL;
  label->block = NULL;
  label->id = next_label_id_++;
  label->name = NULL;
  label->local = local;
  label->tag = NULL;
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
  return instr;
}

void LIRBuilder::Comment(const char* format, ...) {
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
  auto instr = AppendInstr(LIR_OPCODE_COMMENT_info);
  instr->arg[0].string = (char*)p;
}

void LIRBuilder::Nop() {
  auto instr = AppendInstr(LIR_OPCODE_NOP_info);
}

void LIRBuilder::SourceOffset(uint64_t offset) {
  auto instr = AppendInstr(LIR_OPCODE_SOURCE_OFFSET_info);
}

void LIRBuilder::DebugBreak() {
  auto instr = AppendInstr(LIR_OPCODE_DEBUG_BREAK_info);
}

void LIRBuilder::Trap() {
  auto instr = AppendInstr(LIR_OPCODE_TRAP_info);
}

void LIRBuilder::Test(int8_t a, int8_t b) {
  auto instr = AppendInstr(LIR_OPCODE_TEST_info);
}

void LIRBuilder::Test(int16_t a, int16_t b) {
  auto instr = AppendInstr(LIR_OPCODE_TEST_info);
}

void LIRBuilder::Test(int32_t a, int32_t b) {
  auto instr = AppendInstr(LIR_OPCODE_TEST_info);
}

void LIRBuilder::Test(int64_t a, int64_t b) {
  auto instr = AppendInstr(LIR_OPCODE_TEST_info);
}

void LIRBuilder::Test(hir::Value* a, hir::Value* b) {
  auto instr = AppendInstr(LIR_OPCODE_TEST_info);
}

void LIRBuilder::JumpEQ(LIRLabel* label) {
  auto instr = AppendInstr(LIR_OPCODE_JUMP_EQ_info);
}

void LIRBuilder::JumpNE(LIRLabel* label) {
  auto instr = AppendInstr(LIR_OPCODE_JUMP_NE_info);
}

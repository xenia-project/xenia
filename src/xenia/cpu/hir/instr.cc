/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/hir/instr.h"

#include "xenia/cpu/hir/block.h"
#include "xenia/cpu/hir/hir_builder.h"
namespace xe {
namespace cpu {
namespace hir {
void Instr::set_srcN(Value* value, uint32_t idx) {
  if (srcs[idx].value == value) {
    return;
  }
  if (srcs_use[idx]) {
    srcs[idx].value->RemoveUse(srcs_use[idx]);
  }
  srcs[idx].value = value;
  srcs_use[idx] = value ? value->AddUse(block->arena, this) : nullptr;
}

void Instr::MoveBefore(Instr* other) {
  if (next == other) {
    return;
  }

  // Remove from current location.
  if (prev) {
    prev->next = next;
  } else {
    block->instr_head = next;
  }
  if (next) {
    next->prev = prev;
  } else {
    block->instr_tail = prev;
  }

  // Insert into new location.
  block = other->block;
  next = other;
  prev = other->prev;
  other->prev = this;
  if (prev) {
    prev->next = this;
  }
  if (other == block->instr_head) {
    block->instr_head = this;
  }
}

void Instr::Replace(const OpcodeInfo* new_opcode, uint16_t new_flags) {
  opcode = new_opcode;
  flags = new_flags;

  if (src1_use) {
    src1.value->RemoveUse(src1_use);
    src1.value = NULL;
    // src1_use = NULL;
  }
  if (src2_use) {
    src2.value->RemoveUse(src2_use);
    src2.value = NULL;
    // src2_use = NULL;
  }
  if (src3_use) {
    src3.value->RemoveUse(src3_use);
    src3.value = NULL;
    // src3_use = NULL;
  }

  if (src1_use) {
    HIRBuilder::GetCurrent()->DeallocateUse(src1_use);
    src1_use = nullptr;
  }
  if (src2_use) {
    HIRBuilder::GetCurrent()->DeallocateUse(src2_use);
    src2_use = nullptr;
  }

  if (src3_use) {
    HIRBuilder::GetCurrent()->DeallocateUse(src3_use);
    src3_use = nullptr;
  }
}

void Instr::UnlinkAndNOP() {
  // Remove all srcs/dest.
  Replace(&OPCODE_NOP_info, 0);

  if (prev) {
    prev->next = next;
  } else {
    block->instr_head = next;
  }
  if (next) {
    next->prev = prev;
  } else {
    block->instr_tail = prev;
  }
}

void Instr::Deallocate() {
  HIRBuilder::GetCurrent()->DeallocateInstruction(this);
}
Instr* Instr::GetDestDefSkipAssigns() {
  Instr* current_def = this;

  while (current_def->opcode == &OPCODE_ASSIGN_info) {
    Instr* next_def = current_def->src1.value->def;

    if (!next_def) {
      return nullptr;
    }

    current_def = next_def;
  }
  return current_def;
}
Instr* Instr::GetDestDefTunnelMovs(unsigned int* tunnel_flags) {
  unsigned int traversed_types = 0;
  unsigned int in_flags = *tunnel_flags;
  Instr* current_def = this;

  while (true) {
    Opcode op = current_def->opcode->num;

    switch (op) {
      case OPCODE_ASSIGN: {
        if ((in_flags & MOVTUNNEL_ASSIGNS)) {
          current_def = current_def->src1.value->def;
          traversed_types |= MOVTUNNEL_ASSIGNS;

        } else {
          goto exit_loop;
        }
        break;
      }
      case OPCODE_ZERO_EXTEND: {
        if ((in_flags & MOVTUNNEL_MOVZX)) {
          current_def = current_def->src1.value->def;
          traversed_types |= MOVTUNNEL_MOVZX;

        } else {
          goto exit_loop;
        }
        break;
      }
      case OPCODE_SIGN_EXTEND: {
        if ((in_flags & MOVTUNNEL_MOVSX)) {
          current_def = current_def->src1.value->def;
          traversed_types |= MOVTUNNEL_MOVSX;

        } else {
          goto exit_loop;
        }
        break;
      }
      case OPCODE_TRUNCATE: {
        if ((in_flags & MOVTUNNEL_TRUNCATE)) {
          current_def = current_def->src1.value->def;
          traversed_types |= MOVTUNNEL_TRUNCATE;

        } else {
          goto exit_loop;
        }
        break;
      }
      case OPCODE_AND: {
        if ((in_flags & MOVTUNNEL_AND32FF)) {
          auto [constant, nonconst] =
              current_def->BinaryValueArrangeAsConstAndVar();
          if (!constant || constant->AsUint64() != 0xFFFFFFFF) {
            goto exit_loop;
          }
          current_def = nonconst->def;
          traversed_types |= MOVTUNNEL_AND32FF;

        } else {
          goto exit_loop;
        }
        break;
      }
      default:
        goto exit_loop;
    }
    if (!current_def) {
      goto exit_loop;
    }
  }
exit_loop:
  *tunnel_flags = traversed_types;
  return current_def;
}
bool Instr::IsFake() const {
  Opcode num = opcode->num;
  switch (num) {
    case OPCODE_NOP:
    case OPCODE_COMMENT:
    case OPCODE_CONTEXT_BARRIER:
    case OPCODE_SOURCE_OFFSET:
      return true;
  }
  return false;
}

const Instr* Instr::GetNonFakePrev() const {
  const Instr* curr = prev;

  while (curr && curr->IsFake()) {
    curr = curr->prev;
  }
  return curr;
}

uint32_t Instr::GuestAddressFor() const {
  Instr* srch = prev;

  while (srch) {
    if (srch->GetOpcodeNum() == OPCODE_SOURCE_OFFSET) {
      return (uint32_t)srch->src1.offset;
    }
    srch = srch->prev;
  }

  return 0;  // eek.
}
bool Instr::AllScalarIntegral() {
  bool result = true;
  if (dest) {
    if (!IsScalarIntegralType(dest->type)) {
      return false;
    }
  }

  VisitValueOperands([&result](Value* v, uint32_t idx) {
    result = result && IsScalarIntegralType(v->type);
  });
  return result;
}
}  // namespace hir
}  // namespace cpu
}  // namespace xe

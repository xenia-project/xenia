/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lir/lir_instr.h>

using namespace alloy;
using namespace alloy::backend::x64::lir;


namespace alloy {
namespace backend {
namespace x64 {
namespace lir {
const char* register_names[] = {
  "al",   "ax",   "eax",  "rax",
  "bl",   "bx",   "ebx",  "rbx",
  "cl",   "cx",   "ecx",  "rcx",
  "dl",   "dx",   "edx",  "rdx",
  "r8b",  "r8w",  "r8d",  "r8",
  "r9b",  "r9w",  "r9d",  "r9",
  "r10b", "r10w", "r10d", "r10",
  "r11b", "r11w", "r11d", "r11",
  "r12b", "r12w", "r12d", "r12",
  "r13b", "r13w", "r13d", "r13",
  "r14b", "r14w", "r14d", "r14",
  "r15b", "r15w", "r15d", "r15",

  "sil", "si", "esi", "rsi",
  "dil", "di", "edi", "rdi",

  "rbp",
  "rsp",

  "xmm0",
  "xmm1",
  "xmm2",
  "xmm3",
  "xmm4",
  "xmm5",
  "xmm6",
  "xmm7",
  "xmm8",
  "xmm9",
  "xmm10",
  "xmm11",
  "xmm12",
  "xmm13",
  "xmm14",
  "xmm15",
};
}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


void LIRInstr::Remove() {
  if (next) {
    next->prev = prev;
  }
  if (prev) {
    prev->next = next;
  }
  if (block->instr_head == this) {
    block->instr_head = next;
  }
  if (block->instr_tail == this) {
    block->instr_tail = prev;
  }
}

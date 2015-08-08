/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_HIR_BLOCK_H_
#define XENIA_CPU_HIR_BLOCK_H_

#include "xenia/base/arena.h"

namespace llvm {
class BitVector;
}  // namespace llvm

namespace xe {
namespace cpu {
namespace hir {

class Block;
class HIRBuilder;
class Instr;
class Label;

class Edge {
 public:
  enum EdgeFlags {
    UNCONDITIONAL = (1 << 0),
    DOMINATES = (1 << 1),
  };

 public:
  Edge* outgoing_next;
  Edge* outgoing_prev;
  Edge* incoming_next;
  Edge* incoming_prev;

  Block* src;
  Block* dest;

  uint32_t flags;
};

class Block {
 public:
  Arena* arena;

  Block* next;
  Block* prev;

  Edge* incoming_edge_head;
  Edge* outgoing_edge_head;
  llvm::BitVector* incoming_values;

  Label* label_head;
  Label* label_tail;

  Instr* instr_head;
  Instr* instr_tail;

  uint16_t ordinal;

  void AssertNoCycles();
};

}  // namespace hir
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_HIR_BLOCK_H_

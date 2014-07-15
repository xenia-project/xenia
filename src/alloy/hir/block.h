/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_HIR_BLOCK_H_
#define ALLOY_HIR_BLOCK_H_

#include <alloy/arena.h>
#include <alloy/core.h>

XEDECLARECLASS1(llvm, BitVector);

namespace alloy {
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
}  // namespace alloy

#endif  // ALLOY_HIR_BLOCK_H_

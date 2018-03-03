/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/data_flow_analysis_pass.h"

#include "xenia/base/assert.h"
#include "xenia/base/platform.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/processor.h"

#if XE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#include <llvm/ADT/BitVector.h>
#pragma warning(pop)
#else
#include <llvm/ADT/BitVector.h>
#include <cmath>
#endif  // XE_COMPILER_MSVC

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::OpcodeSignatureType;
using xe::cpu::hir::Value;

DataFlowAnalysisPass::DataFlowAnalysisPass() : CompilerPass() {}

DataFlowAnalysisPass::~DataFlowAnalysisPass() {}

bool DataFlowAnalysisPass::Run(HIRBuilder* builder) {
  // Linearize blocks so that we can detect cycles and propagate dependencies.
  uint32_t block_count = LinearizeBlocks(builder);

  // Analyze value flow and add locals as needed.
  AnalyzeFlow(builder, block_count);

  return true;
}

uint32_t DataFlowAnalysisPass::LinearizeBlocks(HIRBuilder* builder) {
  // TODO(benvanik): actually do this - we cheat now knowing that they are in
  //     sequential order.
  uint16_t block_ordinal = 0;
  auto block = builder->first_block();
  while (block) {
    block->ordinal = block_ordinal++;
    block = block->next;
  }
  return block_ordinal;
}

void DataFlowAnalysisPass::AnalyzeFlow(HIRBuilder* builder,
                                       uint32_t block_count) {
  uint32_t max_value_estimate =
      builder->max_value_ordinal() + 1 + block_count * 4;

  // Stash for value map. We may want to maintain this during building.
  auto arena = builder->arena();
  auto value_map = reinterpret_cast<Value**>(
      arena->Alloc(sizeof(Value*) * max_value_estimate));

  // Allocate incoming bitvectors for use by blocks. We don't need outgoing
  // because they are only used during the block iteration.
  // Mapped by block ordinal.
  // TODO(benvanik): cache this list, grow as needed, etc.
  auto incoming_bitvectors =
      (llvm::BitVector**)arena->Alloc(sizeof(llvm::BitVector*) * block_count);
  for (auto n = 0u; n < block_count; n++) {
    incoming_bitvectors[n] = new llvm::BitVector(max_value_estimate);
  }

  // Walk blocks in reverse and calculate incoming/outgoing values.
  auto block = builder->last_block();
  while (block) {
    // Allocate bitsets based on max value number.
    block->incoming_values = incoming_bitvectors[block->ordinal];
    auto& incoming_values = *block->incoming_values;

    // Walk instructions and gather up incoming values.
    auto instr = block->instr_head;
    while (instr) {
      uint32_t signature = instr->opcode->signature;
#define SET_INCOMING_VALUE(v)                   \
  if (v->def && v->def->block != block) {       \
    incoming_values.set(v->ordinal);            \
  }                                             \
  assert_true(v->ordinal < max_value_estimate); \
  value_map[v->ordinal] = v;
      if (GET_OPCODE_SIG_TYPE_SRC1(signature) == OPCODE_SIG_TYPE_V) {
        SET_INCOMING_VALUE(instr->src1.value);
      }
      if (GET_OPCODE_SIG_TYPE_SRC2(signature) == OPCODE_SIG_TYPE_V) {
        SET_INCOMING_VALUE(instr->src2.value);
      }
      if (GET_OPCODE_SIG_TYPE_SRC3(signature) == OPCODE_SIG_TYPE_V) {
        SET_INCOMING_VALUE(instr->src3.value);
      }
#undef SET_INCOMING_VALUE
      instr = instr->next;
    }

    // Add all successor incoming values to our outgoing, as we need to
    // pass them through.
    llvm::BitVector outgoing_values(max_value_estimate);
    auto outgoing_edge = block->outgoing_edge_head;
    while (outgoing_edge) {
      if (outgoing_edge->dest->ordinal > block->ordinal) {
        outgoing_values |= *outgoing_edge->dest->incoming_values;
      }
      outgoing_edge = outgoing_edge->outgoing_next;
    }
    incoming_values |= outgoing_values;

    // Add stores for all outgoing values.
    auto outgoing_ordinal = outgoing_values.find_first();
    while (outgoing_ordinal != -1) {
      Value* src_value = value_map[outgoing_ordinal];
      assert_not_null(src_value);
      if (!src_value->local_slot) {
        src_value->local_slot = builder->AllocLocal(src_value->type);
      }
      builder->StoreLocal(src_value->local_slot, src_value);

      // If we are in the block the value was defined in:
      if (src_value->def->block == block) {
        // Move the store to right after the def, or as soon after
        // as we can (respecting PAIRED flags).
        auto def_next = src_value->def->next;
        while (def_next && def_next->opcode->flags & OPCODE_FLAG_PAIRED_PREV) {
          def_next = def_next->next;
        }
        assert_not_null(def_next);
        builder->last_instr()->MoveBefore(def_next);

        // We don't need it in the incoming list.
        incoming_values.reset(outgoing_ordinal);
      } else {
        // Eh, just throw at the end, before the first branch.
        auto tail = block->instr_tail;
        while (tail && tail->opcode->flags & OPCODE_FLAG_BRANCH) {
          tail = tail->prev;
        }
        assert_not_zero(tail);
        builder->last_instr()->MoveBefore(tail->next);
      }

      outgoing_ordinal = outgoing_values.find_next(outgoing_ordinal);
    }

    // Add loads for all incoming values and rename them in the block.
    auto incoming_ordinal = incoming_values.find_first();
    while (incoming_ordinal != -1) {
      Value* src_value = value_map[incoming_ordinal];
      assert_not_null(src_value);
      if (!src_value->local_slot) {
        src_value->local_slot = builder->AllocLocal(src_value->type);
      }
      Value* local_value = builder->LoadLocal(src_value->local_slot);
      builder->last_instr()->MoveBefore(block->instr_head);

      // Swap uses of original value with the local value.
      instr = block->instr_head;
      while (instr) {
        uint32_t signature = instr->opcode->signature;
        if (GET_OPCODE_SIG_TYPE_SRC1(signature) == OPCODE_SIG_TYPE_V) {
          if (instr->src1.value == src_value) {
            instr->set_src1(local_value);
          }
        }
        if (GET_OPCODE_SIG_TYPE_SRC2(signature) == OPCODE_SIG_TYPE_V) {
          if (instr->src2.value == src_value) {
            instr->set_src2(local_value);
          }
        }
        if (GET_OPCODE_SIG_TYPE_SRC3(signature) == OPCODE_SIG_TYPE_V) {
          if (instr->src3.value == src_value) {
            instr->set_src3(local_value);
          }
        }
        instr = instr->next;
      }

      incoming_ordinal = incoming_values.find_next(incoming_ordinal);
    }

    block = block->prev;
  }

  // Cleanup bitvectors.
  for (auto n = 0u; n < block_count; n++) {
    delete incoming_bitvectors[n];
  }
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

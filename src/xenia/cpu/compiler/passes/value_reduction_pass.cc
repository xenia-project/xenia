/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/value_reduction_pass.h"

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
#include "llvm/ADT/BitVector.h"
#include <cmath>
#endif  // XE_COMPILER_MSVC

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::OpcodeInfo;
using xe::cpu::hir::Value;

ValueReductionPass::ValueReductionPass() : CompilerPass() {}

ValueReductionPass::~ValueReductionPass() {}

void ValueReductionPass::ComputeLastUse(Value* value) {
  // TODO(benvanik): compute during construction?
  // Note that this list isn't sorted (unfortunately), so we have to scan
  // them all.
  uint32_t max_ordinal = 0;
  Value::Use* last_use = nullptr;
  auto use = value->use_head;
  while (use) {
    if (!last_use || use->instr->ordinal >= max_ordinal) {
      last_use = use;
      max_ordinal = use->instr->ordinal;
    }
    use = use->next;
  }
  value->last_use = last_use ? last_use->instr : nullptr;
}

bool ValueReductionPass::Run(HIRBuilder* builder) {
  // Walk each block and reuse variable ordinals as much as possible.

  llvm::BitVector ordinals(builder->max_value_ordinal());

  auto block = builder->first_block();
  while (block) {
    // Reset used ordinals.
    ordinals.reset();

    // Renumber all instructions to make liveness tracking easier.
    uint32_t instr_ordinal = 0;
    auto instr = block->instr_head;
    while (instr) {
      instr->ordinal = instr_ordinal++;
      instr = instr->next;
    }

    instr = block->instr_head;
    while (instr) {
      const OpcodeInfo* info = instr->opcode;
      auto dest_type = GET_OPCODE_SIG_TYPE_DEST(info->signature);
      auto src1_type = GET_OPCODE_SIG_TYPE_SRC1(info->signature);
      auto src2_type = GET_OPCODE_SIG_TYPE_SRC2(info->signature);
      auto src3_type = GET_OPCODE_SIG_TYPE_SRC3(info->signature);
      if (src1_type == OPCODE_SIG_TYPE_V) {
        auto v = instr->src1.value;
        if (!v->last_use) {
          ComputeLastUse(v);
        }
        if (v->last_use == instr) {
          // Available.
          if (!instr->src1.value->IsConstant()) {
            ordinals.reset(v->ordinal);
          }
        }
      }
      if (src2_type == OPCODE_SIG_TYPE_V) {
        auto v = instr->src2.value;
        if (!v->last_use) {
          ComputeLastUse(v);
        }
        if (v->last_use == instr) {
          // Available.
          if (!instr->src2.value->IsConstant()) {
            ordinals.reset(v->ordinal);
          }
        }
      }
      if (src3_type == OPCODE_SIG_TYPE_V) {
        auto v = instr->src3.value;
        if (!v->last_use) {
          ComputeLastUse(v);
        }
        if (v->last_use == instr) {
          // Available.
          if (!instr->src3.value->IsConstant()) {
            ordinals.reset(v->ordinal);
          }
        }
      }
      if (dest_type == OPCODE_SIG_TYPE_V) {
        // Dest values are processed last, as they may be able to reuse a
        // source value ordinal.
        auto v = instr->dest;
        // Find a lower ordinal.
        for (auto n = 0u; n < ordinals.size(); n++) {
          if (!ordinals.test(n)) {
            ordinals.set(n);
            v->ordinal = n;
            break;
          }
        }
      }

      instr = instr->next;
    }

    block = block->next;
  }

  return true;
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

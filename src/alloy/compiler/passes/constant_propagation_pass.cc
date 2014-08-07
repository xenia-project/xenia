/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/passes/constant_propagation_pass.h>

#include <alloy/runtime/function.h>
#include <alloy/runtime/runtime.h>

namespace alloy {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace alloy::hir;

using alloy::hir::HIRBuilder;
using alloy::hir::TypeName;
using alloy::hir::Value;

ConstantPropagationPass::ConstantPropagationPass() : CompilerPass() {}

ConstantPropagationPass::~ConstantPropagationPass() {}

int ConstantPropagationPass::Run(HIRBuilder* builder) {
  SCOPE_profile_cpu_f("alloy");

  // Once ContextPromotion has run there will likely be a whole slew of
  // constants that can be pushed through the function.
  // Example:
  //   store_context +100, 1000
  //   v0 = load_context +100
  //   v1 = add v0, v0
  //   store_context +200, v1
  // after PromoteContext:
  //   store_context +100, 1000
  //   v0 = 1000
  //   v1 = add v0, v0
  //   store_context +200, v1
  // after PropagateConstants:
  //   store_context +100, 1000
  //   v0 = 1000
  //   v1 = add 1000, 1000
  //   store_context +200, 2000
  // A DCE run after this should clean up any of the values no longer needed.
  //
  // Special care needs to be taken with paired instructions. For example,
  // DID_CARRY needs to be set as a constant:
  //   v1 = sub.2 20, 1
  //   v2 = did_carry v1
  // should become:
  //   v1 = 19
  //   v2 = 0

  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      auto v = i->dest;
      switch (i->opcode->num) {
        case OPCODE_DEBUG_BREAK_TRUE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantTrue()) {
              i->Replace(&OPCODE_DEBUG_BREAK_info, i->flags);
            } else {
              i->Remove();
            }
          }
          break;

        case OPCODE_TRAP_TRUE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantTrue()) {
              i->Replace(&OPCODE_TRAP_info, i->flags);
            } else {
              i->Remove();
            }
          }
          break;

        case OPCODE_CALL_TRUE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantTrue()) {
              auto symbol_info = i->src2.symbol_info;
              i->Replace(&OPCODE_CALL_info, i->flags);
              i->src1.symbol_info = symbol_info;
            } else {
              i->Remove();
            }
          }
          break;
        case OPCODE_CALL_INDIRECT:
          if (i->src1.value->IsConstant()) {
            runtime::FunctionInfo* symbol_info;
            if (runtime_->LookupFunctionInfo(
                    (uint32_t)i->src1.value->constant.i32, &symbol_info)) {
              break;
            }
            i->Replace(&OPCODE_CALL_info, i->flags);
            i->src1.symbol_info = symbol_info;
          }
          break;
        case OPCODE_CALL_INDIRECT_TRUE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantTrue()) {
              auto value = i->src2.value;
              i->Replace(&OPCODE_CALL_INDIRECT_info, i->flags);
              i->set_src1(value);
            } else {
              i->Remove();
            }
          }
          break;

        case OPCODE_BRANCH_TRUE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantTrue()) {
              auto label = i->src2.label;
              i->Replace(&OPCODE_BRANCH_info, i->flags);
              i->src1.label = label;
            } else {
              i->Remove();
            }
          }
          break;
        case OPCODE_BRANCH_FALSE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantFalse()) {
              auto label = i->src2.label;
              i->Replace(&OPCODE_BRANCH_info, i->flags);
              i->src1.label = label;
            } else {
              i->Remove();
            }
          }
          break;

        case OPCODE_CAST:
          if (i->src1.value->IsConstant()) {
            TypeName target_type = v->type;
            v->set_from(i->src1.value);
            v->Cast(target_type);
            i->Remove();
          }
          break;
        case OPCODE_ZERO_EXTEND:
          if (i->src1.value->IsConstant()) {
            TypeName target_type = v->type;
            v->set_from(i->src1.value);
            v->ZeroExtend(target_type);
            i->Remove();
          }
          break;
        case OPCODE_SIGN_EXTEND:
          if (i->src1.value->IsConstant()) {
            TypeName target_type = v->type;
            v->set_from(i->src1.value);
            v->SignExtend(target_type);
            i->Remove();
          }
          break;
        case OPCODE_TRUNCATE:
          if (i->src1.value->IsConstant()) {
            TypeName target_type = v->type;
            v->set_from(i->src1.value);
            v->Truncate(target_type);
            i->Remove();
          }
          break;

        case OPCODE_SELECT:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantTrue()) {
              v->set_from(i->src2.value);
            } else {
              v->set_from(i->src3.value);
            }
            i->Remove();
          }
          break;
        case OPCODE_IS_TRUE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantTrue()) {
              v->set_constant((int8_t)1);
            } else {
              v->set_constant((int8_t)0);
            }
            i->Remove();
          }
          break;
        case OPCODE_IS_FALSE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantFalse()) {
              v->set_constant((int8_t)1);
            } else {
              v->set_constant((int8_t)0);
            }
            i->Remove();
          }
          break;

        // TODO(benvanik): compares
        case OPCODE_COMPARE_EQ:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantEQ(i->src2.value);
            i->dest->set_constant(value);
            i->Remove();
          }
          break;
        case OPCODE_COMPARE_NE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantNE(i->src2.value);
            i->dest->set_constant(value);
            i->Remove();
          }
          break;
        case OPCODE_COMPARE_SLT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantSLT(i->src2.value);
            i->dest->set_constant(value);
            i->Remove();
          }
          break;
        case OPCODE_COMPARE_SLE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantSLE(i->src2.value);
            i->dest->set_constant(value);
            i->Remove();
          }
          break;
        case OPCODE_COMPARE_SGT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantSGT(i->src2.value);
            i->dest->set_constant(value);
            i->Remove();
          }
          break;
        case OPCODE_COMPARE_SGE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantSGE(i->src2.value);
            i->dest->set_constant(value);
            i->Remove();
          }
          break;
        case OPCODE_COMPARE_ULT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantULT(i->src2.value);
            i->dest->set_constant(value);
            i->Remove();
          }
          break;
        case OPCODE_COMPARE_ULE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantULE(i->src2.value);
            i->dest->set_constant(value);
            i->Remove();
          }
          break;
        case OPCODE_COMPARE_UGT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantUGT(i->src2.value);
            i->dest->set_constant(value);
            i->Remove();
          }
          break;
        case OPCODE_COMPARE_UGE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantUGE(i->src2.value);
            i->dest->set_constant(value);
            i->Remove();
          }
          break;

        case OPCODE_DID_CARRY:
          assert_true(!i->src1.value->IsConstant());
          break;
        case OPCODE_DID_OVERFLOW:
          assert_true(!i->src1.value->IsConstant());
          break;
        case OPCODE_DID_SATURATE:
          assert_true(!i->src1.value->IsConstant());
          break;

        case OPCODE_ADD:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            bool did_carry = v->Add(i->src2.value);
            bool propagate_carry = !!(i->flags & ARITHMETIC_SET_CARRY);
            i->Remove();

            // If carry is set find the DID_CARRY and fix it.
            if (propagate_carry) {
              PropagateCarry(v, did_carry);
            }
          }
          break;
        // TODO(benvanik): ADD_CARRY (w/ ARITHMETIC_SET_CARRY)
        case OPCODE_ADD_CARRY:
          if (i->src1.value->IsConstantZero() && i->src2.value->IsConstantZero()) {
            Value* ca = i->src3.value;
            // If carry is set find the DID_CARRY and fix it.
            if (!!(i->flags & ARITHMETIC_SET_CARRY)) {
              auto next = i->dest->use_head;
              while (next) {
                auto use = next;
                next = use->next;
                if (use->instr->opcode == &OPCODE_DID_CARRY_info) {
                  // Replace carry value.
                  use->instr->Replace(&OPCODE_ASSIGN_info, 0);
                  use->instr->set_src1(ca);
                }
              }
            }
            i->Replace(&OPCODE_ASSIGN_info, 0);
            i->set_src1(ca);
          }
          break;
        case OPCODE_SUB:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            bool did_carry = v->Sub(i->src2.value);
            bool propagate_carry = !!(i->flags & ARITHMETIC_SET_CARRY);
            i->Remove();

            // If carry is set find the DID_CARRY and fix it.
            if (propagate_carry) {
              PropagateCarry(v, did_carry);
            }
          }
          break;
        case OPCODE_MUL:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Mul(i->src2.value);
            i->Remove();
          }
          break;
        case OPCODE_DIV:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Div(i->src2.value);
            i->Remove();
          }
          break;
        // case OPCODE_MUL_ADD:
        // case OPCODE_MUL_SUB
        case OPCODE_NEG:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Neg();
            i->Remove();
          }
          break;
        case OPCODE_ABS:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Abs();
            i->Remove();
          }
          break;
        case OPCODE_SQRT:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Sqrt();
            i->Remove();
          }
          break;
        case OPCODE_RSQRT:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->RSqrt();
            i->Remove();
          }
          break;

        case OPCODE_AND:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->And(i->src2.value);
            i->Remove();
          }
          break;
        case OPCODE_OR:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Or(i->src2.value);
            i->Remove();
          }
          break;
        case OPCODE_XOR:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Xor(i->src2.value);
            i->Remove();
          }
          break;
        case OPCODE_NOT:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Not();
            i->Remove();
          }
          break;
        case OPCODE_SHL:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Shl(i->src2.value);
            i->Remove();
          }
          break;
        // TODO(benvanik): VECTOR_SHL
        case OPCODE_SHR:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Shr(i->src2.value);
            i->Remove();
          }
          break;
        case OPCODE_SHA:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Sha(i->src2.value);
            i->Remove();
          }
          break;
        // TODO(benvanik): ROTATE_LEFT
        case OPCODE_BYTE_SWAP:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->ByteSwap();
            i->Remove();
          }
          break;
        case OPCODE_CNTLZ:
          if (i->src1.value->IsConstant()) {
            v->set_zero(v->type);
            v->CountLeadingZeros(i->src1.value);
            i->Remove();
          }
          break;
        // TODO(benvanik): INSERT/EXTRACT
        // TODO(benvanik): SPLAT/PERMUTE/SWIZZLE
        case OPCODE_SPLAT:
          if (i->src1.value->IsConstant()) {
            // Quite a few of these, from building vec128s.
          }
          break;

        default:
          // Ignored.
          break;
      }
      i = i->next;
    }

    block = block->next;
  }

  return 0;
}

void ConstantPropagationPass::PropagateCarry(Value* v, bool did_carry) {
  auto next = v->use_head;
  while (next) {
    auto use = next;
    next = use->next;
    if (use->instr->opcode == &OPCODE_DID_CARRY_info) {
      // Replace carry value.
      use->instr->dest->set_constant(did_carry ? 1 : 0);
      use->instr->Remove();
    }
  }
}

}  // namespace passes
}  // namespace compiler
}  // namespace alloy

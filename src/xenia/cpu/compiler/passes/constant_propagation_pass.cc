/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/constant_propagation_pass.h"

#include <gflags/gflags.h>
#include <cmath>

#include "xenia/base/assert.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/processor.h"

DEFINE_bool(inline_mmio_access, true, "Inline constant MMIO loads and stores.");

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::TypeName;
using xe::cpu::hir::Value;

ConstantPropagationPass::ConstantPropagationPass()
    : ConditionalGroupSubpass() {}

ConstantPropagationPass::~ConstantPropagationPass() {}

bool ConstantPropagationPass::Run(HIRBuilder* builder, bool& result) {
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

  result = false;
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
            result = true;
          }
          break;

        case OPCODE_TRAP_TRUE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantTrue()) {
              i->Replace(&OPCODE_TRAP_info, i->flags);
            } else {
              i->Remove();
            }
            result = true;
          }
          break;

        case OPCODE_CALL_TRUE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantTrue()) {
              auto symbol = i->src2.symbol;
              i->Replace(&OPCODE_CALL_info, i->flags);
              i->src1.symbol = symbol;
            } else {
              i->Remove();
            }
            result = true;
          }
          break;
        case OPCODE_CALL_INDIRECT:
          if (i->src1.value->IsConstant()) {
            auto function = processor_->LookupFunction(
                uint32_t(i->src1.value->constant.i32));
            if (!function) {
              break;
            }
            i->Replace(&OPCODE_CALL_info, i->flags);
            i->src1.symbol = function;
            result = true;
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
            result = true;
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
            result = true;
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
            result = true;
          }
          break;

        case OPCODE_CAST:
          if (i->src1.value->IsConstant()) {
            TypeName target_type = v->type;
            v->set_from(i->src1.value);
            v->Cast(target_type);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_CONVERT:
          if (i->src1.value->IsConstant()) {
            TypeName target_type = v->type;
            v->set_from(i->src1.value);
            v->Convert(target_type, RoundMode(i->flags));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_ROUND:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Round(RoundMode(i->flags));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_ZERO_EXTEND:
          if (i->src1.value->IsConstant()) {
            TypeName target_type = v->type;
            v->set_from(i->src1.value);
            v->ZeroExtend(target_type);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_SIGN_EXTEND:
          if (i->src1.value->IsConstant()) {
            TypeName target_type = v->type;
            v->set_from(i->src1.value);
            v->SignExtend(target_type);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_TRUNCATE:
          if (i->src1.value->IsConstant()) {
            TypeName target_type = v->type;
            v->set_from(i->src1.value);
            v->Truncate(target_type);
            i->Remove();
            result = true;
          }
          break;

        case OPCODE_LOAD:
        case OPCODE_LOAD_OFFSET:
          if (i->src1.value->IsConstant()) {
            assert_false(i->flags & LOAD_STORE_BYTE_SWAP);
            auto memory = processor_->memory();
            auto address = i->src1.value->constant.i32;
            if (i->opcode->num == OPCODE_LOAD_OFFSET) {
              address += i->src2.value->constant.i32;
            }

            auto mmio_range =
                processor_->memory()->LookupVirtualMappedRange(address);
            if (FLAGS_inline_mmio_access && mmio_range) {
              i->Replace(&OPCODE_LOAD_MMIO_info, 0);
              i->src1.offset = reinterpret_cast<uint64_t>(mmio_range);
              i->src2.offset = address;
              result = true;
            } else {
              auto heap = memory->LookupHeap(address);
              uint32_t protect;
              if (heap && heap->QueryProtect(address, &protect) &&
                  !(protect & kMemoryProtectWrite) &&
                  (protect & kMemoryProtectRead)) {
                // Memory is readonly - can just return the value.
                auto host_addr = memory->TranslateVirtual(address);
                switch (v->type) {
                  case INT8_TYPE:
                    v->set_constant(xe::load<uint8_t>(host_addr));
                    i->Remove();
                    result = true;
                    break;
                  case INT16_TYPE:
                    v->set_constant(xe::load<uint16_t>(host_addr));
                    i->Remove();
                    result = true;
                    break;
                  case INT32_TYPE:
                    v->set_constant(xe::load<uint32_t>(host_addr));
                    i->Remove();
                    result = true;
                    break;
                  case INT64_TYPE:
                    v->set_constant(xe::load<uint64_t>(host_addr));
                    i->Remove();
                    result = true;
                    break;
                  case VEC128_TYPE:
                    vec128_t val;
                    val.low = xe::load<uint64_t>(host_addr);
                    val.high = xe::load<uint64_t>(host_addr + 8);
                    v->set_constant(val);
                    i->Remove();
                    result = true;
                    break;
                  default:
                    assert_unhandled_case(v->type);
                    break;
                }
              }
            }
          }
          break;
        case OPCODE_STORE:
        case OPCODE_STORE_OFFSET:
          if (FLAGS_inline_mmio_access && i->src1.value->IsConstant()) {
            auto address = i->src1.value->constant.i32;
            if (i->opcode->num == OPCODE_STORE_OFFSET) {
              address += i->src2.value->constant.i32;
            }

            auto mmio_range =
                processor_->memory()->LookupVirtualMappedRange(address);
            if (mmio_range) {
              auto value = i->src2.value;
              if (i->opcode->num == OPCODE_STORE_OFFSET) {
                value = i->src3.value;
              }

              i->Replace(&OPCODE_STORE_MMIO_info, 0);
              i->src1.offset = reinterpret_cast<uint64_t>(mmio_range);
              i->src2.offset = address;
              i->set_src3(value);
              result = true;
            }
          }
          break;

        case OPCODE_SELECT:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->type != VEC128_TYPE) {
              if (i->src1.value->IsConstantTrue()) {
                auto src2 = i->src2.value;
                i->Replace(&OPCODE_ASSIGN_info, 0);
                i->set_src1(src2);
                result = true;
              } else if (i->src1.value->IsConstantFalse()) {
                auto src3 = i->src3.value;
                i->Replace(&OPCODE_ASSIGN_info, 0);
                i->set_src1(src3);
                result = true;
              } else if (i->src2.value->IsConstant() &&
                         i->src3.value->IsConstant()) {
                // TODO: Select
                // v->set_from(i->src2.value);
                // v->Select(i->src3.value, i->src1.value);
                // i->Remove();
              }
            } else {
              // TODO: vec128 select
            }
          }
          break;
        case OPCODE_IS_TRUE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantTrue()) {
              v->set_constant(uint8_t(1));
            } else {
              v->set_constant(uint8_t(0));
            }
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_IS_FALSE:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->IsConstantFalse()) {
              v->set_constant(uint8_t(1));
            } else {
              v->set_constant(uint8_t(0));
            }
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_IS_NAN:
          if (i->src1.value->IsConstant()) {
            if (i->src1.value->type == FLOAT32_TYPE &&
                std::isnan(i->src1.value->constant.f32)) {
              v->set_constant(uint8_t(1));
            } else if (i->src1.value->type == FLOAT64_TYPE &&
                       std::isnan(i->src1.value->constant.f64)) {
              v->set_constant(uint8_t(1));
            } else {
              v->set_constant(uint8_t(0));
            }
            i->Remove();
            result = true;
          }
          break;

        // TODO(benvanik): compares
        case OPCODE_COMPARE_EQ:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantEQ(i->src2.value);
            i->dest->set_constant(uint8_t(value));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_COMPARE_NE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantNE(i->src2.value);
            i->dest->set_constant(uint8_t(value));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_COMPARE_SLT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantSLT(i->src2.value);
            i->dest->set_constant(uint8_t(value));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_COMPARE_SLE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantSLE(i->src2.value);
            i->dest->set_constant(uint8_t(value));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_COMPARE_SGT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantSGT(i->src2.value);
            i->dest->set_constant(uint8_t(value));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_COMPARE_SGE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantSGE(i->src2.value);
            i->dest->set_constant(uint8_t(value));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_COMPARE_ULT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantULT(i->src2.value);
            i->dest->set_constant(uint8_t(value));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_COMPARE_ULE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantULE(i->src2.value);
            i->dest->set_constant(uint8_t(value));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_COMPARE_UGT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantUGT(i->src2.value);
            i->dest->set_constant(uint8_t(value));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_COMPARE_UGE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            bool value = i->src1.value->IsConstantUGE(i->src2.value);
            i->dest->set_constant(uint8_t(value));
            i->Remove();
            result = true;
          }
          break;

        case OPCODE_DID_SATURATE:
          // assert_true(!i->src1.value->IsConstant());
          break;

        case OPCODE_ADD:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Add(i->src2.value);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_ADD_CARRY:
          if (i->src1.value->IsConstantZero() &&
              i->src2.value->IsConstantZero()) {
            Value* ca = i->src3.value;
            if (ca->IsConstant()) {
              TypeName target_type = v->type;
              v->set_from(ca);
              v->ZeroExtend(target_type);
              i->Remove();
            } else {
              if (i->dest->type == ca->type) {
                i->Replace(&OPCODE_ASSIGN_info, 0);
                i->set_src1(ca);
              } else {
                i->Replace(&OPCODE_ZERO_EXTEND_info, 0);
                i->set_src1(ca);
              }
            }
            result = true;
          }
          break;
        case OPCODE_SUB:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Sub(i->src2.value);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_MUL:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Mul(i->src2.value);
            i->Remove();
            result = true;
          } else if (i->src1.value->IsConstant() ||
                     i->src2.value->IsConstant()) {
            // Reorder the sources to make things simpler.
            // s1 = non-const, s2 = const
            auto s1 =
                i->src1.value->IsConstant() ? i->src2.value : i->src1.value;
            auto s2 =
                i->src1.value->IsConstant() ? i->src1.value : i->src2.value;

            // Multiplication by one = no-op
            if (s2->type != VEC128_TYPE && s2->IsConstantOne()) {
              i->Replace(&OPCODE_ASSIGN_info, 0);
              i->set_src1(s1);
              result = true;
            } else if (s2->type == VEC128_TYPE) {
              auto& c = s2->constant;
              if (c.v128.f32[0] == 1.f && c.v128.f32[1] == 1.f &&
                  c.v128.f32[2] == 1.f && c.v128.f32[3] == 1.f) {
                i->Replace(&OPCODE_ASSIGN_info, 0);
                i->set_src1(s1);
                result = true;
              }
            }
          }
          break;
        case OPCODE_MUL_HI:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->MulHi(i->src2.value, (i->flags & ARITHMETIC_UNSIGNED) != 0);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_DIV:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Div(i->src2.value, (i->flags & ARITHMETIC_UNSIGNED) != 0);
            i->Remove();
            result = true;
          } else if (i->src2.value->IsConstant()) {
            // Division by one = no-op.
            Value* src1 = i->src1.value;
            if (i->src2.value->type != VEC128_TYPE &&
                i->src2.value->IsConstantOne()) {
              i->Replace(&OPCODE_ASSIGN_info, 0);
              i->set_src1(src1);
              result = true;
            } else if (i->src2.value->type == VEC128_TYPE) {
              auto& c = i->src2.value->constant;
              if (c.v128.f32[0] == 1.f && c.v128.f32[1] == 1.f &&
                  c.v128.f32[2] == 1.f && c.v128.f32[3] == 1.f) {
                i->Replace(&OPCODE_ASSIGN_info, 0);
                i->set_src1(src1);
                result = true;
              }
            }
          }
          break;
        case OPCODE_MUL_ADD:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            if (i->src3.value->IsConstant()) {
              v->set_from(i->src1.value);
              Value::MulAdd(v, i->src1.value, i->src2.value, i->src3.value);
              i->Remove();
              result = true;
            } else {
              // Multiply part is constant.
              Value* mul = builder->AllocValue();
              mul->set_from(i->src1.value);
              mul->Mul(i->src2.value);

              Value* add = i->src3.value;
              i->Replace(&OPCODE_ADD_info, 0);
              i->set_src1(mul);
              i->set_src2(add);

              result = true;
            }
          }
          break;
        case OPCODE_MUL_SUB:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            // Multiply part is constant.
            if (i->src3.value->IsConstant()) {
              v->set_from(i->src1.value);
              Value::MulSub(v, i->src1.value, i->src2.value, i->src3.value);
              i->Remove();
              result = true;
            } else {
              // Multiply part is constant.
              Value* mul = builder->AllocValue();
              mul->set_from(i->src1.value);
              mul->Mul(i->src2.value);

              Value* add = i->src3.value;
              i->Replace(&OPCODE_SUB_info, 0);
              i->set_src1(mul);
              i->set_src2(add);

              result = true;
            }
          }
          break;
        case OPCODE_MAX:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Max(i->src2.value);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_NEG:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Neg();
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_ABS:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Abs();
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_SQRT:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Sqrt();
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_RSQRT:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->RSqrt();
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_RECIP:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Recip();
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_AND:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->And(i->src2.value);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_OR:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Or(i->src2.value);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_XOR:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Xor(i->src2.value);
            i->Remove();
            result = true;
          } else if (!i->src1.value->IsConstant() &&
                     !i->src2.value->IsConstant() &&
                     i->src1.value == i->src2.value) {
            v->set_zero(v->type);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_NOT:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Not();
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_SHL:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Shl(i->src2.value);
            i->Remove();
            result = true;
          } else if (i->src2.value->IsConstantZero()) {
            auto src1 = i->src1.value;
            i->Replace(&OPCODE_ASSIGN_info, 0);
            i->set_src1(src1);
            result = true;
          }
          break;
        case OPCODE_SHR:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Shr(i->src2.value);
            i->Remove();
            result = true;
          } else if (i->src2.value->IsConstantZero()) {
            auto src1 = i->src1.value;
            i->Replace(&OPCODE_ASSIGN_info, 0);
            i->set_src1(src1);
            result = true;
          }
          break;
        case OPCODE_SHA:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->Sha(i->src2.value);
            i->Remove();
            result = true;
          }
          break;
        // TODO(benvanik): ROTATE_LEFT
        case OPCODE_BYTE_SWAP:
          if (i->src1.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->ByteSwap();
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_CNTLZ:
          if (i->src1.value->IsConstant()) {
            v->set_zero(v->type);
            v->CountLeadingZeros(i->src1.value);
            i->Remove();
            result = true;
          }
          break;
        // TODO(benvanik): INSERT/EXTRACT
        // TODO(benvanik): PERMUTE/SWIZZLE
        case OPCODE_EXTRACT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_zero(v->type);
            v->Extract(i->src1.value, i->src2.value);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_SPLAT:
          if (i->src1.value->IsConstant()) {
            v->set_zero(v->type);
            v->Splat(i->src1.value);
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_COMPARE_EQ:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->VectorCompareEQ(i->src2.value, hir::TypeName(i->flags));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_COMPARE_SGT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->VectorCompareSGT(i->src2.value, hir::TypeName(i->flags));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_COMPARE_SGE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->VectorCompareSGE(i->src2.value, hir::TypeName(i->flags));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_COMPARE_UGT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->VectorCompareUGT(i->src2.value, hir::TypeName(i->flags));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_COMPARE_UGE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->VectorCompareUGE(i->src2.value, hir::TypeName(i->flags));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_CONVERT_F2I:
          if (i->src1.value->IsConstant()) {
            v->set_zero(VEC128_TYPE);
            v->VectorConvertF2I(i->src1.value,
                                !!(i->flags & ARITHMETIC_UNSIGNED));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_CONVERT_I2F:
          if (i->src1.value->IsConstant()) {
            v->set_zero(VEC128_TYPE);
            v->VectorConvertI2F(i->src1.value,
                                !!(i->flags & ARITHMETIC_UNSIGNED));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_SHL:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->VectorShl(i->src2.value, hir::TypeName(i->flags));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_SHR:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->VectorShr(i->src2.value, hir::TypeName(i->flags));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_ROTATE_LEFT:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->VectorRol(i->src2.value, hir::TypeName(i->flags));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_ADD:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            uint32_t arith_flags = i->flags >> 8;
            v->VectorAdd(i->src2.value, hir::TypeName(i->flags & 0xFF),
                         !!(arith_flags & ARITHMETIC_UNSIGNED),
                         !!(arith_flags & ARITHMETIC_SATURATE));
            i->Remove();
            result = true;
          }
          break;
        case OPCODE_VECTOR_SUB:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            uint32_t arith_flags = i->flags >> 8;
            v->VectorSub(i->src2.value, hir::TypeName(i->flags & 0xFF),
                         !!(arith_flags & ARITHMETIC_UNSIGNED),
                         !!(arith_flags & ARITHMETIC_SATURATE));
            i->Remove();
            result = true;
          }
          break;

        case OPCODE_DOT_PRODUCT_3:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->DotProduct3(i->src2.value);
            i->Remove();
            result = true;
          }
          break;

        case OPCODE_DOT_PRODUCT_4:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            v->DotProduct4(i->src2.value);
            i->Remove();
            result = true;
          }
          break;

        case OPCODE_VECTOR_AVERAGE:
          if (i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
            v->set_from(i->src1.value);
            uint32_t arith_flags = i->flags >> 8;
            v->VectorAverage(i->src2.value, hir::TypeName(i->flags & 0xFF),
                             !!(arith_flags & ARITHMETIC_UNSIGNED),
                             !!(arith_flags & ARITHMETIC_SATURATE));
            i->Remove();
            result = true;
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

  return true;
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/simplification_pass.h"

#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using xe::cpu::hir::Value;

SimplificationPass::SimplificationPass() : ConditionalGroupSubpass() {}

SimplificationPass::~SimplificationPass() {}

bool SimplificationPass::Run(HIRBuilder* builder, bool& result) {
  result = false;

  result |= SimplifyBitArith(builder);
  result |= EliminateConversions(builder);
  result |= SimplifyAssignments(builder);
  result |= SimplifyBasicArith(builder);
  result |= SimplifyVectorOps(builder);

  return true;
}
// simplifications that apply to both or and xor
bool SimplificationPass::CheckOrXorZero(hir::Instr* i) {
  auto [constant_value, variable_value] = i->BinaryValueArrangeAsConstAndVar();

  if (constant_value && constant_value->IsConstantZero()) {
    i->Replace(&OPCODE_ASSIGN_info, 0);
    i->set_src1(variable_value);
    return true;
  }
  return false;
}
static bool IsScalarBasicCmp(Opcode op) {
  /*
    OPCODE_COMPARE_EQ,
  OPCODE_COMPARE_NE,
  OPCODE_COMPARE_SLT,
  OPCODE_COMPARE_SLE,
  OPCODE_COMPARE_SGT,
  OPCODE_COMPARE_SGE,
  OPCODE_COMPARE_ULT,
  OPCODE_COMPARE_ULE,
  OPCODE_COMPARE_UGT,
  OPCODE_COMPARE_UGE,
  */
  return op >= OPCODE_COMPARE_EQ && op <= OPCODE_COMPARE_UGE;
}

static bool SameValueOrEqualConstant(hir::Value* x, hir::Value* y) {
  if (x == y) return true;

  if (x->IsConstant() && y->IsConstant()) {
    return x->AsUint64() == y->AsUint64();
  }

  return false;
}

static bool CompareDefsHaveSameOpnds(hir::Value* cmp1, hir::Value* cmp2,
                                     hir::Value** out_cmped_l,
                                     hir::Value** out_cmped_r, Opcode* out_l_op,
                                     Opcode* out_r_op) {
  auto df1 = cmp1->def;
  auto df2 = cmp2->def;
  if (!df1 || !df2) return false;
  if (df1->src1.value != df2->src1.value) return false;

  Opcode lop = df1->opcode->num, rop = df2->opcode->num;

  if (!IsScalarBasicCmp(lop) || !IsScalarBasicCmp(rop)) return false;

  if (!SameValueOrEqualConstant(df1->src2.value, df2->src2.value)) {
    return false;
  }

  *out_cmped_l = df1->src1.value;
  *out_cmped_r = df1->src2.value;
  *out_l_op = lop;
  *out_r_op = rop;
  return true;
}

bool SimplificationPass::CheckOr(hir::Instr* i, hir::HIRBuilder* builder) {
  if (CheckOrXorZero(i)) return true;

  if (i->src1.value == i->src2.value) {
    auto old1 = i->src1.value;
    i->Replace(&OPCODE_ASSIGN_info, 0);
    i->set_src1(old1);
    return true;
  }

  if (i->dest->type == INT8_TYPE) {
    Opcode l_op, r_op;
    Value *cmpl, *cmpr;
    if (!CompareDefsHaveSameOpnds(i->src1.value, i->src2.value, &cmpl, &cmpr,
                                  &l_op, &r_op)) {
      return false;
    }
    auto have_both_ops = [l_op, r_op](Opcode expect1, Opcode expect2) {
      return (l_op == expect1 || r_op == expect1) &&
             (l_op == expect2 || r_op == expect2);
    };

    if (have_both_ops(OPCODE_COMPARE_EQ, OPCODE_COMPARE_NE)) {
      // both equal and not equal means always true
      i->Replace(&OPCODE_ASSIGN_info, 0);
      i->set_src1(builder->LoadConstantInt8(1));
      return true;
    }
    const OpcodeInfo* new_cmpop = nullptr;

    if (have_both_ops(OPCODE_COMPARE_EQ, OPCODE_COMPARE_SLT)) {
      new_cmpop = &OPCODE_COMPARE_SLE_info;
    } else if (have_both_ops(OPCODE_COMPARE_EQ, OPCODE_COMPARE_SGT)) {
      new_cmpop = &OPCODE_COMPARE_SGE_info;
    } else if (have_both_ops(OPCODE_COMPARE_EQ, OPCODE_COMPARE_ULT)) {
      new_cmpop = &OPCODE_COMPARE_ULE_info;
    } else if (have_both_ops(OPCODE_COMPARE_EQ, OPCODE_COMPARE_UGT)) {
      new_cmpop = &OPCODE_COMPARE_UGE_info;
    }
    // todo: also check for pointless compares

    if (new_cmpop != nullptr) {
      i->Replace(new_cmpop, 0);
      i->set_src1(cmpl);
      i->set_src2(cmpr);
      return true;
    }
  }
  return false;
}
bool SimplificationPass::CheckBooleanXor1(hir::Instr* i,
                                          hir::HIRBuilder* builder,
                                          hir::Value* xored) {
  unsigned tunflags = MOVTUNNEL_ASSIGNS | MOVTUNNEL_MOVZX;

  Instr* xordef = xored->GetDefTunnelMovs(&tunflags);
  if (!xordef) {
    return false;
  }

  Opcode xorop = xordef->opcode->num;
  bool need_zx = (tunflags & MOVTUNNEL_MOVZX) != 0;

  Value* new_value = nullptr;
  if (xorop == OPCODE_COMPARE_EQ) {
    new_value = builder->CompareNE(xordef->src1.value, xordef->src2.value);

  } else if (xorop == OPCODE_COMPARE_NE) {
    new_value = builder->CompareEQ(xordef->src1.value, xordef->src2.value);
  }  // todo: other conds

  if (!new_value) {
    return false;
  }

  new_value->def->MoveBefore(i);

  i->Replace(need_zx ? &OPCODE_ZERO_EXTEND_info : &OPCODE_ASSIGN_info, 0);
  i->set_src1(new_value);

  return true;
}

bool SimplificationPass::CheckXorOfTwoBools(hir::Instr* i,
                                            hir::HIRBuilder* builder,
                                            hir::Value* b1, hir::Value* b2) {
  // todo: implement
  return false;
}
bool SimplificationPass::CheckXor(hir::Instr* i, hir::HIRBuilder* builder) {
  if (CheckOrXorZero(i)) {
    return true;
  } else {
    Value* src1 = i->src1.value;
    Value* src2 = i->src2.value;

    if (SameValueOrEqualConstant(src1, src2)) {
      i->Replace(&OPCODE_ASSIGN_info, 0);
      i->set_src1(builder->LoadZero(i->dest->type));
      return true;
    }
    auto [constant_value, variable_value] =
        i->BinaryValueArrangeAsConstAndVar();
    ScalarNZM nzm1 = GetScalarNZM(src1);
    ScalarNZM nzm2 = GetScalarNZM(src2);

    if ((nzm1 & nzm2) ==
        0) {  // no bits of the two sources overlap, this ought to be an OR
      // cs:optimizing
      /* i->Replace(&OPCODE_OR_info, 0);
      i->set_src1(src1);
      i->set_src2(src2);*/

      i->opcode = &OPCODE_OR_info;

      return true;
    }

    if (nzm1 == 1ULL && nzm2 == 1ULL) {
      if (constant_value) {
        return CheckBooleanXor1(i, builder, variable_value);
      } else {
        return CheckXorOfTwoBools(i, builder, src1, src2);
      }
    }

    uint64_t type_mask = GetScalarTypeMask(i->dest->type);

    if (!constant_value) return false;

    if (constant_value->AsUint64() == type_mask) {
      i->Replace(&OPCODE_NOT_info, 0);
      i->set_src1(variable_value);
      return true;
    }
  }
  return false;
}
bool SimplificationPass::Is1BitOpcode(hir::Opcode def_opcode) {
  return def_opcode >= OPCODE_COMPARE_EQ && def_opcode <= OPCODE_DID_SATURATE;
}

inline static uint64_t RotateOfSize(ScalarNZM nzm, unsigned rotation,
                                    TypeName typ) {
  unsigned mask = (static_cast<unsigned int>(GetTypeSize(typ)) * CHAR_BIT) - 1;

  rotation &= mask;
  return (nzm << rotation) |
         (nzm >>
          ((static_cast<unsigned int>(-static_cast<int>(rotation))) & mask));
}

static inline uint64_t BswapOfSize(ScalarNZM nzm, TypeName typ) {
  // should work for all sizes, including uint8

  return xe::byte_swap<uint64_t>(nzm) >> (64 - (GetTypeSize(typ) * CHAR_BIT));
}

ScalarNZM SimplificationPass::GetScalarNZM(const hir::Value* value,
                                           unsigned depth) {
redo:

  uint64_t typemask = GetScalarTypeMask(value->type);
  if (value->IsConstant()) {
    return value->constant.u64 & typemask;
  }
  if (depth >= MAX_NZM_DEPTH) {
    return typemask;
  }
  const hir::Instr* def = value->def;
  if (!def) {
    return typemask;
  }

  const hir::Value *def_src1 = def->src1.value, *def_src2 = def->src2.value,
                   *def_src3 = def->src3.value;

  const hir::Opcode def_opcode = def->opcode->num;
  auto RecurseScalarNZM = [depth](const hir::Value* value) {
    return GetScalarNZM(value, depth + 1);
  };

  switch (def_opcode) {
    case OPCODE_SHL: {
      const hir::Value* shifted = def_src1;
      const hir::Value* shiftby = def_src2;
      // todo: nzm shift
      if (shiftby->IsConstant()) {
        ScalarNZM shifted_nzm = RecurseScalarNZM(shifted);
        return (shifted_nzm << shiftby->constant.u8) & typemask;
      }
      break;
    }
    case OPCODE_SHR: {
      const hir::Value* shifted = def_src1;
      const hir::Value* shiftby = def_src2;
      // todo: nzm shift
      if (shiftby->IsConstant()) {
        ScalarNZM shifted_nzm = RecurseScalarNZM(shifted);
        return shifted_nzm >> shiftby->constant.u8;
      }
      break;
    }
    case OPCODE_SHA: {
      uint64_t signmask = GetScalarSignbitMask(value->type);
      const hir::Value* shifted = def_src1;
      const hir::Value* shiftby = def_src2;

      if (shiftby->IsConstant()) {
        ScalarNZM shifted_nzm = RecurseScalarNZM(shifted);
        uint32_t shift_amnt = shiftby->constant.u8;
        if ((shifted_nzm & signmask) == 0) {
          return shifted_nzm >> shift_amnt;
        } else {
          uint64_t sign_shifted_in = typemask >> shift_amnt;  // vacate
          sign_shifted_in = (~sign_shifted_in) &
                            typemask;  // generate duplication of the signbit

          return ((shifted_nzm >> shift_amnt) | sign_shifted_in) &
                 typemask;  // todo: mask with typemask might not be needed
        }
      }
      break;
    }
    case OPCODE_ROTATE_LEFT: {
      const hir::Value* shifted = def_src1;
      const hir::Value* shiftby = def_src2;
      // todo: nzm shift
      if (shiftby->IsConstant()) {
        ScalarNZM rotated_nzm = RecurseScalarNZM(shifted);
        return RotateOfSize(rotated_nzm, shiftby->constant.u8, value->type);
      }
      break;
    }

    case OPCODE_OR:
    case OPCODE_XOR:

      return RecurseScalarNZM(def_src1) | RecurseScalarNZM(def_src2);
    case OPCODE_NOT:
      return typemask;  //~GetScalarNZM(def->src1.value);
    case OPCODE_ASSIGN:
      value = def_src1;
      goto redo;
    case OPCODE_BYTE_SWAP:
      return BswapOfSize(RecurseScalarNZM(def_src1), value->type);
    case OPCODE_ZERO_EXTEND:
      value = def_src1;
      goto redo;
    case OPCODE_TRUNCATE:
      return RecurseScalarNZM(def_src1) & typemask;

    case OPCODE_AND:
      return RecurseScalarNZM(def_src1) & RecurseScalarNZM(def_src2);

    case OPCODE_MIN:
      /*
      * for min:
  the nzm will be that of the narrowest operand, because if one value
 is capable of being much larger than the other it can never actually
 reach a value that is outside the range of the other values nzm,
 because that would make it not the minimum of the two

  ahh, actually, we have to be careful about constants then.... for
 now, just return or
*/
    case OPCODE_MAX:
      return RecurseScalarNZM(def_src1) | RecurseScalarNZM(def_src2);
    case OPCODE_SELECT:
      return RecurseScalarNZM(def_src2) | RecurseScalarNZM(def_src3);

    case OPCODE_CNTLZ: {
      size_t type_bit_size = (CHAR_BIT * GetTypeSize(def_src1->type));
      // if 0, ==type_bit_size
      return type_bit_size | ((type_bit_size)-1);
    }
    case OPCODE_SIGN_EXTEND: {
      ScalarNZM input_nzm = RecurseScalarNZM(def_src1);
      if ((input_nzm & GetScalarSignbitMask(def_src1->type)) == 0) {
        return input_nzm;

        // return input_nzm;
      } else {
        uint64_t from_mask = GetScalarTypeMask(def_src1->type);
        uint64_t to_mask = typemask;
        to_mask &= ~from_mask;
        to_mask |= input_nzm;
        return to_mask & typemask;
      }
      break;
    }
  }

  if (Is1BitOpcode(def_opcode)) {
    return 1ULL;
  }
  return typemask;
}

bool SimplificationPass::TransformANDROLORSHLSeq(
    hir::Instr* i,  // the final instr in the rlwinm sequence (the AND)
    hir::HIRBuilder* builder,
    ScalarNZM input_nzm,    // the NZM of the value being rot'ed/masked (input)
    uint64_t mask,          // mask after rotation
    uint64_t rotation,      // rot amount prior to masking
    hir::Instr* input_def,  // the defining instr of the input var
    hir::Value* input       // the input variable to the rlwinm
) {
  /*

    Usually you perform a rotation at the source level by decomposing it into
    two shifts, one left and one right, and an or of those two. your compiler
    then recognizes that you are doing a shift and generates the machine
    instruction for a 32 bit int, you would do (value<<LEFT_ROT_AMNT) |
    (value>>( (-LEFT_ROT_AMNT)&31 )); in order to reason about the bits actually
    used by our rotate and mask operation we decompose our rotate into these two
    shifts and check them against our mask to determine whether the masking is
    useless, or if it only covers bits from the left or right shift if it is a
    right shift, and the mask exactly covers all bits that were not vacated
    right, we eliminate the mask

  */
  uint32_t right_shift_for_rotate =
      (static_cast<uint32_t>(-static_cast<int32_t>(rotation)) & 31);
  uint32_t left_shift_for_rotate = (rotation & 31);

  uint64_t rotation_shiftmask_right =
      static_cast<uint64_t>((~0U) >> right_shift_for_rotate);
  uint64_t rotation_shiftmask_left =
      static_cast<uint64_t>((~0U) << left_shift_for_rotate);

  if (rotation_shiftmask_left == mask) {
    Value* rotation_fix = builder->Shl(input, left_shift_for_rotate);
    rotation_fix->def->MoveBefore(i);
    i->Replace(&OPCODE_AND_info, 0);
    i->set_src1(rotation_fix);
    i->set_src2(builder->LoadConstantUint64(0xFFFFFFFFULL));
    return true;
  } else if (rotation_shiftmask_right == mask) {
    // no need to AND at all, the bits were vacated

    i->Replace(&OPCODE_SHR_info, 0);
    i->set_src1(input);
    i->set_src2(
        builder->LoadConstantInt8(static_cast<int8_t>(right_shift_for_rotate)));
    return true;
  } else if ((rotation_shiftmask_right & mask) ==
             mask) {  // only uses bits from right
    Value* rotation_fix = builder->Shr(input, right_shift_for_rotate);
    rotation_fix->def->MoveBefore(i);
    i->set_src1(rotation_fix);
    return true;

  } else if ((rotation_shiftmask_left & mask) == mask) {
    Value* rotation_fix = builder->Shl(input, left_shift_for_rotate);
    rotation_fix->def->MoveBefore(i);
    i->set_src1(rotation_fix);
    return true;
  }
  return false;
}
/*
    todo: investigate breaking this down into several different optimization
   rules. one for rotates that can be made into shifts one for 64 bit rotates
   that can become 32 bit and probably one or two other rules would need to be
   added for this)
*/
bool SimplificationPass::TryHandleANDROLORSHLSeq(hir::Instr* i,
                                                 hir::HIRBuilder* builder) {
  // todo: could probably make this work for all types without much trouble, but
  // not sure if it would benefit any of them
  if (i->dest->type != INT64_TYPE) {
    return false;
  }
  auto [defined_by_rotate, mask] =
      i->BinaryValueArrangeByDefOpAndConstant(&OPCODE_ROTATE_LEFT_info);

  if (!defined_by_rotate) {
    return false;
  }

  uint64_t mask_value = mask->AsUint64();

  if (static_cast<uint64_t>(static_cast<uint32_t>(mask_value)) != mask_value) {
    return false;
  }

  Instr* rotinsn = defined_by_rotate->def;

  Value* rotated_variable = rotinsn->src1.value;
  Value* rotation_amount = rotinsn->src2.value;
  if (!rotation_amount->IsConstant()) {
    return false;
  }

  uint32_t rotation_value = rotation_amount->constant.u8;
  if (rotation_value > 31) {
    return false;
  }

  if (rotated_variable->IsConstant()) {
    return false;
  }
  Instr* rotated_variable_def = rotated_variable->GetDefSkipAssigns();

  if (!rotated_variable_def) {
    return false;
  }
  if (rotated_variable_def->opcode != &OPCODE_OR_info) {
    return false;
  }
  // now we have our or that concatenates the var with itself, find the shift by
  // 32
  auto [shl_insn_defined, expected_input] =
      rotated_variable_def->BinaryValueArrangeByPredicateExclusive(
          [](Value* shl_check) {
            Instr* shl_def = shl_check->GetDefSkipAssigns();

            if (!shl_def) {
              return false;
            }

            Value* shl_left = shl_def->src1.value;
            Value* shl_right = shl_def->src2.value;
            if (!shl_left || !shl_right) {
              return false;
            }
            if (shl_def->opcode != &OPCODE_SHL_info) {
              return false;
            }
            return !shl_left->IsConstant() && shl_right->IsConstant() &&
                   shl_right->constant.u8 == 32;
          });

  if (!shl_insn_defined) {
    return false;
  }

  Instr* shl_insn = shl_insn_defined->GetDefSkipAssigns();

  Instr* shl_left_definition = shl_insn->src1.value->GetDefSkipAssigns();
  Instr* expected_input_definition = expected_input->GetDefSkipAssigns();

  // we expect (x << 32) | x

  if (shl_left_definition != expected_input_definition) {
    return false;
  }
  // next check the nzm for the input, if the high 32 bits are clear we know we
  // can optimize. the input ought to have been cleared by an AND with ~0U or
  // truncate zeroextend if its a rlwinx

  // todo: it shouldnt have to search very far for the nzm, we dont need the
  // whole depth of the use-def chain traversed, add a maxdef var? or do
  // additional opts here depending on the nzm
  ScalarNZM input_nzm = GetScalarNZM(expected_input);

  if (static_cast<uint64_t>(static_cast<uint32_t>(input_nzm)) != input_nzm) {
    return false;  // bits from the or may overlap!!
  }

  return TransformANDROLORSHLSeq(i, builder, input_nzm, mask_value,
                                 rotation_value, expected_input_definition,
                                 expected_input);
}
bool SimplificationPass::CheckAnd(hir::Instr* i, hir::HIRBuilder* builder) {
retry_and_simplification:

  auto [constant_value, variable_value] = i->BinaryValueArrangeAsConstAndVar();
  if (!constant_value) {
    // added this for srawi
    ScalarNZM nzml = GetScalarNZM(i->src1.value);
    ScalarNZM nzmr = GetScalarNZM(i->src2.value);

    if ((nzml & nzmr) == 0) {
      i->Replace(&OPCODE_ASSIGN_info, 0);
      i->set_src1(builder->LoadZero(i->dest->type));
      return true;
    }
    return false;
  }

  if (TryHandleANDROLORSHLSeq(i, builder)) {
    return true;
  }

  // todo: check if masking with mask that covers all of zero extension source
  uint64_t type_mask = GetScalarTypeMask(i->dest->type);

  ScalarNZM nzm = GetScalarNZM(variable_value);
  // if masking with entire width, pointless instruction so become an assign
  // chrispy: changed this to use the nzm instead, this optimizes away many and
  // instructions
  // chrispy: changed this again. detecting if nzm is a subset of and mask, if
  // so eliminate ex: (bool value) & 0xff = (bool value). the nzm is not equal
  // to the mask, but it is a subset so can be elimed
  if ((constant_value->AsUint64() & nzm) == nzm) {
    i->Replace(&OPCODE_ASSIGN_info, 0);
    i->set_src1(variable_value);
    return true;
  }

  auto variable_def = variable_value->def;

  if (variable_def) {
    auto true_variable_def = variable_def->GetDestDefSkipAssigns();
    if (true_variable_def) {
      if (true_variable_def->opcode == &OPCODE_AND_info) {
        auto [variable_def_constant, variable_def_variable] =
            true_variable_def->BinaryValueArrangeAsConstAndVar();

        if (variable_def_constant) {
          // todo: check if masked with mask that was a subset of the current
          // one and elim if so
          if (variable_def_constant->AsUint64() == constant_value->AsUint64()) {
            // we already masked the input with the same mask
            i->Replace(&OPCODE_ASSIGN_info, 0);
            i->set_src1(variable_value);
            return true;
          }
        }
      } else if (true_variable_def->opcode == &OPCODE_OR_info) {
        Value* or_left = true_variable_def->src1.value;
        Value* or_right = true_variable_def->src2.value;

        ScalarNZM left_nzm = GetScalarNZM(or_left);

        // use the other or input instead of the or output
        if ((constant_value->AsUint64() & left_nzm) == 0) {
          i->Replace(&OPCODE_AND_info, 0);
          i->set_src1(or_right);
          i->set_src2(constant_value);
          return true;
        }

        ScalarNZM right_nzm = GetScalarNZM(or_right);

        if ((constant_value->AsUint64() & right_nzm) == 0) {
          i->Replace(&OPCODE_AND_info, 0);
          i->set_src1(or_left);
          i->set_src2(constant_value);
          return true;
        }
      } else if (true_variable_def->opcode == &OPCODE_ROTATE_LEFT_info) {
        if (true_variable_def->src2.value->IsConstant()) {
          if (((type_mask << true_variable_def->src2.value->AsUint64()) &
               type_mask) ==
              constant_value->AsUint64()) {  // rotated bits are unused, convert
                                             // to shift if we are the only use
            if (true_variable_def->dest->use_head->next == nullptr) {
              // one use, convert to shift
              true_variable_def->opcode = &OPCODE_SHL_info;
              goto retry_and_simplification;
            }
          }
        }
      }
    }
  }

  return false;
}
bool SimplificationPass::CheckAdd(hir::Instr* i, hir::HIRBuilder* builder) {
  Value* src1 = i->src1.value;
  Value* src2 = i->src2.value;

  ScalarNZM nzm1 = GetScalarNZM(src1);
  ScalarNZM nzm2 = GetScalarNZM(src2);
  if ((nzm1 & nzm2) == 0) {  // no bits overlap, there will never be a carry
                             // from any bits to any others, make this an OR

    /* i->Replace(&OPCODE_OR_info, 0);
    i->set_src1(src1);
    i->set_src2(src2);*/
    i->opcode = &OPCODE_OR_info;
    return true;
  }

  auto [definition, added_constant] =
      i->BinaryValueArrangeByDefOpAndConstant(&OPCODE_NOT_info);

  if (!definition) {
    auto [added_constant_neg, added_var_neg] =
        i->BinaryValueArrangeAsConstAndVar();

    if (!added_constant_neg) {
      return false;
    }
    if (added_constant_neg->AsUint64() &
        GetScalarSignbitMask(added_constant_neg->type)) {
      // adding a value that has its signbit set!

      Value* negconst = builder->CloneValue(added_constant_neg);
      negconst->Neg();
      i->Replace(&OPCODE_SUB_info, 0);
      i->set_src1(added_var_neg);
      i->set_src2(negconst);
      return true;
    }
    return false;
  }

  if (added_constant->AsUint64() == 1) {
    i->Replace(&OPCODE_NEG_info, 0);
    i->set_src1(definition->def->src1.value);
    return true;
  }

  return false;
}
bool SimplificationPass::CheckSelect(hir::Instr* i, hir::HIRBuilder* builder,
                                     hir::Value* condition, hir::Value* iftrue,
                                     hir::Value* iffalse) {
  return false;
}

bool SimplificationPass::CheckSelect(hir::Instr* i, hir::HIRBuilder* builder) {
  Value* src1 = i->src1.value;
  Value* src2 = i->src2.value;
  Value* src3 = i->src3.value;
  return CheckSelect(i, builder, src1, src2, src3);
}

bool SimplificationPass::CheckScalarConstCmp(hir::Instr* i,
                                             hir::HIRBuilder* builder) {
  if (!IsScalarIntegralType(i->src1.value->type)) return false;
  auto [constant_value, variable] = i->BinaryValueArrangeAsConstAndVar();

  if (!constant_value) {
    return false;
  }

  ScalarNZM nzm_for_var = GetScalarNZM(variable);
  Opcode cmpop = i->opcode->num;
  uint64_t constant_unpacked = constant_value->AsUint64();
  uint64_t signbit_for_var = GetScalarSignbitMask(variable->type);
  bool signbit_definitely_0 = (nzm_for_var & signbit_for_var) == 0;

  Instr* var_definition = variable->def;
  Opcode def_opcode = OPCODE_NOP;

  if (var_definition) {
    var_definition = var_definition->GetDestDefSkipAssigns();
    if (!var_definition) {
      return false;
    }
    def_opcode = var_definition->opcode->num;
  }
  if (!var_definition) {
    return false;
  }

  if (cmpop == OPCODE_COMPARE_ULE &&
      constant_unpacked ==
          0) {  // less than or equal to zero = (== 0) = IS_FALSE
    i->opcode = &OPCODE_COMPARE_EQ_info;

    return true;
  }
  // todo: OPCODE_COMPARE_NE too?
  if (cmpop == OPCODE_COMPARE_EQ &&
      def_opcode == OPCODE_NOT) {  // i see this a lot around addic insns

    Value* cloned = builder->CloneValue(constant_value);
    cloned->Not();
    i->Replace(&OPCODE_COMPARE_EQ_info, 0);
    i->set_src1(var_definition->src1.value);
    i->set_src2(cloned);
    return true;
  }
  if (constant_value != i->src2.value) {
    return false;
  }
  if (cmpop == OPCODE_COMPARE_ULT &&
      constant_unpacked == 1) {  // unsigned lt 1 means == 0
                                 // i->Replace(&OPCODE_IS_FALSE_info, 0);

    i->opcode = &OPCODE_COMPARE_EQ_info;

    // i->set_src1(variable);
    i->set_src2(builder->LoadZero(variable->type));
    return true;
  }
  if (cmpop == OPCODE_COMPARE_UGT &&
      constant_unpacked == 0) {  // unsigned gt 1 means != 0

    // i->Replace(&OPCODE_IS_TRUE_info, 0);
    // i->set_src1(variable);
    i->opcode = &OPCODE_COMPARE_NE_info;
    return true;
  }

  if (cmpop == OPCODE_COMPARE_ULT &&
      constant_unpacked == 0) {  // impossible to be unsigned lt 0
  impossible_compare:
    i->Replace(&OPCODE_ASSIGN_info, 0);
    i->set_src1(builder->LoadZeroInt8());
    return true;

  } else if (cmpop == OPCODE_COMPARE_UGT &&
             nzm_for_var < constant_unpacked) {  // impossible!

    goto impossible_compare;
  } else if (cmpop == OPCODE_COMPARE_SLT && signbit_definitely_0 &&
             constant_unpacked == 0) {
    goto impossible_compare;  // cant be less than 0 because signbit cannot be
                              // set
  } else if (cmpop == OPCODE_COMPARE_SGT && signbit_definitely_0 &&
             constant_unpacked == 0) {
    // signbit cant be set, and checking if gt 0, so actually checking != 0
    // i->Replace(&OPCODE_IS_TRUE_info, 0);

    // i->set_src1(variable);
    i->opcode = &OPCODE_COMPARE_NE_info;

    return true;
  }

  // value can only be one of two values, 0 or the bit set
  if (xe::bit_count(nzm_for_var) != 1) {
    return false;
  }

  if (constant_value->AsUint64() == nzm_for_var) {
    const OpcodeInfo* repl = nullptr;
    Value* constant_replacement = nullptr;

    if (cmpop == OPCODE_COMPARE_EQ || cmpop == OPCODE_COMPARE_UGE) {
      repl = &OPCODE_COMPARE_NE_info;
    } else if (cmpop == OPCODE_COMPARE_NE || cmpop == OPCODE_COMPARE_ULT) {
      repl = &OPCODE_COMPARE_EQ_info;

    } else if (cmpop == OPCODE_COMPARE_UGT) {
      // impossible, cannot be greater than mask
      constant_replacement = builder->LoadZeroInt8();

    } else if (cmpop == OPCODE_COMPARE_ULE) {  // less than or equal to mask =
                                               // always true
      constant_replacement = builder->LoadConstantInt8(1);
    }

    if (repl) {
      i->Replace(repl, 0);
      i->set_src1(variable);
      i->set_src2(builder->LoadZero(variable->type));
      return true;
    }
    if (constant_replacement) {
      i->Replace(&OPCODE_ASSIGN_info, 0);
      i->set_src1(constant_replacement);
      return true;
    }
  }

  return false;
}
bool SimplificationPass::CheckIsTrueIsFalse(hir::Instr* i,
                                            hir::HIRBuilder* builder) {
  bool istrue = i->opcode == &OPCODE_COMPARE_NE_info;
  bool isfalse = i->opcode == &OPCODE_COMPARE_EQ_info;

  auto [input_constant, input] = i->BinaryValueArrangeAsConstAndVar();

  if (!input_constant || input_constant->AsUint64() != 0) {
    return false;
  }

  // Value* input = i->src1.value;
  TypeName input_type = input->type;
  if (!IsScalarIntegralType(input_type)) {
    return false;
  }

  ScalarNZM input_nzm = GetScalarNZM(input);

  if (istrue &&
      input_nzm == 1) {  // doing istrue on a value thats already a bool bitwise

    if (input_type == INT8_TYPE) {
      i->Replace(&OPCODE_ASSIGN_info, 0);
      i->set_src1(input);

    } else {
      i->Replace(&OPCODE_TRUNCATE_info, 0);
      i->set_src1(input);
    }
    return true;

  } else if (isfalse && input_nzm == 1) {
    if (input_type == INT8_TYPE) {
      i->Replace(&OPCODE_XOR_info, 0);
      i->set_src1(input);
      i->set_src2(builder->LoadConstantInt8(1));
      return true;
    } else {
      Value* truncated = builder->Truncate(input, INT8_TYPE);
      truncated->def->MoveBefore(i);
      i->Replace(&OPCODE_XOR_info, 0);
      i->set_src1(truncated);
      i->set_src2(builder->LoadConstantInt8(1));
      return true;
    }
  }

  return false;
}
bool SimplificationPass::CheckSHRByConst(hir::Instr* i,
                                         hir::HIRBuilder* builder,
                                         hir::Value* variable,
                                         unsigned int shift) {
  if (shift >= 3 && shift <= 6) {
    // is possible shift of lzcnt res, do some tunneling

    unsigned int tflags = MOVTUNNEL_ASSIGNS | MOVTUNNEL_MOVZX |
                          MOVTUNNEL_TRUNCATE | MOVTUNNEL_MOVSX |
                          MOVTUNNEL_AND32FF;

    Instr* vardef = variable->def;

    hir::Instr* var_def = variable->GetDefTunnelMovs(&tflags);

    if (var_def && var_def->opcode == &OPCODE_CNTLZ_info) {
      Value* lz_input = var_def->src1.value;
      TypeName type_of_lz_input = lz_input->type;
      size_t shift_for_zero =
          xe::log2_floor(GetTypeSize(type_of_lz_input) * CHAR_BIT);

      if (shift == shift_for_zero) {
        // we ought to be OPCODE_IS_FALSE!
        /*
            explanation: if an input to lzcnt is zero, the result will be the
           bit size of the input type, which is always a power of two any
           nonzero result will be less than the bit size so you can test for
           zero by doing, for instance with a 32 bit value, lzcnt32(input) >> 5
            this is a very common way of testing for zero without branching on
           ppc, and the xb360 ppc compiler used it a lot we optimize this away
           for simplicity and to enable further optimizations, but actually this
           is also quite fast on modern x86 processors as well, for instance on
           zen 2 the rcp through of lzcnt is 0.25, meaning four can be executed
           in one cycle

        */

        if (variable->type != INT8_TYPE) {
          Value* isfalsetest = builder->IsFalse(lz_input);

          isfalsetest->def->MoveBefore(i);
          i->Replace(&OPCODE_ZERO_EXTEND_info, 0);
          i->set_src1(isfalsetest);

        } else {
          // i->Replace(&OPCODE_IS_FALSE_info, 0);
          i->Replace(&OPCODE_COMPARE_EQ_info, 0);
          i->set_src1(lz_input);
          i->set_src2(builder->LoadZero(lz_input->type));
        }
        return true;
      }
    }
  }
  return false;
}
bool SimplificationPass::CheckSHR(hir::Instr* i, hir::HIRBuilder* builder) {
  Value* shr_lhs = i->src1.value;
  Value* shr_rhs = i->src2.value;
  if (!shr_lhs || !shr_rhs) return false;
  if (shr_rhs->IsConstant()) {
    return CheckSHRByConst(i, builder, shr_lhs, shr_rhs->AsUint32());
  }

  return false;
}

bool SimplificationPass::CheckSAR(hir::Instr* i, hir::HIRBuilder* builder) {
  Value* l = i->src1.value;
  Value* r = i->src2.value;
  ScalarNZM l_nzm = GetScalarNZM(l);
  uint64_t signbit_mask = GetScalarSignbitMask(l->type);
  size_t typesize = GetTypeSize(l->type);

  /*
    todo: folding this requires the mask of constant bits
  if (r->IsConstant()) {
    uint32_t const_r = r->AsUint32();

    if (const_r == (typesize * CHAR_BIT) - 1) { //the shift is being done to
  fill the result with the signbit of the input.


    }
  }*/
  if ((l_nzm & signbit_mask) == 0) {  // signbit will never be set, might as
                                      // well be an SHR. (this does happen)
    i->opcode = &OPCODE_SHR_info;

    return true;
  }

  return false;
}
bool SimplificationPass::SimplifyBitArith(hir::HIRBuilder* builder) {
  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      // vector types use the same opcodes as scalar ones for AND/OR/XOR! we
      // don't handle these in our simplifications, so skip
      if (i->AllScalarIntegral()) {
        Opcode iop = i->opcode->num;

        if (iop == OPCODE_OR) {
          result |= CheckOr(i, builder);
        } else if (iop == OPCODE_XOR) {
          result |= CheckXor(i, builder);
        } else if (iop == OPCODE_AND) {
          result |= CheckAnd(i, builder);
        } else if (iop == OPCODE_ADD) {
          result |= CheckAdd(i, builder);
        } else if (IsScalarBasicCmp(iop)) {
          result |= CheckScalarConstCmp(i, builder);
          result |= CheckIsTrueIsFalse(i, builder);
        } else if (iop == OPCODE_SHR) {
          result |= CheckSHR(i, builder);
        } else if (iop == OPCODE_SHA) {
          result |= CheckSAR(i, builder);
        }
      }

      i = i->next;
    }
    block = block->next;
  }
  return result;
}
bool SimplificationPass::EliminateConversions(HIRBuilder* builder) {
  // First, we check for truncates/extensions that can be skipped.
  // This generates some assignments which then the second step will clean up.
  // Both zero/sign extends can be skipped:
  //   v1.i64 = zero/sign_extend v0.i32
  //   v2.i32 = truncate v1.i64
  // becomes:
  //   v1.i64 = zero/sign_extend v0.i32 (may be dead code removed later)
  //   v2.i32 = v0.i32

  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      // To make things easier we check in reverse (source of truncate/extend
      // back to definition).
      if (i->opcode == &OPCODE_TRUNCATE_info) {
        // Matches zero/sign_extend + truncate.
        result |= CheckTruncate(i);
      } else if (i->opcode == &OPCODE_BYTE_SWAP_info) {
        // Matches byte swap + byte swap.
        // This is pretty rare within the same basic block, but is in the
        // memcpy hot path and (probably) worth it. Maybe.
        result |= CheckByteSwap(i);
      }
      i = i->next;
    }
    block = block->next;
  }
  return result;
}

bool SimplificationPass::CheckTruncate(Instr* i) {
  // Walk backward up src's chain looking for an extend. We may have
  // assigns, so skip those.
  auto src = i->src1.value;
  auto def = src->def;
  while (def && def->opcode == &OPCODE_ASSIGN_info) {
    // Skip asignments.
    def = def->src1.value->def;
  }
  if (def) {
    if (def->opcode == &OPCODE_SIGN_EXTEND_info) {
      // Value comes from a sign extend.
      if (def->src1.value->type == i->dest->type) {
        // Types match, use original by turning this into an assign.
        i->Replace(&OPCODE_ASSIGN_info, 0);
        i->set_src1(def->src1.value);
        return true;
      }
    } else if (def->opcode == &OPCODE_ZERO_EXTEND_info) {
      // Value comes from a zero extend.
      if (def->src1.value->type == i->dest->type) {
        // Types match, use original by turning this into an assign.
        i->Replace(&OPCODE_ASSIGN_info, 0);
        i->set_src1(def->src1.value);
        return true;
      }
    }
  }
  return false;
}

bool SimplificationPass::CheckByteSwap(Instr* i) {
  // Walk backward up src's chain looking for a byte swap. We may have
  // assigns, so skip those.
  auto src = i->src1.value;
  auto def = src->def;
  while (def && def->opcode == &OPCODE_ASSIGN_info) {
    // Skip asignments.
    def = def->src1.value->def;
  }
  if (def && def->opcode == &OPCODE_BYTE_SWAP_info) {
    // Value comes from a byte swap.
    if (def->src1.value->type == i->dest->type) {
      // Types match, use original by turning this into an assign.
      i->Replace(&OPCODE_ASSIGN_info, 0);
      i->set_src1(def->src1.value);
      return true;
    }
  }
  return false;
}
bool SimplificationPass::SimplifyAssignments(HIRBuilder* builder) {
  // Run over the instructions and rename assigned variables:
  //   v1 = v0
  //   v2 = v1
  //   v3 = add v0, v2
  // becomes:
  //   v1 = v0
  //   v2 = v0
  //   v3 = add v0, v0
  // This could be run several times, as it could make other passes faster
  // to compute (for example, ConstantPropagation). DCE will take care of
  // the useless assigns.
  //
  // We do this by walking each instruction. For each value op we
  // look at its def instr to see if it's an assign - if so, we use the src
  // of that instr. Because we may have chains, we do this recursively until
  // we find a non-assign def.

  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      i->VisitValueOperands([&result, i, this](Value* value, uint32_t idx) {
        bool modified = false;
        i->set_srcN(CheckValue(value, modified), idx);
        result |= modified;
      });

      i = i->next;
    }
    block = block->next;
  }
  return result;
}

Value* SimplificationPass::CheckValue(Value* value, bool& result) {
  auto def = value->def;
  if (def && def->opcode == &OPCODE_ASSIGN_info) {
    // Value comes from an assignment - recursively find if it comes from
    // another assignment. It probably doesn't, if we already replaced it.
    auto replacement = def->src1.value;
    while (true) {
      def = replacement->def;
      if (!def || def->opcode != &OPCODE_ASSIGN_info) {
        break;
      }
      replacement = def->src1.value;
    }
    result = true;
    return replacement;
  }
  result = false;
  return value;
}
bool SimplificationPass::SimplifyAddWithSHL(hir::Instr* i,
                                            hir::HIRBuilder* builder) {
  /*
  example: (x <<1 ) + x == (x*3)

*/
  auto [shlinsn, addend] =
      i->BinaryValueArrangeByDefiningOpcode(&OPCODE_SHL_info);
  if (!shlinsn) {
    return false;
  }
  Instr* shift_insn = shlinsn->def;

  Value* shift = shift_insn->src2.value;

  // if not a constant shift, we cant combine to a multiply
  if (!shift->IsConstant()) {
    return false;
  }

  Value* shouldbeaddend = shift_insn->src1.value;

  if (!shouldbeaddend->IsEqual(addend)) {
    return false;
  }

  uint64_t multiplier = 1ULL << shift->constant.u8;

  multiplier++;

  hir::Value* oldvalue = shouldbeaddend;

  i->Replace(&OPCODE_MUL_info, ARITHMETIC_UNSIGNED);
  i->set_src1(oldvalue);

  // this sequence needs to be broken out into some kind of LoadConstant(type,
  // raw_value) method of hirbuilder
  auto constmul = builder->AllocValue(oldvalue->type);
  // could cause problems on big endian targets...
  constmul->flags |= VALUE_IS_CONSTANT;
  constmul->constant.u64 = multiplier;

  i->set_src2(constmul);

  return true;
}
bool SimplificationPass::SimplifyAddToSelf(hir::Instr* i,
                                           hir::HIRBuilder* builder) {
  /*
          heres a super easy one
  */

  if (i->src1.value != i->src2.value) {
    return false;
  }

  i->opcode = &OPCODE_SHL_info;

  i->set_src2(builder->LoadConstantUint8(1));

  return true;
}
bool SimplificationPass::SimplifyAddArith(hir::Instr* i,
                                          hir::HIRBuilder* builder) {
  if (SimplifyAddWithSHL(i, builder)) {
    return true;
  }
  if (SimplifyAddToSelf(i, builder)) {
    return true;
  }
  return false;
}

bool SimplificationPass::SimplifySubArith(hir::Instr* i,
                                          hir::HIRBuilder* builder) {
  /*
  todo: handle expressions like (x*8) - (x*5) == (x*3)...if these can even
  happen of course */
  return false;
}
bool SimplificationPass::SimplifySHLArith(hir::Instr* i,
                                          hir::HIRBuilder* builder) {
  Value* sh = i->src2.value;

  Value* shifted = i->src1.value;

  if (!sh->IsConstant()) {
    return false;
  }

  hir::Instr* definition = shifted->GetDefSkipAssigns();

  if (!definition) {
    return false;
  }

  if (definition->GetOpcodeNum() != OPCODE_MUL) {
    return false;
  }

  if (definition->flags != ARITHMETIC_UNSIGNED) {
    return false;
  }

  auto [mulconst, mulnonconst] = definition->BinaryValueArrangeAsConstAndVar();

  if (!mulconst) {
    return false;
  }

  auto newmul = builder->AllocValue(mulconst->type);
  newmul->set_from(mulconst);

  newmul->Shl(sh);

  i->Replace(&OPCODE_MUL_info, ARITHMETIC_UNSIGNED);
  i->set_src1(mulnonconst);
  i->set_src2(newmul);

  return true;
}
bool SimplificationPass::SimplifyBasicArith(hir::Instr* i,
                                            hir::HIRBuilder* builder) {
  if (!i->dest) {
    return false;
  }
  if (i->dest->MaybeFloaty()) {
    return false;
  }

  hir::Opcode op = i->GetOpcodeNum();

  switch (op) {
    case OPCODE_ADD: {
      return SimplifyAddArith(i, builder);
    }
    case OPCODE_SUB: {
      return SimplifySubArith(i, builder);
    }
    case OPCODE_SHL: {
      return SimplifySHLArith(i, builder);
    }
  }
  return false;
}
bool SimplificationPass::SimplifyBasicArith(hir::HIRBuilder* builder) {
  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      result |= SimplifyBasicArith(i, builder);
      i = i->next;
    }
    block = block->next;
  }
  return result;
}

static bool CouldEverProduceDenormal(hir::Instr* i) {
  if (!i) {
    return false;
  }
  Opcode denflushed_opcode = i->GetOpcodeNum();

  if (denflushed_opcode == OPCODE_VECTOR_DENORMFLUSH) {
    return false;
  } else if (denflushed_opcode == OPCODE_UNPACK) {
    // todo: more unpack operations likely cannot produce denormals
    if (i->flags == PACK_TYPE_FLOAT16_4 || i->flags == PACK_TYPE_FLOAT16_2) {
      return false;  // xenos half float format does not support denormals
    }
  } else if (denflushed_opcode == OPCODE_VECTOR_CONVERT_I2F) {
    return false;
  }
  return true;  // todo: recurse, check values for min/max, abs, and others
}

bool SimplificationPass::SimplifyVectorOps(hir::Instr* i,
                                           hir::HIRBuilder* builder) {
  Opcode opc = i->GetOpcodeNum();
  /*
        if the input to an unconditional denormal flush is an output of an
     unconditional denormal flush, it is a pointless instruction and should be
     elimed
  */
  if (opc == OPCODE_VECTOR_DENORMFLUSH) {
    hir::Instr* denflushed_def = i->src1.value->GetDefSkipAssigns();

    if (denflushed_def) {
      if (!CouldEverProduceDenormal(denflushed_def)) {
        i->opcode = &OPCODE_ASSIGN_info;
        return true;
      }
    }
  }
  return false;
}
bool SimplificationPass::SimplifyVectorOps(hir::HIRBuilder* builder) {
  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      bool looks_vectory = false;

      i->VisitValueOperands([&looks_vectory](Value* val, uint32_t idx) {
        if (val->type == VEC128_TYPE) {
          looks_vectory = true;
        }
      });
      result |= SimplifyVectorOps(i, builder);
      i = i->next;
    }
    block = block->next;
  }
  return result;
}
/*
        todo: add load-store simplification pass

        do things like load-store byteswap elimination, for instance,

        if a value is loaded, ored with a constant mask, and then stored, we
   simply have to byteswap the mask it will be ored with and then we can
   eliminate the two byteswaps

        the same can be done for and, or, xor, andn with constant masks


        this can also be done for comparisons with 0 for equality and not equal


        another optimization: with ppc you cannot move a floating point register
   directly to a gp one, a gp one directly to a floating point register, or a
   vmx one to either. so guest code will store the result to the stack, and then
   load it to the register it needs in HIR we can sidestep this. we will still
   need to byteswap and store the result for correctness, but we can eliminate
   the load and byteswap by grabbing the original value from the store

        skyth's sanic idb, 0x824D7724
    lis       r11,
    lfs       f0, flt_8200CBCC@l(r11)
    fmuls     f0, time, f0
    fctidz    f0, f0        # vcvttss2si
    stfd      f0, 0x190+var_138(r1)
    lwz       r30, 0x190+var_138+4(r1)
    cmplwi    cr6, r30, 0x63 # 'c'
    ble       cr6, counter_op



*/

/*
        todo: simple loop unrolling
        skyth sanic 0x831D9908

        mr        r30, r4
        mr        r29, r5
        mr        r11, r7
        li        r31, 0

loc_831D9928:
        slwi      r9, r11, 1
        addi      r10, r11, 1
        addi      r8, r1, 0xD0+var_80
        clrlwi    r11, r10, 16
        cmplwi    cr6, r11, 0x10
        sthx      r31, r9, r8
        ble       cr6, loc_831D9928

        v5 = 1;
                do
                {
                        v6 = 2 * v5;
                        v5 = (unsigned __int16)(v5 + 1);
                        *(_WORD *)&v24[v6] = 0;
                }
                while ( v5 <= 0x10 );
                v7 = 0;
                do
                {
                        v8 = __ROL4__(*(unsigned __int8 *)(v7 + a2), 1);
                        v7 = (unsigned __int16)(v7 + 1);
                        ++*(_WORD *)&v24[v8];
                }
                while ( v7 < 8 );
                v9 = 1;
                v25[0] = 0;
                do
                {
                        v10 = 2 * v9;
                        v11 = 16 - v9;
                        v9 = (unsigned __int16)(v9 + 1);
                        v25[v10 / 2] = (*(_WORD *)&v24[v10] << v11) + *(_WORD
*)&v24[v10 + 48];
                }
                while ( v9 <= 0x10 );


        skyth sanic:
        sub_831BBAE0

        sub_831A41A8


*/
}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

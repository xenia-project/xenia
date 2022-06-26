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
  bool iter_result = false;

  do {
    iter_result = false;
    iter_result |= SimplifyBitArith(builder);
    iter_result |= EliminateConversions(builder);
    iter_result |= SimplifyAssignments(builder);
    result |= iter_result;
  } while (iter_result);
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
bool SimplificationPass::CheckXor(hir::Instr* i, hir::HIRBuilder* builder) {
  if (CheckOrXorZero(i)) {
    return true;
  } else {
    if (i->src1.value == i->src2.value) {
      i->Replace(&OPCODE_ASSIGN_info, 0);
      i->set_src1(builder->LoadZero(i->dest->type));
      return true;
    }
    uint64_t type_mask = GetScalarTypeMask(i->dest->type);

    auto [constant_value, variable_value] =
        i->BinaryValueArrangeAsConstAndVar();

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
  return def_opcode >= OPCODE_IS_TRUE && def_opcode <= OPCODE_DID_SATURATE;
}
uint64_t SimplificationPass::GetScalarNZM(hir::Value* value, hir::Instr* def,

                                          uint64_t typemask,
                                          hir::Opcode def_opcode) {
  if (def_opcode == OPCODE_SHL) {
    hir::Value* shifted = def->src1.value;
    hir::Value* shiftby = def->src2.value;
    // todo: nzm shift
    if (shiftby->IsConstant()) {
      uint64_t shifted_nzm = GetScalarNZM(shifted);
      return shifted_nzm << shiftby->AsUint64();
    }
  } else if (def_opcode == OPCODE_SHR) {
    hir::Value* shifted = def->src1.value;
    hir::Value* shiftby = def->src2.value;
    // todo: nzm shift
    if (shiftby->IsConstant()) {
      uint64_t shifted_nzm = GetScalarNZM(shifted);
      return shifted_nzm >> shiftby->AsUint64();
    }
  }
  // todo : sha, check signbit
  else if (def_opcode == OPCODE_ROTATE_LEFT) {
    hir::Value* shifted = def->src1.value;
    hir::Value* shiftby = def->src2.value;
    // todo: nzm shift
    if (shiftby->IsConstant()) {
      uint64_t shifted_nzm = GetScalarNZM(shifted);
      return xe::rotate_left(shifted_nzm,
                             static_cast<uint8_t>(shiftby->AsUint64()));
    }
  } else if (def_opcode == OPCODE_XOR || def_opcode == OPCODE_OR) {
    return GetScalarNZM(def->src1.value) | GetScalarNZM(def->src2.value);
  } else if (def_opcode == OPCODE_NOT) {
    return typemask;  //~GetScalarNZM(def->src1.value);
  } else if (def_opcode == OPCODE_ASSIGN) {
    return GetScalarNZM(def->src1.value);
  } else if (def_opcode == OPCODE_BYTE_SWAP) {
    uint64_t input_nzm = GetScalarNZM(def->src1.value);
    switch (GetTypeSize(def->dest->type)) {
      case 1:
        return input_nzm;
      case 2:
        return xe::byte_swap<unsigned short>(
            static_cast<unsigned short>(input_nzm));

      case 4:
        return xe::byte_swap<unsigned int>(
            static_cast<unsigned int>(input_nzm));
      case 8:
        return xe::byte_swap<unsigned long long>(input_nzm);
      default:
        xenia_assert(0);
        return typemask;
    }
  } else if (def_opcode == OPCODE_ZERO_EXTEND) {
    return GetScalarNZM(def->src1.value);
  } else if (def_opcode == OPCODE_TRUNCATE) {
    return GetScalarNZM(def->src1.value);  // caller will truncate by masking
  } else if (def_opcode == OPCODE_AND) {
    return GetScalarNZM(def->src1.value) & GetScalarNZM(def->src2.value);
  } else if (def_opcode == OPCODE_SELECT) {
    return GetScalarNZM(def->src2.value) | GetScalarNZM(def->src3.value);
  } else if (def_opcode == OPCODE_MIN) {
    /*
        the nzm will be that of the narrowest operand, because if one value is
       capable of being much larger than the other it can never actually reach
        a value that is outside the range of the other values nzm, because that
       would make it not the minimum of the two

        ahh, actually, we have to be careful about constants then.... for now,
       just return or
    */
    return GetScalarNZM(def->src2.value) | GetScalarNZM(def->src1.value);
  } else if (def_opcode == OPCODE_MAX) {
    return GetScalarNZM(def->src2.value) | GetScalarNZM(def->src1.value);
  } else if (Is1BitOpcode(def_opcode)) {
    return 1ULL;
  } else if (def_opcode == OPCODE_CAST) {
    return GetScalarNZM(def->src1.value);
  }

  return typemask;
}
uint64_t SimplificationPass::GetScalarNZM(hir::Value* value) {
  if (value->IsConstant()) {
    return value->AsUint64();
  }

  uint64_t default_return = GetScalarTypeMask(value->type);

  hir::Instr* def = value->def;
  if (!def) {
    return default_return;
  }
  return GetScalarNZM(value, def, default_return, def->opcode->num) &
         default_return;
}
bool SimplificationPass::CheckAnd(hir::Instr* i, hir::HIRBuilder* builder) {
retry_and_simplification:
  auto [constant_value, variable_value] = i->BinaryValueArrangeAsConstAndVar();
  if (!constant_value) {
    // added this for srawi
    uint64_t nzml = GetScalarNZM(i->src1.value);
    uint64_t nzmr = GetScalarNZM(i->src2.value);

    if ((nzml & nzmr) == 0) {
      i->Replace(&OPCODE_ASSIGN_info, 0);
      i->set_src1(builder->LoadZero(i->dest->type));
      return true;
    }
    return false;
  }

  // todo: check if masking with mask that covers all of zero extension source
  uint64_t type_mask = GetScalarTypeMask(i->dest->type);
  // if masking with entire width, pointless instruction so become an assign

  if (constant_value->AsUint64() == type_mask) {
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

        uint64_t left_nzm = GetScalarNZM(or_left);

        // use the other or input instead of the or output
        if ((constant_value->AsUint64() & left_nzm) == 0) {
          i->Replace(&OPCODE_AND_info, 0);
          i->set_src1(or_right);
          i->set_src2(constant_value);
          return true;
        }

        uint64_t right_nzm = GetScalarNZM(or_right);

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
  auto [definition, added_constant] =
      i->BinaryValueArrangeByDefOpAndConstant(&OPCODE_NOT_info);

  if (!definition) {
    auto [added_constant_neg, added_var_neg] =
        i->BinaryValueArrangeAsConstAndVar();

    if (!added_constant_neg) return false;
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

  uint64_t nzm_for_var = GetScalarNZM(variable);
  Opcode cmpop = i->opcode->num;
  uint64_t constant_unpacked = constant_value->AsUint64();
  uint64_t signbit_for_var = GetScalarSignbitMask(variable->type);
  bool signbit_definitely_0 = (nzm_for_var & signbit_for_var) == 0;

  Instr* var_definition = variable->def;
  Opcode def_opcode = OPCODE_NOP;

  if (var_definition) {
    var_definition = var_definition->GetDestDefSkipAssigns();
    def_opcode = var_definition->opcode->num;
  }
  // x == 0 -> !x
  if (cmpop == OPCODE_COMPARE_EQ && constant_unpacked == 0) {
    i->Replace(&OPCODE_IS_FALSE_info, 0);
    i->set_src1(variable);
    return true;
  }
  // x != 0 -> !!x
  if (cmpop == OPCODE_COMPARE_NE && constant_unpacked == 0) {
    i->Replace(&OPCODE_IS_TRUE_info, 0);
    i->set_src1(variable);
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
    i->Replace(&OPCODE_IS_FALSE_info, 0);
    i->set_src1(variable);
    return true;
  }
  if (cmpop == OPCODE_COMPARE_UGT &&
      constant_unpacked == 0) {  // unsigned gt 1 means != 0

    i->Replace(&OPCODE_IS_TRUE_info, 0);
    i->set_src1(variable);
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
    i->Replace(&OPCODE_IS_TRUE_info, 0);
    i->set_src1(variable);
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
      repl = &OPCODE_IS_TRUE_info;
    } else if (cmpop == OPCODE_COMPARE_NE || cmpop == OPCODE_COMPARE_ULT) {
      repl = &OPCODE_IS_FALSE_info;

    } else if (cmpop == OPCODE_COMPARE_UGT) {
      // impossible, cannot be greater than mask

      /* i->Replace(&OPCODE_ASSIGN_info, 0);
      i->set_src1(builder->LoadZeroInt8());
      return true;
      */
      constant_replacement = builder->LoadZeroInt8();

    } else if (cmpop == OPCODE_COMPARE_ULE) {  // less than or equal to mask =
                                               // always true
      constant_replacement = builder->LoadConstantInt8(1);
    }

    if (repl) {
      i->Replace(repl, 0);
      i->set_src1(variable);
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
  bool istrue = i->opcode == &OPCODE_IS_TRUE_info;
  bool isfalse = i->opcode == &OPCODE_IS_FALSE_info;

  Value* input = i->src1.value;
  TypeName input_type = input->type;
  if (!IsScalarIntegralType(input_type)) {
    return false;
  }

  uint64_t input_nzm = GetScalarNZM(input);

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

  /* Instr* input_def = input->def;
   if (!input_def) {
     return false;
   }

   input_def = input_def->GetDestDefSkipAssigns();*/
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
      if (i->dest && IsScalarIntegralType( i->dest->type) ) {
        if (i->opcode == &OPCODE_OR_info) {
          result |= CheckOr(i, builder);
        } else if (i->opcode == &OPCODE_XOR_info) {
          result |= CheckXor(i, builder);
        } else if (i->opcode == &OPCODE_AND_info) {
          result |= CheckAnd(i, builder);
        } else if (i->opcode == &OPCODE_ADD_info) {
          result |= CheckAdd(i, builder);
        } else if (IsScalarBasicCmp(i->opcode->num)) {
          result |= CheckScalarConstCmp(i, builder);
        } else if (i->opcode == &OPCODE_IS_FALSE_info ||
                   i->opcode == &OPCODE_IS_TRUE_info) {
          result |= CheckIsTrueIsFalse(i, builder);
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
      uint32_t signature = i->opcode->signature;
      if (GET_OPCODE_SIG_TYPE_SRC1(signature) == OPCODE_SIG_TYPE_V) {
        bool modified = false;
        i->set_src1(CheckValue(i->src1.value, modified));
        result |= modified;
      }
      if (GET_OPCODE_SIG_TYPE_SRC2(signature) == OPCODE_SIG_TYPE_V) {
        bool modified = false;
        i->set_src2(CheckValue(i->src2.value, modified));
        result |= modified;
      }
      if (GET_OPCODE_SIG_TYPE_SRC3(signature) == OPCODE_SIG_TYPE_V) {
        bool modified = false;
        i->set_src3(CheckValue(i->src3.value, modified));
        result |= modified;
      }

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

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/ppc/ppc_emit-private.h"

#include "xenia/base/assert.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_hir_builder.h"

namespace xe {
namespace cpu {
namespace ppc {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Value;

// Integer arithmetic (A-3)

Value* AddDidCarry(PPCHIRBuilder& f, Value* v1, Value* v2) {
  return f.CompareUGT(f.Truncate(v2, INT32_TYPE),
                      f.Not(f.Truncate(v1, INT32_TYPE)));
}

Value* SubDidCarry(PPCHIRBuilder& f, Value* v1, Value* v2) {
  return f.Or(f.CompareUGT(f.Truncate(v1, INT32_TYPE),
                           f.Not(f.Neg(f.Truncate(v2, INT32_TYPE)))),
              f.IsFalse(f.Truncate(v2, INT32_TYPE)));
}

// https://github.com/sebastianbiallas/pearpc/blob/0b3c823f61456faa677f6209545a7b906e797421/src/cpu/cpu_generic/ppc_tools.h#L26
Value* AddWithCarryDidCarry(PPCHIRBuilder& f, Value* v1, Value* v2, Value* v3) {
  v1 = f.Truncate(v1, INT32_TYPE);
  v2 = f.Truncate(v2, INT32_TYPE);
  assert_true(v3->type == INT8_TYPE);
  v3 = f.ZeroExtend(v3, INT32_TYPE);
  return f.Or(f.CompareULT(f.Add(f.Add(v1, v2), v3), v3),
              f.CompareULT(f.Add(v1, v2), v1));
}

int InstrEmit_addx(PPCHIRBuilder& f, const InstrData& i) {
  // RD <- (RA) + (RB)
  Value* v = f.Add(f.LoadGPR(i.XO.RA), f.LoadGPR(i.XO.RB));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    assert_always();
    // e.update_xer_with_overflow(EFLAGS OF?);
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_addcx(PPCHIRBuilder& f, const InstrData& i) {
  // RD <- (RA) + (RB)
  // CA <- carry bit
  Value* ra = f.LoadGPR(i.XO.RA);
  Value* rb = f.LoadGPR(i.XO.RB);
  Value* v = f.Add(ra, rb);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    assert_always();
    // e.update_xer_with_overflow(EFLAGS OF?);
  } else {
    f.StoreCA(AddDidCarry(f, ra, rb));
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_addex(PPCHIRBuilder& f, const InstrData& i) {
  // RD <- (RA) + (RB) + XER[CA]
  // CA <- carry bit
  Value* ra = f.LoadGPR(i.XO.RA);
  Value* rb = f.LoadGPR(i.XO.RB);
  Value* v = f.AddWithCarry(ra, rb, f.LoadCA());
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    assert_always();
    // e.update_xer_with_overflow(EFLAGS OF?);
  } else {
    f.StoreCA(AddWithCarryDidCarry(f, ra, rb, f.LoadCA()));
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_addi(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   RT <- EXTS(SI)
  // else
  //   RT <- (RA) + EXTS(SI)
  Value* si = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  Value* v = si;
  if (i.D.RA) {
    v = f.Add(f.LoadGPR(i.D.RA), si);
  }
  f.StoreGPR(i.D.RT, v);
  return 0;
}

int InstrEmit_addic(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- (RA) + EXTS(SI)
  // CA <- carry bit
  Value* ra = f.LoadGPR(i.D.RA);
  Value* v = f.Add(ra, f.LoadConstantInt64(XEEXTS16(i.D.DS)));
  f.StoreGPR(i.D.RT, v);
  f.StoreCA(AddDidCarry(f, ra, f.LoadConstantInt64(XEEXTS16(i.D.DS))));
  return 0;
}

int InstrEmit_addicx(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- (RA) + EXTS(SI)
  // CA <- carry bit
  Value* ra = f.LoadGPR(i.D.RA);
  Value* v = f.Add(f.LoadGPR(i.D.RA), f.LoadConstantInt64(XEEXTS16(i.D.DS)));
  f.StoreGPR(i.D.RT, v);
  f.StoreCA(AddDidCarry(f, ra, f.LoadConstantInt64(XEEXTS16(i.D.DS))));
  f.UpdateCR(0, v);
  return 0;
}

int InstrEmit_addis(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   RT <- EXTS(SI) || i16.0
  // else
  //   RT <- (RA) + EXTS(SI) || i16.0
  Value* si = f.LoadConstantInt64(XEEXTS16(i.D.DS) << 16);
  Value* v = si;
  if (i.D.RA) {
    v = f.Add(f.LoadGPR(i.D.RA), si);
  }
  f.StoreGPR(i.D.RT, v);
  return 0;
}

int InstrEmit_addmex(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- (RA) + CA - 1
  // CA <- carry bit
  Value* ra = f.LoadGPR(i.XO.RA);
  Value* v = f.AddWithCarry(ra, f.LoadConstantInt64(-1), f.LoadCA());
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    // With XER[SO] update too.
    // e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
    assert_always();
  } else {
    // Just CA update.
    f.StoreCA(AddWithCarryDidCarry(f, ra, f.LoadConstantInt64(-1), f.LoadCA()));
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_addzex(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- (RA) + CA
  // CA <- carry bit
  Value* ra = f.LoadGPR(i.XO.RA);
  Value* v = f.AddWithCarry(ra, f.LoadZeroInt64(), f.LoadCA());
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    // With XER[SO] update too.
    // e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
    assert_always();
    return 1;
  } else {
    // Just CA update.
    f.StoreCA(AddWithCarryDidCarry(f, ra, f.LoadZeroInt64(), f.LoadCA()));
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_divdx(PPCHIRBuilder& f, const InstrData& i) {
  // dividend <- (RA)
  // divisor <- (RB)
  // if divisor = 0 then
  //   if OE = 1 then
  //     XER[OV] <- 1
  //   return
  // RT <- dividend ÷ divisor
  Value* divisor = f.LoadGPR(i.XO.RB);
  // TODO(benvanik): check if zero
  //                 if OE=1, set XER[OV] = 1
  //                 else skip the divide
  Value* v = f.Div(f.LoadGPR(i.XO.RA), divisor);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    // If we are OE=1 we need to clear the overflow bit.
    // e.update_xer_with_overflow(e.get_uint64(0));
    assert_always();
    return 1;
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_divdux(PPCHIRBuilder& f, const InstrData& i) {
  // dividend <- (RA)
  // divisor <- (RB)
  // if divisor = 0 then
  //   if OE = 1 then
  //     XER[OV] <- 1
  //   return
  // RT <- dividend ÷ divisor
  Value* divisor = f.LoadGPR(i.XO.RB);
  // TODO(benvanik): check if zero
  //                 if OE=1, set XER[OV] = 1
  //                 else skip the divide
  Value* v = f.Div(f.LoadGPR(i.XO.RA), divisor, ARITHMETIC_UNSIGNED);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    // If we are OE=1 we need to clear the overflow bit.
    // e.update_xer_with_overflow(e.get_uint64(0));
    assert_always();
    return 1;
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_divwx(PPCHIRBuilder& f, const InstrData& i) {
  // dividend[0:31] <- (RA)[32:63]
  // divisor[0:31] <- (RB)[32:63]
  // if divisor = 0 then
  //   if OE = 1 then
  //     XER[OV] <- 1
  //   return
  // RT[32:63] <- dividend ÷ divisor
  // RT[0:31] <- undefined
  Value* divisor = f.Truncate(f.LoadGPR(i.XO.RB), INT32_TYPE);
  // TODO(benvanik): check if zero
  //                 if OE=1, set XER[OV] = 1
  //                 else skip the divide
  Value* v = f.Div(f.Truncate(f.LoadGPR(i.XO.RA), INT32_TYPE), divisor);
  v = f.ZeroExtend(v, INT64_TYPE);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    // If we are OE=1 we need to clear the overflow bit.
    // e.update_xer_with_overflow(e.get_uint64(0));
    assert_always();
    return 1;
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_divwux(PPCHIRBuilder& f, const InstrData& i) {
  // dividend[0:31] <- (RA)[32:63]
  // divisor[0:31] <- (RB)[32:63]
  // if divisor = 0 then
  //   if OE = 1 then
  //     XER[OV] <- 1
  //   return
  // RT[32:63] <- dividend ÷ divisor
  // RT[0:31] <- undefined
  Value* divisor = f.Truncate(f.LoadGPR(i.XO.RB), INT32_TYPE);
  // TODO(benvanik): check if zero
  //                 if OE=1, set XER[OV] = 1
  //                 else skip the divide
  Value* v = f.Div(f.Truncate(f.LoadGPR(i.XO.RA), INT32_TYPE), divisor,
                   ARITHMETIC_UNSIGNED);
  v = f.ZeroExtend(v, INT64_TYPE);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    // If we are OE=1 we need to clear the overflow bit.
    // e.update_xer_with_overflow(e.get_uint64(0));
    assert_always();
    return 1;
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_mulhdx(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- ((RA) × (RB) as 128)[0:63]
  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  Value* v = f.MulHi(f.LoadGPR(i.XO.RA), f.LoadGPR(i.XO.RB));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_mulhdux(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- ((RA) × (RB) as 128)[0:63]
  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  Value* v =
      f.MulHi(f.LoadGPR(i.XO.RA), f.LoadGPR(i.XO.RB), ARITHMETIC_UNSIGNED);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_mulhwx(PPCHIRBuilder& f, const InstrData& i) {
  // RT[32:64] <- ((RA)[32:63] × (RB)[32:63])[0:31]
  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  Value* v = f.SignExtend(f.MulHi(f.Truncate(f.LoadGPR(i.XO.RA), INT32_TYPE),
                                  f.Truncate(f.LoadGPR(i.XO.RB), INT32_TYPE)),
                          INT64_TYPE);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_mulhwux(PPCHIRBuilder& f, const InstrData& i) {
  // RT[32:64] <- ((RA)[32:63] × (RB)[32:63])[0:31]
  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  Value* v = f.ZeroExtend(
      f.MulHi(f.Truncate(f.LoadGPR(i.XO.RA), INT32_TYPE),
              f.Truncate(f.LoadGPR(i.XO.RB), INT32_TYPE), ARITHMETIC_UNSIGNED),
      INT64_TYPE);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_mulldx(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- ((RA) × (RB))[64:127]
  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  Value* v = f.Mul(f.LoadGPR(i.XO.RA), f.LoadGPR(i.XO.RB));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_mulli(PPCHIRBuilder& f, const InstrData& i) {
  // prod[0:127] <- (RA) × EXTS(SI)
  // RT <- prod[64:127]
  Value* v = f.Mul(f.LoadGPR(i.D.RA), f.LoadConstantInt64(XEEXTS16(i.D.DS)));
  f.StoreGPR(i.D.RT, v);
  return 0;
}

int InstrEmit_mullwx(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- (RA)[32:63] × (RB)[32:63]
  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  Value* v = f.Mul(
      f.SignExtend(f.Truncate(f.LoadGPR(i.XO.RA), INT32_TYPE), INT64_TYPE),
      f.SignExtend(f.Truncate(f.LoadGPR(i.XO.RB), INT32_TYPE), INT64_TYPE));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_negx(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- ¬(RA) + 1
  if (i.XO.OE) {
    // With XER update.
    // This is a different codepath as we need to use llvm.ssub.with.overflow.

    // if RA == 0x8000000000000000 then no-op and set OV=1
    // This may just magically do that...

    assert_always();
    // Function* ssub_with_overflow = Intrinsic::getDeclaration(
    //    e.gen_module(), Intrinsic::ssub_with_overflow, jit_type_nint);
    // jit_value_t v = b.CreateCall2(ssub_with_overflow,
    //                         e.get_int64(0), f.LoadGPR(i.XO.RA));
    // jit_value_t v0 = b.CreateExtractValue(v, 0);
    // f.StoreGPR(i.XO.RT, v0);
    // e.update_xer_with_overflow(b.CreateExtractValue(v, 1));

    // if (i.XO.Rc) {
    //  // With cr0 update.
    //  f.UpdateCR(0, v0, e.get_int64(0), true);
    //}
  } else {
    // No OE bit setting.
    Value* v = f.Neg(f.LoadGPR(i.XO.RA));
    f.StoreGPR(i.XO.RT, v);
    if (i.XO.Rc) {
      f.UpdateCR(0, v);
    }
  }
  return 0;
}

int InstrEmit_subfx(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- ¬(RA) + (RB) + 1
  Value* v = f.Sub(f.LoadGPR(i.XO.RB), f.LoadGPR(i.XO.RA));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    assert_always();
    // e.update_xer_with_overflow(EFLAGS??);
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_subfcx(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- ¬(RA) + (RB) + 1
  Value* ra = f.LoadGPR(i.XO.RA);
  Value* rb = f.LoadGPR(i.XO.RB);
  Value* v = f.Sub(rb, ra);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    assert_always();
    // e.update_xer_with_overflow(EFLAGS??);
  } else {
    f.StoreCA(SubDidCarry(f, rb, ra));
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_subficx(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- ¬(RA) + EXTS(SI) + 1
  Value* ra = f.LoadGPR(i.D.RA);
  Value* v = f.Sub(f.LoadConstantInt64(XEEXTS16(i.D.DS)), ra);
  f.StoreGPR(i.D.RT, v);
  f.StoreCA(SubDidCarry(f, f.LoadConstantInt64(XEEXTS16(i.D.DS)), ra));
  return 0;
}

int InstrEmit_subfex(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- ¬(RA) + (RB) + CA
  Value* not_ra = f.Not(f.LoadGPR(i.XO.RA));
  Value* rb = f.LoadGPR(i.XO.RB);
  Value* v = f.AddWithCarry(not_ra, rb, f.LoadCA());
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    assert_always();
    // e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  } else {
    f.StoreCA(AddWithCarryDidCarry(f, not_ra, rb, f.LoadCA()));
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_subfmex(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- ¬(RA) + CA - 1
  Value* not_ra = f.Not(f.LoadGPR(i.XO.RA));
  Value* v = f.AddWithCarry(not_ra, f.LoadConstantInt64(-1), f.LoadCA());
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    assert_always();
    // e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  } else {
    f.StoreCA(
        AddWithCarryDidCarry(f, not_ra, f.LoadConstantInt64(-1), f.LoadCA()));
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_subfzex(PPCHIRBuilder& f, const InstrData& i) {
  // RT <- ¬(RA) + CA
  Value* not_ra = f.Not(f.LoadGPR(i.XO.RA));
  Value* v = f.AddWithCarry(not_ra, f.LoadZeroInt64(), f.LoadCA());
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    assert_always();
    // e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  } else {
    f.StoreCA(AddWithCarryDidCarry(f, not_ra, f.LoadZeroInt64(), f.LoadCA()));
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

// Integer compare (A-4)

int InstrEmit_cmp(PPCHIRBuilder& f, const InstrData& i) {
  // if L = 0 then
  //   a <- EXTS((RA)[32:63])
  //   b <- EXTS((RB)[32:63])
  // else
  //   a <- (RA)
  //   b <- (RB)
  // if a < b then
  //   c <- 0b100
  // else if a > b then
  //   c <- 0b010
  // else
  //   c <- 0b001
  // CR[4×BF+32:4×BF+35] <- c || XER[SO]
  uint32_t BF = i.X.RT >> 2;
  uint32_t L = i.X.RT & 1;
  Value* lhs;
  Value* rhs;
  if (L) {
    lhs = f.LoadGPR(i.X.RA);
    rhs = f.LoadGPR(i.X.RB);
  } else {
    lhs = f.Truncate(f.LoadGPR(i.X.RA), INT32_TYPE);
    rhs = f.Truncate(f.LoadGPR(i.X.RB), INT32_TYPE);
  }
  f.UpdateCR(BF, lhs, rhs);
  return 0;
}

int InstrEmit_cmpi(PPCHIRBuilder& f, const InstrData& i) {
  // if L = 0 then
  //   a <- EXTS((RA)[32:63])
  // else
  //   a <- (RA)
  // if a < EXTS(SI) then
  //   c <- 0b100
  // else if a > EXTS(SI) then
  //   c <- 0b010
  // else
  //   c <- 0b001
  // CR[4×BF+32:4×BF+35] <- c || XER[SO]
  uint32_t BF = i.D.RT >> 2;
  uint32_t L = i.D.RT & 1;
  Value* lhs;
  Value* rhs;
  if (L) {
    lhs = f.LoadGPR(i.D.RA);
    rhs = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  } else {
    lhs = f.Truncate(f.LoadGPR(i.D.RA), INT32_TYPE);
    rhs = f.LoadConstantInt32(int32_t(XEEXTS16(i.D.DS)));
  }
  f.UpdateCR(BF, lhs, rhs);
  return 0;
}

int InstrEmit_cmpl(PPCHIRBuilder& f, const InstrData& i) {
  // if L = 0 then
  //   a <- i32.0 || (RA)[32:63]
  //   b <- i32.0 || (RB)[32:63]
  // else
  //   a <- (RA)
  //   b <- (RB)
  // if a <u b then
  //   c <- 0b100
  // else if a >u b then
  //   c <- 0b010
  // else
  //   c <- 0b001
  // CR[4×BF+32:4×BF+35] <- c || XER[SO]
  uint32_t BF = i.X.RT >> 2;
  uint32_t L = i.X.RT & 1;
  Value* lhs;
  Value* rhs;
  if (L) {
    lhs = f.LoadGPR(i.X.RA);
    rhs = f.LoadGPR(i.X.RB);
  } else {
    lhs = f.Truncate(f.LoadGPR(i.X.RA), INT32_TYPE);
    rhs = f.Truncate(f.LoadGPR(i.X.RB), INT32_TYPE);
  }
  f.UpdateCR(BF, lhs, rhs, false);
  return 0;
}

int InstrEmit_cmpli(PPCHIRBuilder& f, const InstrData& i) {
  // if L = 0 then
  //   a <- i32.0 || (RA)[32:63]
  // else
  //   a <- (RA)
  // if a <u i48.0 || SI then
  //   c <- 0b100
  // else if a >u i48.0 || SI then
  //   c <- 0b010
  // else
  //   c <- 0b001
  // CR[4×BF+32:4×BF+35] <- c || XER[SO]
  uint32_t BF = i.D.RT >> 2;
  uint32_t L = i.D.RT & 1;
  Value* lhs;
  Value* rhs;
  if (L) {
    lhs = f.LoadGPR(i.D.RA);
    rhs = f.LoadConstantUint64(i.D.DS);
  } else {
    lhs = f.Truncate(f.LoadGPR(i.D.RA), INT32_TYPE);
    rhs = f.LoadConstantUint32(i.D.DS);
  }
  f.UpdateCR(BF, lhs, rhs, false);
  return 0;
}

// Integer logical (A-5)

int InstrEmit_andx(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) & (RB)
  Value* ra = f.And(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

int InstrEmit_andcx(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) & ¬(RB)
  Value* ra = f.And(f.LoadGPR(i.X.RT), f.Not(f.LoadGPR(i.X.RB)));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

int InstrEmit_andix(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) & (i48.0 || UI)
  Value* ra = f.And(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS)));
  f.StoreGPR(i.D.RA, ra);
  f.UpdateCR(0, ra);
  return 0;
}

int InstrEmit_andisx(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) & (i32.0 || UI || i16.0)
  Value* ra =
      f.And(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS) << 16));
  f.StoreGPR(i.D.RA, ra);
  f.UpdateCR(0, ra);
  return 0;
}

int InstrEmit_cntlzdx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- 0
  // do while n < 64
  //   if (RS)[n] = 1 then leave n
  //   n <- n + 1
  // RA <- n
  Value* v = f.CountLeadingZeros(f.LoadGPR(i.X.RT));
  v = f.ZeroExtend(v, INT64_TYPE);
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_cntlzwx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- 32
  // do while n < 64
  //   if (RS)[n] = 1 then leave n
  //   n <- n + 1
  // RA <- n - 32
  Value* v = f.CountLeadingZeros(f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE));
  v = f.ZeroExtend(v, INT64_TYPE);
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_eqvx(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) == (RB)
  Value* ra = f.Not(f.Xor(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB)));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

int InstrEmit_extsbx(PPCHIRBuilder& f, const InstrData& i) {
  // s <- (RS)[56]
  // RA[56:63] <- (RS)[56:63]
  // RA[0:55] <- i56.s
  Value* rt = f.LoadGPR(i.X.RT);
  rt = f.SignExtend(f.Truncate(rt, INT8_TYPE), INT64_TYPE);
  f.StoreGPR(i.X.RA, rt);
  if (i.X.Rc) {
    f.UpdateCR(0, rt);
  }
  return 0;
}

int InstrEmit_extshx(PPCHIRBuilder& f, const InstrData& i) {
  // s <- (RS)[48]
  // RA[48:63] <- (RS)[48:63]
  // RA[0:47] <- 48.s
  Value* rt = f.LoadGPR(i.X.RT);
  rt = f.SignExtend(f.Truncate(rt, INT16_TYPE), INT64_TYPE);
  f.StoreGPR(i.X.RA, rt);
  if (i.X.Rc) {
    f.UpdateCR(0, rt);
  }
  return 0;
}

int InstrEmit_extswx(PPCHIRBuilder& f, const InstrData& i) {
  // s <- (RS)[32]
  // RA[32:63] <- (RS)[32:63]
  // RA[0:31] <- i32.s
  Value* rt = f.LoadGPR(i.X.RT);
  rt = f.SignExtend(f.Truncate(rt, INT32_TYPE), INT64_TYPE);
  f.StoreGPR(i.X.RA, rt);
  if (i.X.Rc) {
    f.UpdateCR(0, rt);
  }
  return 0;
}

int InstrEmit_nandx(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- ¬((RS) & (RB))
  Value* ra = f.Not(f.And(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB)));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

int InstrEmit_norx(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- ¬((RS) | (RB))
  Value* ra = f.Not(f.Or(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB)));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

int InstrEmit_orx(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) | (RB)
  if (i.X.RT == i.X.RB && i.X.RT == i.X.RA && !i.X.Rc) {
    // Sometimes used as no-op.
    f.Nop();
    return 0;
  }
  Value* ra;
  if (i.X.RT == i.X.RB) {
    ra = f.LoadGPR(i.X.RT);
  } else {
    ra = f.Or(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB));
  }
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

int InstrEmit_orcx(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) | ¬(RB)
  Value* ra = f.Or(f.LoadGPR(i.X.RT), f.Not(f.LoadGPR(i.X.RB)));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

int InstrEmit_ori(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) | (i48.0 || UI)
  if (!i.D.RA && !i.D.RT && !i.D.DS) {
    f.Nop();
    return 0;
  }
  Value* ra = f.Or(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS)));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

int InstrEmit_oris(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) | (i32.0 || UI || i16.0)
  Value* ra =
      f.Or(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS) << 16));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

int InstrEmit_xorx(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) XOR (RB)
  Value* ra = f.Xor(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

int InstrEmit_xori(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) XOR (i48.0 || UI)
  Value* ra = f.Xor(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS)));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

int InstrEmit_xoris(PPCHIRBuilder& f, const InstrData& i) {
  // RA <- (RS) XOR (i32.0 || UI || i16.0)
  Value* ra =
      f.Xor(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS) << 16));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

// Integer rotate (A-6)

int InstrEmit_rldclx(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_rldcrx(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_rldicx(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_rldiclx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- sh[5] || sh[0:4]
  // r <- ROTL64((RS), n)
  // b <- mb[5] || mb[0:4]
  // m <- MASK(b, 63)
  // RA <- r & m
  uint32_t sh = (i.MD.SH5 << 5) | i.MD.SH;
  uint32_t mb = (i.MD.MB5 << 5) | i.MD.MB;
  uint64_t m = XEMASK(mb, 63);
  Value* v = f.LoadGPR(i.MD.RT);
  if (sh == 64 - mb) {
    // srdi == rldicl ra,rs,64-n,n
    v = f.Shr(v, int8_t(mb));
  } else {
    if (sh) {
      v = f.RotateLeft(v, f.LoadConstantInt8(sh));
    }
    if (m != 0xFFFFFFFFFFFFFFFF) {
      v = f.And(v, f.LoadConstantUint64(m));
    }
  }
  f.StoreGPR(i.MD.RA, v);
  if (i.MD.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_rldicrx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- sh[5] || sh[0:4]
  // r <- ROTL64((RS), n)
  // e <- me[5] || me[0:4]
  // m <- MASK(0, e)
  // RA <- r & m
  uint32_t sh = (i.MD.SH5 << 5) | i.MD.SH;
  uint32_t mb = (i.MD.MB5 << 5) | i.MD.MB;
  uint64_t m = XEMASK(0, mb);
  Value* v = f.LoadGPR(i.MD.RT);
  if (mb == 63 - sh) {
    // sldi ==  rldicr ra,rs,n,63-n
    v = f.Shl(v, int8_t(sh));
  } else {
    if (sh) {
      v = f.RotateLeft(v, f.LoadConstantInt8(sh));
    }
    if (m != 0xFFFFFFFFFFFFFFFF) {
      v = f.And(v, f.LoadConstantUint64(m));
    }
  }
  f.StoreGPR(i.MD.RA, v);
  if (i.MD.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_rldimix(PPCHIRBuilder& f, const InstrData& i) {
  // n <- sh[5] || sh[0:4]
  // r <- ROTL64((RS), n)
  // b <- me[5] || me[0:4]
  // m <- MASK(b, ¬n)
  // RA <- (r & m) | ((RA)&¬m)
  uint32_t sh = (i.MD.SH5 << 5) | i.MD.SH;
  uint32_t mb = (i.MD.MB5 << 5) | i.MD.MB;
  uint64_t m = XEMASK(mb, ~sh);
  Value* v = f.LoadGPR(i.MD.RT);
  if (sh) {
    v = f.RotateLeft(v, f.LoadConstantInt8(sh));
  }
  if (m != 0xFFFFFFFFFFFFFFFF) {
    Value* ra = f.LoadGPR(i.MD.RA);
    v = f.Or(f.And(v, f.LoadConstantUint64(m)),
             f.And(ra, f.LoadConstantUint64(~m)));
  }
  f.StoreGPR(i.MD.RA, v);
  if (i.MD.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_rlwimix(PPCHIRBuilder& f, const InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r&m | (RA)&¬m
  Value* v = f.LoadGPR(i.M.RT);
  // (x||x)
  v = f.Or(f.Shl(v, 32), f.ZeroExtend(f.Truncate(v, INT32_TYPE), INT64_TYPE));
  if (i.M.SH) {
    v = f.RotateLeft(v, f.LoadConstantInt8(i.M.SH));
  }
  // Compiler sometimes masks with 0xFFFFFFFF (identity) - avoid the work here
  // as our truncation/zero-extend does it for us.
  uint64_t m = XEMASK(i.M.MB + 32, i.M.ME + 32);
  if (m != 0xFFFFFFFFFFFFFFFFull) {
    v = f.And(v, f.LoadConstantUint64(m));
  }
  v = f.Or(v, f.And(f.LoadGPR(i.M.RA), f.LoadConstantUint64(~m)));
  f.StoreGPR(i.M.RA, v);
  if (i.M.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_rlwinmx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r & m
  Value* v = f.LoadGPR(i.M.RT);

  // (x||x)
  v = f.Or(f.Shl(v, 32), f.ZeroExtend(f.Truncate(v, INT32_TYPE), INT64_TYPE));

  // TODO(benvanik): optimize srwi
  // TODO(benvanik): optimize slwi
  // The compiler will generate a bunch of these for the special case of SH=0.
  // Which seems to just select some bits and set cr0 for use with a branch.
  // We can detect this and do less work.
  if (i.M.SH) {
    v = f.RotateLeft(v, f.LoadConstantInt8(i.M.SH));
  }
  // Compiler sometimes masks with 0xFFFFFFFF (identity) - avoid the work here
  // as our truncation/zero-extend does it for us.
  uint64_t m = XEMASK(i.M.MB + 32, i.M.ME + 32);
  if (m != 0xFFFFFFFFFFFFFFFFull) {
    v = f.And(v, f.LoadConstantUint64(m));
  }
  f.StoreGPR(i.M.RA, v);
  if (i.M.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_rlwnmx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r & m
  Value* sh =
      f.And(f.Truncate(f.LoadGPR(i.M.SH), INT8_TYPE), f.LoadConstantInt8(0x1F));
  Value* v = f.LoadGPR(i.M.RT);
  // (x||x)
  v = f.Or(f.Shl(v, 32), f.ZeroExtend(f.Truncate(v, INT32_TYPE), INT64_TYPE));
  v = f.RotateLeft(v, sh);
  v = f.And(v, f.LoadConstantUint64(XEMASK(i.M.MB + 32, i.M.ME + 32)));
  f.StoreGPR(i.M.RA, v);
  if (i.M.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

// Integer shift (A-7)

int InstrEmit_sldx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- (RB)[58:63]
  // r <- ROTL64((RS), n)
  // if (RB)[57] = 0 then
  //   m <- MASK(0, 63-n)
  // else
  //   m <- i64.0
  // RA <- r & m
  Value* sh =
      f.And(f.Truncate(f.LoadGPR(i.X.RB), INT8_TYPE), f.LoadConstantInt8(0x7F));
  Value* v = f.Select(f.IsTrue(f.Shr(sh, 6)), f.LoadZeroInt64(),
                      f.Shl(f.LoadGPR(i.X.RT), sh));
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_slwx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], n)
  // if (RB)[58] = 0 then
  //   m <- MASK(32, 63-n)
  // else
  //   m <- i64.0
  // RA <- r & m
  Value* sh =
      f.And(f.Truncate(f.LoadGPR(i.X.RB), INT8_TYPE), f.LoadConstantInt8(0x3F));
  Value* v = f.Select(f.IsTrue(f.Shr(sh, 5)), f.LoadZeroInt32(),
                      f.Shl(f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE), sh));
  v = f.ZeroExtend(v, INT64_TYPE);
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_srdx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- (RB)[58:63]
  // r <- ROTL64((RS), 64-n)
  // if (RB)[57] = 0 then
  //   m <- MASK(n, 63)
  // else
  //   m <- i64.0
  // RA <- r & m
  Value* sh =
      f.And(f.Truncate(f.LoadGPR(i.X.RB), INT8_TYPE), f.LoadConstantInt8(0x7F));
  Value* v = f.Select(f.IsTrue(f.And(sh, f.LoadConstantInt8(0x40))),
                      f.LoadZeroInt64(), f.Shr(f.LoadGPR(i.X.RT), sh));
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_srwx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], 64-n)
  // if (RB)[58] = 0 then
  //   m <- MASK(n+32, 63)
  // else
  //   m <- i64.0
  // RA <- r & m
  Value* sh =
      f.And(f.Truncate(f.LoadGPR(i.X.RB), INT8_TYPE), f.LoadConstantInt8(0x3F));
  Value* v =
      f.Select(f.IsTrue(f.And(sh, f.LoadConstantInt8(0x20))), f.LoadZeroInt32(),
               f.Shr(f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE), sh));
  v = f.ZeroExtend(v, INT64_TYPE);
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_sradx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- rB[58-63]
  // r <- ROTL[64](rS, 64 - n)
  // if rB[57] = 0 then m ← MASK(n, 63)
  // else m ← (64)0
  // S ← rS[0]
  // rA <- (r & m) | (((64)S) & ¬ m)
  // XER[CA] <- S & ((r & ¬ m) ¦ 0)
  // if n == 0: rA <- rS, XER[CA] = 0
  // if n >= 64: rA <- 64 sign bits of rS, XER[CA] = sign bit of rS
  Value* rt = f.LoadGPR(i.X.RT);
  Value* sh =
      f.And(f.Truncate(f.LoadGPR(i.X.RB), INT8_TYPE), f.LoadConstantInt8(0x7F));
  Value* clamp_sh = f.Min(sh, f.LoadConstantInt8(0x3F));
  Value* v = f.Sha(rt, clamp_sh);

  // CA is set if any bits are shifted out of the right and if the result
  // is negative.
  Value* ca =
      f.And(f.IsTrue(f.Shr(rt, 63)), f.CompareNE(f.Shl(v, clamp_sh), rt));
  f.StoreCA(ca);

  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_sradix(PPCHIRBuilder& f, const InstrData& i) {
  // n <- sh[5] || sh[0-4]
  // r <- ROTL[64](rS, 64 - n)
  // m ← MASK(n, 63)
  // S ← rS[0]
  // rA <- (r & m) | (((64)S) & ¬ m)
  // XER[CA] <- S & ((r & ¬ m) ¦ 0)
  // if n == 0: rA <- rS, XER[CA] = 0
  // if n >= 64: rA <- 64 sign bits of rS, XER[CA] = sign bit of rS
  Value* v = f.LoadGPR(i.XS.RT);
  int8_t sh = (i.XS.SH5 << 5) | i.XS.SH;

  // CA is set if any bits are shifted out of the right and if the result
  // is negative.
  if (sh) {
    uint64_t mask = XEMASK(64 - sh, 63);
    Value* ca = f.And(f.Truncate(f.Shr(v, 63), INT8_TYPE),
                      f.IsTrue(f.And(v, f.LoadConstantUint64(mask))));
    f.StoreCA(ca);

    v = f.Sha(v, sh);
  } else {
    f.StoreCA(f.LoadZeroInt8());
  }

  f.StoreGPR(i.XS.RA, v);
  if (i.XS.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_srawx(PPCHIRBuilder& f, const InstrData& i) {
  // n <- rB[59-63]
  // r <- ROTL32((RS)[32:63], 64-n)
  // m <- MASK(n+32, 63)
  // s <- (RS)[32]
  // RA <- r&m | (i64.s)&¬m
  // CA <- s & ((r&¬m)[32:63]≠0)
  // if n == 0: rA <- sign_extend(rS), XER[CA] = 0
  // if n >= 32: rA <- 64 sign bits of rS, XER[CA] = sign bit of lo_32(rS)
  Value* rt = f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE);
  Value* sh =
      f.And(f.Truncate(f.LoadGPR(i.X.RB), INT8_TYPE), f.LoadConstantInt8(0x3F));
  Value* clamp_sh = f.Min(sh, f.LoadConstantInt8(0x1F));
  Value* v = f.Sha(rt, f.Min(sh, clamp_sh));

  // CA is set if any bits are shifted out of the right and if the result
  // is negative.
  Value* ca =
      f.And(f.IsTrue(f.Shr(rt, 31)), f.CompareNE(f.Shl(v, clamp_sh), rt));
  f.StoreCA(ca);

  v = f.SignExtend(v, INT64_TYPE);
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

int InstrEmit_srawix(PPCHIRBuilder& f, const InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], 64-n)
  // m <- MASK(n+32, 63)
  // s <- (RS)[32]
  // RA <- r&m | (i64.s)&¬m
  // CA <- s & ((r&¬m)[32:63]≠0)
  // if n == 0: rA <- sign_extend(rS), XER[CA] = 0
  // if n >= 32: rA <- 64 sign bits of rS, XER[CA] = sign bit of lo_32(rS)
  Value* v = f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE);
  Value* ca;
  if (!i.X.RB) {
    // No shift, just a fancy sign extend and CA clearer.
    v = f.SignExtend(v, INT64_TYPE);
    ca = f.LoadZeroInt8();
  } else {
    // CA is set if any bits are shifted out of the right and if the result
    // is negative.
    uint32_t mask = (uint32_t)XEMASK(64 - i.X.RB, 63);
    ca = f.And(f.Truncate(f.Shr(v, 31), INT8_TYPE),
               f.IsTrue(f.And(v, f.LoadConstantUint32(mask))));

    v = f.Sha(v, (int8_t)i.X.RB), v = f.SignExtend(v, INT64_TYPE);
  }
  f.StoreCA(ca);
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

void RegisterEmitCategoryALU() {
  XEREGISTERINSTR(addx);
  XEREGISTERINSTR(addcx);
  XEREGISTERINSTR(addex);
  XEREGISTERINSTR(addi);
  XEREGISTERINSTR(addic);
  XEREGISTERINSTR(addicx);
  XEREGISTERINSTR(addis);
  XEREGISTERINSTR(addmex);
  XEREGISTERINSTR(addzex);
  XEREGISTERINSTR(divdx);
  XEREGISTERINSTR(divdux);
  XEREGISTERINSTR(divwx);
  XEREGISTERINSTR(divwux);
  XEREGISTERINSTR(mulhdx);
  XEREGISTERINSTR(mulhdux);
  XEREGISTERINSTR(mulhwx);
  XEREGISTERINSTR(mulhwux);
  XEREGISTERINSTR(mulldx);
  XEREGISTERINSTR(mulli);
  XEREGISTERINSTR(mullwx);
  XEREGISTERINSTR(negx);
  XEREGISTERINSTR(subfx);
  XEREGISTERINSTR(subfcx);
  XEREGISTERINSTR(subficx);
  XEREGISTERINSTR(subfex);
  XEREGISTERINSTR(subfmex);
  XEREGISTERINSTR(subfzex);
  XEREGISTERINSTR(cmp);
  XEREGISTERINSTR(cmpi);
  XEREGISTERINSTR(cmpl);
  XEREGISTERINSTR(cmpli);
  XEREGISTERINSTR(andx);
  XEREGISTERINSTR(andcx);
  XEREGISTERINSTR(andix);
  XEREGISTERINSTR(andisx);
  XEREGISTERINSTR(cntlzdx);
  XEREGISTERINSTR(cntlzwx);
  XEREGISTERINSTR(eqvx);
  XEREGISTERINSTR(extsbx);
  XEREGISTERINSTR(extshx);
  XEREGISTERINSTR(extswx);
  XEREGISTERINSTR(nandx);
  XEREGISTERINSTR(norx);
  XEREGISTERINSTR(orx);
  XEREGISTERINSTR(orcx);
  XEREGISTERINSTR(ori);
  XEREGISTERINSTR(oris);
  XEREGISTERINSTR(xorx);
  XEREGISTERINSTR(xori);
  XEREGISTERINSTR(xoris);
  XEREGISTERINSTR(rldclx);
  XEREGISTERINSTR(rldcrx);
  XEREGISTERINSTR(rldicx);
  XEREGISTERINSTR(rldiclx);
  XEREGISTERINSTR(rldicrx);
  XEREGISTERINSTR(rldimix);
  XEREGISTERINSTR(rlwimix);
  XEREGISTERINSTR(rlwinmx);
  XEREGISTERINSTR(rlwnmx);
  XEREGISTERINSTR(sldx);
  XEREGISTERINSTR(slwx);
  XEREGISTERINSTR(srdx);
  XEREGISTERINSTR(srwx);
  XEREGISTERINSTR(sradx);
  XEREGISTERINSTR(sradix);
  XEREGISTERINSTR(srawx);
  XEREGISTERINSTR(srawix);
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

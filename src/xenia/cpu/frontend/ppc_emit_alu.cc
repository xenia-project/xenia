/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/frontend/ppc_emit-private.h"

#include "xenia/base/assert.h"
#include "xenia/cpu/frontend/ppc_context.h"
#include "xenia/cpu/frontend/ppc_hir_builder.h"

namespace xe {
namespace cpu {
namespace frontend {

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

XEEMITTER(addx, 0x7C000214, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(addcx, 0x7C000014, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(addex, 0x7C000114, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(addi, 0x38000000, D)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(addic, 0x30000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // RT <- (RA) + EXTS(SI)
  // CA <- carry bit
  Value* ra = f.LoadGPR(i.D.RA);
  Value* v = f.Add(ra, f.LoadConstantInt64(XEEXTS16(i.D.DS)));
  f.StoreGPR(i.D.RT, v);
  f.StoreCA(AddDidCarry(f, ra, f.LoadConstantInt64(XEEXTS16(i.D.DS))));
  return 0;
}

XEEMITTER(addicx, 0x34000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // RT <- (RA) + EXTS(SI)
  // CA <- carry bit
  Value* ra = f.LoadGPR(i.D.RA);
  Value* v = f.Add(f.LoadGPR(i.D.RA), f.LoadConstantInt64(XEEXTS16(i.D.DS)));
  f.StoreGPR(i.D.RT, v);
  f.StoreCA(AddDidCarry(f, ra, f.LoadConstantInt64(XEEXTS16(i.D.DS))));
  f.UpdateCR(0, v);
  return 0;
}

XEEMITTER(addis, 0x3C000000, D)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(addmex, 0x7C0001D4, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(addzex, 0x7C000194, XO)(PPCHIRBuilder& f, InstrData& i) {
  // RT <- (RA) + CA
  // CA <- carry bit
  Value* ra = f.LoadGPR(i.XO.RA);
  Value* v = f.AddWithCarry(ra, f.LoadZeroInt64(), f.LoadCA());
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    // With XER[SO] update too.
    // e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
    assert_always();
  } else {
    // Just CA update.
    f.StoreCA(AddWithCarryDidCarry(f, ra, f.LoadZeroInt64(), f.LoadCA()));
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(divdx, 0x7C0003D2, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(divdux, 0x7C000392, XO)(PPCHIRBuilder& f, InstrData& i) {
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
    f.UpdateCR(0, v, false);
  }
  return 0;
}

XEEMITTER(divwx, 0x7C0003D6, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(divwux, 0x7C000396, XO)(PPCHIRBuilder& f, InstrData& i) {
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
    f.UpdateCR(0, v, false);
  }
  return 0;
}

XEEMITTER(mulhdx, 0x7C000092, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(mulhdux, 0x7C000012, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(mulhwx, 0x7C000096, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(mulhwux, 0x7C000016, XO)(PPCHIRBuilder& f, InstrData& i) {
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
    f.UpdateCR(0, v, false);
  }
  return 0;
}

XEEMITTER(mulldx, 0x7C0001D2, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(mulli, 0x1C000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // prod[0:127] <- (RA) × EXTS(SI)
  // RT <- prod[64:127]
  Value* v = f.Mul(f.LoadGPR(i.D.RA), f.LoadConstantInt64(XEEXTS16(i.D.DS)));
  f.StoreGPR(i.D.RT, v);
  return 0;
}

XEEMITTER(mullwx, 0x7C0001D6, XO)(PPCHIRBuilder& f, InstrData& i) {
  // RT <- (RA)[32:63] × (RB)[32:63]
  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  Value* v = f.SignExtend(f.Mul(f.Truncate(f.LoadGPR(i.XO.RA), INT32_TYPE),
                                f.Truncate(f.LoadGPR(i.XO.RB), INT32_TYPE)),
                          INT64_TYPE);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(negx, 0x7C0000D0, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(subfx, 0x7C000050, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(subfcx, 0x7C000010, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(subficx, 0x20000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // RT <- ¬(RA) + EXTS(SI) + 1
  Value* ra = f.LoadGPR(i.D.RA);
  Value* v = f.Sub(f.LoadConstantInt64(XEEXTS16(i.D.DS)), ra);
  f.StoreGPR(i.D.RT, v);
  f.StoreCA(SubDidCarry(f, f.LoadConstantInt64(XEEXTS16(i.D.DS)), ra));
  return 0;
}

XEEMITTER(subfex, 0x7C000110, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(subfmex, 0x7C0001D0, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(subfzex, 0x7C000190, XO)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(cmp, 0x7C000000, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(cmpi, 0x2C000000, D)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(cmpl, 0x7C000040, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(cmpli, 0x28000000, D)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(andx, 0x7C000038, X)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) & (RB)
  Value* ra = f.And(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

XEEMITTER(andcx, 0x7C000078, X)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) & ¬(RB)
  Value* ra = f.And(f.LoadGPR(i.X.RT), f.Not(f.LoadGPR(i.X.RB)));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

XEEMITTER(andix, 0x70000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) & (i48.0 || UI)
  Value* ra = f.And(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS)));
  f.StoreGPR(i.D.RA, ra);
  f.UpdateCR(0, ra);
  return 0;
}

XEEMITTER(andisx, 0x74000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) & (i32.0 || UI || i16.0)
  Value* ra =
      f.And(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS) << 16));
  f.StoreGPR(i.D.RA, ra);
  f.UpdateCR(0, ra);
  return 0;
}

XEEMITTER(cntlzdx, 0x7C000074, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(cntlzwx, 0x7C000034, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(eqvx, 0x7C000238, X)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) == (RB)
  Value* ra = f.Xor(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB));
  ra = f.Not(ra);
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

XEEMITTER(extsbx, 0x7C000774, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(extshx, 0x7C000734, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(extswx, 0x7C0007B4, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(nandx, 0x7C0003B8, X)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- ¬((RS) & (RB))
  Value* ra = f.Not(f.And(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB)));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

XEEMITTER(norx, 0x7C0000F8, X)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- ¬((RS) | (RB))
  Value* ra = f.Not(f.Or(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB)));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

XEEMITTER(orx, 0x7C000378, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(orcx, 0x7C000338, X)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) | ¬(RB)
  Value* ra = f.Or(f.LoadGPR(i.X.RT), f.Not(f.LoadGPR(i.X.RB)));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

XEEMITTER(ori, 0x60000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) | (i48.0 || UI)
  if (!i.D.RA && !i.D.RT && !i.D.DS) {
    f.Nop();
    return 0;
  }
  Value* ra = f.Or(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS)));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

XEEMITTER(oris, 0x64000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) | (i32.0 || UI || i16.0)
  Value* ra =
      f.Or(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS) << 16));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

XEEMITTER(xorx, 0x7C000278, X)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) XOR (RB)
  Value* ra = f.Xor(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB));
  f.StoreGPR(i.X.RA, ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  return 0;
}

XEEMITTER(xori, 0x68000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) XOR (i48.0 || UI)
  Value* ra = f.Xor(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS)));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

XEEMITTER(xoris, 0x6C000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // RA <- (RS) XOR (i32.0 || UI || i16.0)
  Value* ra =
      f.Xor(f.LoadGPR(i.D.RT), f.LoadConstantUint64(XEEXTZ16(i.D.DS) << 16));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

// Integer rotate (A-6)

XEEMITTER(rld, 0x78000000, MDS)(PPCHIRBuilder& f, InstrData& i) {
  if (i.MD.idx == 0) {
    // XEEMITTER(rldiclx,      0x78000000, MD )
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
  } else if (i.MD.idx == 1) {
    // XEEMITTER(rldicrx,      0x78000004, MD )
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
  } else if (i.MD.idx == 2) {
    // XEEMITTER(rldicx,       0x78000008, MD )
    XEINSTRNOTIMPLEMENTED();
    return 1;
  } else if (i.MDS.idx == 8) {
    // XEEMITTER(rldclx,       0x78000010, MDS)
    XEINSTRNOTIMPLEMENTED();
    return 1;
  } else if (i.MDS.idx == 9) {
    // XEEMITTER(rldcrx,       0x78000012, MDS)
    XEINSTRNOTIMPLEMENTED();
    return 1;
  } else if (i.MD.idx == 3) {
    // XEEMITTER(rldimix,      0x7800000C, MD )
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
  } else {
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
}

XEEMITTER(rlwimix, 0x50000000, M)(PPCHIRBuilder& f, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r&m | (RA)&¬m
  Value* v = f.Truncate(f.LoadGPR(i.M.RT), INT32_TYPE);
  if (i.M.SH) {
    v = f.RotateLeft(v, f.LoadConstantUint32(i.M.SH));
  }
  // Compiler sometimes masks with 0xFFFFFFFF (identity) - avoid the work here
  // as our truncation/zero-extend does it for us.
  uint32_t m = (uint32_t)XEMASK(i.M.MB + 32, i.M.ME + 32);
  if (!(i.M.MB == 0 && i.M.ME == 31)) {
    v = f.And(v, f.LoadConstantUint32(m));
  }
  v = f.ZeroExtend(v, INT64_TYPE);
  v = f.Or(v, f.And(f.LoadGPR(i.M.RA), f.LoadConstantUint64(~(uint64_t)m)));
  f.StoreGPR(i.M.RA, v);
  if (i.M.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(rlwinmx, 0x54000000, M)(PPCHIRBuilder& f, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r & m
  Value* v = f.Truncate(f.LoadGPR(i.M.RT), INT32_TYPE);
  // TODO(benvanik): optimize srwi
  // TODO(benvanik): optimize slwi
  // The compiler will generate a bunch of these for the special case of SH=0.
  // Which seems to just select some bits and set cr0 for use with a branch.
  // We can detect this and do less work.
  if (i.M.SH) {
    v = f.RotateLeft(v, f.LoadConstantUint32(i.M.SH));
  }
  // Compiler sometimes masks with 0xFFFFFFFF (identity) - avoid the work here
  // as our truncation/zero-extend does it for us.
  if (!(i.M.MB == 0 && i.M.ME == 31)) {
    v = f.And(v,
              f.LoadConstantUint32(uint32_t(XEMASK(i.M.MB + 32, i.M.ME + 32))));
  }
  v = f.ZeroExtend(v, INT64_TYPE);
  f.StoreGPR(i.M.RA, v);
  if (i.M.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(rlwnmx, 0x5C000000, M)(PPCHIRBuilder& f, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r & m
  Value* v = f.Truncate(f.LoadGPR(i.M.RT), INT32_TYPE);
  Value* sh = f.And(f.Truncate(f.LoadGPR(i.M.SH), INT32_TYPE),
                    f.LoadConstantUint32(0x1F));
  v = f.RotateLeft(v, sh);
  // Compiler sometimes masks with 0xFFFFFFFF (identity) - avoid the work here
  // as our truncation/zero-extend does it for us.
  if (!(i.M.MB == 0 && i.M.ME == 31)) {
    v = f.And(v,
              f.LoadConstantUint32(uint32_t(XEMASK(i.M.MB + 32, i.M.ME + 32))));
  }
  v = f.ZeroExtend(v, INT64_TYPE);
  f.StoreGPR(i.M.RA, v);
  if (i.M.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

// Integer shift (A-7)

XEEMITTER(sldx, 0x7C000036, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(slwx, 0x7C000030, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(srdx, 0x7C000436, X)(PPCHIRBuilder& f, InstrData& i) {
  // n <- (RB)[58:63]
  // r <- ROTL64((RS), 64-n)
  // if (RB)[57] = 0 then
  //   m <- MASK(n, 63)
  // else
  //   m <- i64.0
  // RA <- r & m
  // TODO(benvanik): if >3F, zero out the result.
  Value* sh = f.Truncate(f.LoadGPR(i.X.RB), INT8_TYPE);
  Value* v = f.Select(f.IsTrue(f.And(sh, f.LoadConstantInt8(0x40))),
                      f.LoadZeroInt64(), f.Shr(f.LoadGPR(i.X.RT), sh));
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(srwx, 0x7C000430, X)(PPCHIRBuilder& f, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], 64-n)
  // if (RB)[58] = 0 then
  //   m <- MASK(n+32, 63)
  // else
  //   m <- i64.0
  // RA <- r & m
  // TODO(benvanik): if >1F, zero out the result.
  Value* sh = f.Truncate(f.LoadGPR(i.X.RB), INT8_TYPE);
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

XEEMITTER(sradx, 0x7C000634, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(sradix, 0x7C000674, XS)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(srawx, 0x7C000630, X)(PPCHIRBuilder& f, InstrData& i) {
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

XEEMITTER(srawix, 0x7C000670, X)(PPCHIRBuilder& f, InstrData& i) {
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
  XEREGISTERINSTR(addx, 0x7C000214);
  XEREGISTERINSTR(addcx, 0X7C000014);
  XEREGISTERINSTR(addex, 0x7C000114);
  XEREGISTERINSTR(addi, 0x38000000);
  XEREGISTERINSTR(addic, 0x30000000);
  XEREGISTERINSTR(addicx, 0x34000000);
  XEREGISTERINSTR(addis, 0x3C000000);
  XEREGISTERINSTR(addmex, 0x7C0001D4);
  XEREGISTERINSTR(addzex, 0x7C000194);
  XEREGISTERINSTR(divdx, 0x7C0003D2);
  XEREGISTERINSTR(divdux, 0x7C000392);
  XEREGISTERINSTR(divwx, 0x7C0003D6);
  XEREGISTERINSTR(divwux, 0x7C000396);
  XEREGISTERINSTR(mulhdx, 0x7C000092);
  XEREGISTERINSTR(mulhdux, 0x7C000012);
  XEREGISTERINSTR(mulhwx, 0x7C000096);
  XEREGISTERINSTR(mulhwux, 0x7C000016);
  XEREGISTERINSTR(mulldx, 0x7C0001D2);
  XEREGISTERINSTR(mulli, 0x1C000000);
  XEREGISTERINSTR(mullwx, 0x7C0001D6);
  XEREGISTERINSTR(negx, 0x7C0000D0);
  XEREGISTERINSTR(subfx, 0x7C000050);
  XEREGISTERINSTR(subfcx, 0x7C000010);
  XEREGISTERINSTR(subficx, 0x20000000);
  XEREGISTERINSTR(subfex, 0x7C000110);
  XEREGISTERINSTR(subfmex, 0x7C0001D0);
  XEREGISTERINSTR(subfzex, 0x7C000190);
  XEREGISTERINSTR(cmp, 0x7C000000);
  XEREGISTERINSTR(cmpi, 0x2C000000);
  XEREGISTERINSTR(cmpl, 0x7C000040);
  XEREGISTERINSTR(cmpli, 0x28000000);
  XEREGISTERINSTR(andx, 0x7C000038);
  XEREGISTERINSTR(andcx, 0x7C000078);
  XEREGISTERINSTR(andix, 0x70000000);
  XEREGISTERINSTR(andisx, 0x74000000);
  XEREGISTERINSTR(cntlzdx, 0x7C000074);
  XEREGISTERINSTR(cntlzwx, 0x7C000034);
  XEREGISTERINSTR(eqvx, 0x7C000238);
  XEREGISTERINSTR(extsbx, 0x7C000774);
  XEREGISTERINSTR(extshx, 0x7C000734);
  XEREGISTERINSTR(extswx, 0x7C0007B4);
  XEREGISTERINSTR(nandx, 0x7C0003B8);
  XEREGISTERINSTR(norx, 0x7C0000F8);
  XEREGISTERINSTR(orx, 0x7C000378);
  XEREGISTERINSTR(orcx, 0x7C000338);
  XEREGISTERINSTR(ori, 0x60000000);
  XEREGISTERINSTR(oris, 0x64000000);
  XEREGISTERINSTR(xorx, 0x7C000278);
  XEREGISTERINSTR(xori, 0x68000000);
  XEREGISTERINSTR(xoris, 0x6C000000);
  XEREGISTERINSTR(rld, 0x78000000);
  // -- // XEREGISTERINSTR(rldclx,       0x78000010);
  // -- // XEREGISTERINSTR(rldcrx,       0x78000012);
  // -- // XEREGISTERINSTR(rldicx,       0x78000008);
  // -- // XEREGISTERINSTR(rldiclx,      0x78000000);
  // -- // XEREGISTERINSTR(rldicrx,      0x78000004);
  // -- // XEREGISTERINSTR(rldimix,      0x7800000C);
  XEREGISTERINSTR(rlwimix, 0x50000000);
  XEREGISTERINSTR(rlwinmx, 0x54000000);
  XEREGISTERINSTR(rlwnmx, 0x5C000000);
  XEREGISTERINSTR(sldx, 0x7C000036);
  XEREGISTERINSTR(slwx, 0x7C000030);
  XEREGISTERINSTR(srdx, 0x7C000436);
  XEREGISTERINSTR(srwx, 0x7C000430);
  XEREGISTERINSTR(sradx, 0x7C000634);
  XEREGISTERINSTR(sradix, 0x7C000674);
  XEREGISTERINSTR(sradix, 0x7C000676);  // HACK
  XEREGISTERINSTR(srawx, 0x7C000630);
  XEREGISTERINSTR(srawix, 0x7C000670);
}

}  // namespace frontend
}  // namespace cpu
}  // namespace xe

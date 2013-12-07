/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_emit-private.h>

#include <alloy/frontend/ppc/ppc_context.h>
#include <alloy/frontend/ppc/ppc_function_builder.h>


using namespace alloy::frontend::ppc;
using namespace alloy::hir;
using namespace alloy::runtime;


namespace alloy {
namespace frontend {
namespace ppc {


// Integer arithmetic (A-3)

XEEMITTER(addx,         0x7C000214, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RD <- (RA) + (RB)
  Value* v = f.Add(
      f.LoadGPR(i.XO.RA),
      f.LoadGPR(i.XO.RB));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    XEASSERTALWAYS();
    //e.update_xer_with_overflow(EFLAGS OF?);
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(addcx,        0x7C000014, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RD <- (RA) + (RB)
  // CA <- carry bit
  Value* v = f.Add(
      f.LoadGPR(i.XO.RA),
      f.LoadGPR(i.XO.RB),
      ARITHMETIC_SET_CARRY);
  f.StoreCA(f.DidCarry(v));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    XEASSERTALWAYS();
    //e.update_xer_with_overflow(EFLAGS OF?);
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(addex,        0x7C000114, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RD <- (RA) + (RB) + XER[CA]
  Value* v = f.AddWithCarry(
      f.LoadGPR(i.XO.RA),
      f.LoadGPR(i.XO.RB),
      f.LoadCA(),
      ARITHMETIC_SET_CARRY);
  f.StoreCA(f.DidCarry(v));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    XEASSERTALWAYS();
    //e.update_xer_with_overflow(EFLAGS OF?);
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(addi,         0x38000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // if RA = 0 then
  //   RT <- EXTS(SI)
  // else
  //   RT <- (RA) + EXTS(SI)
  Value* si = f.LoadConstant(XEEXTS16(i.D.DS));
  Value* v = si;
  if (i.D.RA) {
    v = f.Add(f.LoadGPR(i.D.RA), si);
  }
  f.StoreGPR(i.D.RT, v);
  return 0;
}

XEEMITTER(addic,        0x30000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- (RA) + EXTS(SI)
  Value* v = f.Add(
      f.LoadGPR(i.D.RA),
      f.LoadConstant(XEEXTS16(i.D.DS)),
      ARITHMETIC_SET_CARRY);
  f.StoreCA(f.DidCarry(v));
  f.StoreGPR(i.D.RT, v);
  return 0;
}

XEEMITTER(addicx,       0x34000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- (RA) + EXTS(SI)
  Value* v = f.Add(
      f.LoadGPR(i.D.RA),
      f.LoadConstant(XEEXTS16(i.D.DS)),
      ARITHMETIC_SET_CARRY);
  f.StoreCA(f.DidCarry(v));
  f.StoreGPR(i.D.RT, v);
  f.UpdateCR(0, v);
  return 0;
}

XEEMITTER(addis,        0x3C000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // if RA = 0 then
  //   RT <- EXTS(SI) || i16.0
  // else
  //   RT <- (RA) + EXTS(SI) || i16.0
  Value* si = f.LoadConstant(XEEXTS16(i.D.DS) << 16);
  Value* v = si;
  if (i.D.RA) {
    v = f.Add(f.LoadGPR(i.D.RA), si);
  }
  f.StoreGPR(i.D.RT, v);
  return 0;
}

XEEMITTER(addmex,       0x7C0001D4, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- (RA) + CA - 1
  Value* v = f.AddWithCarry(
      f.LoadGPR(i.XO.RA),
      f.LoadConstant((int64_t)-1),
      f.LoadCA(),
      ARITHMETIC_SET_CARRY);
  if (i.XO.OE) {
    // With XER[SO] update too.
    //e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
    XEASSERTALWAYS();
  } else {
    // Just CA update.
    f.StoreCA(f.DidCarry(v));
  }
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(addzex,       0x7C000194, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- (RA) + CA
  Value* v = f.AddWithCarry(
      f.LoadGPR(i.XO.RA),
      f.LoadZero(INT64_TYPE),
      f.LoadCA(),
      ARITHMETIC_SET_CARRY);
  if (i.XO.OE) {
    // With XER[SO] update too.
    //e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
    XEASSERTALWAYS();
  } else {
    // Just CA update.
    f.StoreCA(f.DidCarry(v));
  }
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(divdx,        0x7C0003D2, XO )(PPCFunctionBuilder& f, InstrData& i) {
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
    //e.update_xer_with_overflow(e.get_uint64(0));
    XEASSERTALWAYS();
    return 1;
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(divdux,       0x7C000392, XO )(PPCFunctionBuilder& f, InstrData& i) {
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
    //e.update_xer_with_overflow(e.get_uint64(0));
    XEASSERTALWAYS();
    return 1;
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v, false);
  }
  return 0;
}

XEEMITTER(divwx,        0x7C0003D6, XO )(PPCFunctionBuilder& f, InstrData& i) {
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
    //e.update_xer_with_overflow(e.get_uint64(0));
    XEASSERTALWAYS();
    return 1;
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(divwux,       0x7C000396, XO )(PPCFunctionBuilder& f, InstrData& i) {
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
    //e.update_xer_with_overflow(e.get_uint64(0));
    XEASSERTALWAYS();
    return 1;
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v, false);
  }
  return 0;
}

XEEMITTER(mulhdx,       0x7C000092, XO )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhdux,      0x7C000012, XO )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhwx,       0x7C000096, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT[32:64] <- ((RA)[32:63] × (RB)[32:63])[0:31]
  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  Value* v = f.Mul(
      f.ZeroExtend(f.Truncate(f.LoadGPR(i.XO.RA), INT32_TYPE), INT64_TYPE),
      f.ZeroExtend(f.Truncate(f.LoadGPR(i.XO.RB), INT32_TYPE), INT64_TYPE));
  v = f.Shr(v, 32);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(mulhwux,      0x7C000016, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT[32:64] <- ((RA)[32:63] × (RB)[32:63])[0:31]
  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  Value* v = f.Mul(
      f.ZeroExtend(f.Truncate(f.LoadGPR(i.XO.RA), INT32_TYPE), INT64_TYPE),
      f.ZeroExtend(f.Truncate(f.LoadGPR(i.XO.RB), INT32_TYPE), INT64_TYPE));
  v = f.Shr(v, 32);
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v, false);
  }
  return 0;
}

XEEMITTER(mulldx,       0x7C0001D2, XO )(PPCFunctionBuilder& f, InstrData& i) {
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

XEEMITTER(mulli,        0x1C000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // prod[0:127] <- (RA) × EXTS(SI)
  // RT <- prod[64:127]
  Value* v = f.Mul(f.LoadGPR(i.D.RA), f.LoadConstant(XEEXTS16(i.D.DS)));
  f.StoreGPR(i.D.RT, v);
  return 0;
}

XEEMITTER(mullwx,       0x7C0001D6, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- (RA)[32:63] × (RB)[32:63]
  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  Value* v = f.Mul(
      f.ZeroExtend(f.Truncate(f.LoadGPR(i.XO.RA), INT32_TYPE), INT64_TYPE),
      f.ZeroExtend(f.Truncate(f.LoadGPR(i.XO.RB), INT32_TYPE), INT64_TYPE));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(negx,         0x7C0000D0, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- ¬(RA) + 1
  if (i.XO.OE) {
    // With XER update.
    // This is a different codepath as we need to use llvm.ssub.with.overflow.

    // if RA == 0x8000000000000000 then no-op and set OV=1
    // This may just magically do that...

    XEASSERTALWAYS();
    //Function* ssub_with_overflow = Intrinsic::getDeclaration(
    //    e.gen_module(), Intrinsic::ssub_with_overflow, jit_type_nint);
    //jit_value_t v = b.CreateCall2(ssub_with_overflow,
    //                         e.get_int64(0), f.LoadGPR(i.XO.RA));
    //jit_value_t v0 = b.CreateExtractValue(v, 0);
    //f.StoreGPR(i.XO.RT, v0);
    //e.update_xer_with_overflow(b.CreateExtractValue(v, 1));

    //if (i.XO.Rc) {
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

XEEMITTER(subfx,        0x7C000050, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- ¬(RA) + (RB) + 1
  Value* v = f.Sub(f.LoadGPR(i.XO.RB), f.LoadGPR(i.XO.RA));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    XEASSERTALWAYS();
    //e.update_xer_with_overflow(EFLAGS??);
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(subfcx,       0x7C000010, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- ¬(RA) + (RB) + 1
  Value* v = f.Sub(
      f.LoadGPR(i.XO.RB),
      f.LoadGPR(i.XO.RA),
      ARITHMETIC_SET_CARRY);
  f.StoreCA(f.DidCarry(v));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    XEASSERTALWAYS();
    //e.update_xer_with_overflow(EFLAGS??);
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(subficx,      0x20000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- ¬(RA) + EXTS(SI) + 1
  Value* v = f.Sub(
      f.LoadConstant(XEEXTS16(i.D.DS)),
      f.LoadGPR(i.D.RA),
      ARITHMETIC_SET_CARRY);
  f.StoreCA(f.DidCarry(v));
  f.StoreGPR(i.D.RT, v);
  return 0;
}

XEEMITTER(subfex,       0x7C000110, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- ¬(RA) + (RB) + CA
  Value* v = f.AddWithCarry(
      f.Not(f.LoadGPR(i.XO.RA)),
      f.LoadGPR(i.XO.RB),
      f.LoadCA(),
      ARITHMETIC_SET_CARRY);
  f.StoreCA(f.DidCarry(v));
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.OE) {
    XEASSERTALWAYS();
    //e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  }
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(subfmex,      0x7C0001D0, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- ¬(RA) + CA - 1
  Value* v = f.AddWithCarry(
      f.Not(f.LoadGPR(i.XO.RA)),
      f.LoadConstant((int64_t)-1),
      f.LoadCA());
  if (i.XO.OE) {
    XEASSERTALWAYS();
    //e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  } else {
    f.StoreCA(f.DidCarry(v));
  }
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(subfzex,      0x7C000190, XO )(PPCFunctionBuilder& f, InstrData& i) {
  // RT <- ¬(RA) + CA
  Value* v = f.AddWithCarry(
      f.Not(f.LoadGPR(i.XO.RA)),
      f.LoadZero(INT64_TYPE),
      f.LoadCA());
  if (i.XO.OE) {
    XEASSERTALWAYS();
    //e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  } else {
    f.StoreCA(f.DidCarry(v));
  }
  f.StoreGPR(i.XO.RT, v);
  if (i.XO.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}


// Integer compare (A-4)

XEEMITTER(cmp,          0x7C000000, X  )(PPCFunctionBuilder& f, InstrData& i) {
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

XEEMITTER(cmpi,         0x2C000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
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
    rhs = f.LoadConstant(XEEXTS16(i.D.DS));
  } else {
    lhs = f.Truncate(f.LoadGPR(i.D.RA), INT32_TYPE);
    rhs = f.LoadConstant((int32_t)XEEXTS16(i.D.DS));
  }
  f.UpdateCR(BF, lhs, rhs);
  return 0;
}

XEEMITTER(cmpl,         0x7C000040, X  )(PPCFunctionBuilder& f, InstrData& i) {
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

XEEMITTER(cmpli,        0x28000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
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
    rhs = f.LoadConstant((uint64_t)i.D.DS);
  } else {
    lhs = f.Truncate(f.LoadGPR(i.D.RA), INT32_TYPE);
    rhs = f.LoadConstant((uint32_t)i.D.DS);
  }
  f.UpdateCR(BF, lhs, rhs, false);
  return 0;
}


// Integer logical (A-5)

XEEMITTER(andx,         0x7C000038, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) & (RB)
  Value* ra = f.Add(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB));
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  f.StoreGPR(i.X.RA, ra);
  return 0;
}

XEEMITTER(andcx,        0x7C000078, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) & ¬(RB)
  Value* ra = f.Add(f.LoadGPR(i.X.RT), f.Not(f.LoadGPR(i.X.RB)));
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  f.StoreGPR(i.X.RA, ra);
  return 0;
}

XEEMITTER(andix,        0x70000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) & (i48.0 || UI)
  Value* ra = f.Add(
      f.LoadGPR(i.D.RT),
      f.LoadConstant((uint64_t)i.D.DS));
  f.UpdateCR(0, ra);
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

XEEMITTER(andisx,       0x74000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) & (i32.0 || UI || i16.0)
  Value* ra = f.Add(
      f.LoadGPR(i.D.RT),
      f.LoadConstant((uint64_t)(i.D.DS << 16)));
  f.UpdateCR(0, ra);
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

XEEMITTER(cntlzdx,      0x7C000074, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // n <- 0
  // do while n < 64
  //   if (RS)[n] = 1 then leave n
  //   n <- n + 1
  // RA <- n
  Value* v = f.CountLeadingZeros(f.LoadGPR(i.X.RT));
  v = f.ZeroExtend(v, INT64_TYPE);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  f.StoreGPR(i.X.RA, v);
  return 0;
}

XEEMITTER(cntlzwx,      0x7C000034, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // n <- 32
  // do while n < 64
  //   if (RS)[n] = 1 then leave n
  //   n <- n + 1
  // RA <- n - 32
  Value* v = f.CountLeadingZeros(
      f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE));
  v = f.ZeroExtend(v, INT64_TYPE);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  f.StoreGPR(i.X.RA, v);
  return 0;
}

XEEMITTER(eqvx,         0x7C000238, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) == (RB)

  // UNTESTED: ensure this is correct.
  XEASSERTALWAYS();
  f.DebugBreak();

  Value* ra = f.Xor(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB));
  ra = f.Not(ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  f.StoreGPR(i.X.RA, ra);
  return 0;
}

XEEMITTER(extsbx,       0x7C000774, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // s <- (RS)[56]
  // RA[56:63] <- (RS)[56:63]
  // RA[0:55] <- i56.s
  Value* rt = f.LoadGPR(i.X.RT);
  rt = f.SignExtend(f.Truncate(rt, INT8_TYPE), INT64_TYPE);
  if (i.X.Rc) {
    f.UpdateCR(0, rt);
  }
  f.StoreGPR(i.X.RA, rt);
  return 0;
}

XEEMITTER(extshx,       0x7C000734, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // s <- (RS)[48]
  // RA[48:63] <- (RS)[48:63]
  // RA[0:47] <- 48.s
  Value* rt = f.LoadGPR(i.X.RT);
  rt = f.SignExtend(f.Truncate(rt, INT16_TYPE), INT64_TYPE);
  if (i.X.Rc) {
    f.UpdateCR(0, rt);
  }
  f.StoreGPR(i.X.RA, rt);
  return 0;
}

XEEMITTER(extswx,       0x7C0007B4, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // s <- (RS)[32]
  // RA[32:63] <- (RS)[32:63]
  // RA[0:31] <- i32.s
  Value* rt = f.LoadGPR(i.X.RT);
  rt = f.SignExtend(f.Truncate(rt, INT32_TYPE), INT64_TYPE);
  if (i.X.Rc) {
    f.UpdateCR(0, rt);
  }
  f.StoreGPR(i.X.RA, rt);
  return 0;
}

XEEMITTER(nandx,        0x7C0003B8, X  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(norx,         0x7C0000F8, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- ¬((RS) | (RB))
  Value* ra = f.Or(
      f.LoadGPR(i.X.RT),
      f.LoadGPR(i.X.RB));
  ra = f.Not(ra);
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  f.StoreGPR(i.X.RA, ra);
  return 0;
}

XEEMITTER(orx,          0x7C000378, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) | (RB)
  if (i.X.RT == i.X.RB && i.X.RT == i.X.RA &&
      !i.X.Rc) {
    // Sometimes used as no-op.
    f.Nop();
    return 0;
  }
  Value* ra;
  if (i.X.RT == i.X.RB) {
    ra = f.LoadGPR(i.X.RT);
  } else {
    ra = f.Or(
        f.LoadGPR(i.X.RT),
        f.LoadGPR(i.X.RB));
  }
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  f.StoreGPR(i.X.RA, ra);
  return 0;
}

XEEMITTER(orcx,         0x7C000338, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) | ¬(RB)
  Value* ra = f.Or(
      f.LoadGPR(i.X.RT),
      f.Not(f.LoadGPR(i.X.RB)));
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  f.StoreGPR(i.X.RA, ra);
  return 0;
}

XEEMITTER(ori,          0x60000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) | (i48.0 || UI)
  Value* ra = f.Or(
      f.LoadGPR(i.D.RT),
      f.LoadConstant((uint64_t)i.D.DS));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

XEEMITTER(oris,         0x64000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) | (i32.0 || UI || i16.0)
  Value* ra = f.Or(
      f.LoadGPR(i.D.RT),
      f.LoadConstant((uint64_t)(i.D.DS << 16)));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

XEEMITTER(xorx,         0x7C000278, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) XOR (RB)
  Value* ra = f.Xor(
      f.LoadGPR(i.X.RT),
      f.LoadGPR(i.X.RB));
  if (i.X.Rc) {
    f.UpdateCR(0, ra);
  }
  f.StoreGPR(i.X.RA, ra);
  return 0;
}

XEEMITTER(xori,         0x68000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) XOR (i48.0 || UI)
  Value* ra = f.Xor(
      f.LoadGPR(i.D.RT),
      f.LoadConstant((uint64_t)i.D.DS));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}

XEEMITTER(xoris,        0x6C000000, D  )(PPCFunctionBuilder& f, InstrData& i) {
  // RA <- (RS) XOR (i32.0 || UI || i16.0)
  Value* ra = f.Xor(
      f.LoadGPR(i.D.RT),
      f.LoadConstant((uint64_t)(i.D.DS << 16)));
  f.StoreGPR(i.D.RA, ra);
  return 0;
}


// Integer rotate (A-6)

//XEEMITTER(rld,          0x78000000, MDS)(PPCFunctionBuilder& f, InstrData& i) {
//  if (i.MD.idx == 0) {
//    // XEEMITTER(rldiclx,      0x78000000, MD )
//    // n <- sh[5] || sh[0:4]
//    // r <- ROTL64((RS), n)
//    // b <- mb[5] || mb[0:4]
//    // m <- MASK(b, 63)
//    // RA <- r & m
//
//    uint32_t sh = (i.MD.SH5 << 5) | i.MD.SH;
//    uint32_t mb = (i.MD.MB5 << 5) | i.MD.MB;
//    uint64_t m = XEMASK(mb, 63);
//
//    GpVar v(c.newGpVar());
//    c.mov(v, f.LoadGPR(i.MD.RT));
//    if (sh) {
//      c.rol(v, imm(sh));
//    }
//    if (m != 0xFFFFFFFFFFFFFFFF) {
//      GpVar mask(c.newGpVar());
//      c.mov(mask, imm(m));
//      c.and_(v, mask);
//    }
//    f.StoreGPR(i.MD.RA, v);
//
//    if (i.MD.Rc) {
//      // With cr0 update.
//      f.UpdateCR(0, v);
//    }
//
//    e.clear_constant_gpr_value(i.MD.RA);
//
//    return 0;
//  } else if (i.MD.idx == 1) {
//    // XEEMITTER(rldicrx,      0x78000004, MD )
//    // n <- sh[5] || sh[0:4]
//    // r <- ROTL64((RS), n)
//    // e <- me[5] || me[0:4]
//    // m <- MASK(0, e)
//    // RA <- r & m
//
//    uint32_t sh = (i.MD.SH5 << 5) | i.MD.SH;
//    uint32_t mb = (i.MD.MB5 << 5) | i.MD.MB;
//    uint64_t m = XEMASK(0, mb);
//
//    GpVar v(c.newGpVar());
//    c.mov(v, f.LoadGPR(i.MD.RT));
//    if (sh) {
//      c.rol(v, imm(sh));
//    }
//    if (m != 0xFFFFFFFFFFFFFFFF) {
//      GpVar mask(c.newGpVar());
//      c.mov(mask, imm(m));
//      c.and_(v, mask);
//    }
//    f.StoreGPR(i.MD.RA, v);
//
//    if (i.MD.Rc) {
//      // With cr0 update.
//      f.UpdateCR(0, v);
//    }
//
//    e.clear_constant_gpr_value(i.MD.RA);
//
//    return 0;
//  } else if (i.MD.idx == 2) {
//    // XEEMITTER(rldicx,       0x78000008, MD )
//    XEINSTRNOTIMPLEMENTED();
//    return 1;
//  } else if (i.MDS.idx == 8) {
//    // XEEMITTER(rldclx,       0x78000010, MDS)
//    XEINSTRNOTIMPLEMENTED();
//    return 1;
//  } else if (i.MDS.idx == 9) {
//    // XEEMITTER(rldcrx,       0x78000012, MDS)
//    XEINSTRNOTIMPLEMENTED();
//    return 1;
//  } else if (i.MD.idx == 3) {
//    // XEEMITTER(rldimix,      0x7800000C, MD )
//    // n <- sh[5] || sh[0:4]
//    // r <- ROTL64((RS), n)
//    // b <- me[5] || me[0:4]
//    // m <- MASK(b, ¬n)
//    // RA <- (r & m) | ((RA)&¬m)
//
//    uint32_t sh = (i.MD.SH5 << 5) | i.MD.SH;
//    uint32_t mb = (i.MD.MB5 << 5) | i.MD.MB;
//    uint64_t m = XEMASK(mb, ~sh);
//
//    GpVar v(c.newGpVar());
//    c.mov(v, f.LoadGPR(i.MD.RT));
//    if (sh) {
//      c.rol(v, imm(sh));
//    }
//    if (m != 0xFFFFFFFFFFFFFFFF) {
//      GpVar mask(c.newGpVar());
//      c.mov(mask, e.get_uint64(m));
//      c.and_(v, mask);
//      GpVar ra(c.newGpVar());
//      c.mov(ra, f.LoadGPR(i.MD.RA));
//      c.not_(mask);
//      c.and_(ra, mask);
//      c.or_(v, ra);
//    }
//    f.StoreGPR(i.MD.RA, v);
//
//    if (i.MD.Rc) {
//      // With cr0 update.
//      f.UpdateCR(0, v);
//    }
//
//    e.clear_constant_gpr_value(i.MD.RA);
//
//    return 0;
//  } else {
//    XEINSTRNOTIMPLEMENTED();
//    return 1;
//  }
//}

XEEMITTER(rlwimix,      0x50000000, M  )(PPCFunctionBuilder& f, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r&m | (RA)&¬m
  Value* v = f.Truncate(f.LoadGPR(i.M.RT), INT32_TYPE);
  if (i.M.SH) {
    v = f.RotateLeft(v, f.LoadConstant(i.M.SH));
  }
  // Compiler sometimes masks with 0xFFFFFFFF (identity) - avoid the work here
  // as our truncation/zero-extend does it for us.
  uint32_t m = (uint32_t)XEMASK(i.M.MB + 32, i.M.ME + 32);
  if (!(i.M.MB == 0 && i.M.ME == 31)) {
    v = f.And(v, f.LoadConstant(m));
  }
  v = f.ZeroExtend(v, INT64_TYPE);
  Value* ra = f.LoadGPR(i.M.RA);
  v = f.Or(v, f.And(f.LoadGPR(i.M.RA), f.LoadConstant(~m)));
  if (i.M.Rc) {
    f.UpdateCR(0, v);
  }
  f.StoreGPR(i.M.RA, v);
  return 0;
}

XEEMITTER(rlwinmx,      0x54000000, M  )(PPCFunctionBuilder& f, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r & m
  Value* v = f.Truncate(f.LoadGPR(i.M.RT), INT32_TYPE);
  // The compiler will generate a bunch of these for the special case of SH=0.
  // Which seems to just select some bits and set cr0 for use with a branch.
  // We can detect this and do less work.
  if (i.M.SH) {
    v = f.RotateLeft(v, f.LoadConstant(i.M.SH));
  }
  // Compiler sometimes masks with 0xFFFFFFFF (identity) - avoid the work here
  // as our truncation/zero-extend does it for us.
  if (!(i.M.MB == 0 && i.M.ME == 31)) {
    v = f.And(v, f.LoadConstant((uint32_t)XEMASK(i.M.MB + 32, i.M.ME + 32)));
  }
  v = f.ZeroExtend(v, INT64_TYPE);
  if (i.M.Rc) {
    f.UpdateCR(0, v);
  }
  f.StoreGPR(i.M.RA, v);
  return 0;
}

XEEMITTER(rlwnmx,       0x5C000000, M  )(PPCFunctionBuilder& f, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r & m
  Value* v = f.Truncate(f.LoadGPR(i.M.RT), INT32_TYPE);
  Value* sh = f.And(f.LoadGPR(i.M.SH), f.LoadConstant(0x1F));
  v = f.RotateLeft(v, sh);
  // Compiler sometimes masks with 0xFFFFFFFF (identity) - avoid the work here
  // as our truncation/zero-extend does it for us.
  if (!(i.M.MB == 0 && i.M.ME == 31)) {
    v = f.And(v, f.LoadConstant((uint32_t)XEMASK(i.M.MB + 32, i.M.ME + 32)));
  }
  v = f.ZeroExtend(v, INT64_TYPE);
  if (i.M.Rc) {
    f.UpdateCR(0, v);
  }
  f.StoreGPR(i.M.RA, v);
  return 0;
}


// Integer shift (A-7)

XEEMITTER(sldx,         0x7C000036, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL64((RS), n)
  // if (RB)[58] = 0 then
  //   m <- MASK(0, 63-n)
  // else
  //   m <- i64.0
  // RA <- r & m
  Value* v = f.Shl(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB));
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(slwx,         0x7C000030, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], n)
  // if (RB)[58] = 0 then
  //   m <- MASK(32, 63-n)
  // else
  //   m <- i64.0
  // RA <- r & m
  Value* v = f.Shl(f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE),
                   f.LoadGPR(i.X.RB));
  v = f.ZeroExtend(v, INT64_TYPE);
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(srdx,         0x7C000436, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL64((RS), 64-n)
  // if (RB)[58] = 0 then
  //   m <- MASK(n, 63)
  // else
  //   m <- i64.0
  // RA <- r & m
  Value* v = f.Shr(f.LoadGPR(i.X.RT), f.LoadGPR(i.X.RB));
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

XEEMITTER(srwx,         0x7C000430, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], 64-n)
  // if (RB)[58] = 0 then
  //   m <- MASK(n+32, 63)
  // else
  //   m <- i64.0
  // RA <- r & m
  Value* v = f.Shr(f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE),
                   f.LoadGPR(i.X.RB));
  v = f.ZeroExtend(v, INT64_TYPE);
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}

// XEEMITTER(sradx,        0x7C000634, X  )(PPCFunctionBuilder& f, InstrData& i) {
//   // n <- rB[58-63]
//   // r <- ROTL[64](rS, 64 - n)
//   // if rB[57] = 0 then m ← MASK(n, 63)
//   // else m ← (64)0
//   // S ← rS[0]
//   // rA <- (r & m) | (((64)S) & ¬ m)
//   // XER[CA] <- S & ((r & ¬ m) ¦ 0)

//   // if n == 0: rA <- rS, XER[CA] = 0
//   // if n >= 64: rA <- 64 sign bits of rS, XER[CA] = sign bit of rS

//   GpVar v(c.newGpVar());
//   c.mov(v, f.LoadGPR(i.X.RT));
//   GpVar sh(c.newGpVar());
//   c.mov(sh, f.LoadGPR(i.X.RB));
//   c.and_(sh, imm(0x7F));

//   // CA is set if any bits are shifted out of the right and if the result
//   // is negative. Start tracking that here.
//   GpVar ca(c.newGpVar());
//   c.mov(ca, imm(0xFFFFFFFFFFFFFFFF));
//   GpVar ca_sh(c.newGpVar());
//   c.mov(ca_sh, imm(63));
//   c.sub(ca_sh, sh);
//   c.shl(ca, ca_sh);
//   c.shr(ca, ca_sh);
//   c.and_(ca, v);
//   c.cmp(ca, imm(0));
//   c.xor_(ca, ca);
//   c.setnz(ca.r8());

//   // Shift right.
//   c.sar(v, sh);

//   // CA is set to 1 if the low-order 32 bits of (RS) contain a negative number
//   // and any 1-bits are shifted out of position 63; otherwise CA is set to 0.
//   // We already have ca set to indicate the pos 63 bit, now just and in sign.
//   GpVar ca_2(c.newGpVar());
//   c.mov(ca_2, v);
//   c.shr(ca_2, imm(63));
//   c.and_(ca, ca_2);

//   f.StoreGPR(i.X.RA, v);
//   e.update_xer_with_carry(ca);

//   if (i.X.Rc) {
//     f.UpdateCR(0, v);
//   }
//   return 0;
// }

// XEEMITTER(sradix,       0x7C000674, XS )(PPCFunctionBuilder& f, InstrData& i) {
//   // n <- sh[5] || sh[0-4]
//   // r <- ROTL[64](rS, 64 - n)
//   // m ← MASK(n, 63)
//   // S ← rS[0]
//   // rA <- (r & m) | (((64)S) & ¬ m)
//   // XER[CA] <- S & ((r & ¬ m) ¦ 0)

//   // if n == 0: rA <- rS, XER[CA] = 0
//   // if n >= 64: rA <- 64 sign bits of rS, XER[CA] = sign bit of rS

//   GpVar v(c.newGpVar());
//   c.mov(v, f.LoadGPR(i.XS.RA));
//   GpVar sh(c.newGpVar());
//   c.mov(sh, imm((i.XS.SH5 << 5) | i.XS.SH));

//   // CA is set if any bits are shifted out of the right and if the result
//   // is negative. Start tracking that here.
//   GpVar ca(c.newGpVar());
//   c.mov(ca, imm(0xFFFFFFFFFFFFFFFF));
//   GpVar ca_sh(c.newGpVar());
//   c.mov(ca_sh, imm(63));
//   c.sub(ca_sh, sh);
//   c.shl(ca, ca_sh);
//   c.shr(ca, ca_sh);
//   c.and_(ca, v);
//   c.cmp(ca, imm(0));
//   c.xor_(ca, ca);
//   c.setnz(ca.r8());

//   // Shift right.
//   c.sar(v, sh);

//   // CA is set to 1 if the low-order 32 bits of (RS) contain a negative number
//   // and any 1-bits are shifted out of position 63; otherwise CA is set to 0.
//   // We already have ca set to indicate the pos 63 bit, now just and in sign.
//   GpVar ca_2(c.newGpVar());
//   c.mov(ca_2, v);
//   c.shr(ca_2, imm(63));
//   c.and_(ca, ca_2);

//   f.StoreGPR(i.XS.RT, v);
//   e.update_xer_with_carry(ca);

//   if (i.X.Rc) {
//     f.UpdateCR(0, v);
//   }
//   e.clear_constant_gpr_value(i.X.RA);
//   return 0;
// }

// XEEMITTER(srawx,        0x7C000630, X  )(PPCFunctionBuilder& f, InstrData& i) {
//   // n <- rB[59-63]
//   // r <- ROTL32((RS)[32:63], 64-n)
//   // m <- MASK(n+32, 63)
//   // s <- (RS)[32]
//   // RA <- r&m | (i64.s)&¬m
//   // CA <- s & ((r&¬m)[32:63]≠0)

//   // if n == 0: rA <- sign_extend(rS), XER[CA] = 0
//   // if n >= 32: rA <- 64 sign bits of rS, XER[CA] = sign bit of lo_32(rS)

//   GpVar v(c.newGpVar());
//   c.mov(v, f.LoadGPR(i.X.RT));
//   GpVar sh(c.newGpVar());
//   c.mov(sh, f.LoadGPR(i.X.RB));
//   c.and_(sh, imm(0x7F));

//   GpVar ca(c.newGpVar());
//   Label skip(c.newLabel());
//   Label full(c.newLabel());
//   c.test(sh, sh);
//   c.jnz(full);
//   {
//     // No shift, just a fancy sign extend and CA clearer.
//     c.cdqe(v);
//     c.mov(ca, imm(0));
//   }
//   c.jmp(skip);
//   c.bind(full);
//   {
//     // CA is set if any bits are shifted out of the right and if the result
//     // is negative. Start tracking that here.
//     c.mov(ca, v);
//     c.and_(ca, imm(~XEMASK(32 + i.X.RB, 64)));
//     c.cmp(ca, imm(0));
//     c.xor_(ca, ca);
//     c.setnz(ca.r8());

//     // Shift right and sign extend the 32bit part.
//     c.sar(v.r32(), imm(i.X.RB));
//     c.cdqe(v);

//     // CA is set to 1 if the low-order 32 bits of (RS) contain a negative number
//     // and any 1-bits are shifted out of position 63; otherwise CA is set to 0.
//     // We already have ca set to indicate the shift bits, now just and in sign.
//     GpVar ca_2(c.newGpVar());
//     c.mov(ca_2, v.r32());
//     c.shr(ca_2, imm(31));
//     c.and_(ca, ca_2);
//   }
//   c.bind(skip);

//   f.StoreGPR(i.X.RA, v);
//   e.update_xer_with_carry(ca);
//   if (i.X.Rc) {
//     f.UpdateCR(0, v);
//   }
//   return 0;
// }

XEEMITTER(srawix,       0x7C000670, X  )(PPCFunctionBuilder& f, InstrData& i) {
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
    ca = f.LoadZero(INT8_TYPE);
  } else {
    // CA is set if any bits are shifted out of the right and if the result
    // is negative. Start tracking that here.
    ca = f.IsTrue(
        f.And(v, f.LoadConstant((uint32_t)~XEMASK(32 + i.X.RB, 64))));

    // Shift right.
    v = f.Sha(v, (int8_t)i.X.RB),

    // CA is set to 1 if the low-order 32 bits of (RS) contain a negative number
    // and any 1-bits are shifted out of position 63; otherwise CA is set to 0.
    // We already have ca set to indicate the shift bits, now just and in sign.
    ca = f.And(ca, f.IsTrue(f.Shr(v, 31)));

    // Sign extend out to 64bit.
    v = f.SignExtend(v, INT64_TYPE);
  }
  f.StoreCA(ca);
  f.StoreGPR(i.X.RA, v);
  if (i.X.Rc) {
    f.UpdateCR(0, v);
  }
  return 0;
}


void RegisterEmitCategoryALU() {
  XEREGISTERINSTR(addx,         0x7C000214);
  XEREGISTERINSTR(addcx,        0X7C000014);
  XEREGISTERINSTR(addex,        0x7C000114);
  XEREGISTERINSTR(addi,         0x38000000);
  XEREGISTERINSTR(addic,        0x30000000);
  XEREGISTERINSTR(addicx,       0x34000000);
  XEREGISTERINSTR(addis,        0x3C000000);
  XEREGISTERINSTR(addmex,       0x7C0001D4);
  XEREGISTERINSTR(addzex,       0x7C000194);
  XEREGISTERINSTR(divdx,        0x7C0003D2);
  XEREGISTERINSTR(divdux,       0x7C000392);
  XEREGISTERINSTR(divwx,        0x7C0003D6);
  XEREGISTERINSTR(divwux,       0x7C000396);
  XEREGISTERINSTR(mulhdx,       0x7C000092);
  XEREGISTERINSTR(mulhdux,      0x7C000012);
  XEREGISTERINSTR(mulhwx,       0x7C000096);
  XEREGISTERINSTR(mulhwux,      0x7C000016);
  XEREGISTERINSTR(mulldx,       0x7C0001D2);
  XEREGISTERINSTR(mulli,        0x1C000000);
  XEREGISTERINSTR(mullwx,       0x7C0001D6);
  XEREGISTERINSTR(negx,         0x7C0000D0);
  XEREGISTERINSTR(subfx,        0x7C000050);
  XEREGISTERINSTR(subfcx,       0x7C000010);
  XEREGISTERINSTR(subficx,      0x20000000);
  XEREGISTERINSTR(subfex,       0x7C000110);
  XEREGISTERINSTR(subfmex,      0x7C0001D0);
  XEREGISTERINSTR(subfzex,      0x7C000190);
  XEREGISTERINSTR(cmp,          0x7C000000);
  XEREGISTERINSTR(cmpi,         0x2C000000);
  XEREGISTERINSTR(cmpl,         0x7C000040);
  XEREGISTERINSTR(cmpli,        0x28000000);
  XEREGISTERINSTR(andx,         0x7C000038);
  XEREGISTERINSTR(andcx,        0x7C000078);
  XEREGISTERINSTR(andix,        0x70000000);
  XEREGISTERINSTR(andisx,       0x74000000);
  XEREGISTERINSTR(cntlzdx,      0x7C000074);
  XEREGISTERINSTR(cntlzwx,      0x7C000034);
  XEREGISTERINSTR(eqvx,         0x7C000238);
  XEREGISTERINSTR(extsbx,       0x7C000774);
  XEREGISTERINSTR(extshx,       0x7C000734);
  XEREGISTERINSTR(extswx,       0x7C0007B4);
  XEREGISTERINSTR(nandx,        0x7C0003B8);
  XEREGISTERINSTR(norx,         0x7C0000F8);
  XEREGISTERINSTR(orx,          0x7C000378);
  XEREGISTERINSTR(orcx,         0x7C000338);
  XEREGISTERINSTR(ori,          0x60000000);
  XEREGISTERINSTR(oris,         0x64000000);
  XEREGISTERINSTR(xorx,         0x7C000278);
  XEREGISTERINSTR(xori,         0x68000000);
  XEREGISTERINSTR(xoris,        0x6C000000);
  // XEREGISTERINSTR(rld,          0x78000000);
  // -- // XEREGISTERINSTR(rldclx,       0x78000010);
  // -- // XEREGISTERINSTR(rldcrx,       0x78000012);
  // -- // XEREGISTERINSTR(rldicx,       0x78000008);
  // -- // XEREGISTERINSTR(rldiclx,      0x78000000);
  // -- // XEREGISTERINSTR(rldicrx,      0x78000004);
  // -- // XEREGISTERINSTR(rldimix,      0x7800000C);
  XEREGISTERINSTR(rlwimix,      0x50000000);
  XEREGISTERINSTR(rlwinmx,      0x54000000);
  XEREGISTERINSTR(rlwnmx,       0x5C000000);
  XEREGISTERINSTR(sldx,         0x7C000036);
  XEREGISTERINSTR(slwx,         0x7C000030);
  XEREGISTERINSTR(srdx,         0x7C000436);
  XEREGISTERINSTR(srwx,         0x7C000430);
  // XEREGISTERINSTR(sradx,        0x7C000634);
  // XEREGISTERINSTR(sradix,       0x7C000674);
  // XEREGISTERINSTR(srawx,        0x7C000630);
  XEREGISTERINSTR(srawix,       0x7C000670);
}


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy

/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "cpu/codegen/emit.h"

#include <llvm/IR/Intrinsics.h>

#include <xenia/cpu/codegen/function_generator.h>


using namespace llvm;
using namespace xe::cpu::codegen;
using namespace xe::cpu::ppc;


namespace xe {
namespace cpu {
namespace codegen {


// Integer arithmetic (A-3)

XEEMITTER(addx,         0x7C000214, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RD <- (RA) + (RB)

  if (i.XO.OE) {
    // With XER update.
    // This is a different codepath as we need to use llvm.sadd.with.overflow.

    Function* sadd_with_overflow = Intrinsic::getDeclaration(
        g.gen_module(), Intrinsic::sadd_with_overflow, b.getInt64Ty());
    Value* v = b.CreateCall2(sadd_with_overflow,
                             g.gpr_value(i.XO.RA), g.gpr_value(i.XO.RB));
    g.update_gpr_value(i.XO.RT, b.CreateExtractValue(v, 0));
    g.update_xer_with_overflow(b.CreateExtractValue(v, 1));

    if (i.XO.Rc) {
      // With cr0 update.
      g.update_cr_with_cond(0, v, b.getInt64(0), true);
    }

    return 0;
  } else {
    // No OE bit setting.
    Value* v = b.CreateAdd(g.gpr_value(i.XO.RA), g.gpr_value(i.XO.RB));
    g.update_gpr_value(i.XO.RT, v);

    if (i.XO.Rc) {
      // With cr0 update.
      g.update_cr_with_cond(0, v, b.getInt64(0), true);
    }

    return 0;
  }
}

XEEMITTER(addcx,        0X7C000014, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addex,        0x7C000114, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addi,         0x38000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   RT <- EXTS(SI)
  // else
  //   RT <- (RA) + EXTS(SI)

  Value* v = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    v = b.CreateAdd(g.gpr_value(i.D.RA), v);
  }
  g.update_gpr_value(i.D.RT, v);

  return 0;
}

XEEMITTER(addic,        0x30000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RT <- (RA) + EXTS(SI)

  Function* sadd_with_overflow = Intrinsic::getDeclaration(
      g.gen_module(), Intrinsic::sadd_with_overflow, b.getInt64Ty());
  Value* v = b.CreateCall2(sadd_with_overflow,
                           g.gpr_value(i.D.RA), b.getInt64(XEEXTS16(i.D.DS)));
  g.update_gpr_value(i.D.RT, b.CreateExtractValue(v, 0));
  g.update_xer_with_carry(b.CreateExtractValue(v, 1));

  return 0;
}

XEEMITTER(addicx,       0x34000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addis,        0x3C000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   RT <- EXTS(SI) || i16.0
  // else
  //   RT <- (RA) + EXTS(SI) || i16.0

  Value* v = b.getInt64(XEEXTS16(i.D.DS) << 16);
  if (i.D.RA) {
    v = b.CreateAdd(g.gpr_value(i.D.RA), v);
  }
  g.update_gpr_value(i.D.RT, v);

  return 0;
}

XEEMITTER(addmex,       0x7C0001D4, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addzex,       0x7C000194, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RT <- (RA) + CA

  Function* sadd_with_overflow = Intrinsic::getDeclaration(
      g.gen_module(), Intrinsic::sadd_with_overflow, b.getInt64Ty());
  Value* ca = b.CreateAnd(b.CreateLShr(g.xer_value(), 29), 0x1);
  Value* v = b.CreateCall2(sadd_with_overflow,
                           g.gpr_value(i.XO.RA), ca);
  Value* add_value = b.CreateExtractValue(v, 0);
  g.update_gpr_value(i.XO.RT, add_value);
  if (i.XO.OE) {
    // With XER[SO] update too.
    g.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  } else {
    // Just CA update.
    g.update_xer_with_carry(b.CreateExtractValue(v, 1));
  }

  if (i.XO.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, add_value, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(divdx,        0x7C0003D2, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(divdux,       0x7C000392, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(divwx,        0x7C0003D6, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(divwux,       0x7C000396, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // dividend[0:31] <- (RA)[32:63]
  // divisor[0:31] <- (RB)[32:63]
  // if divisor = 0 then
  //   if OE = 1 then
  //     XER[OV] <- 1
  //   return
  // RT[32:63] <- dividend ÷ divisor
  // RT[0:31] <- undefined

  Value* dividend = b.CreateTrunc(g.gpr_value(i.XO.RA), b.getInt32Ty());
  Value* divisor = b.CreateTrunc(g.gpr_value(i.XO.RB), b.getInt32Ty());

  // Note that we skip the zero handling block and just avoid the divide if
  // we are OE=0.
  BasicBlock* zero_bb = i.XO.OE ?
      BasicBlock::Create(*g.context(), "", g.gen_fn()) : NULL;
  BasicBlock* nonzero_bb = BasicBlock::Create(*g.context(), "", g.gen_fn());
  BasicBlock* after_bb = BasicBlock::Create(*g.context(), "", g.gen_fn());
  b.CreateCondBr(b.CreateICmpEQ(divisor, b.getInt32(0)),
                 i.XO.OE ? zero_bb : after_bb, nonzero_bb);

  if (zero_bb) {
    // Divisor was zero - do XER update.
    b.SetInsertPoint(zero_bb);
    g.update_xer_with_overflow(b.getInt1(1));
    b.CreateBr(after_bb);
  }

  // Divide.
  b.SetInsertPoint(nonzero_bb);
  Value* v = b.CreateUDiv(dividend, divisor);
  v = b.CreateZExt(v, b.getInt64Ty());
  g.update_gpr_value(i.XO.RT, v);

  // If we are OE=1 we need to clear the overflow bit.
  g.update_xer_with_overflow(b.getInt1(0));

  if (i.XO.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  b.CreateBr(after_bb);

  // Resume.
  b.SetInsertPoint(after_bb);

  return 0;
}

XEEMITTER(mulhdx,       0x7C000092, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhdux,      0x7C000012, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhwx,       0x7C000096, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhwux,      0x7C000016, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulldx,       0x7C0001D2, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulli,        0x1C000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // prod[0:127] <- (RA) × EXTS(SI)
  // RT <- prod[64:127]

  // TODO(benvanik): ensure this has the right behavior when the value
  // overflows. It should be truncating the result, but I'm not sure what LLVM
  // does.

  Value* v = b.CreateMul(g.gpr_value(i.D.RA), b.getInt64(XEEXTS16(i.D.DS)));
  g.update_gpr_value(i.D.RT, b.CreateTrunc(v, b.getInt64Ty()));

  return 0;
}

XEEMITTER(mullwx,       0x7C0001D6, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RT <- (RA)[32:63] × (RB)[32:63]

  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  Value* v = b.CreateMul(b.CreateSExt(g.gpr_value(i.XO.RA), b.getInt64Ty()),
                         b.CreateSExt(g.gpr_value(i.XO.RB), b.getInt64Ty()));
  g.update_gpr_value(i.XO.RT, v);

  if (i.XO.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(negx,         0x7C0000D0, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(subfx,        0x7C000050, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RT <- ¬(RA) + (RB) + 1

  if (i.XO.OE) {
    // With XER update.
    // This is a different codepath as we need to use llvm.ssub.with.overflow.

    Function* ssub_with_overflow = Intrinsic::getDeclaration(
        g.gen_module(), Intrinsic::ssub_with_overflow, b.getInt64Ty());
    Value* v = b.CreateCall2(ssub_with_overflow,
                             g.gpr_value(i.XO.RB), g.gpr_value(i.XO.RA));
    g.update_gpr_value(i.XO.RT, b.CreateExtractValue(v, 0));
    g.update_xer_with_overflow(b.CreateExtractValue(v, 1));

    if (i.XO.Rc) {
      // With cr0 update.
      g.update_cr_with_cond(0, v, b.getInt64(0), true);
    }

    return 0;
  } else {
    // No OE bit setting.
    Value* v = b.CreateSub(g.gpr_value(i.XO.RB), g.gpr_value(i.XO.RA));
    g.update_gpr_value(i.XO.RT, v);

    if (i.XO.Rc) {
      // With cr0 update.
      g.update_cr_with_cond(0, v, b.getInt64(0), true);
    }

    return 0;
  }
}

XEEMITTER(subfcx,       0x7C000010, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(subficx,      0x20000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RT <- ¬(RA) + EXTS(SI) + 1

  Function* ssub_with_overflow = Intrinsic::getDeclaration(
      g.gen_module(), Intrinsic::ssub_with_overflow, b.getInt64Ty());
  Value* v = b.CreateCall2(ssub_with_overflow,
                           b.getInt64(XEEXTS16(i.D.DS)), g.gpr_value(i.D.RA));
  g.update_gpr_value(i.D.RT, b.CreateExtractValue(v, 0));
  g.update_xer_with_carry(b.CreateExtractValue(v, 1));

  return 0;
}

XEEMITTER(subfex,       0x7C000110, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RT <- ¬(RA) + (RB) + CA

  // TODO(benvanik): possible that the add of rb+ca needs to also check for
  //     overflow!

  Value* ca = b.CreateAnd(b.CreateLShr(g.xer_value(), 29), 0x1);
  Function* uadd_with_overflow = Intrinsic::getDeclaration(
      g.gen_module(), Intrinsic::uadd_with_overflow, b.getInt64Ty());
  Value* v = b.CreateCall2(uadd_with_overflow,
                           b.CreateNot(g.gpr_value(i.XO.RA)),
                           b.CreateAdd(g.gpr_value(i.XO.RB), ca));
  g.update_gpr_value(i.XO.RT, b.CreateExtractValue(v, 0));

  if (i.XO.OE) {
    // With XER update.
    g.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  } else {
    g.update_xer_with_carry(b.CreateExtractValue(v, 1));
  }

  if (i.XO.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(subfmex,      0x7C0001D0, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(subfzex,      0x7C000190, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer compare (A-4)

XEEMITTER(cmp,          0x7C000000, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
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

  Value* lhs = g.gpr_value(i.X.RA);
  Value* rhs = g.gpr_value(i.X.RB);
  if (!L) {
    // 32-bit - truncate and sign extend.
    lhs = b.CreateTrunc(lhs, b.getInt32Ty());
    lhs = b.CreateSExt(lhs, b.getInt64Ty());
    rhs = b.CreateTrunc(rhs, b.getInt32Ty());
    rhs = b.CreateSExt(rhs, b.getInt64Ty());
  }

  g.update_cr_with_cond(BF, lhs, rhs, true);

  return 0;
}

XEEMITTER(cmpi,         0x2C000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
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

  Value* lhs = g.gpr_value(i.D.RA);
  if (!L) {
    // 32-bit - truncate and sign extend.
    lhs = b.CreateTrunc(lhs, b.getInt32Ty());
    lhs = b.CreateSExt(lhs, b.getInt64Ty());
  }

  Value* rhs = b.getInt64(XEEXTS16(i.D.DS));
  g.update_cr_with_cond(BF, lhs, rhs, true);

  return 0;
}

XEEMITTER(cmpl,         0x7C000040, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
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

  Value* lhs = g.gpr_value(i.X.RA);
  Value* rhs = g.gpr_value(i.X.RB);
  if (!L) {
    // 32-bit - truncate and zero extend.
    lhs = b.CreateTrunc(lhs, b.getInt32Ty());
    lhs = b.CreateZExt(lhs, b.getInt64Ty());
    rhs = b.CreateTrunc(rhs, b.getInt32Ty());
    rhs = b.CreateZExt(rhs, b.getInt64Ty());
  }

  g.update_cr_with_cond(BF, lhs, rhs, false);

  return 0;
}

XEEMITTER(cmpli,        0x28000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
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

  Value* lhs = g.gpr_value(i.D.RA);
  if (!L) {
    // 32-bit - truncate and zero extend.
    lhs = b.CreateTrunc(lhs, b.getInt32Ty());
    lhs = b.CreateZExt(lhs, b.getInt64Ty());
  }

  Value* rhs = b.getInt64(i.D.DS);
  g.update_cr_with_cond(BF, lhs, rhs, false);

  return 0;
}


// Integer logical (A-5)

XEEMITTER(andx,         0x7C000038, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) & (RB)

  Value* v = b.CreateAnd(g.gpr_value(i.X.RT), g.gpr_value(i.X.RB));
  g.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(andcx,        0x7C000078, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) & ¬(RB)

  Value* v = b.CreateXor(g.gpr_value(i.X.RB), -1);
  v = b.CreateAnd(g.gpr_value(i.X.RT), v);
  g.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(andix,        0x70000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) & (i48.0 || UI)

  Value* v = b.CreateAnd(g.gpr_value(i.D.RT), (uint64_t)i.D.DS);
  g.update_gpr_value(i.D.RA, v);

  // With cr0 update.
  g.update_cr_with_cond(0, v, b.getInt64(0), true);

  return 1;
}

XEEMITTER(andisx,       0x74000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) & (i32.0 || UI || i16.0)

  Value* v = b.CreateAnd(g.gpr_value(i.D.RT), ((uint64_t)i.D.DS) << 16);
  g.update_gpr_value(i.D.RA, v);

  // With cr0 update.
  g.update_cr_with_cond(0, v, b.getInt64(0), true);

  return 1;
}

XEEMITTER(cntlzdx,      0x7C000074, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(cntlzwx,      0x7C000034, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // n <- 32
  // do while n < 64
  //   if (RS) = 1 then leave n
  //   n <- n + 1
  // RA <- n - 32

  Value* v = g.gpr_value(i.X.RT);
  v = b.CreateTrunc(v, b.getInt32Ty());

  std::vector<Type*> arg_types;
  arg_types.push_back(b.getInt32Ty());
  Function* ctlz = Intrinsic::getDeclaration(
      g.gen_fn()->getParent(), Intrinsic::ctlz, arg_types);
  Value* count = b.CreateCall2(ctlz, v, b.getInt1(1));

  count = b.CreateZExt(count, b.getInt64Ty());
  g.update_gpr_value(i.X.RA, count);

  if (i.X.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, count, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(eqvx,         0x7C000238, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(extsbx,       0x7C000774, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // s <- (RS)[56]
  // RA[56:63] <- (RS)[56:63]
  // RA[0:55] <- i56.s

  Value* v = g.gpr_value(i.X.RT);
  v = b.CreateTrunc(v, b.getInt8Ty());
  v = b.CreateSExt(v, b.getInt64Ty());
  g.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // Update cr0.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(extshx,       0x7C000734, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(extswx,       0x7C0007B4, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(nandx,        0x7C0003B8, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(norx,         0x7C0000F8, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- ¬((RS) | (RB))

  Value* v = b.CreateOr(g.gpr_value(i.X.RT), g.gpr_value(i.X.RB));
  v = b.CreateXor(v, -1);
  g.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(orx,          0x7C000378, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) | (RB)

  Value* v = b.CreateOr(g.gpr_value(i.X.RT), g.gpr_value(i.X.RB));
  g.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(orcx,         0x7C000338, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(ori,          0x60000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) | (i48.0 || UI)

  Value* v = b.CreateOr(g.gpr_value(i.D.RT), (uint64_t)i.D.DS);
  g.update_gpr_value(i.D.RA, v);

  return 0;
}

XEEMITTER(oris,         0x64000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) | (i32.0 || UI || i16.0)

  Value* v = b.CreateOr(g.gpr_value(i.D.RT), ((uint64_t)i.D.DS) << 16);
  g.update_gpr_value(i.D.RA, v);

  return 0;
}

XEEMITTER(xorx,         0x7C000278, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) XOR (RB)

  Value* v = b.CreateXor(g.gpr_value(i.X.RT), g.gpr_value(i.X.RB));
  g.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(xori,         0x68000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) XOR (i48.0 || UI)

  Value* v = b.CreateXor(g.gpr_value(i.D.RT), (uint64_t)i.D.DS);
  g.update_gpr_value(i.D.RA, v);

  return 0;
}

XEEMITTER(xoris,        0x6C000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) XOR (i32.0 || UI || i16.0)

  Value* v = b.CreateXor(g.gpr_value(i.D.RT), ((uint64_t)i.D.DS) << 16);
  g.update_gpr_value(i.D.RA, v);

  return 0;
}


// Integer rotate (A-6)

XEEMITTER(rldclx,       0x78000010, MDS)(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rldcrx,       0x78000012, MDS)(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rldicx,       0x78000008, MD )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rldiclx,      0x78000000, MD )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // n <- sh[5] || sh[0:4]
  // r <- ROTL64((RS), n)
  // b <- mb[5] || mb[0:4]
  // m <- MASK(b, 63)
  // RA <- r & m

  // uint32_t sh = (i.MD.SH5 << 5) | i.MD.SH;
  // uint32_t mb = (i.MD.MB5 << 5) | i.MD.MB;

  // Value* v = g.gpr_value(i.MD.RS);
  // if (sh) {
  //   v = // rotate by sh
  // }
  // if (mb) {
  //   v = // mask b mb->63
  // }
  // g.update_gpr_value(i.MD.RA, v);

  // if (i.MD.Rc) {
  //   // With cr0 update.
  //   g.update_cr_with_cond(0, v, b.getInt64(0), true);
  // }

  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rldicrx,      0x78000004, MD )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rldimix,      0x7800000C, MD )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rlwimix,      0x50000000, M  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r&m | (RA)&¬m
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rlwinmx,      0x54000000, M  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r & m

  // The compiler will generate a bunch of these for the special case of
  // SH=0, MB=ME
  // Which seems to just select a single bit and set cr0 for use with a branch.
  // We can detect this and do less work.
  if (!i.M.SH && i.M.MB == i.M.ME) {
    Value* v = b.CreateAnd(g.gpr_value(i.M.RS), 1 << i.M.MB);
    g.update_gpr_value(i.M.RA, v);
    if (i.M.Rc) {
      // With cr0 update.
      g.update_cr_with_cond(0, v, b.getInt64(0), true);
    }
    return 0;
  }

  // // ROTL32(x, y) = rotl(i64.(x||x), y)
  // Value* v = b.CreateZExt(b.CreateTrunc(g.gpr_value(i.M.RS)), b.getInt64Ty());
  // v = b.CreateOr(b.CreateLShr(v, 32), v);
  // // (v << shift) | (v >> (64 - shift));
  // v = b.CreateOr(b.CreateShl(v, i.M.SH), b.CreateLShr(v, 32 - i.M.SH));
  // v = b.CreateAnd(v, XEMASK(i.M.MB + 32, i.M.ME + 32));

  // if (i.M.Rc) {
  //   // With cr0 update.
  //   g.update_cr_with_cond(0, v, b.getInt64(0), true);
  // }

  printf("rlwinmx %d %d %d\n", i.M.SH, i.M.MB, i.M.ME);
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rlwnmx,       0x5C000000, M  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer shift (A-7)

XEEMITTER(sldx,         0x7C000036, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(slwx,         0x7C000030, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], n)
  // if (RB)[58] = 0 then
  //   m <- MASK(32, 63-n)
  // else
  //   m <- i64.0
  // RA <- r & m

  Value* v = b.CreateShl(g.gpr_value(i.X.RT), g.gpr_value(i.X.RB));
  v = b.CreateAnd(v, UINT32_MAX);
  g.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(sradx,        0x7C000634, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(sradix,       0x7C000674, XS )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(srawx,        0x7C000630, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(srawix,       0x7C000670, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], 64-n)
  // m <- MASK(n+32, 63)
  // s <- (RS)[32]
  // RA <- r&m | (i64.s)&¬m
  // CA <- s & ((r&¬m)[32:63]≠0)

  Value* rs64 = g.gpr_value(i.X.RT);
  Value* rs32 = b.CreateTrunc(rs64, b.getInt32Ty());

  Value* v;
  Value* ca;
  if (!i.X.RB) {
    // No shift, just a fancy sign extend and CA clearer.
    v = rs32;
    ca = b.getInt64(0);
  } else {
    v = b.CreateAShr(rs32, i.X.RB);

    // CA is set to 1 if the low-order 32 bits of (RS) contain a negative number
    // and any 1-bits are shifted out of position 63; otherwise CA is set to 0.
    ca = b.CreateAnd(b.CreateICmpSLT(v, b.getInt32(0)),
                     b.CreateICmpSLT(rs64, b.getInt64(0)));
  }
  v = b.CreateSExt(v, b.getInt64Ty());
  g.update_gpr_value(i.X.RA, v);
  g.update_xer_with_carry(ca);

  if (i.X.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEEMITTER(srdx,         0x7C000436, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(srwx,         0x7C000430, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


void RegisterEmitCategoryALU() {
  XEREGISTEREMITTER(addx,         0x7C000214);
  XEREGISTEREMITTER(addcx,        0X7C000014);
  XEREGISTEREMITTER(addex,        0x7C000114);
  XEREGISTEREMITTER(addi,         0x38000000);
  XEREGISTEREMITTER(addic,        0x30000000);
  XEREGISTEREMITTER(addicx,       0x34000000);
  XEREGISTEREMITTER(addis,        0x3C000000);
  XEREGISTEREMITTER(addmex,       0x7C0001D4);
  XEREGISTEREMITTER(addzex,       0x7C000194);
  XEREGISTEREMITTER(divdx,        0x7C0003D2);
  XEREGISTEREMITTER(divdux,       0x7C000392);
  XEREGISTEREMITTER(divwx,        0x7C0003D6);
  XEREGISTEREMITTER(divwux,       0x7C000396);
  XEREGISTEREMITTER(mulhdx,       0x7C000092);
  XEREGISTEREMITTER(mulhdux,      0x7C000012);
  XEREGISTEREMITTER(mulhwx,       0x7C000096);
  XEREGISTEREMITTER(mulhwux,      0x7C000016);
  XEREGISTEREMITTER(mulldx,       0x7C0001D2);
  XEREGISTEREMITTER(mulli,        0x1C000000);
  XEREGISTEREMITTER(mullwx,       0x7C0001D6);
  XEREGISTEREMITTER(negx,         0x7C0000D0);
  XEREGISTEREMITTER(subfx,        0x7C000050);
  XEREGISTEREMITTER(subfcx,       0x7C000010);
  XEREGISTEREMITTER(subficx,      0x20000000);
  XEREGISTEREMITTER(subfex,       0x7C000110);
  XEREGISTEREMITTER(subfmex,      0x7C0001D0);
  XEREGISTEREMITTER(subfzex,      0x7C000190);
  XEREGISTEREMITTER(cmp,          0x7C000000);
  XEREGISTEREMITTER(cmpi,         0x2C000000);
  XEREGISTEREMITTER(cmpl,         0x7C000040);
  XEREGISTEREMITTER(cmpli,        0x28000000);
  XEREGISTEREMITTER(andx,         0x7C000038);
  XEREGISTEREMITTER(andcx,        0x7C000078);
  XEREGISTEREMITTER(andix,        0x70000000);
  XEREGISTEREMITTER(andisx,       0x74000000);
  XEREGISTEREMITTER(cntlzdx,      0x7C000074);
  XEREGISTEREMITTER(cntlzwx,      0x7C000034);
  XEREGISTEREMITTER(eqvx,         0x7C000238);
  XEREGISTEREMITTER(extsbx,       0x7C000774);
  XEREGISTEREMITTER(extshx,       0x7C000734);
  XEREGISTEREMITTER(extswx,       0x7C0007B4);
  XEREGISTEREMITTER(nandx,        0x7C0003B8);
  XEREGISTEREMITTER(norx,         0x7C0000F8);
  XEREGISTEREMITTER(orx,          0x7C000378);
  XEREGISTEREMITTER(orcx,         0x7C000338);
  XEREGISTEREMITTER(ori,          0x60000000);
  XEREGISTEREMITTER(oris,         0x64000000);
  XEREGISTEREMITTER(xorx,         0x7C000278);
  XEREGISTEREMITTER(xori,         0x68000000);
  XEREGISTEREMITTER(xoris,        0x6C000000);
  XEREGISTEREMITTER(rldclx,       0x78000010);
  XEREGISTEREMITTER(rldcrx,       0x78000012);
  XEREGISTEREMITTER(rldicx,       0x78000008);
  XEREGISTEREMITTER(rldiclx,      0x78000000);
  XEREGISTEREMITTER(rldicrx,      0x78000004);
  XEREGISTEREMITTER(rldimix,      0x7800000C);
  XEREGISTEREMITTER(rlwimix,      0x50000000);
  XEREGISTEREMITTER(rlwinmx,      0x54000000);
  XEREGISTEREMITTER(rlwnmx,       0x5C000000);
  XEREGISTEREMITTER(sldx,         0x7C000036);
  XEREGISTEREMITTER(slwx,         0x7C000030);
  XEREGISTEREMITTER(sradx,        0x7C000634);
  XEREGISTEREMITTER(sradix,       0x7C000674);
  XEREGISTEREMITTER(srawx,        0x7C000630);
  XEREGISTEREMITTER(srawix,       0x7C000670);
  XEREGISTEREMITTER(srdx,         0x7C000436);
  XEREGISTEREMITTER(srwx,         0x7C000430);
}


}  // namespace codegen
}  // namespace cpu
}  // namespace xe

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

XEDISASMR(addx,         0x7C000214, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("add", "Add",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(addx,         0x7C000214, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RD <- (RA) + (RB)

  if (i.XO.OE) {
    // With XER update.
    // This is a different codepath as we need to use llvm.sadd.with.overflow.

    Function* sadd_with_overflow = Intrinsic::getDeclaration(
        g.gen_module(), Intrinsic::sadd_with_overflow, b.getInt64Ty());
    Value* v = b.CreateCall2(sadd_with_overflow,
                             g.gpr_value(i.XO.RA), g.gpr_value(i.XO.RB));
    Value* v0 = b.CreateExtractValue(v, 0);
    g.update_gpr_value(i.XO.RT, v0);
    g.update_xer_with_overflow(b.CreateExtractValue(v, 1));

    if (i.XO.Rc) {
      // With cr0 update.
      g.update_cr_with_cond(0, v0, b.getInt64(0), true);
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

XEDISASMR(addcx,        0x7C000014, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("addcx", "Add Carrying",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(addcx,        0x7C000014, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(addex,        0x7C000114, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("adde", "Add Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(addex,        0x7C000114, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(addi,         0x38000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("addi", "Add Immediate", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddSImmOperand(0, 4);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
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

XEDISASMR(addic,        0x30000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("addic", "Add Immediate Carrying", InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
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

XEDISASMR(addicx,       0x34000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("addic", "Add Immediate Carrying and Record",
         InstrDisasm::kRc | InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(addicx,       0x34000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(addis,        0x3C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("addis", "Add Immediate Shifted", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddSImmOperand(0, 4);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
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

XEDISASMR(addmex,       0x7C0001D4, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("addme", "Add to Minus One Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(addmex,       0x7C0001D4, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(addzex,       0x7C000194, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("addze", "Add to Zero Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(divdx,        0x7C0003D2, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("divd", "Divide Doubleword",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(divdx,        0x7C0003D2, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(divdux,       0x7C000392, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("divdu", "Divide Doubleword Unsigned",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(divdux,       0x7C000392, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(divwx,        0x7C0003D6, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("divw", "Divide Word",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(divwx,        0x7C0003D6, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(divwux,       0x7C000396, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("divwu", "Divide Word Unsigned",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
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
  if (i.XO.OE) {
    g.update_xer_with_overflow(b.getInt1(0));
  }

  if (i.XO.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  b.CreateBr(after_bb);

  // Resume.
  b.SetInsertPoint(after_bb);

  return 0;
}

XEDISASMR(mulhdx,       0x7C000092, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mulhd", "Multiply High Doubleword", i.XO.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(mulhdx,       0x7C000092, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mulhdux,      0x7C000012, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mulhdu", "Multiply High Doubleword Unsigned",
         i.XO.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(mulhdux,      0x7C000012, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mulhwx,       0x7C000096, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mulhw", "Multiply High Word", i.XO.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(mulhwx,       0x7C000096, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mulhwux,      0x7C000016, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mulhwu", "Multiply High Word Unsigned",
         i.XO.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(mulhwux,      0x7C000016, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mulldx,       0x7C0001D2, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mulld", "Multiply Low Doubleword",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(mulldx,       0x7C0001D2, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mulli,        0x1C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("mulli", "Multiply Low Immediate", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
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

XEDISASMR(mullwx,       0x7C0001D6, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mullw", "Multiply Low Word",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(negx,         0x7C0000D0, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("neg", "Negate",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(negx,         0x7C0000D0, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(subfx,        0x7C000050, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("subf", "Subtract From",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
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
    Value* v0 = b.CreateExtractValue(v, 0);
    g.update_gpr_value(i.XO.RT, v0);
    g.update_xer_with_overflow(b.CreateExtractValue(v, 1));

    if (i.XO.Rc) {
      // With cr0 update.
      g.update_cr_with_cond(0, v0, b.getInt64(0), true);
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

XEDISASMR(subfcx,       0x7C000010, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("subfc", "Subtract From Carrying",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(subfcx,       0x7C000010, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(subficx,      0x20000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("subfic", "Subtract From Immediate Carrying", InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(subfex,       0x7C000110, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("subfe", "Subtract From Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(subfex,       0x7C000110, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RT <- ¬(RA) + (RB) + CA

  // TODO(benvanik): possible that the add of rb+ca needs to also check for
  //     overflow!

  Value* ca = b.CreateAnd(b.CreateLShr(g.xer_value(), 29), 0x1);
  Function* uadd_with_overflow = Intrinsic::getDeclaration(
      g.gen_module(), Intrinsic::uadd_with_overflow, b.getInt64Ty());
  Value* v = b.CreateCall2(uadd_with_overflow,
                           b.CreateNeg(g.gpr_value(i.XO.RA)),
                           b.CreateAdd(g.gpr_value(i.XO.RB), ca));
  Value* v0 = b.CreateExtractValue(v, 0);
  g.update_gpr_value(i.XO.RT, v0);

  if (i.XO.OE) {
    // With XER update.
    g.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  } else {
    g.update_xer_with_carry(b.CreateExtractValue(v, 1));
  }

  if (i.XO.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v0, b.getInt64(0), true);
  }

  return 0;
}

XEDISASMR(subfmex,      0x7C0001D0, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("subfme", "Subtract From Minus One Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(subfmex,      0x7C0001D0, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(subfzex,      0x7C000190, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("subfze", "Subtract From Zero Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(subfzex,      0x7C000190, XO )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer compare (A-4)

XEDISASMR(cmp,          0x7C000000, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("cmp", "Compare", 0);
  d.AddCR(i.X.RT >> 2, InstrRegister::kWrite);
  d.AddUImmOperand(i.X.RT >> 2, 1);
  d.AddUImmOperand(i.X.RT & 1, 1);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
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

XEDISASMR(cmpi,         0x2C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("cmpi", "Compare Immediate", 0);
  d.AddCR(i.D.RT >> 2, InstrRegister::kWrite);
  d.AddUImmOperand(i.D.RT >> 2, 1);
  d.AddUImmOperand(i.D.RT & 1, 1);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
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

XEDISASMR(cmpl,         0x7C000040, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("cmpl", "Compare Logical", 0);
  d.AddCR(i.X.RT >> 2, InstrRegister::kWrite);
  d.AddUImmOperand(i.X.RT >> 2, 1);
  d.AddUImmOperand(i.X.RT & 1, 1);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(cmpli,        0x28000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("cmpli", "Compare Logical Immediate", 0);
  d.AddCR(i.D.RT >> 2, InstrRegister::kWrite);
  d.AddUImmOperand(i.D.RT >> 2, 1);
  d.AddUImmOperand(i.D.RT & 1, 1);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(i.D.DS, 2);
  return d.Finish();
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

XEDISASMR(andx,         0x7C000038, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("and", "AND", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
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

XEDISASMR(andcx,        0x7C000078, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("andc", "AND with Complement", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(andix,        0x70000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("andi", "AND Immediate", 0);
  d.AddCR(0, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.D.DS, 2);
  return d.Finish();
}
XEEMITTER(andix,        0x70000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) & (i48.0 || UI)

  Value* v = b.CreateAnd(g.gpr_value(i.D.RT), (uint64_t)i.D.DS);
  g.update_gpr_value(i.D.RA, v);

  // With cr0 update.
  g.update_cr_with_cond(0, v, b.getInt64(0), true);

  return 0;
}

XEDISASMR(andisx,       0x74000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("andi", "AND Immediate Shifted", 0);
  d.AddCR(0, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.D.DS, 2);
  return d.Finish();
}
XEEMITTER(andisx,       0x74000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) & (i32.0 || UI || i16.0)

  Value* v = b.CreateAnd(g.gpr_value(i.D.RT), ((uint64_t)i.D.DS) << 16);
  g.update_gpr_value(i.D.RA, v);

  // With cr0 update.
  g.update_cr_with_cond(0, v, b.getInt64(0), true);

  return 1;
}

XEDISASMR(cntlzdx,      0x7C000074, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("cntlzd", "Count Leading Zeros Doubleword",
         i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(cntlzdx,      0x7C000074, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(cntlzwx,      0x7C000034, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("cntlzw", "Count Leading Zeros Word",
         i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(eqvx,         0x7C000238, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("eqv", "Equivalent", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(eqvx,         0x7C000238, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(extsbx,       0x7C000774, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("extsb", "Extend Sign Byte", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(extshx,       0x7C000734, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("extsh", "Extend Sign Halfword", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(extshx,       0x7C000734, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(extswx,       0x7C0007B4, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("extsw", "Extend Sign Word", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(extswx,       0x7C0007B4, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(nandx,        0x7C0003B8, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("nand", "NAND", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(nandx,        0x7C0003B8, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(norx,         0x7C0000F8, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("nor", "NOR", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(orx,          0x7C000378, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("or", "OR", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(orcx,         0x7C000338, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("orc", "OR with Complement", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(orcx,         0x7C000338, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(ori,          0x60000000, D  )(InstrData& i, InstrDisasm& d) {
  if (!i.D.RA && !i.D.RT && !i.D.DS) {
    d.Init("no-op", "OR Immediate", 0);
  } else {
    d.Init("ori", "OR Immediate", 0);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.D.DS, 2);
  return d.Finish();
}
XEEMITTER(ori,          0x60000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) | (i48.0 || UI)

  Value* v = b.CreateOr(g.gpr_value(i.D.RT), (uint64_t)i.D.DS);
  g.update_gpr_value(i.D.RA, v);

  return 0;
}

XEDISASMR(oris,         0x64000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("oris", "OR Immediate Shifted", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.D.DS, 2);
  return d.Finish();
}
XEEMITTER(oris,         0x64000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) | (i32.0 || UI || i16.0)

  Value* v = b.CreateOr(g.gpr_value(i.D.RT), ((uint64_t)i.D.DS) << 16);
  g.update_gpr_value(i.D.RA, v);

  return 0;
}

XEDISASMR(xorx,         0x7C000278, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("xor", "XOR", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(xori,         0x68000000, D  )(InstrData& i, InstrDisasm& d) {
  if (!i.D.RA && !i.D.RT && !i.D.DS) {
    d.Init("xnop", "XOR Immediate", 0);
  } else {
    d.Init("xori", "XOR Immediate", 0);
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
    d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
    d.AddUImmOperand(i.D.DS, 2);
  }
  return d.Finish();
}
XEEMITTER(xori,         0x68000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) XOR (i48.0 || UI)

  Value* v = b.CreateXor(g.gpr_value(i.D.RT), (uint64_t)i.D.DS);
  g.update_gpr_value(i.D.RA, v);

  return 0;
}

XEDISASMR(xoris,        0x6C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("xoris", "XOR Immediate Shifted", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.D.DS, 2);
  return d.Finish();
}
XEEMITTER(xoris,        0x6C000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // RA <- (RS) XOR (i32.0 || UI || i16.0)

  Value* v = b.CreateXor(g.gpr_value(i.D.RT), ((uint64_t)i.D.DS) << 16);
  g.update_gpr_value(i.D.RA, v);

  return 0;
}


// Integer rotate (A-6)

XEDISASMR(rldclx,       0x78000010, MDS)(InstrData& i, InstrDisasm& d) {
  d.Init("rldcl", "Rotate Left Doubleword then Clear Left",
         i.MDS.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.MDS.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.MDS.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.MDS.RB, InstrRegister::kRead);
  d.AddUImmOperand((i.MDS.MB5 << 5) | i.MDS.MB, 1);
  return d.Finish();
}
XEEMITTER(rldclx,       0x78000010, MDS)(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(rldcrx,       0x78000012, MDS)(InstrData& i, InstrDisasm& d) {
  d.Init("rldcr", "Rotate Left Doubleword then Clear Right",
         i.MDS.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.MDS.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.MDS.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.MDS.RB, InstrRegister::kRead);
  d.AddUImmOperand((i.MDS.MB5 << 5) | i.MDS.MB, 1);
  return d.Finish();
}
XEEMITTER(rldcrx,       0x78000012, MDS)(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(rldicx,       0x78000008, MD )(InstrData& i, InstrDisasm& d) {
  d.Init("rldic", "Rotate Left Doubleword Immediate then Clear",
         i.MD.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.MD.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.MD.RT, InstrRegister::kRead);
  d.AddUImmOperand((i.MD.SH5 << 5) | i.MD.SH, 1);
  d.AddUImmOperand((i.MD.MB5 << 5) | i.MD.MB, 1);
  return d.Finish();
}
XEEMITTER(rldicx,       0x78000008, MD )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(rldiclx,      0x78000000, MD )(InstrData& i, InstrDisasm& d) {
  d.Init("rldicl", "Rotate Left Doubleword Immediate then Clear Left",
         i.MD.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.MD.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.MD.RT, InstrRegister::kRead);
  d.AddUImmOperand((i.MD.SH5 << 5) | i.MD.SH, 1);
  d.AddUImmOperand((i.MD.MB5 << 5) | i.MD.MB, 1);
  return d.Finish();
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

XEDISASMR(rldicrx,      0x78000004, MD )(InstrData& i, InstrDisasm& d) {
  d.Init("rldicr", "Rotate Left Doubleword Immediate then Clear Right",
         i.MD.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.MD.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.MD.RT, InstrRegister::kRead);
  d.AddUImmOperand((i.MD.SH5 << 5) | i.MD.SH, 1);
  d.AddUImmOperand((i.MD.MB5 << 5) | i.MD.MB, 1);
  return d.Finish();
}
XEEMITTER(rldicrx,      0x78000004, MD )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(rldimix,      0x7800000C, MD )(InstrData& i, InstrDisasm& d) {
  d.Init("rldimi", "Rotate Left Doubleword Immediate then Mask Insert",
         i.MD.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.MD.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.MD.RT, InstrRegister::kRead);
  d.AddUImmOperand((i.MD.SH5 << 5) | i.MD.SH, 1);
  d.AddUImmOperand((i.MD.MB5 << 5) | i.MD.MB, 1);
  return d.Finish();
}
XEEMITTER(rldimix,      0x7800000C, MD )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(rlwimix,      0x50000000, M  )(InstrData& i, InstrDisasm& d) {
  d.Init("rlwimi", "Rotate Left Word Immediate then Mask Insert",
         i.M.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.M.SH, 1);
  d.AddUImmOperand(i.M.MB, 1);
  d.AddUImmOperand(i.M.ME, 1);
  return d.Finish();
}
XEEMITTER(rlwimix,      0x50000000, M  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r&m | (RA)&¬m

  // ROTL32(x, y) = rotl(i64.(x||x), y)
  Value* v = b.CreateAnd(g.gpr_value(i.M.RT), UINT32_MAX);
  v = b.CreateOr(b.CreateShl(v, 32), v);
  // (v << shift) | (v >> (32 - shift));
  v = b.CreateOr(b.CreateShl(v, i.M.SH), b.CreateLShr(v, 32 - i.M.SH));
  uint64_t m = XEMASK(i.M.MB + 32, i.M.ME + 32);
  v = b.CreateAnd(v, m);
  v = b.CreateOr(v, b.CreateAnd(g.gpr_value(i.M.RA), ~m));
  g.update_gpr_value(i.M.RA, v);

  if (i.M.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEDISASMR(rlwinmx,      0x54000000, M  )(InstrData& i, InstrDisasm& d) {
  d.Init("rlwinm", "Rotate Left Word Immediate then AND with Mask",
         i.M.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.M.SH, 1);
  d.AddUImmOperand(i.M.MB, 1);
  d.AddUImmOperand(i.M.ME, 1);
  return d.Finish();
}
XEEMITTER(rlwinmx,      0x54000000, M  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r & m

  // The compiler will generate a bunch of these for the special case of SH=0.
  // Which seems to just select some bits and set cr0 for use with a branch.
  // We can detect this and do less work.
  if (!i.M.SH) {
    Value* v = b.CreateAnd(
        b.CreateTrunc(g.gpr_value(i.M.RT), b.getInt32Ty()),
        b.getInt32((uint32_t)XEMASK(i.M.MB + 32, i.M.ME + 32)));
    v = b.CreateZExt(v, b.getInt64Ty());
    g.update_gpr_value(i.M.RA, v);
    if (i.M.Rc) {
      // With cr0 update.
      g.update_cr_with_cond(0, v, b.getInt64(0), true);
    }
    return 0;
  }

  // ROTL32(x, y) = rotl(i64.(x||x), y)
  Value* v = b.CreateAnd(g.gpr_value(i.M.RT), UINT32_MAX);
  v = b.CreateOr(b.CreateShl(v, 32), v);
  // (v << shift) | (v >> (32 - shift));
  v = b.CreateOr(b.CreateShl(v, i.M.SH), b.CreateLShr(v, 32 - i.M.SH));
  v = b.CreateAnd(v, XEMASK(i.M.MB + 32, i.M.ME + 32));
  g.update_gpr_value(i.M.RA, v);

  if (i.M.Rc) {
    // With cr0 update.
    g.update_cr_with_cond(0, v, b.getInt64(0), true);
  }

  return 0;
}

XEDISASMR(rlwnmx,       0x5C000000, M  )(InstrData& i, InstrDisasm& d) {
  d.Init("rlwnm", "Rotate Left Word then AND with Mask",
         i.M.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.M.SH, InstrRegister::kRead);
  d.AddUImmOperand(i.M.MB, 1);
  d.AddUImmOperand(i.M.ME, 1);
  return d.Finish();
}
XEEMITTER(rlwnmx,       0x5C000000, M  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer shift (A-7)

XEDISASMR(sldx,         0x7C000036, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("sld", "Shift Left Doubleword", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(sldx,         0x7C000036, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(slwx,         0x7C000030, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("slw", "Shift Left Word", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
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

XEDISASMR(sradx,        0x7C000634, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("srad", "Shift Right Algebraic Doubleword",
         i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(sradx,        0x7C000634, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(sradix,       0x7C000674, XS )(InstrData& i, InstrDisasm& d) {
  d.Init("sradi", "Shift Right Algebraic Doubleword Immediate",
         (i.XS.Rc ? InstrDisasm::kRc : 0) | InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XS.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XS.RT, InstrRegister::kRead);
  d.AddUImmOperand((i.XS.SH5 << 5) | i.XS.SH, 1);
  return d.Finish();
}
XEEMITTER(sradix,       0x7C000674, XS )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(srawx,        0x7C000630, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("sraw", "Shift Right Algebraic Word",
         (i.X.Rc ? InstrDisasm::kRc : 0) | InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(srawx,        0x7C000630, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(srawix,       0x7C000670, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("srawi", "Shift Right Algebraic Word Immediate",
         (i.X.Rc ? InstrDisasm::kRc : 0) | InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.X.RB, 1);
  return d.Finish();
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

XEDISASMR(srdx,         0x7C000436, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("srd", "Shift Right Doubleword", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(srdx,         0x7C000436, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(srwx,         0x7C000430, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("srw", "Shift Right Word", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(srwx,         0x7C000430, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
  XEREGISTERINSTR(rldclx,       0x78000010);
  XEREGISTERINSTR(rldcrx,       0x78000012);
  XEREGISTERINSTR(rldicx,       0x78000008);
  XEREGISTERINSTR(rldiclx,      0x78000000);
  XEREGISTERINSTR(rldicrx,      0x78000004);
  XEREGISTERINSTR(rldimix,      0x7800000C);
  XEREGISTERINSTR(rlwimix,      0x50000000);
  XEREGISTERINSTR(rlwinmx,      0x54000000);
  XEREGISTERINSTR(rlwnmx,       0x5C000000);
  XEREGISTERINSTR(sldx,         0x7C000036);
  XEREGISTERINSTR(slwx,         0x7C000030);
  XEREGISTERINSTR(sradx,        0x7C000634);
  XEREGISTERINSTR(sradix,       0x7C000674);
  XEREGISTERINSTR(srawx,        0x7C000630);
  XEREGISTERINSTR(srawix,       0x7C000670);
  XEREGISTERINSTR(srdx,         0x7C000436);
  XEREGISTERINSTR(srwx,         0x7C000430);
}


}  // namespace codegen
}  // namespace cpu
}  // namespace xe

/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/x64/x64_emit.h>

#include <xenia/cpu/cpu-private.h>


using namespace xe::cpu;
using namespace xe::cpu::ppc;

using namespace AsmJit;


namespace xe {
namespace cpu {
namespace x64 {


// Integer arithmetic (A-3)

XEEMITTER(addx,         0x7C000214, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RD <- (RA) + (RB)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.XO.RA));
  c.add(v, e.gpr_value(i.XO.RB));

  if (i.XO.OE) {
    // With XER update.
    XEASSERTALWAYS();
    //e.update_xer_with_overflow(EFLAGS OF?);
  }

  e.update_gpr_value(i.XO.RT, v);

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(addcx,        0x7C000014, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addex,        0x7C000114, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addi,         0x38000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // if RA = 0 then
  //   RT <- EXTS(SI)
  // else
  //   RT <- (RA) + EXTS(SI)

  GpVar v = e.get_uint64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    c.add(v, e.gpr_value(i.D.RA));
  }
  e.update_gpr_value(i.D.RT, v);

  if (i.D.RA) {
    uint64_t value;
    if (e.get_constant_gpr_value(i.D.RA, &value)) {
      e.set_constant_gpr_value(i.D.RT, value + XEEXTS16(i.D.DS));
    } else {
      e.clear_constant_gpr_value(i.D.RT);
    }
  } else {
    e.set_constant_gpr_value(i.D.RT, XEEXTS16(i.D.DS));
  }

  return 0;
}

XEEMITTER(addic,        0x30000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT <- (RA) + EXTS(SI)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.D.RA));
  c.add(v, imm(XEEXTS16(i.D.DS)));
  GpVar cc(c.newGpVar());
  c.setc(cc.r8());

  e.update_gpr_value(i.D.RT, v);
  e.update_xer_with_carry(cc);

  uint64_t value;
  if (e.get_constant_gpr_value(i.D.RA, &value)) {
    e.set_constant_gpr_value(i.D.RT, value + XEEXTS16(i.D.DS));
  } else {
    e.clear_constant_gpr_value(i.D.RT);
  }

  return 0;
}

XEEMITTER(addicx,       0x34000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT <- (RA) + EXTS(SI)
  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.D.RA));
  c.add(v, imm(XEEXTS16(i.D.DS)));
  GpVar cc(c.newGpVar());
  c.setc(cc.r8());

  e.update_gpr_value(i.D.RT, v);
  e.update_cr_with_cond(0, v);
  e.update_xer_with_carry(cc);

  uint64_t value;
  if (e.get_constant_gpr_value(i.D.RA, &value)) {
    e.set_constant_gpr_value(i.D.RT, value + XEEXTS16(i.D.DS));
  } else {
    e.clear_constant_gpr_value(i.D.RT);
  }

  return 0;
}

XEEMITTER(addis,        0x3C000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // if RA = 0 then
  //   RT <- EXTS(SI) || i16.0
  // else
  //   RT <- (RA) + EXTS(SI) || i16.0

  GpVar v(e.get_uint64(XEEXTS16(i.D.DS) << 16));
  if (i.D.RA) {
    c.add(v, e.gpr_value(i.D.RA));
  }
  e.update_gpr_value(i.D.RT, v);

  if (i.D.RA) {
    uint64_t value;
    if (e.get_constant_gpr_value(i.D.RA, &value)) {
      e.set_constant_gpr_value(i.D.RT, value + (XEEXTS16(i.D.DS) << 16));
    } else {
      e.clear_constant_gpr_value(i.D.RT);
    }
  } else {
    e.set_constant_gpr_value(i.D.RT, XEEXTS16(i.D.DS) << 16);
  }

  return 0;
}

XEEMITTER(addmex,       0x7C0001D4, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addzex,       0x7C000194, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT <- (RA) + CA

  // Add in carry flag from XER, only if needed.
  // It may be possible to do this much more efficiently.
  GpVar xer(c.newGpVar());
  c.mov(xer, e.xer_value());
  c.shr(xer, imm(29));
  c.and_(xer, imm(1));
  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.XO.RA));
  c.add(v, xer);
  GpVar cc(c.newGpVar());
  c.setc(cc.r8());

  e.update_gpr_value(i.XO.RT, v);
  e.update_xer_with_carry(cc);

  if (i.XO.OE) {
    // With XER[SO] update too.
    //e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  } else {
    // Just CA update.
    //e.update_xer_with_carry(b.CreateExtractValue(v, 1));
  }

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(divdx,        0x7C0003D2, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(divdux,       0x7C000392, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // dividend <- (RA)
  // divisor <- (RB)
  // if divisor = 0 then
  //   if OE = 1 then
  //     XER[OV] <- 1
  //   return
  // RT <- dividend ÷ divisor

  GpVar dividend(c.newGpVar());
  GpVar divisor(c.newGpVar());
  c.mov(dividend, e.gpr_value(i.XO.RA));
  c.mov(divisor, e.gpr_value(i.XO.RB));

#if 0
  // Note that we skip the zero handling block and just avoid the divide if
  // we are OE=0.
  BasicBlock* zero_bb = i.XO.OE ?
      BasicBlock::Create(*e.context(), "", e.fn()) : NULL;
  BasicBlock* nonzero_bb = BasicBlock::Create(*e.context(), "", e.fn());
  BasicBlock* after_bb = BasicBlock::Create(*e.context(), "", e.fn());
  b.CreateCondBr(b.CreateICmpEQ(divisor, b.get_int32(0)),
                 i.XO.OE ? zero_bb : after_bb, nonzero_bb);

  if (zero_bb) {
    // Divisor was zero - do XER update.
    b.SetInsertPoint(zero_bb);
    e.update_xer_with_overflow(b.getInt1(1));
    b.CreateBr(after_bb);
  }
#endif

  // Divide.
  GpVar dividend_hi(c.newGpVar());
  c.alloc(dividend_hi, rdx);
  c.mov(dividend_hi, imm(0));
  c.alloc(dividend, rax);
  c.div(dividend_hi, dividend.r64(), divisor.r64());
  e.update_gpr_value(i.XO.RT, dividend);

  // If we are OE=1 we need to clear the overflow bit.
  if (i.XO.OE) {
    e.update_xer_with_overflow(e.get_uint64(0));
  }

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, dividend, false);
  }

  c.unuse(dividend_hi);
  c.unuse(dividend);

#if 0
  b.CreateBr(after_bb);
#endif

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(divwx,        0x7C0003D6, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // dividend[0:31] <- (RA)[32:63]
  // divisor[0:31] <- (RB)[32:63]
  // if divisor = 0 then
  //   if OE = 1 then
  //     XER[OV] <- 1
  //   return
  // RT[32:63] <- dividend ÷ divisor
  // RT[0:31] <- undefined

  GpVar dividend(c.newGpVar());
  GpVar divisor(c.newGpVar());
  c.mov(dividend.r32(), e.gpr_value(i.XO.RA).r32());
  c.mov(divisor.r32(), e.gpr_value(i.XO.RB).r32());

#if 0
  // Note that we skip the zero handling block and just avoid the divide if
  // we are OE=0.
  BasicBlock* zero_bb = i.XO.OE ?
      BasicBlock::Create(*e.context(), "", e.fn()) : NULL;
  BasicBlock* nonzero_bb = BasicBlock::Create(*e.context(), "", e.fn());
  BasicBlock* after_bb = BasicBlock::Create(*e.context(), "", e.fn());
  b.CreateCondBr(b.CreateICmpEQ(divisor, b.get_int32(0)),
                 i.XO.OE ? zero_bb : after_bb, nonzero_bb);

  if (zero_bb) {
    // Divisor was zero - do XER update.
    b.SetInsertPoint(zero_bb);
    e.update_xer_with_overflow(b.getInt1(1));
    b.CreateBr(after_bb);
  }
#endif

  // Divide.
  GpVar dividend_hi(c.newGpVar());
  c.alloc(dividend_hi, rdx);
  c.mov(dividend_hi, imm(0));
  c.alloc(dividend, rax);
  c.idiv(dividend_hi, dividend.r64(), divisor.r64());
  e.update_gpr_value(i.XO.RT, dividend);

  // If we are OE=1 we need to clear the overflow bit.
  if (i.XO.OE) {
    e.update_xer_with_overflow(e.get_uint64(0));
  }

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, dividend, true);
  }

#if 0
  b.CreateBr(after_bb);
#endif

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(divwux,       0x7C000396, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // dividend[0:31] <- (RA)[32:63]
  // divisor[0:31] <- (RB)[32:63]
  // if divisor = 0 then
  //   if OE = 1 then
  //     XER[OV] <- 1
  //   return
  // RT[32:63] <- dividend ÷ divisor
  // RT[0:31] <- undefined

  GpVar dividend(c.newGpVar());
  GpVar divisor(c.newGpVar());
  c.mov(dividend.r32(), e.gpr_value(i.XO.RA).r32());
  c.mov(divisor.r32(), e.gpr_value(i.XO.RB).r32());

#if 0
  // Note that we skip the zero handling block and just avoid the divide if
  // we are OE=0.
  BasicBlock* zero_bb = i.XO.OE ?
      BasicBlock::Create(*e.context(), "", e.fn()) : NULL;
  BasicBlock* nonzero_bb = BasicBlock::Create(*e.context(), "", e.fn());
  BasicBlock* after_bb = BasicBlock::Create(*e.context(), "", e.fn());
  b.CreateCondBr(b.CreateICmpEQ(divisor, b.get_int32(0)),
                 i.XO.OE ? zero_bb : after_bb, nonzero_bb);

  if (zero_bb) {
    // Divisor was zero - do XER update.
    b.SetInsertPoint(zero_bb);
    e.update_xer_with_overflow(b.getInt1(1));
    b.CreateBr(after_bb);
  }
#endif

  // Divide.
  GpVar dividend_hi(c.newGpVar());
  c.alloc(dividend_hi, rdx);
  c.mov(dividend_hi, imm(0));
  c.alloc(dividend, rax);
  c.div(dividend_hi, dividend.r64(), divisor.r64());
  e.update_gpr_value(i.XO.RT, dividend);

  // If we are OE=1 we need to clear the overflow bit.
  if (i.XO.OE) {
    e.update_xer_with_overflow(e.get_uint64(0));
  }

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, dividend, false);
  }

  c.unuse(dividend_hi);
  c.unuse(dividend);

#if 0
  b.CreateBr(after_bb);
#endif

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(mulhdx,       0x7C000092, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhdux,      0x7C000012, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhwx,       0x7C000096, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT[32:64] <- ((RA)[32:63] × (RB)[32:63])[0:31]

  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  GpVar v_0(c.newGpVar());
  GpVar v_1(c.newGpVar());
  GpVar hi(c.newGpVar());
  c.alloc(v_0, rax);
  c.alloc(hi, rdx);
  c.mov(v_0.r32(), e.gpr_value(i.XO.RA).r32());
  c.mov(v_1.r32(), e.gpr_value(i.XO.RB).r32());
  c.imul(hi.r32(), v_0.r32(), v_1.r32());
  e.update_gpr_value(i.XO.RT, hi);

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, hi);
  }

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(mulhwux,      0x7C000016, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT[32:64] <- ((RA)[32:63] × (RB)[32:63])[0:31]

  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  GpVar v_0(c.newGpVar());
  GpVar v_1(c.newGpVar());
  GpVar hi(c.newGpVar());
  c.alloc(v_0, rax);
  c.alloc(hi, rdx);
  c.mov(v_0.r32(), e.gpr_value(i.XO.RA).r32());
  c.mov(v_1.r32(), e.gpr_value(i.XO.RB).r32());
  c.mul(hi.r32(), v_0.r32(), v_1.r32());
  e.update_gpr_value(i.XO.RT, hi);

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, hi);
  }

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(mulldx,       0x7C0001D2, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT <- ((RA) × (RB))[64:127]

  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  GpVar v_0(c.newGpVar());
  GpVar v_1(c.newGpVar());
  c.mov(v_0, e.gpr_value(i.XO.RA));
  c.mov(v_1, e.gpr_value(i.XO.RB));
  c.imul(v_0.r64(), v_1.r64());
  e.update_gpr_value(i.XO.RT, v_0);

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v_0);
  }

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(mulli,        0x1C000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // prod[0:127] <- (RA) × EXTS(SI)
  // RT <- prod[64:127]

  // TODO(benvanik): ensure this has the right behavior when the value
  // overflows. It should be truncating the result, but I'm not sure what LLVM
  // does.

  GpVar v_lo(c.newGpVar());
  GpVar v_hi(c.newGpVar());
  c.alloc(v_lo, rax);
  c.alloc(v_hi, rdx);
  c.mov(v_lo, e.get_uint64(XEEXTS16(i.D.DS)));
  c.mul(v_hi, v_lo, e.gpr_value(i.D.RA));
  e.update_gpr_value(i.D.RT, v_lo);

  e.clear_constant_gpr_value(i.D.RT);

  return 0;
}

XEEMITTER(mullwx,       0x7C0001D6, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT <- (RA)[32:63] × (RB)[32:63]

  if (i.XO.OE) {
    // With XER update.
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  GpVar v_0(c.newGpVar());
  GpVar v_1(c.newGpVar());
  c.mov(v_0.r32(), e.gpr_value(i.XO.RA).r32());
  c.mov(v_1.r32(), e.gpr_value(i.XO.RB).r32());
  c.imul(v_0.r64(), v_1.r64());
  e.update_gpr_value(i.XO.RT, v_0);

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v_0);
  }

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(negx,         0x7C0000D0, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
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
    //                         e.get_int64(0), e.gpr_value(i.XO.RA));
    //jit_value_t v0 = b.CreateExtractValue(v, 0);
    //e.update_gpr_value(i.XO.RT, v0);
    //e.update_xer_with_overflow(b.CreateExtractValue(v, 1));

    //if (i.XO.Rc) {
    //  // With cr0 update.
    //  e.update_cr_with_cond(0, v0, e.get_int64(0), true);
    //}
  } else {
    // No OE bit setting.
    GpVar v(c.newGpVar());
    c.mov(v, e.gpr_value(i.XO.RA));
    c.neg(v);
    e.update_gpr_value(i.XO.RT, v);

    if (i.XO.Rc) {
      // With cr0 update.
      e.update_cr_with_cond(0, v);
    }
  }

  uint64_t value;
  if (e.get_constant_gpr_value(i.XO.RA, &value)) {
    e.set_constant_gpr_value(i.XO.RT, ~value + 1);
  } else {
    e.clear_constant_gpr_value(i.XO.RT);
  }

  return 0;
}

XEEMITTER(subfx,        0x7C000050, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT <- ¬(RA) + (RB) + 1

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.XO.RA));
  c.not_(v);
  c.stc();  // Always carrying.
  c.adc(v, e.gpr_value(i.XO.RB));

  e.update_gpr_value(i.XO.RT, v);

  if (i.XO.OE) {
    // With XER update.
    XEASSERTALWAYS();
    //e.update_xer_with_overflow(EFLAGS??);
  }

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(subfcx,       0x7C000010, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT <- ¬(RA) + (RB) + 1

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.XO.RA));
  c.not_(v);
  c.stc();  // Always carrying.
  c.adc(v, e.gpr_value(i.XO.RB));
  GpVar cc(c.newGpVar());
  c.setc(cc.r8());

  e.update_gpr_value(i.XO.RT, v);
  e.update_xer_with_carry(cc);

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(subficx,      0x20000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT <- ¬(RA) + EXTS(SI) + 1

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.D.RA));
  c.not_(v);
  c.stc();  // Always carrying.
  c.adc(v, imm(XEEXTS16(i.D.DS)));
  GpVar cc(c.newGpVar());
  c.setc(cc.r8());

  e.update_gpr_value(i.D.RT, v);
  e.update_xer_with_carry(cc);

  uint64_t value;
  if (e.get_constant_gpr_value(i.D.RA, &value)) {
    e.set_constant_gpr_value(i.D.RT, ~value + XEEXTS16(i.D.DS) + 1);
  } else {
    e.clear_constant_gpr_value(i.D.RT);
  }

  return 0;
}

XEEMITTER(subfex,       0x7C000110, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RT <- ¬(RA) + (RB) + CA

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.XO.RA));
  c.not_(v);

  // Add in carry flag from XER, only if needed.
  // It may be possible to do this much more efficiently.
  GpVar xer(c.newGpVar());
  c.mov(xer, e.xer_value());
  c.shr(xer, imm(29));
  c.and_(xer, imm(1));
  Label post_stc_label = c.newLabel();
  c.jz(post_stc_label, kCondHintLikely);
  c.stc();
  c.bind(post_stc_label);

  c.adc(v, e.gpr_value(i.XO.RB));
  GpVar cc(c.newGpVar());
  c.setc(cc.r8());

  e.update_gpr_value(i.XO.RT, v);

  if (i.XO.OE) {
    // With XER update.
    XEASSERTALWAYS();
    //e.update_xer_with_overflow_and_carry(b.CreateExtractValue(v, 1));
  } else {
    e.update_xer_with_carry(cc);
  }

  if (i.XO.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.XO.RT);

  return 0;
}

XEEMITTER(subfmex,      0x7C0001D0, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(subfzex,      0x7C000190, XO )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer compare (A-4)

XEEMITTER(cmp,          0x7C000000, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
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

  GpVar lhs(c.newGpVar());
  GpVar rhs(c.newGpVar());
  c.mov(lhs, e.gpr_value(i.X.RA));
  c.mov(rhs, e.gpr_value(i.X.RB));
  if (!L) {
    // 32-bit - truncate and sign extend.
    c.cdqe(lhs);
    c.cdqe(rhs);
  }

  e.update_cr_with_cond(BF, lhs, rhs);

  return 0;
}

XEEMITTER(cmpi,         0x2C000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
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

  GpVar lhs(c.newGpVar());
  c.mov(lhs, e.gpr_value(i.D.RA));
  if (!L) {
    // 32-bit - truncate and sign extend.
    c.cdqe(lhs);
  }

  e.update_cr_with_cond(BF, lhs, e.get_uint64(XEEXTS16(i.D.DS)));

  return 0;
}

XEEMITTER(cmpl,         0x7C000040, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
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

  GpVar lhs(c.newGpVar());
  GpVar rhs(c.newGpVar());
  c.mov(lhs, e.gpr_value(i.X.RA));
  c.mov(rhs, e.gpr_value(i.X.RB));
  if (!L) {
    // 32-bit - truncate and zero extend.
    c.mov(lhs.r32(), lhs.r32());
    c.mov(rhs.r32(), rhs.r32());
  }

  e.update_cr_with_cond(BF, lhs, rhs, false);

  return 0;
}

XEEMITTER(cmpli,        0x28000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
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

  GpVar lhs(c.newGpVar());
  c.mov(lhs, e.gpr_value(i.D.RA));
  if (!L) {
    // 32-bit - truncate and zero extend.
    c.mov(lhs.r32(), lhs.r32());
  }

  e.update_cr_with_cond(BF, lhs, e.get_uint64(i.D.DS), false);

  return 0;
}


// Integer logical (A-5)

XEEMITTER(andx,         0x7C000038, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) & (RB)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.and_(v, e.gpr_value(i.X.RB));
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(andcx,        0x7C000078, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) & ¬(RB)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RB));
  c.not_(v);
  c.and_(v, e.gpr_value(i.X.RT));
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(andix,        0x70000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) & (i48.0 || UI)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.D.RT));
  c.and_(v, imm(i.D.DS));
  e.update_gpr_value(i.D.RA, v);

  // With cr0 update.
  e.update_cr_with_cond(0, v);

  uint64_t value;
  if (e.get_constant_gpr_value(i.D.RT, &value)) {
    e.set_constant_gpr_value(i.D.RA, value & i.D.DS);
  } else {
    e.clear_constant_gpr_value(i.D.RA);
  }

  return 0;
}

XEEMITTER(andisx,       0x74000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) & (i32.0 || UI || i16.0)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.D.RT));
  c.and_(v, imm(i.D.DS << 16));
  e.update_gpr_value(i.D.RA, v);

  // With cr0 update.
  e.update_cr_with_cond(0, v);

  uint64_t value;
  if (e.get_constant_gpr_value(i.D.RT, &value)) {
    e.set_constant_gpr_value(i.D.RA, value & (i.D.DS << 16));
  } else {
    e.clear_constant_gpr_value(i.D.RA);
  }

  return 1;
}

XEEMITTER(cntlzdx,      0x7C000074, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- 0
  // do while n < 64
  //   if (RS)[n] = 1 then leave n
  //   n <- n + 1
  // RA <- n

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.bsr(v, v);
  c.cmovz(v, e.get_uint64(0));
  c.xor_(v, imm(0x3F));
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(cntlzwx,      0x7C000034, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- 32
  // do while n < 64
  //   if (RS)[n] = 1 then leave n
  //   n <- n + 1
  // RA <- n - 32

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.bsr(v.r32(), v.r32());
  c.cmovz(v, e.get_uint64(63));
  c.xor_(v, imm(0x1F));
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(eqvx,         0x7C000238, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) == (RB)

  // UNTESTED: ensure this is correct.
  //XEASSERTALWAYS();
  //c.int3();

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.xor_(v, e.gpr_value(i.X.RB));
  c.not_(v);
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(extsbx,       0x7C000774, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // s <- (RS)[56]
  // RA[56:63] <- (RS)[56:63]
  // RA[0:55] <- i56.s

  // TODO(benvanik): see if there's a faster way to do this.
  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.cbw(v);
  c.cwde(v);
  c.cdqe(v);
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // Update cr0.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(extshx,       0x7C000734, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // s <- (RS)[48]
  // RA[48:63] <- (RS)[48:63]
  // RA[0:47] <- 48.s

  // TODO(benvanik): see if there's a faster way to do this.
  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.cwde(v);
  c.cdqe(v);
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // Update cr0.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(extswx,       0x7C0007B4, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // s <- (RS)[32]
  // RA[32:63] <- (RS)[32:63]
  // RA[0:31] <- i32.s

  // TODO(benvanik): see if there's a faster way to do this.
  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.cdqe(v);
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // Update cr0.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(nandx,        0x7C0003B8, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(norx,         0x7C0000F8, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- ¬((RS) | (RB))

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.or_(v, e.gpr_value(i.X.RB));
  c.not_(v);
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(orx,          0x7C000378, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) | (RB)

  GpVar v(c.newGpVar());
  if (i.X.RT == i.X.RB) {
    c.mov(v, e.gpr_value(i.X.RT));
  } else {
    c.mov(v, e.gpr_value(i.X.RT));
    c.or_(v, e.gpr_value(i.X.RB));
  }
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(orcx,         0x7C000338, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) | ¬(RB)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RB));
  c.not_(v);
  c.or_(v, e.gpr_value(i.X.RT));
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(ori,          0x60000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) | (i48.0 || UI)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.D.RT));
  c.or_(v, imm(i.D.DS));
  e.update_gpr_value(i.D.RA, v);

  uint64_t value;
  if (e.get_constant_gpr_value(i.D.RT, &value)) {
    e.set_constant_gpr_value(i.D.RA, value | i.D.DS);
  } else {
    e.clear_constant_gpr_value(i.D.RA);
  }

  return 0;
}

XEEMITTER(oris,         0x64000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) | (i32.0 || UI || i16.0)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.D.RT));
  c.or_(v, imm(i.D.DS << 16));
  e.update_gpr_value(i.D.RA, v);

  uint64_t value;
  if (e.get_constant_gpr_value(i.D.RT, &value)) {
    e.set_constant_gpr_value(i.D.RA, value | (i.D.DS << 16));
  } else {
    e.clear_constant_gpr_value(i.D.RA);
  }

  return 0;
}

XEEMITTER(xorx,         0x7C000278, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) XOR (RB)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.xor_(v, e.gpr_value(i.X.RB));
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(xori,         0x68000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) XOR (i48.0 || UI)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.D.RT));
  c.xor_(v, imm(i.D.DS));
  e.update_gpr_value(i.D.RA, v);

  uint64_t value;
  if (e.get_constant_gpr_value(i.D.RT, &value)) {
    e.set_constant_gpr_value(i.D.RA, value ^ i.D.DS);
  } else {
    e.clear_constant_gpr_value(i.D.RA);
  }

  return 0;
}

XEEMITTER(xoris,        0x6C000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // RA <- (RS) XOR (i32.0 || UI || i16.0)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.D.RT));
  c.xor_(v, imm(i.D.DS << 16));
  e.update_gpr_value(i.D.RA, v);

  uint64_t value;
  if (e.get_constant_gpr_value(i.D.RT, &value)) {
    e.set_constant_gpr_value(i.D.RA, value ^ (i.D.DS << 16));
  } else {
    e.clear_constant_gpr_value(i.D.RA);
  }

  return 0;
}


// Integer rotate (A-6)

XEEMITTER(rld,          0x78000000, MDS)(X64Emitter& e, X86Compiler& c, InstrData& i) {
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

    GpVar v(c.newGpVar());
    c.mov(v, e.gpr_value(i.MD.RT));
    if (sh) {
      c.rol(v, imm(sh));
    }
    if (m != 0xFFFFFFFFFFFFFFFF) {
      GpVar mask(c.newGpVar());
      c.mov(mask, imm(m));
      c.and_(v, mask);
    }
    e.update_gpr_value(i.MD.RA, v);

    if (i.MD.Rc) {
      // With cr0 update.
      e.update_cr_with_cond(0, v);
    }

    e.clear_constant_gpr_value(i.MD.RA);

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

    GpVar v(c.newGpVar());
    c.mov(v, e.gpr_value(i.MD.RT));
    if (sh) {
      c.rol(v, imm(sh));
    }
    if (m != 0xFFFFFFFFFFFFFFFF) {
      GpVar mask(c.newGpVar());
      c.mov(mask, imm(m));
      c.and_(v, mask);
    }
    e.update_gpr_value(i.MD.RA, v);

    if (i.MD.Rc) {
      // With cr0 update.
      e.update_cr_with_cond(0, v);
    }

    e.clear_constant_gpr_value(i.MD.RA);

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
    XEINSTRNOTIMPLEMENTED();
    return 1;
  } else {
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
}

XEEMITTER(rlwimix,      0x50000000, M  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r&m | (RA)&¬m

  GpVar v(c.newGpVar());
  c.mov(v.r32(), e.gpr_value(i.M.RT).r32()); // truncate
  if (i.M.SH) {
    c.rol(v.r32(), imm(i.M.SH));
  }
  uint64_t m = XEMASK(i.M.MB + 32, i.M.ME + 32);
  GpVar mask(c.newGpVar());
  c.mov(mask, imm(m));
  c.and_(v, mask);

  GpVar old_ra(c.newGpVar());
  c.mov(old_ra, e.gpr_value(i.M.RA));
  c.not_(mask);
  c.and_(old_ra, mask);
  c.or_(v, old_ra);
  e.update_gpr_value(i.M.RA, v);

  if (i.M.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.M.RA);

  return 0;
}

XEEMITTER(rlwinmx,      0x54000000, M  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], n)
  // m <- MASK(MB+32, ME+32)
  // RA <- r & m

  GpVar v(c.newGpVar());
  c.mov(v.r32(), e.gpr_value(i.M.RT).r32()); // truncate

  // The compiler will generate a bunch of these for the special case of SH=0.
  // Which seems to just select some bits and set cr0 for use with a branch.
  // We can detect this and do less work.
  if (i.M.SH) {
    c.rol(v.r32(), imm(i.M.SH));
  }

  c.and_(v, imm(XEMASK(i.M.MB + 32, i.M.ME + 32)));
  e.update_gpr_value(i.M.RA, v);

  if (i.M.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.M.RA);

  return 0;
}

XEEMITTER(rlwnmx,       0x5C000000, M  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer shift (A-7)

XEEMITTER(sldx,         0x7C000036, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL64((RS), n)
  // if (RB)[58] = 0 then
  //   m <- MASK(0, 63-n)
  // else
  //   m <- i64.0
  // RA <- r & m

  GpVar sh(c.newGpVar());
  c.mov(sh, e.gpr_value(i.X.RB));
  c.and_(sh, imm(0x3F));
  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.shl(v, sh);
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(slwx,         0x7C000030, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], n)
  // if (RB)[58] = 0 then
  //   m <- MASK(32, 63-n)
  // else
  //   m <- i64.0
  // RA <- r & m

  GpVar sh(c.newGpVar());
  c.mov(sh, e.gpr_value(i.X.RB));
  c.and_(sh, imm(0x1F));
  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.shl(v, sh);
  c.mov(v.r32(), v.r32());
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(sradx,        0x7C000634, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- rB[58-63]
  // r <- ROTL[64](rS, 64 - n)
  // if rB[57] = 0 then m ← MASK(n, 63)
  // else m ← (64)0
  // S ← rS[0]
  // rA <- (r & m) | (((64)S) & ¬ m)
  // XER[CA] <- S & ((r & ¬ m) ¦ 0)

  // if n == 0: rA <- rS, XER[CA] = 0
  // if n >= 64: rA <- 64 sign bits of rS, XER[CA] = sign bit of rS

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  GpVar sh(c.newGpVar());
  c.mov(sh, e.gpr_value(i.X.RB));
  c.and_(sh, imm(0x7F));

  // CA is set if any bits are shifted out of the right and if the result
  // is negative. Start tracking that here.
  GpVar ca(c.newGpVar());
  c.mov(ca, imm(0xFFFFFFFFFFFFFFFF));
  GpVar ca_sh(c.newGpVar());
  c.mov(ca_sh, imm(63));
  c.sub(ca_sh, sh);
  c.shl(ca, ca_sh);
  c.shr(ca, ca_sh);
  c.and_(ca, v);
  c.cmp(ca, imm(0));
  c.xor_(ca, ca);
  c.setnz(ca.r8());

  // Shift right.
  c.sar(v, sh);

  // CA is set to 1 if the low-order 32 bits of (RS) contain a negative number
  // and any 1-bits are shifted out of position 63; otherwise CA is set to 0.
  // We already have ca set to indicate the pos 63 bit, now just and in sign.
  GpVar ca_2(c.newGpVar());
  c.mov(ca_2, v);
  c.shr(ca_2, imm(63));
  c.and_(ca, ca_2);

  e.update_gpr_value(i.X.RA, v);
  e.update_xer_with_carry(ca);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(sradix,       0x7C000674, XS )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- sh[5] || sh[0-4]
  // r <- ROTL[64](rS, 64 - n)
  // m ← MASK(n, 63)
  // S ← rS[0]
  // rA <- (r & m) | (((64)S) & ¬ m)
  // XER[CA] <- S & ((r & ¬ m) ¦ 0)

  // if n == 0: rA <- rS, XER[CA] = 0
  // if n >= 64: rA <- 64 sign bits of rS, XER[CA] = sign bit of rS

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.XS.RA));
  GpVar sh(c.newGpVar());
  c.mov(sh, imm((i.XS.SH5 << 5) | i.XS.SH));

  // CA is set if any bits are shifted out of the right and if the result
  // is negative. Start tracking that here.
  GpVar ca(c.newGpVar());
  c.mov(ca, imm(0xFFFFFFFFFFFFFFFF));
  GpVar ca_sh(c.newGpVar());
  c.mov(ca_sh, imm(63));
  c.sub(ca_sh, sh);
  c.shl(ca, ca_sh);
  c.shr(ca, ca_sh);
  c.and_(ca, v);
  c.cmp(ca, imm(0));
  c.xor_(ca, ca);
  c.setnz(ca.r8());

  // Shift right.
  c.sar(v, sh);

  // CA is set to 1 if the low-order 32 bits of (RS) contain a negative number
  // and any 1-bits are shifted out of position 63; otherwise CA is set to 0.
  // We already have ca set to indicate the pos 63 bit, now just and in sign.
  GpVar ca_2(c.newGpVar());
  c.mov(ca_2, v);
  c.shr(ca_2, imm(63));
  c.and_(ca, ca_2);

  e.update_gpr_value(i.XS.RT, v);
  e.update_xer_with_carry(ca);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(srawx,        0x7C000630, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- rB[59-63]
  // r <- ROTL32((RS)[32:63], 64-n)
  // m <- MASK(n+32, 63)
  // s <- (RS)[32]
  // RA <- r&m | (i64.s)&¬m
  // CA <- s & ((r&¬m)[32:63]≠0)

  // if n == 0: rA <- sign_extend(rS), XER[CA] = 0
  // if n >= 32: rA <- 64 sign bits of rS, XER[CA] = sign bit of lo_32(rS)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  GpVar sh(c.newGpVar());
  c.mov(sh, e.gpr_value(i.X.RB));
  c.and_(sh, imm(0x7F));

  GpVar ca(c.newGpVar());
  Label skip(c.newLabel());
  Label full(c.newLabel());
  c.test(sh, imm(0));
  c.jnz(full);
  {
    // No shift, just a fancy sign extend and CA clearer.
    c.cdqe(v);
    c.mov(ca, imm(0));
  }
  c.jmp(skip);
  c.bind(full);
  {
    // CA is set if any bits are shifted out of the right and if the result
    // is negative. Start tracking that here.
    c.mov(ca, v);
    c.and_(ca, imm(~XEMASK(32 + i.X.RB, 64)));
    c.cmp(ca, imm(0));
    c.xor_(ca, ca);
    c.setnz(ca.r8());

    // Shift right and sign extend the 32bit part.
    c.sar(v.r32(), imm(i.X.RB));
    c.cdqe(v);

    // CA is set to 1 if the low-order 32 bits of (RS) contain a negative number
    // and any 1-bits are shifted out of position 63; otherwise CA is set to 0.
    // We already have ca set to indicate the shift bits, now just and in sign.
    GpVar ca_2(c.newGpVar());
    c.mov(ca_2, v.r32());
    c.shr(ca_2, imm(31));
    c.and_(ca, ca_2);
  }
  c.bind(skip);

  e.update_gpr_value(i.X.RA, v);
  e.update_xer_with_carry(ca);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(srawix,       0x7C000670, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- SH
  // r <- ROTL32((RS)[32:63], 64-n)
  // m <- MASK(n+32, 63)
  // s <- (RS)[32]
  // RA <- r&m | (i64.s)&¬m
  // CA <- s & ((r&¬m)[32:63]≠0)

  // if n == 0: rA <- sign_extend(rS), XER[CA] = 0
  // if n >= 32: rA <- 64 sign bits of rS, XER[CA] = sign bit of lo_32(rS)

  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));

  GpVar ca(c.newGpVar());
  if (!i.X.RB) {
    // No shift, just a fancy sign extend and CA clearer.
    c.cdqe(v);
    c.mov(ca, imm(0));
  } else {
    // CA is set if any bits are shifted out of the right and if the result
    // is negative. Start tracking that here.
    c.mov(ca, v);
    c.and_(ca, imm(~XEMASK(32 + i.X.RB, 64)));
    c.cmp(ca, imm(0));
    c.xor_(ca, ca);
    c.setnz(ca.r8());

    // Shift right and sign extend the 32bit part.
    c.sar(v.r32(), imm(i.X.RB));
    c.cdqe(v);

    // CA is set to 1 if the low-order 32 bits of (RS) contain a negative number
    // and any 1-bits are shifted out of position 63; otherwise CA is set to 0.
    // We already have ca set to indicate the shift bits, now just and in sign.
    GpVar ca_2(c.newGpVar());
    c.mov(ca_2, v.r32());
    c.shr(ca_2, imm(31));
    c.and_(ca, ca_2);
  }

  e.update_gpr_value(i.X.RA, v);
  e.update_xer_with_carry(ca);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(srdx,         0x7C000436, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL64((RS), 64-n)
  // if (RB)[58] = 0 then
  //   m <- MASK(n, 63)
  // else
  //   m <- i64.0
  // RA <- r & m

  GpVar sh(c.newGpVar());
  c.mov(sh, e.gpr_value(i.X.RB));
  c.and_(sh, imm(0x3F));
  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.shr(v, sh);
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}

XEEMITTER(srwx,         0x7C000430, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- (RB)[59:63]
  // r <- ROTL32((RS)[32:63], 64-n)
  // if (RB)[58] = 0 then
  //   m <- MASK(n+32, 63)
  // else
  //   m <- i64.0
  // RA <- r & m

  GpVar sh(c.newGpVar());
  c.mov(sh, e.gpr_value(i.X.RB));
  c.and_(sh, imm(0x1F));
  GpVar v(c.newGpVar());
  c.mov(v, e.gpr_value(i.X.RT));
  c.shr(v, sh);
  c.mov(v.r32(), v.r32());
  e.update_gpr_value(i.X.RA, v);

  if (i.X.Rc) {
    // With cr0 update.
    e.update_cr_with_cond(0, v);
  }

  e.clear_constant_gpr_value(i.X.RA);

  return 0;
}


void X64RegisterEmitCategoryALU() {
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
  XEREGISTERINSTR(rld,          0x78000000);
  // XEREGISTERINSTR(rldclx,       0x78000010);
  // XEREGISTERINSTR(rldcrx,       0x78000012);
  // XEREGISTERINSTR(rldicx,       0x78000008);
  // XEREGISTERINSTR(rldiclx,      0x78000000);
  // XEREGISTERINSTR(rldicrx,      0x78000004);
  // XEREGISTERINSTR(rldimix,      0x7800000C);
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


}  // namespace x64
}  // namespace cpu
}  // namespace xe

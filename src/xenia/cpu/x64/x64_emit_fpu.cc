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


// Enable rounding numbers to single precision as required.
// This adds a bunch of work per operation and I'm not sure it's required.
#define ROUND_TO_SINGLE


namespace xe {
namespace cpu {
namespace x64 {


// Floating-point arithmetic (A-8)

XEEMITTER(faddx,        0xFC00002A, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- (frA) + (frB)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.A.FRA));
  c.addsd(v, e.fpr_value(i.A.FRB));
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(faddsx,       0xEC00002A, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- (frA) + (frB)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.A.FRA));
  c.addsd(v, e.fpr_value(i.A.FRB));
#if defined(ROUND_TO_SINGLE)
  // TODO(benvanik): check rounding mode? etc?
  // This converts to a single then back to a double to approximate the
  // rounding on the 360.
  c.cvtsd2ss(v, v);
  c.cvtss2sd(v, v);
#endif  // ROUND_TO_SINGLE
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fdivx,        0xFC000024, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- frA / frB

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.A.FRA));
  c.divsd(v, e.fpr_value(i.A.FRB));
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fdivsx,       0xEC000024, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- frA / frB

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.A.FRA));
  c.divsd(v, e.fpr_value(i.A.FRB));
#if defined(ROUND_TO_SINGLE)
  // TODO(benvanik): check rounding mode? etc?
  // This converts to a single then back to a double to approximate the
  // rounding on the 360.
  c.cvtsd2ss(v, v);
  c.cvtss2sd(v, v);
#endif  // ROUND_TO_SINGLE
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fmulx,        0xFC000032, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- (frA) x (frC)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.A.FRA));
  c.mulsd(v, e.fpr_value(i.A.FRC));
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fmulsx,       0xEC000032, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- (frA) x (frC)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.A.FRA));
  c.mulsd(v, e.fpr_value(i.A.FRC));
#if defined(ROUND_TO_SINGLE)
  // TODO(benvanik): check rounding mode? etc?
  // This converts to a single then back to a double to approximate the
  // rounding on the 360.
  c.cvtsd2ss(v, v);
  c.cvtss2sd(v, v);
#endif  // ROUND_TO_SINGLE
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fresx,        0xEC000030, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(frsqrtex,     0xFC000034, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fsubx,        0xFC000028, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- (frA) - (frB)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.A.FRA));
  c.subsd(v, e.fpr_value(i.A.FRB));
#if defined(ROUND_TO_SINGLE)
  // TODO(benvanik): check rounding mode? etc?
  // This converts to a single then back to a double to approximate the
  // rounding on the 360.
  c.cvtsd2ss(v, v);
  c.cvtss2sd(v, v);
#endif  // ROUND_TO_SINGLE
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fsubsx,       0xEC000028, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- (frA) - (frB)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.A.FRA));
  c.subsd(v, e.fpr_value(i.A.FRB));
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fselx,        0xFC00002E, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // if (frA) >= 0.0
  // then frD <- (frC)
  // else frD <- (frB)

  XmmVar v(c.newXmmVar());
  GpVar zero(c.newGpVar());
  c.mov(zero, imm(0));
  c.movq(v, zero);
  c.cmpsd(e.fpr_value(i.A.FRA), v, 0);

  // TODO(benvanik): find a way to do this without jumps.
  Label choose_b(c.newLabel());
  Label done(c.newLabel());
  c.jl(choose_b);
  c.movq(v, e.fpr_value(i.A.FRC));
  c.jmp(done);
  c.bind(choose_b);
  c.movq(v, e.fpr_value(i.A.FRB));
  c.bind(done);
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fsqrtx,       0xFC00002C, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fsqrtsx,      0xEC00002C, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point multiply-add (A-9)

XEEMITTER(fmaddx,       0xFC00003A, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmaddsx,      0xEC00003A, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmsubx,       0xFC000038, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmsubsx,      0xEC000038, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnmaddx,      0xFC00003E, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnmaddsx,     0xEC00003E, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnmsubx,      0xFC00003C, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- -([frA x frC] - frB)

  XmmVar a_mul_c(c.newXmmVar());
  // TODO(benvanik): I'm sure there's an SSE op for this.
  // NOTE: we do (frB - [frA x frC]) as that's pretty much the same.
  c.movq(a_mul_c, e.fpr_value(i.A.FRA));
  c.mulsd(a_mul_c, e.fpr_value(i.A.FRC));
  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.A.FRB));
  c.subsd(v, a_mul_c);
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fnmsubsx,     0xEC00003C, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- -([frA x frC] - frB)

  XmmVar a_mul_c(c.newXmmVar());
  // TODO(benvanik): I'm sure there's an SSE op for this.
  // NOTE: we do (frB - [frA x frC]) as that's pretty much the same.
  c.movq(a_mul_c, e.fpr_value(i.A.FRA));
  c.mulsd(a_mul_c, e.fpr_value(i.A.FRC));
  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.A.FRB));
  c.subsd(v, a_mul_c);
#if defined(ROUND_TO_SINGLE)
  // TODO(benvanik): check rounding mode? etc?
  // This converts to a single then back to a double to approximate the
  // rounding on the 360.
  c.cvtsd2ss(v, v);
  c.cvtss2sd(v, v);
#endif  // ROUND_TO_SINGLE
  e.update_fpr_value(i.A.FRT, v);

  // TODO(benvanik): update status/control register.

  if (i.A.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}


// Floating-point rounding and conversion (A-10)

XEEMITTER(fcfidx,       0xFC00069C, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fctidx,       0xFC00065C, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fctidzx,      0xFC00065E, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fctiwx,       0xFC00001C, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fctiwzx,      0xFC00001E, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(frspx,        0xFC000018, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- Round_single(frB)

#if defined(ROUND_TO_SINGLE)
  XmmVar v(c.newXmmVar());
  // TODO(benvanik): check rounding mode? etc?
  // This converts to a single then back to a double to approximate the
  // rounding on the 360.
  c.cvtsd2ss(v, e.fpr_value(i.X.RB));
  c.cvtss2sd(v, v);
#else
  XmmVar v(e.fpr_value(i.X.RB));
#endif  // ROUND_TO_SINGLE
  e.update_fpr_value(i.X.RT, v);

  // TODO(benvanik): update status/control register.

  if (i.X.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}


// Floating-point compare (A-11)

XEEMITTER(fcmpo,        0xFC000040, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fcmpu,        0xFC000000, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // if (FRA) is a NaN or (FRB) is a NaN then
  //   c <- 0b0001
  // else if (FRA) < (FRB) then
  //   c <- 0b1000
  // else if (FRA) > (FRB) then
  //   c <- 0b0100
  // else {
  //   c <- 0b0010
  // }
  // FPCC <- c
  // CR[4*BF:4*BF+3] <- c
  // if (FRA) is an SNaN or (FRB) is an SNaN then
  //   VXSNAN <- 1
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point status and control register (A

XEEMITTER(mcrfs,        0xFC000080, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mffsx,        0xFC00048E, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsb0x,      0xFC00008C, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsb1x,      0xFC00004C, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsfx,       0xFC00058E, XFL)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsfix,      0xFC00010C, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point move (A-21)

XEEMITTER(fabsx,        0xFC000210, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- abs(frB)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.X.RB));
  // XOR with 0 in the sign bit and ones everywhere else.
  GpVar gp_bit(c.newGpVar());
  c.mov(gp_bit, imm(0x7FFFFFFFFFFFFFFF));
  XmmVar bit(c.newXmmVar());
  c.movq(bit, gp_bit);
  c.xorpd(v, bit);
  e.update_fpr_value(i.X.RT, v);

  if (i.X.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fmrx,         0xFC000090, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- (frB)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.X.RB));
  e.update_fpr_value(i.X.RT, v);

  if (i.X.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}

XEEMITTER(fnabsx,       0xFC000110, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnegx,        0xFC000050, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // frD <- ¬ frB[0] || frB[1-63]

  XmmVar v(c.newXmmVar());
  c.movq(v, e.fpr_value(i.X.RB));
  // XOR with 1 in the sign bit.
  GpVar gp_bit(c.newGpVar());
  c.mov(gp_bit, imm(0x8000000000000000));
  XmmVar bit(c.newXmmVar());
  c.movq(bit, gp_bit);
  c.xorpd(v, bit);
  e.update_fpr_value(i.X.RT, v);

  if (i.X.Rc) {
    // With cr0 update.
    XEASSERTALWAYS();
    //e.update_cr_with_cond(0, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}


void X64RegisterEmitCategoryFPU() {
  XEREGISTERINSTR(faddx,        0xFC00002A);
  XEREGISTERINSTR(faddsx,       0xEC00002A);
  XEREGISTERINSTR(fdivx,        0xFC000024);
  XEREGISTERINSTR(fdivsx,       0xEC000024);
  XEREGISTERINSTR(fmulx,        0xFC000032);
  XEREGISTERINSTR(fmulsx,       0xEC000032);
  XEREGISTERINSTR(fresx,        0xEC000030);
  XEREGISTERINSTR(frsqrtex,     0xFC000034);
  XEREGISTERINSTR(fsubx,        0xFC000028);
  XEREGISTERINSTR(fsubsx,       0xEC000028);
  XEREGISTERINSTR(fselx,        0xFC00002E);
  XEREGISTERINSTR(fsqrtx,       0xFC00002C);
  XEREGISTERINSTR(fsqrtsx,      0xEC00002C);
  XEREGISTERINSTR(fmaddx,       0xFC00003A);
  XEREGISTERINSTR(fmaddsx,      0xEC00003A);
  XEREGISTERINSTR(fmsubx,       0xFC000038);
  XEREGISTERINSTR(fmsubsx,      0xEC000038);
  XEREGISTERINSTR(fnmaddx,      0xFC00003E);
  XEREGISTERINSTR(fnmaddsx,     0xEC00003E);
  XEREGISTERINSTR(fnmsubx,      0xFC00003C);
  XEREGISTERINSTR(fnmsubsx,     0xEC00003C);
  XEREGISTERINSTR(fcfidx,       0xFC00069C);
  XEREGISTERINSTR(fctidx,       0xFC00065C);
  XEREGISTERINSTR(fctidzx,      0xFC00065E);
  XEREGISTERINSTR(fctiwx,       0xFC00001C);
  XEREGISTERINSTR(fctiwzx,      0xFC00001E);
  XEREGISTERINSTR(frspx,        0xFC000018);
  XEREGISTERINSTR(fcmpo,        0xFC000040);
  XEREGISTERINSTR(fcmpu,        0xFC000000);
  XEREGISTERINSTR(mcrfs,        0xFC000080);
  XEREGISTERINSTR(mffsx,        0xFC00048E);
  XEREGISTERINSTR(mtfsb0x,      0xFC00008C);
  XEREGISTERINSTR(mtfsb1x,      0xFC00004C);
  XEREGISTERINSTR(mtfsfx,       0xFC00058E);
  XEREGISTERINSTR(mtfsfix,      0xFC00010C);
  XEREGISTERINSTR(fabsx,        0xFC000210);
  XEREGISTERINSTR(fmrx,         0xFC000090);
  XEREGISTERINSTR(fnabsx,       0xFC000110);
  XEREGISTERINSTR(fnegx,        0xFC000050);
}


}  // namespace x64
}  // namespace cpu
}  // namespace xe

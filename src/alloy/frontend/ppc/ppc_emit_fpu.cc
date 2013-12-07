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


// Good source of information:
// http://mamedev.org/source/src/emu/cpu/powerpc/ppc_ops.c
// The correctness of that code is not reflected here yet -_-


// Enable rounding numbers to single precision as required.
// This adds a bunch of work per operation and I'm not sure it's required.
#define ROUND_TO_SINGLE


// Floating-point arithmetic (A-8)

XEEMITTER(faddx,        0xFC00002A, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frA) + (frB)
  Value* v = f.Add(f.LoadFPR(i.A.FRA), f.LoadFPR(i.A.FRB));
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(faddsx,       0xEC00002A, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frA) + (frB)
  Value* v = f.Add(f.LoadFPR(i.A.FRA), f.LoadFPR(i.A.FRB));
  v = f.Convert(f.Convert(v, FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fdivx,        0xFC000024, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- frA / frB
  Value* v = f.Div(f.LoadFPR(i.A.FRA), f.LoadFPR(i.A.FRB));
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fdivsx,       0xEC000024, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- frA / frB
  Value* v = f.Div(f.LoadFPR(i.A.FRA), f.LoadFPR(i.A.FRB));
  v = f.Convert(f.Convert(v, FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fmulx,        0xFC000032, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frA) x (frC)
  Value* v = f.Mul(f.LoadFPR(i.A.FRA), f.LoadFPR(i.A.FRC));
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fmulsx,       0xEC000032, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frA) x (frC)
  Value* v = f.Mul(f.LoadFPR(i.A.FRA), f.LoadFPR(i.A.FRC));
  v = f.Convert(f.Convert(v, FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fresx,        0xEC000030, A  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(frsqrtex,     0xFC000034, A  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fsubx,        0xFC000028, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frA) - (frB)
  Value* v = f.Sub(f.LoadFPR(i.A.FRA), f.LoadFPR(i.A.FRB));
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fsubsx,       0xEC000028, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frA) - (frB)
  Value* v = f.Sub(f.LoadFPR(i.A.FRA), f.LoadFPR(i.A.FRB));
  v = f.Convert(f.Convert(v, FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fselx,        0xFC00002E, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // if (frA) >= 0.0
  // then frD <- (frC)
  // else frD <- (frB)
  Value* ge = f.CompareSGE(f.LoadFPR(i.A.FRA), f.LoadConstant(0.0));
  Value* v = f.Select(ge, f.LoadFPR(i.A.FRC), f.LoadFPR(i.A.FRB));
  f.StoreFPR(i.A.FRT, v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fsqrtx,       0xFC00002C, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // Double precision:
  // frD <- sqrt(frB)
  Value* v = f.Sqrt(f.LoadFPR(i.A.FRA));
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fsqrtsx,      0xEC00002C, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // Single precision:
  // frD <- sqrt(frB)
  Value* v = f.Sqrt(f.LoadFPR(i.A.FRA));
  v = f.Convert(f.Convert(v, FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}


// Floating-point multiply-add (A-9)

XEEMITTER(fmaddx,       0xFC00003A, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frA x frC) + frB
  Value* v = f.MulAdd(
      f.LoadFPR(i.A.FRA),
      f.LoadFPR(i.A.FRC),
      f.LoadFPR(i.A.FRB));
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fmaddsx,      0xEC00003A, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frA x frC) + frB
  Value* v = f.MulAdd(
      f.LoadFPR(i.A.FRA),
      f.LoadFPR(i.A.FRC),
      f.LoadFPR(i.A.FRB));
  v = f.Convert(f.Convert(v, FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fmsubx,       0xFC000038, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frA x frC) - frB
  Value* v = f.MulSub(
      f.LoadFPR(i.A.FRA),
      f.LoadFPR(i.A.FRC),
      f.LoadFPR(i.A.FRB));
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fmsubsx,      0xEC000038, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frA x frC) - frB
  Value* v = f.MulSub(
      f.LoadFPR(i.A.FRA),
      f.LoadFPR(i.A.FRC),
      f.LoadFPR(i.A.FRB));
  v = f.Convert(f.Convert(v, FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fnmaddx,      0xFC00003E, A  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnmaddsx,     0xEC00003E, A  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnmsubx,      0xFC00003C, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- -([frA x frC] - frB)
  Value* v = f.Neg(f.MulSub(
      f.LoadFPR(i.A.FRA),
      f.LoadFPR(i.A.FRC),
      f.LoadFPR(i.A.FRB)));
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fnmsubsx,     0xEC00003C, A  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- -([frA x frC] - frB)
  Value* v = f.Neg(f.MulSub(
      f.LoadFPR(i.A.FRA),
      f.LoadFPR(i.A.FRC),
      f.LoadFPR(i.A.FRB)));
  v = f.Convert(f.Convert(v, FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}


// Floating-point rounding and conversion (A-10)

XEEMITTER(fcfidx,       0xFC00069C, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- signed_int64_to_double( frB )
  Value* v = f.Convert(
      f.Cast(f.LoadFPR(i.A.FRB), INT64_TYPE),
      FLOAT64_TYPE);
  f.StoreFPR(i.A.FRT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fctidx,       0xFC00065C, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- double_to_signed_int64( frB )
  // TODO(benvanik): pull from FPSCR[RN]
  RoundMode round_mode = ROUND_TO_ZERO;
  Value* v = f.Convert(f.LoadFPR(i.X.RB), INT64_TYPE, round_mode);
  v = f.Cast(v, FLOAT64_TYPE);
  f.StoreFPR(i.X.RT, v);
  // f.UpdateFPRF(v);
  if (i.X.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fctidzx,      0xFC00065E, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // TODO(benvanik): assuming round to zero is always set, is that ok?
  return InstrEmit_fctidx(f, i);
}

XEEMITTER(fctiwx,       0xFC00001C, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- double_to_signed_int32( frB )
  // TODO(benvanik): pull from FPSCR[RN]
  RoundMode round_mode = ROUND_TO_ZERO;
  Value* v = f.Convert(f.LoadFPR(i.X.RB), INT32_TYPE, round_mode);
  v = f.Cast(f.ZeroExtend(v, INT64_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.X.RT, v);
  // f.UpdateFPRF(v);
  if (i.A.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fctiwzx,      0xFC00001E, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // TODO(benvanik): assuming round to zero is always set, is that ok?
  return InstrEmit_fctiwx(f, i);
}

XEEMITTER(frspx,        0xFC000018, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- Round_single(frB)
  // TODO(benvanik): pull from FPSCR[RN]
  RoundMode round_mode = ROUND_TO_ZERO;
  Value* v = f.Convert(f.LoadFPR(i.X.RB), FLOAT32_TYPE, round_mode);
  v = f.Convert(v, FLOAT64_TYPE);
  f.StoreFPR(i.X.RT, v);
  // f.UpdateFPRF(v);
  if (i.X.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}


// Floating-point compare (A-11)

int InstrEmit_fcmpx_(PPCFunctionBuilder& f, InstrData& i, bool ordered) {
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

  // TODO(benvanik): update FPCC for mffsx/etc
  // TODO(benvanik): update VXSNAN
  const uint32_t crf = i.X.RT >> 2;
  // f.UpdateFPRF(v);
  f.UpdateCR(crf, f.LoadFPR(i.X.RA), f.LoadFPR(i.X.RB), true);
  return 0;
}
XEEMITTER(fcmpo,        0xFC000040, X  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_fcmpx_(f, i, true);
}
XEEMITTER(fcmpu,        0xFC000000, X  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_fcmpx_(f, i, false);
}


// Floating-point status and control register (A

XEEMITTER(mcrfs,        0xFC000080, X  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mffsx,        0xFC00048E, X  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsb0x,      0xFC00008C, X  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsb1x,      0xFC00004C, X  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsfx,       0xFC00058E, XFL)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsfix,      0xFC00010C, X  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point move (A-21)

XEEMITTER(fabsx,        0xFC000210, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- abs(frB)
  Value* v = f.Abs(f.LoadFPR(i.X.RB));
  f.StoreFPR(i.X.RT, v);
  if (i.X.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fmrx,         0xFC000090, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- (frB)
  Value* v = f.LoadFPR(i.X.RB);
  f.StoreFPR(i.X.RT, v);
  if (i.X.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}

XEEMITTER(fnabsx,       0xFC000110, X  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnegx,        0xFC000050, X  )(PPCFunctionBuilder& f, InstrData& i) {
  // frD <- ¬ frB[0] || frB[1-63]
  Value* v = f.Neg(f.LoadFPR(i.X.RB));
  f.StoreFPR(i.X.RT, v);
  if (i.X.Rc) {
    //e.update_cr_with_cond(1, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  return 0;
}


void RegisterEmitCategoryFPU() {
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


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy

/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_disasm-private.h>


using namespace alloy::frontend::ppc;


namespace alloy {
namespace frontend {
namespace ppc {


// Floating-point arithmetic (A-8)

XEDISASMR(faddx,        0xFC00002A, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fadd", "Floating Add [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(faddsx,       0xEC00002A, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fadds", "Floating Add [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fdivx,        0xFC000024, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fdiv", "Floating Divide [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fdivsx,       0xEC000024, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fdivs", "Floating Divide [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fmulx,        0xFC000032, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fmul", "Floating Multiply [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fmulsx,       0xEC000032, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fmuls", "Floating Multiply [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fresx,        0xEC000030, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fres", "Floating Reciprocal Estimate [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(frsqrtex,     0xFC000034, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("frsqrte", "Floating Reciprocal Square Root Estimate [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fsubx,        0xFC000028, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fsub", "Floating Subtract [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fsubsx,       0xEC000028, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fsubs", "Floating Subtract [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fselx,        0xFC00002E, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fsel", "Floating Select",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fsqrtx,       0xFC00002C, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fsqrt", "Floating Square Root [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fsqrtsx,      0xEC00002C, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fsqrts", "Floating Square Root [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  return d.Finish();
}


// Floating-point multiply-add (A-9)

XEDISASMR(fmaddx,       0xFC00003A, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fmadd", "Floating Multiply-Add [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fmaddsx,      0xEC00003A, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fmadds", "Floating Multiply-Add [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fmsubx,       0xFC000038, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fmsub", "Floating Multiply-Subtract[Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fmsubsx,      0xEC000038, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fmsubs", "Floating Multiply-Subtract [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fnmaddx,      0xFC00003E, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fnmadd", "Floating Negative Multiply-Add [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fnmaddsx,     0xEC00003E, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fnmadds", "Floating Negative Multiply-Add [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fnmsubx,      0xFC00003C, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fnmsub", "Floating Negative Multiply-Subtract [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fnmsubsx,     0xEC00003C, A  )(InstrData& i, InstrDisasm& d) {
  d.Init("fnmsubs", "Floating Negative Multiply-Add [Single]",
         InstrDisasm::kFP | (i.A.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRB, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.A.FRC, InstrRegister::kRead);
  return d.Finish();
}


// Floating-point rounding and conversion (A-10)

XEDISASMR(fcfidx,       0xFC00069C, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fcfid", "Floating Convert From Integer Doubleword",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fctidx,       0xFC00065C, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fctid", "Floating Convert To Integer Doubleword",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fctidzx,      0xFC00065E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fctidz",
         "Floating Convert To Integer Doubleword with round toward Zero",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fctiwx,       0xFC00001C, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fctiw", "Floating Convert To Integer Word",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fctiwzx,      0xFC00001E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fctiwz", "Floating Convert To Integer Word with round toward Zero",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(frspx,        0xFC000018, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("frsp", "Floating Round to Single-Precision",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}


// Floating-point compare (A-11)

XEDISASMR(fcmpo,        0xFC000040, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fcmpo", "Floating Compare Ordered", 0);
  d.AddCR(i.X.RT >> 2, InstrRegister::kWrite);
  d.AddUImmOperand(i.X.RT >> 2, 1);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fcmpu,        0xFC000000, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fcmpu", "Floating Compare Unordered", 0);
  d.AddCR(i.X.RT >> 2, InstrRegister::kWrite);
  d.AddUImmOperand(i.X.RT >> 2, 1);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}


// Floating-point status and control register (A

XEDISASMR(mcrfs,        0xFC000080, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mffsx,        0xFC00048E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("mffs", "Move from FPSCR",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddFPSCR(InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  return d.Finish();
}

XEDISASMR(mtfsb0x,      0xFC00008C, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mtfsb1x,      0xFC00004C, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mtfsfx,       0xFC00058E, XFL)(InstrData& i, InstrDisasm& d) {
  d.Init("mtfsf", "Move to FPSCR Fields",
         InstrDisasm::kFP | (i.XFL.Rc ? InstrDisasm::kRc : 0));
  d.AddFPSCR(InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.XFL.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(mtfsfix,      0xFC00010C, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point move (A-21)

XEDISASMR(fabsx,        0xFC000210, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fabs", "Floating Absolute Value",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fmrx,         0xFC000090, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fmr", "Floating Move Register",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fnabsx,       0xFC000110, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fnabs", "Floating Negative Absolute Value",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(fnegx,        0xFC000050, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fneg", "Floating Negate",
         InstrDisasm::kFP | (i.X.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}


void RegisterDisasmCategoryFPU() {
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

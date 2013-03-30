/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/ppc/disasm.h>


using namespace xe::cpu::ppc;


namespace xe {
namespace cpu {
namespace ppc {


// Floating-point arithmetic (A-8)

XEDISASMR(faddx,        0xFC00002A, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(faddsx,       0xEC00002A, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fdivx,        0xFC000024, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fdivsx,       0xEC000024, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fmulx,        0xFC000032, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fmulsx,       0xEC000032, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fresx,        0xEC000030, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(frsqrtex,     0xFC000034, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fsubx,        0xFC000028, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fsubsx,       0xEC000028, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fselx,        0xFC00002E, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fsqrtx,       0xFC00002C, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fsqrtsx,      0xEC00002C, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point multiply-add (A-9)

XEDISASMR(fmaddx,       0xFC00003A, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fmaddsx,      0xEC00003A, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fmsubx,       0xFC000038, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fmsubsx,      0xEC000038, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fnmaddx,      0xFC00003E, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fnmaddsx,     0xEC00003E, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fnmsubx,      0xFC00003C, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fnmsubsx,     0xEC00003C, A  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point rounding and conversion (A-10)

XEDISASMR(fcfidx,       0xFC00069C, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fctidx,       0xFC00065C, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fctidzx,      0xFC00065E, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fctiwx,       0xFC00001C, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fctiwzx,      0xFC00001E, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(frspx,        0xFC000018, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point compare (A-11)

XEDISASMR(fcmpo,        0xFC000040, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fcmpu,        0xFC000000, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("fcmpu", "Floating Compare Unordered",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}


// Floating-point status and control register (A

XEDISASMR(mcrfs,        0xFC000080, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mffsx,        0xFC00048E, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mtfsfix,      0xFC00010C, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point move (A-21)

XEDISASMR(fabsx,        0xFC000210, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fmrx,         0xFC000090, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fnabsx,       0xFC000110, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(fnegx,        0xFC000050, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
}  // namespace cpu
}  // namespace xe

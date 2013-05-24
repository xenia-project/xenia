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


// Floating-point arithmetic (A-8)

XEEMITTER(faddx,        0xFC00002A, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(faddsx,       0xEC00002A, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fdivx,        0xFC000024, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fdivsx,       0xEC000024, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmulx,        0xFC000032, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmulsx,       0xEC000032, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fsubsx,       0xEC000028, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fselx,        0xFC00002E, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnmsubsx,     0xEC00003C, A  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmrx,         0xFC000090, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnabsx,       0xFC000110, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnegx,        0xFC000050, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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

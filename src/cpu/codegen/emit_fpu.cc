/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "cpu/codegen/emit.h"

#include <xenia/cpu/codegen/function_generator.h>


using namespace llvm;
using namespace xe::cpu::codegen;
using namespace xe::cpu::ppc;


namespace xe {
namespace cpu {
namespace codegen {


// Floating-point arithmetic (A-8)

XEEMITTER(faddx,        0xFC00002A, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(faddsx,       0xEC00002A, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fdivx,        0xFC000024, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fdivsx,       0xEC000024, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmulx,        0xFC000032, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmulsx,       0xEC000032, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fresx,        0xEC000030, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(frsqrtex,     0xFC000034, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fsubx,        0xFC000028, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fsubsx,       0xEC000028, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fselx,        0xFC00002E, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fsqrtx,       0xFC00002C, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fsqrtsx,      0xEC00002C, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point multiply-add (A-9)

XEEMITTER(fmaddx,       0xFC00003A, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmaddsx,      0xEC00003A, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmsubx,       0xFC000038, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmsubsx,      0xEC000038, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnmaddx,      0xFC00003E, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnmaddsx,     0xEC00003E, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnmsubx,      0xFC00003C, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnmsubsx,     0xEC00003C, A  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point rounding and conversion (A-10)

XEEMITTER(fcfidx,       0xFC00069C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fctidx,       0xFC00065C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fctidzx,      0xFC00065E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fctiwx,       0xFC00001C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fctiwzx,      0xFC00001E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(frspx,        0xFC000018, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point compare (A-11)

XEEMITTER(fcmpo,        0xFC000040, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fcmpu,        0xFC000000, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point status and control register (A

XEEMITTER(mcrfs,        0xFC000080, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mffsx,        0xFC00048E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsb0x,      0xFC00008C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsb1x,      0xFC00004C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsfx,       0xFC00058E, XFL)(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtfsfix,      0xFC00010C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Floating-point move (A-21)

XEEMITTER(fabsx,        0xFC000210, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fmrx,         0xFC000090, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnabsx,       0xFC000110, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(fnegx,        0xFC000050, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


void RegisterEmitCategoryFPU() {
  XEREGISTEREMITTER(faddx,        0xFC00002A);
  XEREGISTEREMITTER(faddsx,       0xEC00002A);
  XEREGISTEREMITTER(fdivx,        0xFC000024);
  XEREGISTEREMITTER(fdivsx,       0xEC000024);
  XEREGISTEREMITTER(fmulx,        0xFC000032);
  XEREGISTEREMITTER(fmulsx,       0xEC000032);
  XEREGISTEREMITTER(fresx,        0xEC000030);
  XEREGISTEREMITTER(frsqrtex,     0xFC000034);
  XEREGISTEREMITTER(fsubx,        0xFC000028);
  XEREGISTEREMITTER(fsubsx,       0xEC000028);
  XEREGISTEREMITTER(fselx,        0xFC00002E);
  XEREGISTEREMITTER(fsqrtx,       0xFC00002C);
  XEREGISTEREMITTER(fsqrtsx,      0xEC00002C);
  XEREGISTEREMITTER(fmaddx,       0xFC00003A);
  XEREGISTEREMITTER(fmaddsx,      0xEC00003A);
  XEREGISTEREMITTER(fmsubx,       0xFC000038);
  XEREGISTEREMITTER(fmsubsx,      0xEC000038);
  XEREGISTEREMITTER(fnmaddx,      0xFC00003E);
  XEREGISTEREMITTER(fnmaddsx,     0xEC00003E);
  XEREGISTEREMITTER(fnmsubx,      0xFC00003C);
  XEREGISTEREMITTER(fnmsubsx,     0xEC00003C);
  XEREGISTEREMITTER(fcfidx,       0xFC00069C);
  XEREGISTEREMITTER(fctidx,       0xFC00065C);
  XEREGISTEREMITTER(fctidzx,      0xFC00065E);
  XEREGISTEREMITTER(fctiwx,       0xFC00001C);
  XEREGISTEREMITTER(fctiwzx,      0xFC00001E);
  XEREGISTEREMITTER(frspx,        0xFC000018);
  XEREGISTEREMITTER(fcmpo,        0xFC000040);
  XEREGISTEREMITTER(fcmpu,        0xFC000000);
  XEREGISTEREMITTER(mcrfs,        0xFC000080);
  XEREGISTEREMITTER(mffsx,        0xFC00048E);
  XEREGISTEREMITTER(mtfsb0x,      0xFC00008C);
  XEREGISTEREMITTER(mtfsb1x,      0xFC00004C);
  XEREGISTEREMITTER(mtfsfx,       0xFC00058E);
  XEREGISTEREMITTER(mtfsfix,      0xFC00010C);
  XEREGISTEREMITTER(fabsx,        0xFC000210);
  XEREGISTEREMITTER(fmrx,         0xFC000090);
  XEREGISTEREMITTER(fnabsx,       0xFC000110);
  XEREGISTEREMITTER(fnegx,        0xFC000050);
}


}  // namespace codegen
}  // namespace cpu
}  // namespace xe

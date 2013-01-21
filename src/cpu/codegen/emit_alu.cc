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


// Integer arithmetic (A-3)

XEEMITTER(addx,         0x7C000214, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addcx,        0X7C000014, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addex,        0x7C000114, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addi,         0x38000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addic,        0x30000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addicx,       0x34000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addis,        0x3C000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addmex,       0x7C0001D4, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(addzex,       0x7C000194, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(divdx,        0x7C0003D2, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(divdux,       0x7C000392, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(divwx,        0x7C0003D6, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(divwux,       0x7C000396, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhdx,       0x7C000092, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhdux,      0x7C000012, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhwx,       0x7C000096, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulhwux,      0x7C000016, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulldx,       0x7C0001D2, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mulli,        0x1C000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mullwx,       0x7C0001D6, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(negx,         0x7C0000D0, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(subfx,        0x7C000050, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(subfcx,       0x7C000010, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(subficx,      0x20000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(subfex,       0x7C000110, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(subfmex,      0x7C0001D0, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(subfzex,      0x7C000190, XO )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer compare (A-4)

XEEMITTER(cmp,          0x7C000000, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(cmpi,         0x2C000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(cmpl,         0x7C000040, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(cmpli,        0x28000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer logical (A-5)

XEEMITTER(andx,         0x7C000038, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(andcx,        0x7C000078, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(andix,        0x70000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(andisx,       0x74000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(cntlzdx,      0x7C000074, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(cntlzwx,      0x7C000034, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(eqvx,         0x7C000238, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(extsbx,       0x7C000774, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(extshx,       0x7C000734, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(extswx,       0x7C0007B4, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(nandx,        0x7C0003B8, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(norx,         0x7C0000F8, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(orx,          0x7C000378, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(orcx,         0x7C000338, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(ori,          0x60000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(oris,         0x64000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(xorx,         0x7C000278, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(xori,         0x68000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(xoris,        0x6C000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer rotate (A-6)

XEEMITTER(rldclx,       0x78000010, MDS)(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rldcrx,       0x78000012, MDS)(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rldicx,       0x78000008, MD )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rldiclx,      0x78000000, MD )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rldicrx,      0x78000004, MD )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rldimix,      0x7800000C, MD )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rlwimix,      0x50000000, M  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rlwinmx,      0x54000000, M  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(rlwnmx,       0x5C000000, M  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer shift (A-7)

XEEMITTER(sldx,         0x7C000036, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(slwx,         0x7C000030, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(sradx,        0x7C000634, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(sradix,       0x7C000674, XS )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(srawx,        0x7C000630, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(srawix,       0x7C000670, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(srdx,         0x7C000436, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(srwx,         0x7C000430, X  )(FunctionGenerator& g, InstrData& i) {
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

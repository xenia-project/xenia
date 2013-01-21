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


XEEMITTER(bx,           0x48000000, I  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(bcx,          0x40000000, B  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(bcctrx,       0x4C000420, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(bclrx,        0x4C000020, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Condition register logical (A-23)

XEEMITTER(crand,        0x4C000202, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crandc,       0x4C000102, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(creqv,        0x4C000242, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crnand,       0x4C0001C2, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crnor,        0x4C000042, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(cror,         0x4C000382, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crorc,        0x4C000342, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crxor,        0x4C000182, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mcrf,         0x4C000000, XL )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// System linkage (A-24)

XEEMITTER(sc,           0x44000002, SC )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Trap (A-25)

XEEMITTER(td,           0x7C000088, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(tdi,          0x08000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(tw,           0x7C000008, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(twi,          0x0C000000, D  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Processor control (A-26)

XEEMITTER(mfcr,         0x7C000026, X  )(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mfspr,        0x7C0002A6, XFX)(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mftb,         0x7C0002E6, XFX)(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtcrf,        0x7C000120, XFX)(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtspr,        0x7C0003A6, XFX)(FunctionGenerator& g, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


void RegisterEmitCategoryControl() {
  XEREGISTEREMITTER(bx,           0x48000000);
  XEREGISTEREMITTER(bcx,          0x40000000);
  XEREGISTEREMITTER(bcctrx,       0x4C000420);
  XEREGISTEREMITTER(bclrx,        0x4C000020);
  XEREGISTEREMITTER(crand,        0x4C000202);
  XEREGISTEREMITTER(crandc,       0x4C000102);
  XEREGISTEREMITTER(creqv,        0x4C000242);
  XEREGISTEREMITTER(crnand,       0x4C0001C2);
  XEREGISTEREMITTER(crnor,        0x4C000042);
  XEREGISTEREMITTER(cror,         0x4C000382);
  XEREGISTEREMITTER(crorc,        0x4C000342);
  XEREGISTEREMITTER(crxor,        0x4C000182);
  XEREGISTEREMITTER(mcrf,         0x4C000000);
  XEREGISTEREMITTER(sc,           0x44000002);
  XEREGISTEREMITTER(td,           0x7C000088);
  XEREGISTEREMITTER(tdi,          0x08000000);
  XEREGISTEREMITTER(tw,           0x7C000008);
  XEREGISTEREMITTER(twi,          0x0C000000);
  XEREGISTEREMITTER(mfcr,         0x7C000026);
  XEREGISTEREMITTER(mfspr,        0x7C0002A6);
  XEREGISTEREMITTER(mftb,         0x7C0002E6);
  XEREGISTEREMITTER(mtcrf,        0x7C000120);
  XEREGISTEREMITTER(mtspr,        0x7C0003A6);
}


}  // namespace codegen
}  // namespace cpu
}  // namespace xe

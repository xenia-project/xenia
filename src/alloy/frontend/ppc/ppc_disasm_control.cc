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


XEDISASMR(bx,           0x48000000, I  )(InstrData& i, InstrDisasm& d) {
  d.Init("b", "Branch", i.I.LK ? InstrDisasm::kLR : 0);
  uint32_t nia;
  if (i.I.AA) {
    nia = (uint32_t)XEEXTS26(i.I.LI << 2);
  } else {
    nia = (uint32_t)(i.address + XEEXTS26(i.I.LI << 2));
  }
  d.AddUImmOperand(nia, 4);
  return d.Finish();
}

XEDISASMR(bcx,          0x40000000, B  )(InstrData& i, InstrDisasm& d) {
  // TODO(benvanik): mnemonics
  d.Init("bc", "Branch Conditional", i.B.LK ? InstrDisasm::kLR : 0);
  if (!XESELECTBITS(i.B.BO, 2, 2)) {
    d.AddCTR(InstrRegister::kReadWrite);
  }
  if (!XESELECTBITS(i.B.BO, 4, 4)) {
    d.AddCR(i.B.BI >> 2, InstrRegister::kRead);
  }
  d.AddUImmOperand(i.B.BO, 1);
  d.AddUImmOperand(i.B.BI, 1);
  uint32_t nia;
  if (i.B.AA) {
    nia = (uint32_t)XEEXTS16(i.B.BD << 2);
  } else {
    nia = (uint32_t)(i.address + XEEXTS16(i.B.BD << 2));
  }
  d.AddUImmOperand(nia, 4);
  return d.Finish();
}

XEDISASMR(bcctrx,       0x4C000420, XL )(InstrData& i, InstrDisasm& d) {
  // TODO(benvanik): mnemonics
  d.Init("bcctr", "Branch Conditional to Count Register",
      i.XL.LK ? InstrDisasm::kLR : 0);
  if (!XESELECTBITS(i.XL.BO, 4, 4)) {
    d.AddCR(i.XL.BI >> 2, InstrRegister::kRead);
  }
  d.AddUImmOperand(i.XL.BO, 1);
  d.AddUImmOperand(i.XL.BI, 1);
  d.AddCTR(InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(bclrx,        0x4C000020, XL )(InstrData& i, InstrDisasm& d) {
  const char* name = "bclr";
  if (i.code == 0x4E800020) {
    name = "blr";
  }
  d.Init(name, "Branch Conditional to Link Register",
      i.XL.LK ? InstrDisasm::kLR : 0);
  if (!XESELECTBITS(i.B.BO, 2, 2)) {
    d.AddCTR(InstrRegister::kReadWrite);
  }
  if (!XESELECTBITS(i.B.BO, 4, 4)) {
    d.AddCR(i.B.BI >> 2, InstrRegister::kRead);
  }
  d.AddUImmOperand(i.XL.BO, 1);
  d.AddUImmOperand(i.XL.BI, 1);
  d.AddLR(InstrRegister::kRead);
  return d.Finish();
}


// Condition register logical (A-23)

XEDISASMR(crand,        0x4C000202, XL )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(crandc,       0x4C000102, XL )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(creqv,        0x4C000242, XL )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(crnand,       0x4C0001C2, XL )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(crnor,        0x4C000042, XL )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(cror,         0x4C000382, XL )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(crorc,        0x4C000342, XL )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(crxor,        0x4C000182, XL )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mcrf,         0x4C000000, XL )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// System linkage (A-24)

XEDISASMR(sc,           0x44000002, SC )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Trap (A-25)

XEDISASMR(td,           0x7C000088, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("td", "Trap Doubleword", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(tdi,          0x08000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("tdi", "Trap Doubleword Immediate", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(tw,           0x7C000008, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("tw", "Trap Word", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(twi,          0x0C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("twi", "Trap Word Immediate", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  return d.Finish();
}


// Processor control (A-26)

XEDISASMR(mfcr,         0x7C000026, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("mfcr", "Move From Condition Register", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  for (int n = 0; n < 8; n++) {
    d.AddCR(0, InstrRegister::kRead);
  }
  return d.Finish();
}

XEDISASMR(mfspr,        0x7C0002A6, XFX)(InstrData& i, InstrDisasm& d) {
  d.Init("mfspr", "Move From Special Purpose Register", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.XFX.RT, InstrRegister::kWrite);
  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  switch (n) {
  case 1:
    d.AddRegOperand(InstrRegister::kXER, 0, InstrRegister::kRead);
    break;
  case 8:
    d.AddRegOperand(InstrRegister::kLR, 0, InstrRegister::kRead);
    break;
  case 9:
    d.AddRegOperand(InstrRegister::kCTR, 0, InstrRegister::kRead);
    break;
  }
  return d.Finish();
}

XEDISASMR(mftb,         0x7C0002E6, XFX)(InstrData& i, InstrDisasm& d) {
  d.Init("mftb", "Move From Time Base", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.XFX.RT, InstrRegister::kWrite);
  return d.Finish();
}

XEDISASMR(mtcrf,        0x7C000120, XFX)(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(mtspr,        0x7C0003A6, XFX)(InstrData& i, InstrDisasm& d) {
  d.Init("mtspr", "Move To Special Purpose Register", 0);
  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  switch (n) {
  case 1:
    d.AddRegOperand(InstrRegister::kXER, 0, InstrRegister::kWrite);
    break;
  case 8:
    d.AddRegOperand(InstrRegister::kLR, 0, InstrRegister::kWrite);
    break;
  case 9:
    d.AddRegOperand(InstrRegister::kCTR, 0, InstrRegister::kWrite);
    break;
  }
  d.AddRegOperand(InstrRegister::kGPR, i.XFX.RT, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(mfmsr,        0x7C0000A6, X)(InstrData& i, InstrDisasm& d) {
  d.Init("mfmsr", "Move From Machine State Register", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  return d.Finish();
}

XEDISASMR(mtmsr,        0x7C000124, X)(InstrData& i, InstrDisasm& d) {
  d.Init("mtmsr", "Move To Machine State Register", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddSImmOperand((i.X.RA & 16) ? 1 : 0, 1);
  return d.Finish();
}

XEDISASMR(mtmsrd,       0x7C000164, X)(InstrData& i, InstrDisasm& d) {
  d.Init("mtmsrd", "Move To Machine State Register Doubleword", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddSImmOperand((i.X.RA & 16) ? 1 : 0, 1);
  return d.Finish();
}


void RegisterDisasmCategoryControl() {
  XEREGISTERINSTR(bx,           0x48000000);
  XEREGISTERINSTR(bcx,          0x40000000);
  XEREGISTERINSTR(bcctrx,       0x4C000420);
  XEREGISTERINSTR(bclrx,        0x4C000020);
  XEREGISTERINSTR(crand,        0x4C000202);
  XEREGISTERINSTR(crandc,       0x4C000102);
  XEREGISTERINSTR(creqv,        0x4C000242);
  XEREGISTERINSTR(crnand,       0x4C0001C2);
  XEREGISTERINSTR(crnor,        0x4C000042);
  XEREGISTERINSTR(cror,         0x4C000382);
  XEREGISTERINSTR(crorc,        0x4C000342);
  XEREGISTERINSTR(crxor,        0x4C000182);
  XEREGISTERINSTR(mcrf,         0x4C000000);
  XEREGISTERINSTR(sc,           0x44000002);
  XEREGISTERINSTR(td,           0x7C000088);
  XEREGISTERINSTR(tdi,          0x08000000);
  XEREGISTERINSTR(tw,           0x7C000008);
  XEREGISTERINSTR(twi,          0x0C000000);
  XEREGISTERINSTR(mfcr,         0x7C000026);
  XEREGISTERINSTR(mfspr,        0x7C0002A6);
  XEREGISTERINSTR(mftb,         0x7C0002E6);
  XEREGISTERINSTR(mtcrf,        0x7C000120);
  XEREGISTERINSTR(mtspr,        0x7C0003A6);
  XEREGISTERINSTR(mfmsr,        0x7C0000A6);
  XEREGISTERINSTR(mtmsr,        0x7C000124);
  XEREGISTERINSTR(mtmsrd,       0x7C000164);
}


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy

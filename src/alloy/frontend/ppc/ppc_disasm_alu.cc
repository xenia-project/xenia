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


// Integer arithmetic (A-3)

XEDISASMR(addx,         0x7C000214, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("add", "Add",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(addcx,        0x7C000014, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("addc", "Add Carrying",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(addex,        0x7C000114, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("adde", "Add Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(addi,         0x38000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("addi", "Add Immediate", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddSImmOperand(0, 4);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(addic,        0x30000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("addic", "Add Immediate Carrying", InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(addicx,       0x34000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("addic", "Add Immediate Carrying and Record",
         InstrDisasm::kRc | InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(addis,        0x3C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("addis", "Add Immediate Shifted", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddSImmOperand(0, 4);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(addmex,       0x7C0001D4, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("addme", "Add to Minus One Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(addzex,       0x7C000194, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("addze", "Add to Zero Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(divdx,        0x7C0003D2, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("divd", "Divide Doubleword",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(divdux,       0x7C000392, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("divdu", "Divide Doubleword Unsigned",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(divwx,        0x7C0003D6, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("divw", "Divide Word",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(divwux,       0x7C000396, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("divwu", "Divide Word Unsigned",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(mulhdx,       0x7C000092, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mulhd", "Multiply High Doubleword", i.XO.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(mulhdux,      0x7C000012, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mulhdu", "Multiply High Doubleword Unsigned",
         i.XO.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(mulhwx,       0x7C000096, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mulhw", "Multiply High Word", i.XO.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(mulhwux,      0x7C000016, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mulhwu", "Multiply High Word Unsigned",
         i.XO.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(mulldx,       0x7C0001D2, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mulld", "Multiply Low Doubleword",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(mulli,        0x1C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("mulli", "Multiply Low Immediate", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(mullwx,       0x7C0001D6, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("mullw", "Multiply Low Word",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(negx,         0x7C0000D0, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("neg", "Negate",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(subfx,        0x7C000050, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("subf", "Subtract From",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0));
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(subfcx,       0x7C000010, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("subfc", "Subtract From Carrying",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(subficx,      0x20000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("subfic", "Subtract From Immediate Carrying", InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(subfex,       0x7C000110, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("subfe", "Subtract From Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(subfmex,      0x7C0001D0, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("subfme", "Subtract From Minus One Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(subfzex,      0x7C000190, XO )(InstrData& i, InstrDisasm& d) {
  d.Init("subfze", "Subtract From Zero Extended",
         (i.XO.OE ? InstrDisasm::kOE : 0) | (i.XO.Rc ? InstrDisasm::kRc : 0) |
         InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XO.RA, InstrRegister::kRead);
  return d.Finish();
}


// Integer compare (A-4)

XEDISASMR(cmp,          0x7C000000, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("cmp", "Compare", 0);
  d.AddCR(i.X.RT >> 2, InstrRegister::kWrite);
  d.AddUImmOperand(i.X.RT >> 2, 1);
  d.AddUImmOperand(i.X.RT & 1, 1);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(cmpi,         0x2C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("cmpi", "Compare Immediate", 0);
  d.AddCR(i.D.RT >> 2, InstrRegister::kWrite);
  d.AddUImmOperand(i.D.RT >> 2, 1);
  d.AddUImmOperand(i.D.RT & 1, 1);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(cmpl,         0x7C000040, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("cmpl", "Compare Logical", 0);
  d.AddCR(i.X.RT >> 2, InstrRegister::kWrite);
  d.AddUImmOperand(i.X.RT >> 2, 1);
  d.AddUImmOperand(i.X.RT & 1, 1);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(cmpli,        0x28000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("cmpli", "Compare Logical Immediate", 0);
  d.AddCR(i.D.RT >> 2, InstrRegister::kWrite);
  d.AddUImmOperand(i.D.RT >> 2, 1);
  d.AddUImmOperand(i.D.RT & 1, 1);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  d.AddSImmOperand(i.D.DS, 2);
  return d.Finish();
}


// Integer logical (A-5)

XEDISASMR(andx,         0x7C000038, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("and", "AND", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(andcx,        0x7C000078, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("andc", "AND with Complement", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(andix,        0x70000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("andi", "AND Immediate", 0);
  d.AddCR(0, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.D.DS, 2);
  return d.Finish();
}

XEDISASMR(andisx,       0x74000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("andi", "AND Immediate Shifted", 0);
  d.AddCR(0, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.D.DS, 2);
  return d.Finish();
}

XEDISASMR(cntlzdx,      0x7C000074, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("cntlzd", "Count Leading Zeros Doubleword",
         i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(cntlzwx,      0x7C000034, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("cntlzw", "Count Leading Zeros Word",
         i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(eqvx,         0x7C000238, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("eqv", "Equivalent", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(extsbx,       0x7C000774, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("extsb", "Extend Sign Byte", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(extshx,       0x7C000734, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("extsh", "Extend Sign Halfword", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(extswx,       0x7C0007B4, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("extsw", "Extend Sign Word", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(nandx,        0x7C0003B8, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("nand", "NAND", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(norx,         0x7C0000F8, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("nor", "NOR", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(orx,          0x7C000378, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("or", "OR", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(orcx,         0x7C000338, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("orc", "OR with Complement", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(ori,          0x60000000, D  )(InstrData& i, InstrDisasm& d) {
  if (!i.D.RA && !i.D.RT && !i.D.DS) {
    d.Init("no-op", "OR Immediate", 0);
  } else {
    d.Init("ori", "OR Immediate", 0);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.D.DS, 2);
  return d.Finish();
}

XEDISASMR(oris,         0x64000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("oris", "OR Immediate Shifted", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.D.DS, 2);
  return d.Finish();
}

XEDISASMR(xorx,         0x7C000278, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("xor", "XOR", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(xori,         0x68000000, D  )(InstrData& i, InstrDisasm& d) {
  if (!i.D.RA && !i.D.RT && !i.D.DS) {
    d.Init("xnop", "XOR Immediate", 0);
  } else {
    d.Init("xori", "XOR Immediate", 0);
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
    d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
    d.AddUImmOperand(i.D.DS, 2);
  }
  return d.Finish();
}

XEDISASMR(xoris,        0x6C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("xoris", "XOR Immediate Shifted", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.D.DS, 2);
  return d.Finish();
}


// Integer rotate (A-6)

XEDISASMR(rld,          0x78000000, MDS)(InstrData& i, InstrDisasm& d) {
  if (i.MD.idx == 0) {
    // XEDISASMR(rldiclx,      0x78000000, MD )
    d.Init("rldicl", "Rotate Left Doubleword Immediate then Clear Left",
           i.MD.Rc ? InstrDisasm::kRc : 0);
    d.AddRegOperand(InstrRegister::kGPR, i.MD.RA, InstrRegister::kWrite);
    d.AddRegOperand(InstrRegister::kGPR, i.MD.RT, InstrRegister::kRead);
    d.AddUImmOperand((i.MD.SH5 << 5) | i.MD.SH, 1);
    d.AddUImmOperand((i.MD.MB5 << 5) | i.MD.MB, 1);
    return d.Finish();
  } else if (i.MD.idx == 1) {
    // XEDISASMR(rldicrx,      0x78000004, MD )
    d.Init("rldicr", "Rotate Left Doubleword Immediate then Clear Right",
           i.MD.Rc ? InstrDisasm::kRc : 0);
    d.AddRegOperand(InstrRegister::kGPR, i.MD.RA, InstrRegister::kWrite);
    d.AddRegOperand(InstrRegister::kGPR, i.MD.RT, InstrRegister::kRead);
    d.AddUImmOperand((i.MD.SH5 << 5) | i.MD.SH, 1);
    d.AddUImmOperand((i.MD.MB5 << 5) | i.MD.MB, 1);
    return d.Finish();
  } else if (i.MD.idx == 2) {
    // XEDISASMR(rldicx,       0x78000008, MD )
    const char* name;
    const char* desc;
    uint32_t sh = (i.MD.SH5 << 5) | i.MD.SH;
    uint32_t mb = (i.MD.MB5 << 5) | i.MD.MB;
    if (mb == 0x3E) {
      name = "sldi";
      desc = "Shift Left Immediate";
    } else {
      name = "rldic";
      desc = "Rotate Left Doubleword Immediate then Clear";
    }
    d.Init(name, desc,
           i.MD.Rc ? InstrDisasm::kRc : 0);
    d.AddRegOperand(InstrRegister::kGPR, i.MD.RA, InstrRegister::kWrite);
    d.AddRegOperand(InstrRegister::kGPR, i.MD.RT, InstrRegister::kRead);
    d.AddUImmOperand(sh, 1);
    d.AddUImmOperand(mb, 1);
    return d.Finish();
  } else if (i.MDS.idx == 8) {
    // XEDISASMR(rldclx,       0x78000010, MDS)
    d.Init("rldcl", "Rotate Left Doubleword then Clear Left",
           i.MDS.Rc ? InstrDisasm::kRc : 0);
    d.AddRegOperand(InstrRegister::kGPR, i.MDS.RA, InstrRegister::kWrite);
    d.AddRegOperand(InstrRegister::kGPR, i.MDS.RT, InstrRegister::kRead);
    d.AddRegOperand(InstrRegister::kGPR, i.MDS.RB, InstrRegister::kRead);
    d.AddUImmOperand((i.MDS.MB5 << 5) | i.MDS.MB, 1);
    return d.Finish();
  } else if (i.MDS.idx == 9) {
    // XEDISASMR(rldcrx,       0x78000012, MDS)
    d.Init("rldcr", "Rotate Left Doubleword then Clear Right",
           i.MDS.Rc ? InstrDisasm::kRc : 0);
    d.AddRegOperand(InstrRegister::kGPR, i.MDS.RA, InstrRegister::kWrite);
    d.AddRegOperand(InstrRegister::kGPR, i.MDS.RT, InstrRegister::kRead);
    d.AddRegOperand(InstrRegister::kGPR, i.MDS.RB, InstrRegister::kRead);
    d.AddUImmOperand((i.MDS.MB5 << 5) | i.MDS.MB, 1);
    return d.Finish();
  } else if (i.MD.idx == 3) {
    // XEDISASMR(rldimix,      0x7800000C, MD )
    d.Init("rldimi", "Rotate Left Doubleword Immediate then Mask Insert",
           i.MD.Rc ? InstrDisasm::kRc : 0);
    d.AddRegOperand(InstrRegister::kGPR, i.MD.RA, InstrRegister::kReadWrite);
    d.AddRegOperand(InstrRegister::kGPR, i.MD.RT, InstrRegister::kRead);
    d.AddUImmOperand((i.MD.SH5 << 5) | i.MD.SH, 1);
    d.AddUImmOperand((i.MD.MB5 << 5) | i.MD.MB, 1);
    return d.Finish();
  } else {
    XEASSERTALWAYS();
    return 1;
  }
}

XEDISASMR(rlwimix,      0x50000000, M  )(InstrData& i, InstrDisasm& d) {
  d.Init("rlwimi", "Rotate Left Word Immediate then Mask Insert",
         i.M.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.M.SH, 1);
  d.AddUImmOperand(i.M.MB, 1);
  d.AddUImmOperand(i.M.ME, 1);
  return d.Finish();
}

XEDISASMR(rlwinmx,      0x54000000, M  )(InstrData& i, InstrDisasm& d) {
  d.Init("rlwinm", "Rotate Left Word Immediate then AND with Mask",
         i.M.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.M.SH, 1);
  d.AddUImmOperand(i.M.MB, 1);
  d.AddUImmOperand(i.M.ME, 1);
  return d.Finish();
}

XEDISASMR(rlwnmx,       0x5C000000, M  )(InstrData& i, InstrDisasm& d) {
  d.Init("rlwnm", "Rotate Left Word then AND with Mask",
         i.M.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.M.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.M.SH, InstrRegister::kRead);
  d.AddUImmOperand(i.M.MB, 1);
  d.AddUImmOperand(i.M.ME, 1);
  return d.Finish();
}


// Integer shift (A-7)

XEDISASMR(sldx,         0x7C000036, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("sld", "Shift Left Doubleword", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(slwx,         0x7C000030, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("slw", "Shift Left Word", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(sradx,        0x7C000634, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("srad", "Shift Right Algebraic Doubleword",
         i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(sradix,       0x7C000674, XS )(InstrData& i, InstrDisasm& d) {
  d.Init("sradi", "Shift Right Algebraic Doubleword Immediate",
         (i.XS.Rc ? InstrDisasm::kRc : 0) | InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.XS.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.XS.RT, InstrRegister::kRead);
  d.AddUImmOperand((i.XS.SH5 << 5) | i.XS.SH, 1);
  return d.Finish();
}

XEDISASMR(srawx,        0x7C000630, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("sraw", "Shift Right Algebraic Word",
         (i.X.Rc ? InstrDisasm::kRc : 0) | InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(srawix,       0x7C000670, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("srawi", "Shift Right Algebraic Word Immediate",
         (i.X.Rc ? InstrDisasm::kRc : 0) | InstrDisasm::kCA);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddUImmOperand(i.X.RB, 1);
  return d.Finish();
}

XEDISASMR(srdx,         0x7C000436, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("srd", "Shift Right Doubleword", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(srwx,         0x7C000430, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("srw", "Shift Right Word", i.X.Rc ? InstrDisasm::kRc : 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}


void RegisterDisasmCategoryALU() {
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


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy

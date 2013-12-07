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


// Integer load (A-13)

XEDISASMR(lbz,          0x88000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lbz", "Load Byte and Zero", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lbzu,         0x8C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lbzu", "Load Byte and Zero with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lbzux,        0x7C0000EE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lbzux", "Load Byte and Zero with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lbzx,         0x7C0000AE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lbzx", "Load Byte and Zero Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(ld,           0xE8000000, DS )(InstrData& i, InstrDisasm& d) {
  d.Init("ld", "Load Doubleword", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RT, InstrRegister::kWrite);
  if (i.DS.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.DS.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.DS.DS << 2), 2);
  return d.Finish();
}

XEDISASMR(ldu,          0xE8000001, DS )(InstrData& i, InstrDisasm& d) {
  d.Init("ldu", "Load Doubleword with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.DS.DS << 2), 2);
  return d.Finish();
}

XEDISASMR(ldux,         0x7C00006A, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("ldux", "Load Doubleword with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(ldx,          0x7C00002A, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("ldx", "Load Doubleword Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lha,          0xA8000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lha", "Load Halfword Algebraic", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lhau,         0xAC000000, D  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(lhaux,        0x7C0002EE, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(lhax,         0x7C0002AE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lhax", "Load Halfword Algebraic Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lhz,          0xA0000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lhz", "Load Halfword and Zero", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lhzu,         0xA4000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lhzu", "Load Halfword and Zero with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lhzux,        0x7C00026E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lhzux", "Load Halfword and Zero with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lhzx,         0x7C00022E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lhzx", "Load Halfword and Zero Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lwa,          0xE8000002, DS )(InstrData& i, InstrDisasm& d) {
  d.Init("lwa", "Load Word Algebraic", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.DS.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.DS.DS << 2), 2);
  return d.Finish();
}

XEDISASMR(lwaux,        0x7C0002EA, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwaux", "Load Word Algebraic with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lwax,         0x7C0002AA, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwax", "Load Word Algebraic Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lwz,          0x80000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwz", "Load Word and Zero", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lwzu,         0x84000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwzu", "Load Word and Zero with Udpate", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lwzux,        0x7C00006E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwzux", "Load Word and Zero with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lwzx,         0x7C00002E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwzx", "Load Word and Zero Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}


// Integer store (A-14)

XEDISASMR(stb,          0x98000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stb", "Store Byte", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(stbu,         0x9C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stbu", "Store Byte with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(stbux,        0x7C0001EE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stbux", "Store Byte with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stbx,         0x7C0001AE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stbx", "Store Byte Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  if (i.DS.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(std,          0xF8000000, DS )(InstrData& i, InstrDisasm& d) {
  d.Init("std", "Store Doubleword", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RT, InstrRegister::kRead);
  if (i.DS.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.DS.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.DS.DS << 2), 2);
  return d.Finish();
}

XEDISASMR(stdu,         0xF8000001, DS )(InstrData& i, InstrDisasm& d) {
  d.Init("stdu", "Store Doubleword with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.DS.DS << 2), 2);
  return d.Finish();
}

XEDISASMR(stdux,        0x7C00016A, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stdux", "Store Doubleword with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stdx,         0x7C00012A, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stdx", "Store Doubleword Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(sth,          0xB0000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("sth", "Store Halfword", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(sthu,         0xB4000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("sthu", "Store Halfword with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(sthux,        0x7C00036E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("sthux", "Store Halfword with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(sthx,         0x7C00032E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("sth", "Store Halfword Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stw,          0x90000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stw", "Store Word", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(stwu,         0x94000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stwu", "Store Word with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(stwux,        0x7C00016E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stwux", "Store Word with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stwx,         0x7C00012E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stwx", "Store Word Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}


// Integer load and store with byte reverse (A-1

XEDISASMR(lhbrx,        0x7C00062C, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lhbrx", "Load Halfword Byte-Reverse Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lwbrx,        0x7C00042C, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwbrx", "Load Word Byte-Reverse Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(ldbrx,        0x7C000428, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("ldbrx", "Load Doubleword Byte-Reverse Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(sthbrx,       0x7C00072C, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("sthbrx", "Store Halfword Byte-Reverse Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stwbrx,       0x7C00052C, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stwbrx", "Store Word Byte-Reverse Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stdbrx,       0x7C000528, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stdbrx", "Store Doubleword Byte-Reverse Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}


// Integer load and store multiple (A-16)

XEDISASMR(lmw,          0xB8000000, D  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(stmw,         0xBC000000, D  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer load and store string (A-17)

XEDISASMR(lswi,         0x7C0004AA, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(lswx,         0x7C00042A, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(stswi,        0x7C0005AA, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(stswx,        0x7C00052A, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Memory synchronization (A-18)

XEDISASMR(eieio,        0x7C0006AC, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("eieio", "Enforce In-Order Execution of I/O Instruction", 0);
  return d.Finish();
}

XEDISASMR(isync,        0x4C00012C, XL )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(ldarx,        0x7C0000A8, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("ldarx", "Load Doubleword And Reserve Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lwarx,        0x7C000028, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwarx", "Load Word And Reserve Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stdcx,        0x7C0001AD, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stdcx", "Store Doubleword Conditional Indexed",
      InstrDisasm::kRc);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stwcx,        0x7C00012D, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stwcx", "Store Word Conditional Indexed",
      InstrDisasm::kRc);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(sync,         0x7C0004AC, X  )(InstrData& i, InstrDisasm& d) {
  const char* name;
  const char* desc;
  int L = i.X.RT & 3;
  switch (L) {
  case 0:
    name = "hwsync";
    desc = "Synchronize (heavyweight)";
    break;
  case 1:
    name = "lwsync";
    desc = "Synchronize (lightweight)";
    break;
  default:
  case 2:
  case 3:
    name = "sync";
    desc = "Synchronize";
    break;
  }
  d.Init(name, desc, 0);
  d.AddUImmOperand(L, 1);
  return d.Finish();
}


// Floating-point load (A-19)

XEDISASMR(lfd,          0xC8000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfd", "Load Floating-Point Double", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lfdu,         0xCC000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfdu", "Load Floating-Point Double with Update", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lfdux,        0x7C0004EE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfdux", "Load Floating-Point Double with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lfdx,         0x7C0004AE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfdx", "Load Floating-Point Double Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lfs,          0xC0000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfs", "Load Floating-Point Single", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kWrite);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lfsu,         0xC4000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfsu", "Load Floating-Point Single with Update", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(lfsux,        0x7C00046E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfsux", "Load Floating-Point Single with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(lfsx,         0x7C00042E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfsx", "Load Floating-Point Single Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}


// Floating-point store (A-20)

XEDISASMR(stfd,         0xD8000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfd", "Store Floating-Point Double", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kRead);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(stfdu,        0xDC000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfdu", "Store Floating-Point Double with Update", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(stfdux,       0x7C0005EE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfdux", "Store Floating-Point Double with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stfdx,        0x7C0005AE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfdx", "Store Floating-Point Double Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kRead);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stfiwx,       0x7C0007AE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfiwx", "Store Floating-Point as Integer Word Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kRead);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stfs,         0xD0000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfs", "Store Floating-Point Single", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kRead);
  if (i.D.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(stfsu,        0xD4000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfsu", "Store Floating-Point Single with Update", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}

XEDISASMR(stfsux,       0x7C00056E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfsux", "Store Floating-Point Single with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(stfsx,        0x7C00052E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfsx", "Store Floating-Point Single Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kRead);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}


// Cache management (A-27)

XEDISASMR(dcbf,         0x7C0000AC, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("dcbf", "Data Cache Block Flush", 0);
  /*
  switch (i.X.RT & 3)
  {
    case 0: "dcbf";
    case 1: "dcbfl"
    case 2: RESERVED
    case 3: "dcbflp"
  }
  */
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(dcbst,        0x7C00006C, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(dcbt,         0x7C00022C, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("dcbt", "Data Cache Block Touch", 0);
  // TODO
  return d.Finish();
}

XEDISASMR(dcbtst,       0x7C0001EC, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("dcbtst", "Data Cache Block Touch for Store", 0);
  // TODO
  return d.Finish();
}

XEDISASMR(dcbz,         0x7C0007EC, X  )(InstrData& i, InstrDisasm& d) {
  // or dcbz128 0x7C2007EC
  d.Init("dcbz", "Data Cache Block set to Zero", 0);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(icbi,         0x7C0007AC, X  )(InstrData& i, InstrDisasm& d) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


void RegisterDisasmCategoryMemory() {
  XEREGISTERINSTR(lbz,          0x88000000);
  XEREGISTERINSTR(lbzu,         0x8C000000);
  XEREGISTERINSTR(lbzux,        0x7C0000EE);
  XEREGISTERINSTR(lbzx,         0x7C0000AE);
  XEREGISTERINSTR(ld,           0xE8000000);
  XEREGISTERINSTR(ldu,          0xE8000001);
  XEREGISTERINSTR(ldux,         0x7C00006A);
  XEREGISTERINSTR(ldx,          0x7C00002A);
  XEREGISTERINSTR(lha,          0xA8000000);
  XEREGISTERINSTR(lhau,         0xAC000000);
  XEREGISTERINSTR(lhaux,        0x7C0002EE);
  XEREGISTERINSTR(lhax,         0x7C0002AE);
  XEREGISTERINSTR(lhz,          0xA0000000);
  XEREGISTERINSTR(lhzu,         0xA4000000);
  XEREGISTERINSTR(lhzux,        0x7C00026E);
  XEREGISTERINSTR(lhzx,         0x7C00022E);
  XEREGISTERINSTR(lwa,          0xE8000002);
  XEREGISTERINSTR(lwaux,        0x7C0002EA);
  XEREGISTERINSTR(lwax,         0x7C0002AA);
  XEREGISTERINSTR(lwz,          0x80000000);
  XEREGISTERINSTR(lwzu,         0x84000000);
  XEREGISTERINSTR(lwzux,        0x7C00006E);
  XEREGISTERINSTR(lwzx,         0x7C00002E);
  XEREGISTERINSTR(stb,          0x98000000);
  XEREGISTERINSTR(stbu,         0x9C000000);
  XEREGISTERINSTR(stbux,        0x7C0001EE);
  XEREGISTERINSTR(stbx,         0x7C0001AE);
  XEREGISTERINSTR(std,          0xF8000000);
  XEREGISTERINSTR(stdu,         0xF8000001);
  XEREGISTERINSTR(stdux,        0x7C00016A);
  XEREGISTERINSTR(stdx,         0x7C00012A);
  XEREGISTERINSTR(sth,          0xB0000000);
  XEREGISTERINSTR(sthu,         0xB4000000);
  XEREGISTERINSTR(sthux,        0x7C00036E);
  XEREGISTERINSTR(sthx,         0x7C00032E);
  XEREGISTERINSTR(stw,          0x90000000);
  XEREGISTERINSTR(stwu,         0x94000000);
  XEREGISTERINSTR(stwux,        0x7C00016E);
  XEREGISTERINSTR(stwx,         0x7C00012E);
  XEREGISTERINSTR(lhbrx,        0x7C00062C);
  XEREGISTERINSTR(lwbrx,        0x7C00042C);
  XEREGISTERINSTR(ldbrx,        0x7C000428);
  XEREGISTERINSTR(sthbrx,       0x7C00072C);
  XEREGISTERINSTR(stwbrx,       0x7C00052C);
  XEREGISTERINSTR(stdbrx,       0x7C000528);
  XEREGISTERINSTR(lmw,          0xB8000000);
  XEREGISTERINSTR(stmw,         0xBC000000);
  XEREGISTERINSTR(lswi,         0x7C0004AA);
  XEREGISTERINSTR(lswx,         0x7C00042A);
  XEREGISTERINSTR(stswi,        0x7C0005AA);
  XEREGISTERINSTR(stswx,        0x7C00052A);
  XEREGISTERINSTR(eieio,        0x7C0006AC);
  XEREGISTERINSTR(isync,        0x4C00012C);
  XEREGISTERINSTR(ldarx,        0x7C0000A8);
  XEREGISTERINSTR(lwarx,        0x7C000028);
  XEREGISTERINSTR(stdcx,        0x7C0001AD);
  XEREGISTERINSTR(stwcx,        0x7C00012D);
  XEREGISTERINSTR(sync,         0x7C0004AC);
  XEREGISTERINSTR(lfd,          0xC8000000);
  XEREGISTERINSTR(lfdu,         0xCC000000);
  XEREGISTERINSTR(lfdux,        0x7C0004EE);
  XEREGISTERINSTR(lfdx,         0x7C0004AE);
  XEREGISTERINSTR(lfs,          0xC0000000);
  XEREGISTERINSTR(lfsu,         0xC4000000);
  XEREGISTERINSTR(lfsux,        0x7C00046E);
  XEREGISTERINSTR(lfsx,         0x7C00042E);
  XEREGISTERINSTR(stfd,         0xD8000000);
  XEREGISTERINSTR(stfdu,        0xDC000000);
  XEREGISTERINSTR(stfdux,       0x7C0005EE);
  XEREGISTERINSTR(stfdx,        0x7C0005AE);
  XEREGISTERINSTR(stfiwx,       0x7C0007AE);
  XEREGISTERINSTR(stfs,         0xD0000000);
  XEREGISTERINSTR(stfsu,        0xD4000000);
  XEREGISTERINSTR(stfsux,       0x7C00056E);
  XEREGISTERINSTR(stfsx,        0x7C00052E);
  XEREGISTERINSTR(dcbf,         0x7C0000AC);
  XEREGISTERINSTR(dcbst,        0x7C00006C);
  XEREGISTERINSTR(dcbt,         0x7C00022C);
  XEREGISTERINSTR(dcbtst,       0x7C0001EC);
  XEREGISTERINSTR(dcbz,         0x7C0007EC);
  XEREGISTERINSTR(icbi,         0x7C0007AC);
}


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy

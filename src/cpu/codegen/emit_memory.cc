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
XEEMITTER(lbz,          0x88000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // RT <- i56.0 || MEM(EA, 1)

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 1, false);
  g.update_gpr_value(i.D.RT, v);

  return 0;
}

XEDISASMR(lbzu,         0x8C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lbzu", "Load Byte and Zero with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(lbzu,         0x8C000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // RT <- i56.0 || MEM(EA, 1)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.D.RA), b.getInt64(XEEXTS16(i.D.DS)));
  Value* v = g.ReadMemory(i.address, ea, 1, false);
  g.update_gpr_value(i.D.RT, v);
  g.update_gpr_value(i.D.RA, ea);

  return 0;
}

XEDISASMR(lbzux,        0x7C0000EE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lbzux", "Load Byte and Zero with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(lbzux,        0x7C0000EE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // RT <- i56.0 || MEM(EA, 1)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.ReadMemory(i.address, ea, 1, false);
  g.update_gpr_value(i.X.RT, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(lbzx,         0x7C0000AE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- i56.0 || MEM(EA, 1)

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 1, false);
  g.update_gpr_value(i.X.RT, v);

  return 0;
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
XEEMITTER(ld,           0xE8000000, DS )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(DS || 0b00)
  // RT <- MEM(EA, 8)

  Value* ea = b.getInt64(XEEXTS16(i.DS.DS << 2));
  if (i.DS.RA) {
    ea = b.CreateAdd(g.gpr_value(i.DS.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 8, false);
  g.update_gpr_value(i.DS.RT, v);

  return 0;
}

XEDISASMR(ldu,          0xE8000001, DS )(InstrData& i, InstrDisasm& d) {
  d.Init("ldu", "Load Doubleword with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.DS.DS << 2), 2);
  return d.Finish();
}
XEEMITTER(ldu,          0xE8000001, DS )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(DS || 0b00)
  // RT <- MEM(EA, 8)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.DS.RA),
                          b.getInt64(XEEXTS16(i.DS.DS << 2)));
  Value* v = g.ReadMemory(i.address, ea, 8, false);
  g.update_gpr_value(i.DS.RT, v);
  g.update_gpr_value(i.DS.RA, ea);

  return 0;
}

XEEMITTER(ldux,         0x7C00006A, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(ldx,          0x7C00002A, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
XEEMITTER(lha,          0xA8000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // RT <- EXTS(MEM(EA, 2))

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = b.CreateSExt(g.ReadMemory(i.address, ea, 2, false),
                          b.getInt64Ty());
  g.update_gpr_value(i.D.RT, v);

  return 0;
}

XEEMITTER(lhau,         0xAC000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lhaux,        0x7C0002EE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
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
XEEMITTER(lhax,         0x7C0002AE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- EXTS(MEM(EA, 2))

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = b.CreateSExt(g.ReadMemory(i.address, ea, 2, false),
                          b.getInt64Ty());
  g.update_gpr_value(i.X.RT, v);

  return 0;
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
XEEMITTER(lhz,          0xA0000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // RT <- i48.0 || MEM(EA, 2)

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 2, false);
  g.update_gpr_value(i.D.RT, v);

  return 0;
}

XEDISASMR(lhzu,         0xA4000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lhzu", "Load Halfword and Zero with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(lhzu,         0xA4000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // RT <- i48.0 || MEM(EA, 2)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.D.RA), b.getInt64(XEEXTS16(i.D.DS)));
  Value* v = g.ReadMemory(i.address, ea, 2, false);
  g.update_gpr_value(i.D.RT, v);
  g.update_gpr_value(i.D.RA, ea);

  return 0;
}

XEDISASMR(lhzux,        0x7C00026E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lhzux", "Load Halfword and Zero with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(lhzux,        0x7C00026E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // RT <- i48.0 || MEM(EA, 2)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.ReadMemory(i.address, ea, 2, false);
  g.update_gpr_value(i.X.RT, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(lhzx,         0x7C00022E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- i48.0 || MEM(EA, 2)

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 2, false);
  g.update_gpr_value(i.X.RT, v);

  return 0;
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
XEEMITTER(lwa,          0xE8000002, DS )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D || 00)
  // RT <- EXTS(MEM(EA, 4))

  Value* ea = b.getInt64(XEEXTS16(i.DS.DS << 2));
  if (i.DS.RA) {
    ea = b.CreateAdd(g.gpr_value(i.DS.RA), ea);
  }
  Value* v = b.CreateSExt(g.ReadMemory(i.address, ea, 4, false),
                          b.getInt64Ty());
  g.update_gpr_value(i.DS.RT, v);

  return 0;
}

XEDISASMR(lwaux,        0x7C0002EA, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwaux", "Load Word Algebraic with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(lwaux,        0x7C0002EA, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // RT <- EXTS(MEM(EA, 4))
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = b.CreateSExt(g.ReadMemory(i.address, ea, 4, false),
                          b.getInt64Ty());
  g.update_gpr_value(i.X.RT, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(lwax,         0x7C0002AA, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- EXTS(MEM(EA, 4))

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = b.CreateSExt(g.ReadMemory(i.address, ea, 4, false),
                          b.getInt64Ty());
  g.update_gpr_value(i.X.RT, v);

  return 0;
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
XEEMITTER(lwz,          0x80000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // RT <- i32.0 || MEM(EA, 4)

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 4, false);
  g.update_gpr_value(i.D.RT, v);

  return 0;
}

XEDISASMR(lwzu,         0x84000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwzu", "Load Word and Zero with Udpate", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(lwzu,         0x84000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // RT <- i32.0 || MEM(EA, 4)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.D.RA), b.getInt64(XEEXTS16(i.D.DS)));
  Value* v = g.ReadMemory(i.address, ea, 4, false);
  g.update_gpr_value(i.D.RT, v);
  g.update_gpr_value(i.D.RA, ea);

  return 0;
}

XEDISASMR(lwzux,        0x7C00006E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lwzux", "Load Word and Zero with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(lwzux,        0x7C00006E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // RT <- i32.0 || MEM(EA, 4)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.ReadMemory(i.address, ea, 4, false);
  g.update_gpr_value(i.X.RT, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(lwzx,         0x7C00002E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- i32.0 || MEM(EA, 4)

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 4, false);
  g.update_gpr_value(i.X.RT, v);

  return 0;
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
XEEMITTER(stb,          0x98000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // MEM(EA, 1) <- (RS)[56:63]

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.gpr_value(i.D.RT);
  g.WriteMemory(i.address, ea, 1, v);

  return 0;
}

XEDISASMR(stbu,         0x9C000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stbu", "Store Byte with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(stbu,         0x9C000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // MEM(EA, 1) <- (RS)[56:63]
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.D.RA), b.getInt64(XEEXTS16(i.D.DS)));
  Value* v = g.gpr_value(i.D.RT);
  g.WriteMemory(i.address, ea, 1, v);
  g.update_gpr_value(i.D.RA, ea);

  return 0;
}

XEDISASMR(stbux,        0x7C0001EE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stbux", "Store Byte with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(stbux,        0x7C0001EE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 1) <- (RS)[56:63]
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.gpr_value(i.X.RT);
  g.WriteMemory(i.address, ea, 1, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(stbx,         0x7C0001AE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 1) <- (RS)[56:63]

  Value* ea = g.gpr_value(i.X.RB);
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.gpr_value(i.X.RT);
  g.WriteMemory(i.address, ea, 1, v);

  return 0;
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
XEEMITTER(std,          0xF8000000, DS )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(DS || 0b00)
  // MEM(EA, 8) <- (RS)

  Value* ea = b.getInt64(XEEXTS16(i.DS.DS << 2));
  if (i.DS.RA) {
    ea = b.CreateAdd(g.gpr_value(i.DS.RA), ea);
  }
  Value* v = g.gpr_value(i.DS.RT);
  g.WriteMemory(i.address, ea, 8, v);

  return 0;
}

XEDISASMR(stdu,         0xF8000001, DS )(InstrData& i, InstrDisasm& d) {
  d.Init("stdu", "Store Doubleword with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.DS.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.DS.DS << 2), 2);
  return d.Finish();
}
XEEMITTER(stdu,         0xF8000001, DS )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(DS || 0b00)
  // MEM(EA, 8) <- (RS)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.DS.RA),
                          b.getInt64(XEEXTS16(i.DS.DS << 2)));
  Value* v = g.gpr_value(i.DS.RT);
  g.WriteMemory(i.address, ea, 8, v);
  g.update_gpr_value(i.DS.RA, ea);

  return 0;
}

XEDISASMR(stdux,        0x7C00016A, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stdux", "Store Doubleword with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
    if (i.DS.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(stdux,        0x7C00016A, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 8) <- (RS)

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.gpr_value(i.X.RT);
  g.WriteMemory(i.address, ea, 8, v);

  return 0;
}

XEDISASMR(stdx,         0x7C00012A, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stdx", "Store Doubleword Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(stdx,         0x7C00012A, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 8) <- (RS)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.gpr_value(i.X.RT);
  g.WriteMemory(i.address, ea, 8, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(sth,          0xB0000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // MEM(EA, 2) <- (RS)[48:63]

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.gpr_value(i.D.RT);
  g.WriteMemory(i.address, ea, 2, v);

  return 0;
}

XEDISASMR(sthu,         0xB4000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("sthu", "Store Halfword with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(sthu,         0xB4000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // MEM(EA, 2) <- (RS)[48:63]
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.D.RA),
                          b.getInt64(XEEXTS16(i.D.DS)));
  Value* v = g.gpr_value(i.D.RT);
  g.WriteMemory(i.address, ea, 2, v);
  g.update_gpr_value(i.D.RA, ea);

  return 0;
}

XEDISASMR(sthux,        0x7C00036E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("sthux", "Store Halfword with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(sthux,        0x7C00036E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 2) <- (RS)[48:63]
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.gpr_value(i.X.RT);
  g.WriteMemory(i.address, ea, 2, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(sthx,         0x7C00032E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 2) <- (RS)[48:63]

  Value* ea = g.gpr_value(i.X.RB);
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.gpr_value(i.X.RT);
  g.WriteMemory(i.address, ea, 2, v);

  return 0;
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
XEEMITTER(stw,          0x90000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // MEM(EA, 4) <- (RS)[32:63]

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.gpr_value(i.D.RT);
  g.WriteMemory(i.address, ea, 4, v);

  return 0;
}

XEDISASMR(stwu,         0x94000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stwu", "Store Word with Update", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(stwu,         0x94000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // MEM(EA, 4) <- (RS)[32:63]
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.D.RA),
                          b.getInt64(XEEXTS16(i.D.DS)));
  Value* v = g.gpr_value(i.D.RT);
  g.WriteMemory(i.address, ea, 4, v);
  g.update_gpr_value(i.D.RA, ea);

  return 0;
}

XEDISASMR(stwux,        0x7C00016E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stwux", "Store Word with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(stwux,        0x7C00016E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 4) <- (RS)[32:63]
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.gpr_value(i.X.RT);
  g.WriteMemory(i.address, ea, 4, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(stwx,         0x7C00012E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 4) <- (RS)[32:63]

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.gpr_value(i.X.RT);
  g.WriteMemory(i.address, ea, 4, v);

  return 0;
}


// Integer load and store with byte reverse (A-1

XEEMITTER(lhbrx,        0x7C00062C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lwbrx,        0x7C00042C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(ldbrx,        0x7C000428, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(sthbrx,       0x7C00072C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stwbrx,       0x7C00052C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stdbrx,       0x7C000528, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer load and store multiple (A-16)

XEEMITTER(lmw,          0xB8000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stmw,         0xBC000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Integer load and store string (A-17)

XEEMITTER(lswi,         0x7C0004AA, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lswx,         0x7C00042A, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stswi,        0x7C0005AA, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stswx,        0x7C00052A, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Memory synchronization (A-18)

XEEMITTER(eieio,        0x7C0006AC, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(isync,        0x4C00012C, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(ldarx,        0x7C0000A8, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
XEEMITTER(lwarx,        0x7C000028, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RESERVE <- 1
  // RESERVE_LENGTH <- 4
  // RESERVE_ADDR <- real_addr(EA)
  // RT <- i32.0 || MEM(EA, 4)

  // TODO(benvanik): make this right

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 4, /* acquire */ true);
  g.update_gpr_value(i.X.RT, v);

  return 0;
}

XEEMITTER(stdcx,        0x7C0001AD, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
XEEMITTER(stwcx,        0x7C00012D, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RESERVE stuff...
  // MEM(EA, 4) <- (RS)[32:63]
  // n <- 1 if store performed
  // CR0[LT GT EQ SO] = 0b00 || n || XER[SO]

  // TODO(benvanik): make this right

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.gpr_value(i.D.RT);
  g.WriteMemory(i.address, ea, 4, v, /* release */ true);
  // TODO(benvanik): update CR0

  return 0;
}

XEEMITTER(sync,         0x7C0004AC, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
XEEMITTER(lfd,          0xC8000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // FRT <- MEM(EA, 8)

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 8, false);
  v = b.CreateBitCast(v, b.getDoubleTy());
  g.update_fpr_value(i.D.RT, v);

  return 0;
}

XEDISASMR(lfdu,         0xCC000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfdu", "Load Floating-Point Double with Update", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(lfdu,         0xCC000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // FRT <- MEM(EA, 8)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.D.RA), b.getInt64(XEEXTS16(i.D.DS)));
  Value* v = g.ReadMemory(i.address, ea, 8, false);
  v = b.CreateBitCast(v, b.getDoubleTy());
  g.update_fpr_value(i.D.RT, v);
  g.update_gpr_value(i.D.RA, ea);

  return 0;
}

XEDISASMR(lfdux,        0x7C0004EE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfdux", "Load Floating-Point Double with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(lfdux,        0x7C0004EE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // FRT <- MEM(EA, 8)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.ReadMemory(i.address, ea, 8, false);
  v = b.CreateBitCast(v, b.getDoubleTy());
  g.update_fpr_value(i.X.RT, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(lfdx,         0x7C0004AE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // FRT <- MEM(EA, 8)

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 8, false);
  v = b.CreateBitCast(v, b.getDoubleTy());
  g.update_fpr_value(i.X.RT, v);

  return 0;
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
XEEMITTER(lfs,          0xC0000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // FRT <- DOUBLE(MEM(EA, 4))

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 4, false);
  v = b.CreateFPExt(b.CreateBitCast(v, b.getFloatTy()), b.getDoubleTy());
  g.update_fpr_value(i.D.RT, v);

  return 0;
}

XEDISASMR(lfsu,         0xC4000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfsu", "Load Floating-Point Single with Update", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(lfsu,         0xC4000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // FRT <- DOUBLE(MEM(EA, 4))
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.D.RA), b.getInt64(XEEXTS16(i.D.DS)));
  Value* v = g.ReadMemory(i.address, ea, 4, false);
  v = b.CreateFPExt(b.CreateBitCast(v, b.getFloatTy()), b.getDoubleTy());
  g.update_fpr_value(i.D.RT, v);
  g.update_gpr_value(i.D.RA, ea);

  return 0;
}

XEDISASMR(lfsux,        0x7C00046E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("lfsux", "Load Floating-Point Single with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(lfsux,        0x7C00046E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // FRT <- DOUBLE(MEM(EA, 4))
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.ReadMemory(i.address, ea, 4, false);
  v = b.CreateFPExt(b.CreateBitCast(v, b.getFloatTy()), b.getDoubleTy());
  g.update_fpr_value(i.X.RT, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(lfsx,         0x7C00042E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // FRT <- DOUBLE(MEM(EA, 4))

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.ReadMemory(i.address, ea, 4, false);
  v = b.CreateFPExt(b.CreateBitCast(v, b.getFloatTy()), b.getDoubleTy());
  g.update_fpr_value(i.X.RT, v);

  return 0;
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
XEEMITTER(stfd,         0xD8000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // MEM(EA, 8) <- (FRS)

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.fpr_value(i.D.RT);
  v = b.CreateBitCast(v, b.getInt64Ty());
  g.WriteMemory(i.address, ea, 8, v);

  return 0;
}

XEDISASMR(stfdu,        0xDC000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfdu", "Store Floating-Point Double with Update", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(stfdu,        0xDC000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // MEM(EA, 8) <- (FRS)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.D.RA),
                          b.getInt64(XEEXTS16(i.D.DS)));
  Value* v = g.fpr_value(i.D.RT);
  v = b.CreateBitCast(v, b.getInt64Ty());
  g.WriteMemory(i.address, ea, 8, v);
  g.update_gpr_value(i.D.RA, ea);

  return 0;
}

XEDISASMR(stfdux,       0x7C0005EE, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfdux", "Store Floating-Point Double with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(stfdux,       0x7C0005EE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 8) <- (FRS)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.fpr_value(i.X.RT);
  v = b.CreateBitCast(v, b.getInt64Ty());
  g.WriteMemory(i.address, ea, 8, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(stfdx,        0x7C0005AE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 8) <- (FRS)

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.fpr_value(i.X.RT);
  v = b.CreateBitCast(v, b.getInt64Ty());
  g.WriteMemory(i.address, ea, 8, v);

  return 0;
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
XEEMITTER(stfiwx,       0x7C0007AE, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 4) <- (FRS)[32:63]

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.fpr_value(i.X.RT);
  v = b.CreateBitCast(v, b.getInt64Ty());
  g.WriteMemory(i.address, ea, 4, v);

  return 0;
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
XEEMITTER(stfs,         0xD0000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // MEM(EA, 4) <- SINGLE(FRS)

  Value* ea = b.getInt64(XEEXTS16(i.D.DS));
  if (i.D.RA) {
    ea = b.CreateAdd(g.gpr_value(i.D.RA), ea);
  }
  Value* v = g.fpr_value(i.D.RT);
  v = b.CreateBitCast(b.CreateFPTrunc(v, b.getFloatTy()), b.getInt32Ty());
  g.WriteMemory(i.address, ea, 4, v);

  return 0;
}

XEDISASMR(stfsu,        0xD4000000, D  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfsu", "Store Floating-Point Single with Update", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.D.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.D.RA, InstrRegister::kReadWrite);
  d.AddSImmOperand(XEEXTS16(i.D.DS), 2);
  return d.Finish();
}
XEEMITTER(stfsu,        0xD4000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // MEM(EA, 4) <- SINGLE(FRS)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.D.RA),
                          b.getInt64(XEEXTS16(i.D.DS)));
  Value* v = g.fpr_value(i.D.RT);
  v = b.CreateBitCast(b.CreateFPTrunc(v, b.getFloatTy()), b.getInt32Ty());
  g.WriteMemory(i.address, ea, 4, v);
  g.update_gpr_value(i.D.RA, ea);

  return 0;
}

XEDISASMR(stfsux,       0x7C00056E, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("stfsux", "Store Floating-Point Single with Update Indexed", 0);
  d.AddRegOperand(InstrRegister::kFPR, i.X.RT, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kReadWrite);
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}
XEEMITTER(stfsux,       0x7C00056E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 4) <- SINGLE(FRS)
  // RA <- EA

  Value* ea = b.CreateAdd(g.gpr_value(i.X.RA), g.gpr_value(i.X.RB));
  Value* v = g.fpr_value(i.X.RT);
  v = b.CreateBitCast(b.CreateFPTrunc(v, b.getFloatTy()), b.getInt32Ty());
  g.WriteMemory(i.address, ea, 4, v);
  g.update_gpr_value(i.X.RA, ea);

  return 0;
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
XEEMITTER(stfsx,        0x7C00052E, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 4) <- SINGLE(FRS)

  Value* ea = g.gpr_value(i.X.RB);
  if (i.X.RA) {
    ea = b.CreateAdd(g.gpr_value(i.X.RA), ea);
  }
  Value* v = g.fpr_value(i.X.RT);
  v = b.CreateBitCast(b.CreateFPTrunc(v, b.getFloatTy()), b.getInt32Ty());
  g.WriteMemory(i.address, ea, 4, v);

  return 0;
}


// Cache management (A-27)

XEEMITTER(dcbf,         0x7C0000AC, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(dcbst,        0x7C00006C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEDISASMR(dcbt,         0x7C00022C, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("dcbt", "Data Cache Block Touch", 0);
  // TODO
  return d.Finish();
}
XEEMITTER(dcbt,         0x7C00022C, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // No-op for now.
  // TODO(benvanik): use @llvm.prefetch
  return 0;
}

XEDISASMR(dcbtst,       0x7C0001EC, X  )(InstrData& i, InstrDisasm& d) {
  d.Init("dcbtst", "Data Cache Block Touch for Store", 0);
  // TODO
  return d.Finish();
}
XEEMITTER(dcbtst,       0x7C0001EC, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // No-op for now.
  // TODO(benvanik): use @llvm.prefetch
  return 0;
}

XEEMITTER(dcbz,         0x7C0007EC, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // or dcbz128 0x7C2007EC
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(icbi,         0x7C0007AC, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


void RegisterEmitCategoryMemory() {
  XEREGISTERINSTR(lbz,          0x88000000);
  XEREGISTERINSTR(lbzu,         0x8C000000);
  XEREGISTERINSTR(lbzux,        0x7C0000EE);
  XEREGISTERINSTR(lbzx,         0x7C0000AE);
  XEREGISTERINSTR(ld,           0xE8000000);
  XEREGISTERINSTR(ldu,          0xE8000001);
  XEREGISTEREMITTER(ldux,         0x7C00006A);
  XEREGISTEREMITTER(ldx,          0x7C00002A);
  XEREGISTERINSTR(lha,          0xA8000000);
  XEREGISTEREMITTER(lhau,         0xAC000000);
  XEREGISTEREMITTER(lhaux,        0x7C0002EE);
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
  XEREGISTEREMITTER(lhbrx,        0x7C00062C);
  XEREGISTEREMITTER(lwbrx,        0x7C00042C);
  XEREGISTEREMITTER(ldbrx,        0x7C000428);
  XEREGISTEREMITTER(sthbrx,       0x7C00072C);
  XEREGISTEREMITTER(stwbrx,       0x7C00052C);
  XEREGISTEREMITTER(stdbrx,       0x7C000528);
  XEREGISTEREMITTER(lmw,          0xB8000000);
  XEREGISTEREMITTER(stmw,         0xBC000000);
  XEREGISTEREMITTER(lswi,         0x7C0004AA);
  XEREGISTEREMITTER(lswx,         0x7C00042A);
  XEREGISTEREMITTER(stswi,        0x7C0005AA);
  XEREGISTEREMITTER(stswx,        0x7C00052A);
  XEREGISTEREMITTER(eieio,        0x7C0006AC);
  XEREGISTEREMITTER(isync,        0x4C00012C);
  XEREGISTEREMITTER(ldarx,        0x7C0000A8);
  XEREGISTERINSTR(lwarx,        0x7C000028);
  XEREGISTEREMITTER(stdcx,        0x7C0001AD);
  XEREGISTERINSTR(stwcx,        0x7C00012D);
  XEREGISTEREMITTER(sync,         0x7C0004AC);
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
  XEREGISTEREMITTER(dcbf,         0x7C0000AC);
  XEREGISTEREMITTER(dcbst,        0x7C00006C);
  XEREGISTERINSTR(dcbt,         0x7C00022C);
  XEREGISTERINSTR(dcbtst,       0x7C0001EC);
  XEREGISTEREMITTER(dcbz,         0x7C0007EC);
  XEREGISTEREMITTER(icbi,         0x7C0007AC);
}


}  // namespace codegen
}  // namespace cpu
}  // namespace xe

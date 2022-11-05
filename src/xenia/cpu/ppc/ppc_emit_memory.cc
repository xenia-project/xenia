/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/ppc/ppc_emit-private.h"

#include <stddef.h>
#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_hir_builder.h"

DEFINE_bool(
    disable_prefetch_and_cachecontrol, true,
    "Disables translating ppc prefetch/cache flush instructions to host "
    "prefetch/cacheflush instructions. This may improve performance as these "
    "instructions were written with the Xbox 360's cache in mind, and modern "
    "processors do their own automatic prefetching.",
    "CPU");
namespace xe {
namespace cpu {
namespace ppc {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Value;

Value* CalculateEA(PPCHIRBuilder& f, uint32_t ra, uint32_t rb) {
  return f.Add(f.LoadGPR(ra), f.LoadGPR(rb));
}

Value* CalculateEA_0(PPCHIRBuilder& f, uint32_t ra, uint32_t rb) {
  if (ra) {
    return f.Add(f.LoadGPR(ra), f.LoadGPR(rb));
  } else {
    return f.LoadGPR(rb);
  }
}

Value* CalculateEA_i(PPCHIRBuilder& f, uint32_t ra, uint64_t imm) {
  return f.Add(f.LoadGPR(ra), f.LoadConstantUint64(imm));
}

Value* CalculateEA_0_i(PPCHIRBuilder& f, uint32_t ra, uint64_t imm) {
  if (ra) {
    return f.Add(f.LoadGPR(ra), f.LoadConstantUint64(imm));
  } else {
    return f.LoadConstantUint64(imm);
  }
}

void StoreEA(PPCHIRBuilder& f, uint32_t rt, Value* ea) {
  // Stored back as 64bit right after the add, it seems.
  // f.StoreGPR(rt, f.ZeroExtend(f.Truncate(ea, INT32_TYPE), INT64_TYPE));
  f.StoreGPR(rt, ea);
}

// Integer load (A-13)

int InstrEmit_lbz(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // RT <- i56.0 || MEM(EA, 1)
  Value* b;
  if (i.D.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.D.RA);
  }

  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  Value* rt = f.ZeroExtend(f.LoadOffset(b, offset, INT8_TYPE), INT64_TYPE);
  f.StoreGPR(i.D.RT, rt);
  return 0;
}

int InstrEmit_lbzu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // RT <- i56.0 || MEM(EA, 1)
  // RA <- EA
  Value* ra = f.LoadGPR(i.D.RA);
  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  Value* rt = f.ZeroExtend(f.LoadOffset(ra, offset, INT8_TYPE), INT64_TYPE);
  f.StoreGPR(i.D.RT, rt);
  StoreEA(f, i.D.RA, f.Add(ra, offset));
  return 0;
}

int InstrEmit_lbzux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // RT <- i56.0 || MEM(EA, 1)
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  Value* rt = f.ZeroExtend(f.Load(ea, INT8_TYPE), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_lbzx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- i56.0 || MEM(EA, 1)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.ZeroExtend(f.Load(ea, INT8_TYPE), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  return 0;
}

int InstrEmit_lha(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // RT <- EXTS(MEM(EA, 2))
  Value* b;
  if (i.D.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.D.RA);
  }

  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  Value* rt =
      f.SignExtend(f.ByteSwap(f.LoadOffset(b, offset, INT16_TYPE)), INT64_TYPE);
  f.StoreGPR(i.D.RT, rt);
  return 0;
}

int InstrEmit_lhau(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // RT <- EXTS(MEM(EA, 2))
  // RA <- EA
  Value* ra = f.LoadGPR(i.D.RA);
  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  Value* rt = f.SignExtend(f.ByteSwap(f.LoadOffset(ra, offset, INT16_TYPE)),
                           INT64_TYPE);
  f.StoreGPR(i.D.RT, rt);
  StoreEA(f, i.D.RA, f.Add(ra, offset));
  return 0;
}

int InstrEmit_lhaux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // RT <- EXTS(MEM(EA, 2))
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  Value* rt = f.SignExtend(f.ByteSwap(f.Load(ea, INT16_TYPE)), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_lhax(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- EXTS(MEM(EA, 2))
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.SignExtend(f.ByteSwap(f.Load(ea, INT16_TYPE)), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  return 0;
}

int InstrEmit_lhz(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // RT <- i48.0 || MEM(EA, 2)
  Value* b;
  if (i.D.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.D.RA);
  }

  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  Value* rt =
      f.ZeroExtend(f.ByteSwap(f.LoadOffset(b, offset, INT16_TYPE)), INT64_TYPE);
  f.StoreGPR(i.D.RT, rt);
  return 0;
}

int InstrEmit_lhzu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // RT <- i48.0 || MEM(EA, 2)
  // RA <- EA
  Value* ra = f.LoadGPR(i.D.RA);
  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  Value* rt = f.ZeroExtend(f.ByteSwap(f.LoadOffset(ra, offset, INT16_TYPE)),
                           INT64_TYPE);
  f.StoreGPR(i.D.RT, rt);
  StoreEA(f, i.D.RA, f.Add(ra, offset));
  return 0;
}

int InstrEmit_lhzux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // RT <- i48.0 || MEM(EA, 2)
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  Value* rt = f.ZeroExtend(f.ByteSwap(f.Load(ea, INT16_TYPE)), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_lhzx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- i48.0 || MEM(EA, 2)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.ZeroExtend(f.ByteSwap(f.Load(ea, INT16_TYPE)), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  return 0;
}

int InstrEmit_lwa(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D || 00)
  // RT <- EXTS(MEM(EA, 4))
  Value* b;
  if (i.DS.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.DS.RA);
  }

  Value* offset = f.LoadConstantInt64(XEEXTS16(i.DS.DS << 2));
  Value* rt =
      f.SignExtend(f.ByteSwap(f.LoadOffset(b, offset, INT32_TYPE)), INT64_TYPE);
  f.StoreGPR(i.DS.RT, rt);
  return 0;
}

int InstrEmit_lwaux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // RT <- EXTS(MEM(EA, 4))
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  Value* rt = f.SignExtend(f.ByteSwap(f.Load(ea, INT32_TYPE)), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_lwax(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- EXTS(MEM(EA, 4))
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.SignExtend(f.ByteSwap(f.Load(ea, INT32_TYPE)), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  return 0;
}

int InstrEmit_lwz(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // RT <- i32.0 || MEM(EA, 4)
  Value* b;
  if (i.D.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.D.RA);
  }

  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  Value* rt =
      f.ZeroExtend(f.ByteSwap(f.LoadOffset(b, offset, INT32_TYPE)), INT64_TYPE);
  f.StoreGPR(i.D.RT, rt);
  return 0;
}

int InstrEmit_lwzu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // RT <- i32.0 || MEM(EA, 4)
  // RA <- EA
  Value* ra = f.LoadGPR(i.D.RA);
  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  Value* rt = f.ZeroExtend(f.ByteSwap(f.LoadOffset(ra, offset, INT32_TYPE)),
                           INT64_TYPE);
  f.StoreGPR(i.D.RT, rt);
  StoreEA(f, i.D.RA, f.Add(ra, offset));
  return 0;
}

int InstrEmit_lwzux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // RT <- i32.0 || MEM(EA, 4)
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  Value* rt = f.ZeroExtend(f.ByteSwap(f.Load(ea, INT32_TYPE)), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_lwzx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- i32.0 || MEM(EA, 4)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.ZeroExtend(f.ByteSwap(f.Load(ea, INT32_TYPE)), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  return 0;
}

int InstrEmit_ld(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(DS || 0b00)
  // RT <- MEM(EA, 8)
  Value* b;
  if (i.DS.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.DS.RA);
  }

  Value* offset = f.LoadConstantInt64(XEEXTS16(i.DS.DS << 2));
  Value* rt = f.ByteSwap(f.LoadOffset(b, offset, INT64_TYPE));
  f.StoreGPR(i.DS.RT, rt);
  return 0;
}

int InstrEmit_ldu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(DS || 0b00)
  // RT <- MEM(EA, 8)
  // RA <- EA
  Value* ea = CalculateEA_i(f, i.DS.RA, XEEXTS16(i.DS.DS << 2));
  Value* rt = f.ByteSwap(f.Load(ea, INT64_TYPE));
  f.StoreGPR(i.DS.RT, rt);
  StoreEA(f, i.DS.RA, ea);
  return 0;
}

int InstrEmit_ldux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // RT <- MEM(EA, 8)
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  Value* rt = f.ByteSwap(f.Load(ea, INT64_TYPE));
  f.StoreGPR(i.X.RT, rt);
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_ldx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- MEM(EA, 8)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.ByteSwap(f.Load(ea, INT64_TYPE));
  f.StoreGPR(i.X.RT, rt);
  return 0;
}

// Integer store (A-14)

int InstrEmit_stb(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // MEM(EA, 1) <- (RS)[56:63]
  Value* b;
  if (i.D.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.D.RA);
  }

  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  f.StoreOffset(b, offset, f.Truncate(f.LoadGPR(i.D.RT), INT8_TYPE));
  return 0;
}

int InstrEmit_stbu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // MEM(EA, 1) <- (RS)[56:63]
  // RA <- EA
  Value* ea = CalculateEA_i(f, i.D.RA, XEEXTS16(i.D.DS));
  f.Store(ea, f.Truncate(f.LoadGPR(i.D.RT), INT8_TYPE));
  StoreEA(f, i.D.RA, ea);
  return 0;
}

int InstrEmit_stbux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 1) <- (RS)[56:63]
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  f.Store(ea, f.Truncate(f.LoadGPR(i.X.RT), INT8_TYPE));
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_stbx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 1) <- (RS)[56:63]
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  f.Store(ea, f.Truncate(f.LoadGPR(i.X.RT), INT8_TYPE));
  return 0;
}

int InstrEmit_sth(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // MEM(EA, 2) <- (RS)[48:63]
  Value* b;
  if (i.D.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.D.RA);
  }

  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  f.StoreOffset(b, offset,
                f.ByteSwap(f.Truncate(f.LoadGPR(i.D.RT), INT16_TYPE)));
  return 0;
}

int InstrEmit_sthu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // MEM(EA, 2) <- (RS)[48:63]
  // RA <- EA
  Value* ea = CalculateEA_i(f, i.D.RA, XEEXTS16(i.D.DS));
  f.Store(ea, f.ByteSwap(f.Truncate(f.LoadGPR(i.D.RT), INT16_TYPE)));
  StoreEA(f, i.D.RA, ea);
  return 0;
}

int InstrEmit_sthux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 2) <- (RS)[48:63]
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.Truncate(f.LoadGPR(i.X.RT), INT16_TYPE)));
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_sthx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 2) <- (RS)[48:63]
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.Truncate(f.LoadGPR(i.X.RT), INT16_TYPE)));
  return 0;
}

int InstrEmit_stw(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // MEM(EA, 4) <- (RS)[32:63]
  Value* b;
  if (i.D.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.D.RA);
  }
  Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  f.StoreOffset(b, offset,
                f.ByteSwap(f.Truncate(f.LoadGPR(i.D.RT), INT32_TYPE)));

  return 0;
}

int InstrEmit_stmw(PPCHIRBuilder& f, const InstrData& i) {
  Value* b;
  if (i.D.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.D.RA);
  }

  for (uint32_t j = 0; j < 32 - i.D.RT; ++j) {
    Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS) + j * 4);
    f.StoreOffset(b, offset,
                  f.ByteSwap(f.Truncate(f.LoadGPR(i.D.RT + j), INT32_TYPE)));
  }
  return 0;
}

int InstrEmit_stwu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // MEM(EA, 4) <- (RS)[32:63]
  // RA <- EA
  Value* ea = CalculateEA_i(f, i.D.RA, XEEXTS16(i.D.DS));
  f.Store(ea, f.ByteSwap(f.Truncate(f.LoadGPR(i.D.RT), INT32_TYPE)));
  StoreEA(f, i.D.RA, ea);
  return 0;
}

int InstrEmit_stwux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 4) <- (RS)[32:63]
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE)));
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_stwx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 4) <- (RS)[32:63]
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE)));
  return 0;
}

int InstrEmit_std(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(DS || 0b00)
  // MEM(EA, 8) <- (RS)
  Value* b;
  if (i.DS.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.DS.RA);
  }

  Value* offset = f.LoadConstantInt64(XEEXTS16(i.DS.DS << 2));
  f.StoreOffset(b, offset, f.ByteSwap(f.LoadGPR(i.DS.RT)));
  return 0;
}

int InstrEmit_stdu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(DS || 0b00)
  // MEM(EA, 8) <- (RS)
  // RA <- EA
  Value* ea = CalculateEA_i(f, i.DS.RA, XEEXTS16(i.DS.DS << 2));
  f.Store(ea, f.ByteSwap(f.LoadGPR(i.DS.RT)));
  StoreEA(f, i.DS.RA, ea);
  return 0;
}

int InstrEmit_stdux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 8) <- (RS)
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.LoadGPR(i.X.RT)));
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_stdx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 8) <- (RS)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.LoadGPR(i.X.RT)));
  return 0;
}

// Integer load and store with byte reverse (A-1

int InstrEmit_lhbrx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- i48.0 || bswap(MEM(EA, 2))
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.ZeroExtend(f.Load(ea, INT16_TYPE), INT64_TYPE);
  StoreEA(f, i.X.RT, rt);
  return 0;
}

int InstrEmit_lwbrx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- i32.0 || bswap(MEM(EA, 4))
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.ZeroExtend(f.Load(ea, INT32_TYPE), INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  return 0;
}

int InstrEmit_ldbrx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RT <- bswap(MEM(EA, 8))
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.Load(ea, INT64_TYPE);
  f.StoreGPR(i.X.RT, rt);
  return 0;
}

int InstrEmit_sthbrx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 2) <- bswap((RS)[48:63])
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  f.Store(ea, f.Truncate(f.LoadGPR(i.X.RT), INT16_TYPE));
  return 0;
}

int InstrEmit_stwbrx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 4) <- bswap((RS)[32:63])
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  f.Store(ea, f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE));
  return 0;
}

int InstrEmit_stdbrx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 8) <- bswap(RS)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  f.Store(ea, f.LoadGPR(i.X.RT));
  return 0;
}

// Integer load and store multiple (A-16)

int InstrEmit_lmw(PPCHIRBuilder& f, const InstrData& i) {
  Value* b;
  if (i.D.RA == 0) {
    b = f.LoadZeroInt64();
  } else {
    b = f.LoadGPR(i.D.RA);
  }

  for (uint32_t j = 0; j < 32 - i.D.RT; ++j) {
    if (i.D.RT + j == i.D.RA) {
      continue;
    }
    Value* offset = f.LoadConstantInt64(XEEXTS16(i.D.DS) + j * 4);
    Value* rt = f.ZeroExtend(f.ByteSwap(f.LoadOffset(b, offset, INT32_TYPE)),
                             INT64_TYPE);
    f.StoreGPR(i.D.RT + j, rt);
  }
  return 0;
}

// Integer load and store string (A-17)

int InstrEmit_lswi(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_lswx(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_stswi(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_stswx(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

// Memory synchronization (A-18)

int InstrEmit_eieio(PPCHIRBuilder& f, const InstrData& i) {
  f.MemoryBarrier();
  return 0;
}

int InstrEmit_sync(PPCHIRBuilder& f, const InstrData& i) {
  f.MemoryBarrier();
  return 0;
}

int InstrEmit_isync(PPCHIRBuilder& f, const InstrData& i) {
  // XEINSTRNOTIMPLEMENTED();
  f.Nop();
  return 0;
}

int InstrEmit_ldarx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RESERVE <- 1
  // RESERVE_LENGTH <- 8
  // RESERVE_ADDR <- real_addr(EA)
  // RT <- MEM(EA, 8)

  // NOTE: we assume we are within a global lock.
  // We could assert here that the block (or its parent) has taken a global lock
  // already, but I haven't see anything but interrupt callbacks (which are
  // always under a global lock) do that yet.
  // We issue a memory barrier here to make sure that we get good values.
  f.MemoryBarrier();

  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.ByteSwap(f.Load(ea, INT64_TYPE));
  f.StoreReserved(rt);
  f.StoreGPR(i.X.RT, rt);
  return 0;
}

int InstrEmit_lwarx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RESERVE <- 1
  // RESERVE_LENGTH <- 4
  // RESERVE_ADDR <- real_addr(EA)
  // RT <- i32.0 || MEM(EA, 4)

  // NOTE: we assume we are within a global lock.
  // We could assert here that the block (or its parent) has taken a global lock
  // already, but I haven't see anything but interrupt callbacks (which are
  // always under a global lock) do that yet.
  // We issue a memory barrier here to make sure that we get good values.
  f.MemoryBarrier();

  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.ZeroExtend(f.ByteSwap(f.Load(ea, INT32_TYPE)), INT64_TYPE);
  f.StoreReserved(rt);
  f.StoreGPR(i.X.RT, rt);
  return 0;
}

int InstrEmit_stdcx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RESERVE stuff...
  // MEM(EA, 8) <- (RS)
  // n <- 1 if store performed
  // CR0[LT GT EQ SO] = 0b00 || n || XER[SO]

  // NOTE: we assume we are within a global lock.
  // As we have been exclusively executing this entire time, we assume that no
  // one else could have possibly touched the memory and must always succeed.
  // We use atomic compare exchange here to support reserved load/store without
  // being under the global lock (flag disable_global_lock - see mtmsr/mtmsrd).
  // This will always succeed if under the global lock, however.

  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.ByteSwap(f.LoadGPR(i.X.RT));
  Value* res = f.ByteSwap(f.LoadReserved());
  Value* v = f.AtomicCompareExchange(ea, res, rt);
  f.StoreContext(offsetof(PPCContext, cr0.cr0_eq), v);
  f.StoreContext(offsetof(PPCContext, cr0.cr0_lt), f.LoadZeroInt8());
  f.StoreContext(offsetof(PPCContext, cr0.cr0_gt), f.LoadZeroInt8());

  // Issue memory barrier for when we go out of lock and want others to see our
  // updates.

  f.MemoryBarrier();

  return 0;
}

int InstrEmit_stwcx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // RESERVE stuff...
  // MEM(EA, 4) <- (RS)[32:63]
  // n <- 1 if store performed
  // CR0[LT GT EQ SO] = 0b00 || n || XER[SO]

  // NOTE: we assume we are within a global lock.
  // As we have been exclusively executing this entire time, we assume that no
  // one else could have possibly touched the memory and must always succeed.
  // We use atomic compare exchange here to support reserved load/store without
  // being under the global lock (flag disable_global_lock - see mtmsr/mtmsrd).
  // This will always succeed if under the global lock, however.

  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.ByteSwap(f.Truncate(f.LoadGPR(i.X.RT), INT32_TYPE));
  Value* res = f.ByteSwap(f.Truncate(f.LoadReserved(), INT32_TYPE));
  Value* v = f.AtomicCompareExchange(ea, res, rt);
  f.StoreContext(offsetof(PPCContext, cr0.cr0_eq), v);
  f.StoreContext(offsetof(PPCContext, cr0.cr0_lt), f.LoadZeroInt8());
  f.StoreContext(offsetof(PPCContext, cr0.cr0_gt), f.LoadZeroInt8());

  // Issue memory barrier for when we go out of lock and want others to see our
  // updates.
  f.MemoryBarrier();

  return 0;
}

// Floating-point load (A-19)

int InstrEmit_lfd(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // FRT <- MEM(EA, 8)
  Value* ea = CalculateEA_0_i(f, i.D.RA, XEEXTS16(i.D.DS));
  Value* rt = f.Cast(f.ByteSwap(f.Load(ea, INT64_TYPE)), FLOAT64_TYPE);
  f.StoreFPR(i.D.RT, rt);
  return 0;
}

int InstrEmit_lfdu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // FRT <- MEM(EA, 8)
  // RA <- EA
  Value* ea = CalculateEA_i(f, i.D.RA, XEEXTS16(i.D.DS));
  Value* rt = f.Cast(f.ByteSwap(f.Load(ea, INT64_TYPE)), FLOAT64_TYPE);
  f.StoreFPR(i.D.RT, rt);
  StoreEA(f, i.D.RA, ea);
  return 0;
}

int InstrEmit_lfdux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // FRT <- MEM(EA, 8)
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  Value* rt = f.Cast(f.ByteSwap(f.Load(ea, INT64_TYPE)), FLOAT64_TYPE);
  f.StoreFPR(i.X.RT, rt);
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_lfdx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // FRT <- MEM(EA, 8)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.Cast(f.ByteSwap(f.Load(ea, INT64_TYPE)), FLOAT64_TYPE);
  f.StoreFPR(i.X.RT, rt);
  return 0;
}

int InstrEmit_lfs(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // FRT <- DOUBLE(MEM(EA, 4))
  Value* ea = CalculateEA_0_i(f, i.D.RA, XEEXTS16(i.D.DS));
  Value* rt = f.Convert(
      f.Cast(f.ByteSwap(f.Load(ea, INT32_TYPE)), FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.D.RT, rt);
  return 0;
}

int InstrEmit_lfsu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // FRT <- DOUBLE(MEM(EA, 4))
  // RA <- EA
  Value* ea = CalculateEA_i(f, i.D.RA, XEEXTS16(i.D.DS));
  Value* rt = f.Convert(
      f.Cast(f.ByteSwap(f.Load(ea, INT32_TYPE)), FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.D.RT, rt);
  StoreEA(f, i.D.RA, ea);
  return 0;
}

int InstrEmit_lfsux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // FRT <- DOUBLE(MEM(EA, 4))
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  Value* rt = f.Convert(
      f.Cast(f.ByteSwap(f.Load(ea, INT32_TYPE)), FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.X.RT, rt);
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_lfsx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // FRT <- DOUBLE(MEM(EA, 4))
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* rt = f.Convert(
      f.Cast(f.ByteSwap(f.Load(ea, INT32_TYPE)), FLOAT32_TYPE), FLOAT64_TYPE);
  f.StoreFPR(i.X.RT, rt);
  return 0;
}

// Floating-point store (A-20)

int InstrEmit_stfd(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // MEM(EA, 8) <- (FRS)
  Value* ea = CalculateEA_0_i(f, i.D.RA, XEEXTS16(i.D.DS));
  f.Store(ea, f.ByteSwap(f.Cast(f.LoadFPR(i.D.RT), INT64_TYPE)));
  return 0;
}

int InstrEmit_stfdu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // MEM(EA, 8) <- (FRS)
  // RA <- EA
  Value* ea = CalculateEA_i(f, i.D.RA, XEEXTS16(i.D.DS));
  f.Store(ea, f.ByteSwap(f.Cast(f.LoadFPR(i.D.RT), INT64_TYPE)));
  StoreEA(f, i.D.RA, ea);
  return 0;
}

int InstrEmit_stfdux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 8) <- (FRS)
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.Cast(f.LoadFPR(i.X.RT), INT64_TYPE)));
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_stfdx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 8) <- (FRS)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.Cast(f.LoadFPR(i.X.RT), INT64_TYPE)));
  return 0;
}

int InstrEmit_stfiwx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 4) <- (FRS)[32:63]
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.Truncate(f.Cast(f.LoadFPR(i.X.RT), INT64_TYPE),
                                    INT32_TYPE)));
  return 0;
}

int InstrEmit_stfs(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + EXTS(D)
  // MEM(EA, 4) <- SINGLE(FRS)
  Value* ea = CalculateEA_0_i(f, i.D.RA, XEEXTS16(i.D.DS));
  f.Store(ea, f.ByteSwap(f.Cast(f.Convert(f.LoadFPR(i.D.RT), FLOAT32_TYPE),
                                INT32_TYPE)));
  return 0;
}

int InstrEmit_stfsu(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + EXTS(D)
  // MEM(EA, 4) <- SINGLE(FRS)
  // RA <- EA
  Value* ea = CalculateEA_i(f, i.D.RA, XEEXTS16(i.D.DS));
  f.Store(ea, f.ByteSwap(f.Cast(f.Convert(f.LoadFPR(i.D.RT), FLOAT32_TYPE),
                                INT32_TYPE)));
  StoreEA(f, i.D.RA, ea);
  return 0;
}

int InstrEmit_stfsux(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // MEM(EA, 4) <- SINGLE(FRS)
  // RA <- EA
  Value* ea = CalculateEA(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.Cast(f.Convert(f.LoadFPR(i.X.RT), FLOAT32_TYPE),
                                INT32_TYPE)));
  StoreEA(f, i.X.RA, ea);
  return 0;
}

int InstrEmit_stfsx(PPCHIRBuilder& f, const InstrData& i) {
  // if RA = 0 then
  //   b <- 0
  // else
  //   b <- (RA)
  // EA <- b + (RB)
  // MEM(EA, 4) <- SINGLE(FRS)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  f.Store(ea, f.ByteSwap(f.Cast(f.Convert(f.LoadFPR(i.X.RT), FLOAT32_TYPE),
                                INT32_TYPE)));
  return 0;
}

// Cache management (A-27)
// dcbf, dcbst, dcbt, dcbtst work with 128-byte cache lines, not 32-byte cache
// blocks, on the Xenon:
// https://github.com/ValveSoftware/source-sdk-2013/blob/master/mp/src/mathlib/sseconst.cpp#L321
// https://randomascii.wordpress.com/2018/01/07/finding-a-cpu-design-bug-in-the-xbox-360/

int InstrEmit_dcbf(PPCHIRBuilder& f, const InstrData& i) {
  if (!cvars::disable_prefetch_and_cachecontrol) {
    Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
    f.CacheControl(ea, 128,
                   CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE_AND_FLUSH);
  }
  return 0;
}

int InstrEmit_dcbst(PPCHIRBuilder& f, const InstrData& i) {
  if (!cvars::disable_prefetch_and_cachecontrol) {
    Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
    f.CacheControl(ea, 128, CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE);
  }
  return 0;
}

int InstrEmit_dcbt(PPCHIRBuilder& f, const InstrData& i) {
  if (!cvars::disable_prefetch_and_cachecontrol) {
    Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
    f.CacheControl(ea, 128, CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH);
  }
  return 0;
}

int InstrEmit_dcbtst(PPCHIRBuilder& f, const InstrData& i) {
  if (!cvars::disable_prefetch_and_cachecontrol) {
    Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
    f.CacheControl(ea, 128,
                   CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH_FOR_STORE);
  }
  return 0;
}

int InstrEmit_dcbz(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // memset(EA & ~31, 0, 32)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  // dcbz - 32 byte set
  int block_size = 32;
  int address_mask = ~31;
  f.Memset(f.And(ea, f.LoadConstantInt64(address_mask)), f.LoadZeroInt8(),
           f.LoadConstantInt64(block_size));
  return 0;
}

int InstrEmit_dcbz128(PPCHIRBuilder& f, const InstrData& i) {
  // EA <- (RA) + (RB)
  // memset(EA & ~31, 0, 32)
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  // dcbz128 - 128 byte set
  int block_size = 128;
  int address_mask = ~127;
  f.Memset(f.And(ea, f.LoadConstantInt64(address_mask)), f.LoadZeroInt8(),
           f.LoadConstantInt64(block_size));
  return 0;
}

int InstrEmit_icbi(PPCHIRBuilder& f, const InstrData& i) {
  // XEINSTRNOTIMPLEMENTED();
  f.Nop();
  return 0;
}

void RegisterEmitCategoryMemory() {
  XEREGISTERINSTR(lbz);
  XEREGISTERINSTR(lbzu);
  XEREGISTERINSTR(lbzux);
  XEREGISTERINSTR(lbzx);
  XEREGISTERINSTR(lha);
  XEREGISTERINSTR(lhau);
  XEREGISTERINSTR(lhaux);
  XEREGISTERINSTR(lhax);
  XEREGISTERINSTR(lhz);
  XEREGISTERINSTR(lhzu);
  XEREGISTERINSTR(lhzux);
  XEREGISTERINSTR(lhzx);
  XEREGISTERINSTR(lwa);
  XEREGISTERINSTR(lwaux);
  XEREGISTERINSTR(lwax);
  XEREGISTERINSTR(lwz);
  XEREGISTERINSTR(lwzu);
  XEREGISTERINSTR(lwzux);
  XEREGISTERINSTR(lwzx);
  XEREGISTERINSTR(ld);
  XEREGISTERINSTR(ldu);
  XEREGISTERINSTR(ldux);
  XEREGISTERINSTR(ldx);
  XEREGISTERINSTR(stb);
  XEREGISTERINSTR(stbu);
  XEREGISTERINSTR(stbux);
  XEREGISTERINSTR(stbx);
  XEREGISTERINSTR(sth);
  XEREGISTERINSTR(sthu);
  XEREGISTERINSTR(sthux);
  XEREGISTERINSTR(sthx);
  XEREGISTERINSTR(stw);
  XEREGISTERINSTR(stwu);
  XEREGISTERINSTR(stwux);
  XEREGISTERINSTR(stwx);
  XEREGISTERINSTR(std);
  XEREGISTERINSTR(stdu);
  XEREGISTERINSTR(stdux);
  XEREGISTERINSTR(stdx);
  XEREGISTERINSTR(lhbrx);
  XEREGISTERINSTR(lwbrx);
  XEREGISTERINSTR(ldbrx);
  XEREGISTERINSTR(sthbrx);
  XEREGISTERINSTR(stwbrx);
  XEREGISTERINSTR(stdbrx);
  XEREGISTERINSTR(lmw);
  XEREGISTERINSTR(stmw);
  XEREGISTERINSTR(lswi);
  XEREGISTERINSTR(lswx);
  XEREGISTERINSTR(stswi);
  XEREGISTERINSTR(stswx);
  XEREGISTERINSTR(eieio);
  XEREGISTERINSTR(sync);
  XEREGISTERINSTR(isync);
  XEREGISTERINSTR(ldarx);
  XEREGISTERINSTR(lwarx);
  XEREGISTERINSTR(stdcx);
  XEREGISTERINSTR(stwcx);
  XEREGISTERINSTR(lfd);
  XEREGISTERINSTR(lfdu);
  XEREGISTERINSTR(lfdux);
  XEREGISTERINSTR(lfdx);
  XEREGISTERINSTR(lfs);
  XEREGISTERINSTR(lfsu);
  XEREGISTERINSTR(lfsux);
  XEREGISTERINSTR(lfsx);
  XEREGISTERINSTR(stfd);
  XEREGISTERINSTR(stfdu);
  XEREGISTERINSTR(stfdux);
  XEREGISTERINSTR(stfdx);
  XEREGISTERINSTR(stfiwx);
  XEREGISTERINSTR(stfs);
  XEREGISTERINSTR(stfsu);
  XEREGISTERINSTR(stfsux);
  XEREGISTERINSTR(stfsx);
  XEREGISTERINSTR(dcbf);
  XEREGISTERINSTR(dcbst);
  XEREGISTERINSTR(dcbt);
  XEREGISTERINSTR(dcbtst);
  XEREGISTERINSTR(dcbz);
  XEREGISTERINSTR(dcbz128);
  XEREGISTERINSTR(icbi);
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

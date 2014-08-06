/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_emit-private.h>

#include <alloy/frontend/ppc/ppc_context.h>
#include <alloy/frontend/ppc/ppc_hir_builder.h>

namespace alloy {
namespace frontend {
namespace ppc {

// TODO(benvanik): remove when enums redefined.
using namespace alloy::hir;

using alloy::hir::Label;
using alloy::hir::Value;

int InstrEmit_branch(PPCHIRBuilder& f, const char* src, uint64_t cia,
                     Value* nia, bool lk, Value* cond = NULL,
                     bool expect_true = true, bool nia_is_lr = false) {
  uint32_t call_flags = 0;

  // TODO(benvanik): this may be wrong and overwrite LRs when not desired!
  // The docs say always, though...
  // Note that we do the update before we branch/call as we need it to
  // be correct for returns.
  if (lk) {
    Value* return_address = f.LoadConstant(cia + 4);
    f.SetReturnAddress(return_address);
    f.StoreLR(return_address);
  }

  if (!lk) {
    // If LR is not set this call will never return here.
    call_flags |= CALL_TAIL;
  }

  // TODO(benvanik): set CALL_TAIL if !lk and the last block in the fn.
  //                 This is almost always a jump to restore gpr.

  if (nia->IsConstant()) {
    // Direct branch to address.
    // If it's a block inside of ourself, setup a fast jump.
    // Unless it's to ourselves directly, in which case it's
    // recursion.
    uint64_t nia_value = nia->AsUint64() & 0xFFFFFFFF;
    bool is_recursion = false;
    if (nia_value == f.symbol_info()->address() && lk) {
      is_recursion = true;
    }
    Label* label = is_recursion ? NULL : f.LookupLabel(nia_value);
    if (label) {
      // Branch to label.
      uint32_t branch_flags = 0;
      if (cond) {
        if (expect_true) {
          f.BranchTrue(cond, label, branch_flags);
        } else {
          f.BranchFalse(cond, label, branch_flags);
        }
      } else {
        f.Branch(label, branch_flags);
      }
    } else {
      // Call function.
      auto symbol_info = f.LookupFunction(nia_value);
      if (cond) {
        if (!expect_true) {
          cond = f.IsFalse(cond);
        }
        f.CallTrue(cond, symbol_info, call_flags);
      } else {
        f.Call(symbol_info, call_flags);
      }
    }
  } else {
// Indirect branch to pointer.

// TODO(benvanik): runtime recursion detection?

// TODO(benvanik): run a DFA pass to see if we can detect whether this is
//     a normal function return that is pulling the LR from the stack that
//     it set in the prolog. If so, we can omit the dynamic check!

//// Dynamic test when branching to LR, which is usually used for the return.
//// We only do this if LK=0 as returns wouldn't set LR.
//// Ideally it's a return and we can just do a simple ret and be done.
//// If it's not, we fall through to the full indirection logic.
// if (!lk && reg == kXEPPCRegLR) {
//  // The return block will spill registers for us.
//  // TODO(benvanik): 'lr_mismatch' debug info.
//  // Note: we need to test on *only* the 32-bit target, as the target ptr may
//  //     have garbage in the upper 32 bits.
//  c.cmp(target.r32(), c.getGpArg(1).r32());
//  // TODO(benvanik): evaluate hint here.
//  c.je(e.GetReturnLabel(), kCondHintLikely);
//}
#if 0
    // This breaks longjump, as that uses blr with a non-return lr.
    // It'd be nice to move SET_RETURN_ADDRESS semantics up into context
    // so that we can just use this.
    if (!lk && nia_is_lr) {
      // Return (most likely).
      // TODO(benvanik): test? ReturnCheck()?
      if (cond) {
        if (!expect_true) {
          cond = f.IsFalse(cond);
        }
        f.ReturnTrue(cond);
      } else {
        f.Return();
      }
    } else {
#else
    {
#endif
    // Jump to pointer.
    bool likely_return = !lk && nia_is_lr;
    if (likely_return) {
      call_flags |= CALL_POSSIBLE_RETURN;
    }
    if (cond) {
      if (!expect_true) {
        cond = f.IsFalse(cond);
      }
      f.CallIndirectTrue(cond, nia, call_flags);
    } else {
      f.CallIndirect(nia, call_flags);
    }
  }
}

return 0;
}

XEEMITTER(bx, 0x48000000, I)(PPCHIRBuilder& f, InstrData& i) {
  // if AA then
  //   NIA <- EXTS(LI || 0b00)
  // else
  //   NIA <- CIA + EXTS(LI || 0b00)
  // if LK then
  //   LR <- CIA + 4

  uint32_t nia;
  if (i.I.AA) {
    nia = (uint32_t)XEEXTS26(i.I.LI << 2);
  } else {
    nia = (uint32_t)(i.address + XEEXTS26(i.I.LI << 2));
  }

  return InstrEmit_branch(f, "bx", i.address, f.LoadConstant(nia), i.I.LK);
}

XEEMITTER(bcx, 0x40000000, B)(PPCHIRBuilder& f, InstrData& i) {
  // if ¬BO[2] then
  //   CTR <- CTR - 1
  // ctr_ok <- BO[2] | ((CTR[0:63] != 0) XOR BO[3])
  // cond_ok <- BO[0] | (CR[BI+32] ≡ BO[1])
  // if ctr_ok & cond_ok then
  //   if AA then
  //     NIA <- EXTS(BD || 0b00)
  //   else
  //     NIA <- CIA + EXTS(BD || 0b00)
  // if LK then
  //   LR <- CIA + 4

  // NOTE: the condition bits are reversed!
  // 01234 (docs)
  // 43210 (real)

  Value* ctr_ok = NULL;
  if (XESELECTBITS(i.B.BO, 2, 2)) {
    // Ignore ctr.
  } else {
    // Decrement counter.
    Value* ctr = f.LoadCTR();
    ctr = f.Sub(ctr, f.LoadConstant((int64_t)1));
    f.StoreCTR(ctr);
    // Ctr check.
    ctr = f.Truncate(ctr, INT32_TYPE);
    // TODO(benvanik): could do something similar to cond and avoid the
    // is_true/branch_true pairing.
    if (XESELECTBITS(i.B.BO, 1, 1)) {
      ctr_ok = f.IsFalse(ctr);
    } else {
      ctr_ok = f.IsTrue(ctr);
    }
  }

  Value* cond_ok = NULL;
  bool not_cond_ok = false;
  if (XESELECTBITS(i.B.BO, 4, 4)) {
    // Ignore cond.
  } else {
    Value* cr = f.LoadCRField(i.B.BI >> 2, i.B.BI & 3);
    cond_ok = cr;
    if (XESELECTBITS(i.B.BO, 3, 3)) {
      // Expect true.
      not_cond_ok = false;
    } else {
      // Expect false.
      not_cond_ok = true;
    }
  }

  // We do a bit of optimization here to make the llvm assembly easier to read.
  Value* ok = NULL;
  bool expect_true = true;
  if (ctr_ok && cond_ok) {
    if (not_cond_ok) {
      cond_ok = f.IsFalse(cond_ok);
    }
    ok = f.And(ctr_ok, cond_ok);
  } else if (ctr_ok) {
    ok = ctr_ok;
  } else if (cond_ok) {
    ok = cond_ok;
    expect_true = !not_cond_ok;
  }

  uint32_t nia;
  if (i.B.AA) {
    nia = (uint32_t)XEEXTS16(i.B.BD << 2);
  } else {
    nia = (uint32_t)(i.address + XEEXTS16(i.B.BD << 2));
  }
  return InstrEmit_branch(f, "bcx", i.address, f.LoadConstant(nia), i.B.LK, ok,
                          expect_true);
}

XEEMITTER(bcctrx, 0x4C000420, XL)(PPCHIRBuilder& f, InstrData& i) {
  // cond_ok <- BO[0] | (CR[BI+32] ≡ BO[1])
  // if cond_ok then
  //   NIA <- CTR[0:61] || 0b00
  // if LK then
  //   LR <- CIA + 4

  // NOTE: the condition bits are reversed!
  // 01234 (docs)
  // 43210 (real)

  Value* cond_ok = NULL;
  bool not_cond_ok = false;
  if (XESELECTBITS(i.XL.BO, 4, 4)) {
    // Ignore cond.
  } else {
    Value* cr = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
    cond_ok = cr;
    if (XESELECTBITS(i.XL.BO, 3, 3)) {
      // Expect true.
      not_cond_ok = false;
    } else {
      // Expect false.
      not_cond_ok = true;
    }
  }

  bool expect_true = !not_cond_ok;
  return InstrEmit_branch(f, "bcctrx", i.address, f.LoadCTR(), i.XL.LK, cond_ok,
                          expect_true);
}

XEEMITTER(bclrx, 0x4C000020, XL)(PPCHIRBuilder& f, InstrData& i) {
  // if ¬BO[2] then
  //   CTR <- CTR - 1
  // ctr_ok <- BO[2] | ((CTR[0:63] != 0) XOR BO[3]
  // cond_ok <- BO[0] | (CR[BI+32] ≡ BO[1])
  // if ctr_ok & cond_ok then
  //   NIA <- LR[0:61] || 0b00
  // if LK then
  //   LR <- CIA + 4

  // NOTE: the condition bits are reversed!
  // 01234 (docs)
  // 43210 (real)

  Value* ctr_ok = NULL;
  if (XESELECTBITS(i.XL.BO, 2, 2)) {
    // Ignore ctr.
  } else {
    // Decrement counter.
    Value* ctr = f.LoadCTR();
    ctr = f.Sub(ctr, f.LoadConstant((int64_t)1));
    f.StoreCTR(ctr);
    // Ctr check.
    ctr = f.Truncate(ctr, INT32_TYPE);
    // TODO(benvanik): could do something similar to cond and avoid the
    // is_true/branch_true pairing.
    if (XESELECTBITS(i.XL.BO, 1, 1)) {
      ctr_ok = f.IsFalse(ctr);
    } else {
      ctr_ok = f.IsTrue(ctr);
    }
  }

  Value* cond_ok = NULL;
  bool not_cond_ok = false;
  if (XESELECTBITS(i.XL.BO, 4, 4)) {
    // Ignore cond.
  } else {
    Value* cr = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
    cond_ok = cr;
    if (XESELECTBITS(i.XL.BO, 3, 3)) {
      // Expect true.
      not_cond_ok = false;
    } else {
      // Expect false.
      not_cond_ok = true;
    }
  }

  // We do a bit of optimization here to make the llvm assembly easier to read.
  Value* ok = NULL;
  bool expect_true = true;
  if (ctr_ok && cond_ok) {
    if (not_cond_ok) {
      cond_ok = f.IsFalse(cond_ok);
    }
    ok = f.And(ctr_ok, cond_ok);
  } else if (ctr_ok) {
    ok = ctr_ok;
  } else if (cond_ok) {
    ok = cond_ok;
    expect_true = !not_cond_ok;
  }

  return InstrEmit_branch(f, "bclrx", i.address, f.LoadLR(), i.XL.LK, ok,
                          expect_true, true);
}

// Condition register logical (A-23)

XEEMITTER(crand, 0x4C000202, XL)(PPCHIRBuilder& f, InstrData& i) {
  // CR[bt] <- CR[ba] & CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.And(ba, bb);
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

XEEMITTER(crandc, 0x4C000102, XL)(PPCHIRBuilder& f, InstrData& i) {
  // CR[bt] <- CR[ba] & ¬CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.And(ba, f.Not(bb));
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

XEEMITTER(creqv, 0x4C000242, XL)(PPCHIRBuilder& f, InstrData& i) {
  // CR[bt] <- CR[ba] == CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.CompareEQ(ba, bb);
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

XEEMITTER(crnand, 0x4C0001C2, XL)(PPCHIRBuilder& f, InstrData& i) {
  // CR[bt] <- ¬(CR[ba] & CR[bb])   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.Not(f.And(ba, bb));
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

XEEMITTER(crnor, 0x4C000042, XL)(PPCHIRBuilder& f, InstrData& i) {
  // CR[bt] <- ¬(CR[ba] | CR[bb])   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.Not(f.Or(ba, bb));
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

XEEMITTER(cror, 0x4C000382, XL)(PPCHIRBuilder& f, InstrData& i) {
  // CR[bt] <- CR[ba] | CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.Or(ba, bb);
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

XEEMITTER(crorc, 0x4C000342, XL)(PPCHIRBuilder& f, InstrData& i) {
  // CR[bt] <- CR[ba] | ¬CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.Or(ba, f.Not(bb));
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

XEEMITTER(crxor, 0x4C000182, XL)(PPCHIRBuilder& f, InstrData& i) {
  // CR[bt] <- CR[ba] xor CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.Xor(ba, bb);
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

XEEMITTER(mcrf, 0x4C000000, XL)(PPCHIRBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

// System linkage (A-24)

XEEMITTER(sc, 0x44000002, SC)(PPCHIRBuilder& f, InstrData& i) {
  f.CallExtern(f.symbol_info());
  return 0;
}

// Trap (A-25)

int InstrEmit_trap(PPCHIRBuilder& f, InstrData& i, Value* va, Value* vb,
                   uint32_t TO) {
  // if (a < b) & TO[0] then TRAP
  // if (a > b) & TO[1] then TRAP
  // if (a = b) & TO[2] then TRAP
  // if (a <u b) & TO[3] then TRAP
  // if (a >u b) & TO[4] then TRAP
  // Bits swapped:
  // 01234
  // 43210
  if (!TO) {
    return 0;
  }
  Value* v = nullptr;
  if (TO & (1 << 4)) {
    // a < b
    auto cmp = f.CompareSLT(va, vb);
    v = v ? f.Or(v, cmp) : cmp;
  }
  if (TO & (1 << 3)) {
    // a > b
    auto cmp = f.CompareSGT(va, vb);
    v = v ? f.Or(v, cmp) : cmp;
  }
  if (TO & (1 << 2)) {
    // a = b
    auto cmp = f.CompareEQ(va, vb);
    v = v ? f.Or(v, cmp) : cmp;
  }
  if (TO & (1 << 1)) {
    // a <u b
    auto cmp = f.CompareULT(va, vb);
    v = v ? f.Or(v, cmp) : cmp;
  }
  if (TO & (1 << 0)) {
    // a >u b
    auto cmp = f.CompareUGT(va, vb);
    v = v ? f.Or(v, cmp) : cmp;
  }
  if (v) {
    f.TrapTrue(v);
  }
  return 0;
}

XEEMITTER(td, 0x7C000088, X)(PPCHIRBuilder& f, InstrData& i) {
  // a <- (RA)
  // b <- (RB)
  // if (a < b) & TO[0] then TRAP
  // if (a > b) & TO[1] then TRAP
  // if (a = b) & TO[2] then TRAP
  // if (a <u b) & TO[3] then TRAP
  // if (a >u b) & TO[4] then TRAP
  Value* ra = f.LoadGPR(i.X.RA);
  Value* rb = f.LoadGPR(i.X.RB);
  return InstrEmit_trap(f, i, ra, rb, i.X.RT);
}

XEEMITTER(tdi, 0x08000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // a <- (RA)
  // if (a < EXTS(SI)) & TO[0] then TRAP
  // if (a > EXTS(SI)) & TO[1] then TRAP
  // if (a = EXTS(SI)) & TO[2] then TRAP
  // if (a <u EXTS(SI)) & TO[3] then TRAP
  // if (a >u EXTS(SI)) & TO[4] then TRAP
  Value* ra = f.LoadGPR(i.D.RA);
  Value* rb = f.LoadConstant(XEEXTS16(i.D.DS));
  return InstrEmit_trap(f, i, ra, rb, i.D.RT);
}

XEEMITTER(tw, 0x7C000008, X)(PPCHIRBuilder& f, InstrData& i) {
  // a <- EXTS((RA)[32:63])
  // b <- EXTS((RB)[32:63])
  // if (a < b) & TO[0] then TRAP
  // if (a > b) & TO[1] then TRAP
  // if (a = b) & TO[2] then TRAP
  // if (a <u b) & TO[3] then TRAP
  // if (a >u b) & TO[4] then TRAP
  Value* ra =
      f.SignExtend(f.Truncate(f.LoadGPR(i.X.RA), INT32_TYPE), INT64_TYPE);
  Value* rb =
      f.SignExtend(f.Truncate(f.LoadGPR(i.X.RB), INT32_TYPE), INT64_TYPE);
  return InstrEmit_trap(f, i, ra, rb, i.X.RT);
}

XEEMITTER(twi, 0x0C000000, D)(PPCHIRBuilder& f, InstrData& i) {
  // a <- EXTS((RA)[32:63])
  // if (a < EXTS(SI)) & TO[0] then TRAP
  // if (a > EXTS(SI)) & TO[1] then TRAP
  // if (a = EXTS(SI)) & TO[2] then TRAP
  // if (a <u EXTS(SI)) & TO[3] then TRAP
  // if (a >u EXTS(SI)) & TO[4] then TRAP
  if (i.D.RA == 0 && i.D.RT == 0x1F) {
    // This is a special trap. Probably.
    uint16_t type = (uint16_t)XEEXTS16(i.D.DS);
    f.Trap(type);
    return 0;
  }
  Value* ra =
      f.SignExtend(f.Truncate(f.LoadGPR(i.D.RA), INT32_TYPE), INT64_TYPE);
  Value* rb = f.LoadConstant(XEEXTS16(i.D.DS));
  return InstrEmit_trap(f, i, ra, rb, i.D.RT);
}

// Processor control (A-26)

XEEMITTER(mfcr, 0x7C000026, X)(PPCHIRBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mfspr, 0x7C0002A6, XFX)(PPCHIRBuilder& f, InstrData& i) {
  // n <- spr[5:9] || spr[0:4]
  // if length(SPR(n)) = 64 then
  //   RT <- SPR(n)
  // else
  //   RT <- i32.0 || SPR(n)
  Value* v;
  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  switch (n) {
    case 1:
      // XER
      v = f.LoadXER();
      break;
    case 8:
      // LR
      v = f.LoadLR();
      break;
    case 9:
      // CTR
      v = f.LoadCTR();
      break;
    // 268 + 269 = TB + TBU
    default:
      XEINSTRNOTIMPLEMENTED();
      return 1;
  }
  f.StoreGPR(i.XFX.RT, v);
  return 0;
}

XEEMITTER(mftb, 0x7C0002E6, XFX)(PPCHIRBuilder& f, InstrData& i) {
  Value* time = f.LoadClock();
  f.StoreGPR(i.XFX.RT, time);

  return 0;
}

XEEMITTER(mtcrf, 0x7C000120, XFX)(PPCHIRBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtspr, 0x7C0003A6, XFX)(PPCHIRBuilder& f, InstrData& i) {
  // n <- spr[5:9] || spr[0:4]
  // if length(SPR(n)) = 64 then
  //   SPR(n) <- (RS)
  // else
  //   SPR(n) <- (RS)[32:63]

  Value* rt = f.LoadGPR(i.XFX.RT);

  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  switch (n) {
    case 1:
      // XER
      f.StoreXER(rt);
      break;
    case 8:
      // LR
      f.StoreLR(rt);
      break;
    case 9:
      // CTR
      f.StoreCTR(rt);
      break;
    default:
      XEINSTRNOTIMPLEMENTED();
      return 1;
  }

  return 0;
}

// TODO(benvanik): MSR is used for toggling interrupts, and it'd be nice to
//                 obey that setting. It's usually guarding atomic stores.

XEEMITTER(mfmsr, 0x7C0000A6, X)(PPCHIRBuilder& f, InstrData& i) {
  f.Nop();
  return 0;
}

XEEMITTER(mtmsr, 0x7C000124, X)(PPCHIRBuilder& f, InstrData& i) {
  f.Nop();
  return 0;
}

XEEMITTER(mtmsrd, 0x7C000164, X)(PPCHIRBuilder& f, InstrData& i) {
  f.Nop();
  return 0;
}

void RegisterEmitCategoryControl() {
  XEREGISTERINSTR(bx, 0x48000000);
  XEREGISTERINSTR(bcx, 0x40000000);
  XEREGISTERINSTR(bcctrx, 0x4C000420);
  XEREGISTERINSTR(bclrx, 0x4C000020);
  XEREGISTERINSTR(crand, 0x4C000202);
  XEREGISTERINSTR(crandc, 0x4C000102);
  XEREGISTERINSTR(creqv, 0x4C000242);
  XEREGISTERINSTR(crnand, 0x4C0001C2);
  XEREGISTERINSTR(crnor, 0x4C000042);
  XEREGISTERINSTR(cror, 0x4C000382);
  XEREGISTERINSTR(crorc, 0x4C000342);
  XEREGISTERINSTR(crxor, 0x4C000182);
  XEREGISTERINSTR(mcrf, 0x4C000000);
  XEREGISTERINSTR(sc, 0x44000002);
  XEREGISTERINSTR(td, 0x7C000088);
  XEREGISTERINSTR(tdi, 0x08000000);
  XEREGISTERINSTR(tw, 0x7C000008);
  XEREGISTERINSTR(twi, 0x0C000000);
  XEREGISTERINSTR(mfcr, 0x7C000026);
  XEREGISTERINSTR(mfspr, 0x7C0002A6);
  XEREGISTERINSTR(mftb, 0x7C0002E6);
  XEREGISTERINSTR(mtcrf, 0x7C000120);
  XEREGISTERINSTR(mtspr, 0x7C0003A6);
  XEREGISTERINSTR(mfmsr, 0x7C0000A6);
  XEREGISTERINSTR(mtmsr, 0x7C000124);
  XEREGISTERINSTR(mtmsrd, 0x7C000164);
}

}  // namespace ppc
}  // namespace frontend
}  // namespace alloy

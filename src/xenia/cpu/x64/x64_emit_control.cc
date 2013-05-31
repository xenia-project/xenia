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
using namespace xe::cpu::sdb;

using namespace AsmJit;


namespace xe {
namespace cpu {
namespace x64 {


int XeEmitIndirectBranchTo(
    X64Emitter& e, X86Compiler& c, const char* src, uint32_t cia,
    bool lk, uint32_t reg) {
  // TODO(benvanik): run a DFA pass to see if we can detect whether this is
  //     a normal function return that is pulling the LR from the stack that
  //     it set in the prolog. If so, we can omit the dynamic check!

  // NOTE: we avoid spilling registers until we know that the target is not
  // a basic block within this function.

  GpVar target;
  switch (reg) {
    case kXEPPCRegLR:
      target = e.lr_value();
      break;
    case kXEPPCRegCTR:
      target = e.ctr_value();
      break;
    default:
      XEASSERTALWAYS();
      return 1;
  }

  // Dynamic test when branching to LR, which is usually used for the return.
  // We only do this if LK=0 as returns wouldn't set LR.
  // Ideally it's a return and we can just do a simple ret and be done.
  // If it's not, we fall through to the full indirection logic.
  if (!lk && reg == kXEPPCRegLR) {
    // The return block will spill registers for us.
    // TODO(benvanik): 'lr_mismatch' debug info.
    // Note: we need to test on *only* the 32-bit target, as the target ptr may
    //     have garbage in the upper 32 bits.
    c.test(target.r32(), c.getGpArg(1).r32());
    // TODO(benvanik): evaluate hint here.
    c.jnz(e.GetReturnLabel(), kCondHintLikely);
  }

  // Defer to the generator, which will do fancy things.
  bool likely_local = !lk && reg == kXEPPCRegCTR;
  return e.GenerateIndirectionBranch(cia, target, lk, likely_local);
}

int XeEmitBranchTo(
    X64Emitter& e, X86Compiler& c, const char* src, uint32_t cia,
    bool lk, GpVar* condition = NULL) {
  FunctionBlock* fn_block = e.fn_block();

  // Fast-path for branches to other blocks.
  // Only valid when not tracing branches.
  if (!FLAGS_trace_branches &&
      fn_block->outgoing_type == FunctionBlock::kTargetBlock) {
    XEASSERT(!lk);
    Label target_label = e.GetBlockLabel(fn_block->outgoing_address);
    if (condition) {
      // Fast test -- if condition passed then jump to target.
      // TODO(benvanik): need to spill here? somehow?
      c.test((*condition).r8(), (*condition).r8());
      c.jnz(target_label);
    } else {
      // TODO(benvanik): need to spill here?
      //e.SpillRegisters();
      c.jmp(target_label);
    }
    return 0;
  }

  // Only branch of conditionals when we have one.
  Label post_jump_label;
  if (condition) {
    // TODO(benvanik): add debug info for this?
    post_jump_label = c.newLabel();
    c.test((*condition).r8(), (*condition).r8());
    // TODO(benvanik): experiment with various hints?
    c.jz(post_jump_label, kCondHintNone);
  }

  e.TraceBranch(cia);

  // Get the basic block and switch behavior based on outgoing type.
  int result = 0;
  switch (fn_block->outgoing_type) {
    case FunctionBlock::kTargetBlock:
      // Often taken care of above, when not tracing branches.
      XEASSERT(!lk);
      c.jmp(e.GetBlockLabel(fn_block->outgoing_address));
      break;
    case FunctionBlock::kTargetFunction:
    {
      // Spill all registers to memory.
      // TODO(benvanik): only spill ones used by the target function? Use
      //     calling convention flags on the function to not spill temp
      //     registers?
      e.SpillRegisters();

      XEASSERTNOTNULL(fn_block->outgoing_function);
      // TODO(benvanik): check to see if this is the last block in the function.
      //     This would enable tail calls/etc.
      bool is_end = false;
      if (!lk || is_end) {
        // Tail. No need to refill the local register values, just return.
        // We optimize this by passing in the LR from our parent instead of the
        // next instruction. This allows the return from our callee to pop
        // all the way up.
        e.CallFunction(fn_block->outgoing_function, c.getGpArg(1), true);
        // No ret needed - we jumped!
      } else {
        // Will return here eventually.
        // Refill registers from state.
        GpVar lr(c.newGpVar());
        c.mov(lr, imm(cia + 4));
        e.CallFunction(fn_block->outgoing_function, lr, false);
        e.FillRegisters();
      }
      break;
    }
    case FunctionBlock::kTargetLR:
    {
      // An indirect jump.
      printf("INDIRECT JUMP VIA LR: %.8X\n", cia);
      result = XeEmitIndirectBranchTo(e, c, src, cia, lk, kXEPPCRegLR);
      break;
    }
    case FunctionBlock::kTargetCTR:
    {
      // An indirect jump.
      printf("INDIRECT JUMP VIA CTR: %.8X\n", cia);
      result = XeEmitIndirectBranchTo(e, c, src, cia, lk, kXEPPCRegCTR);
      break;
    }
    default:
    case FunctionBlock::kTargetNone:
      XEASSERTALWAYS();
      result = 1;
      break;
  }

  if (condition) {
    c.bind(post_jump_label);
  }

  return result;
}


XEEMITTER(bx,           0x48000000, I  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // if AA then
  //   NIA <- EXTS(LI || 0b00)
  // else
  //   NIA <- CIA + EXTS(LI || 0b00)
  // if LK then
  //   LR <- CIA + 4

  uint32_t nia;
  if (i.I.AA) {
    nia = XEEXTS26(i.I.LI << 2);
  } else {
    nia = i.address + XEEXTS26(i.I.LI << 2);
  }
  if (i.I.LK) {
    e.update_lr_value(imm(i.address + 4));
  }

  return XeEmitBranchTo(e, c, "bx", i.address, i.I.LK);
}

XEEMITTER(bcx,          0x40000000, B  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
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

  // TODO(benvanik): this may be wrong and overwrite LRs when not desired!
  // The docs say always, though...
  if (i.B.LK) {
    e.update_lr_value(imm(i.address + 4));
  }

  // TODO(benvanik): optimize to just use x64 ops.
  //     Need to handle the case where both ctr and cond set.

  GpVar ctr_ok;
  if (XESELECTBITS(i.B.BO, 2, 2)) {
    // Ignore ctr.
  } else {
    // Decrement counter.
    GpVar ctr(c.newGpVar());
    c.mov(ctr, e.ctr_value());
    c.dec(ctr);
    e.update_ctr_value(ctr);

    // Ctr check.
    c.cmp(ctr, imm(0));
    ctr_ok = c.newGpVar();
    if (XESELECTBITS(i.B.BO, 1, 1)) {
      c.setz(ctr_ok.r8());
    } else {
      c.setnz(ctr_ok.r8());
    }
  }

  GpVar cond_ok;
  if (XESELECTBITS(i.B.BO, 4, 4)) {
    // Ignore cond.
  } else {
    GpVar cr(c.newGpVar());
    c.mov(cr, e.cr_value(i.XL.BI >> 2));
    c.and_(cr, imm(1 << (i.XL.BI & 3)));
    c.cmp(cr, imm(0));
    cond_ok = c.newGpVar();
    if (XESELECTBITS(i.XL.BO, 3, 3)) {
      c.setnz(cond_ok.r8());
    } else {
      c.setz(cond_ok.r8());
    }
  }

  // We do a bit of optimization here to make the llvm assembly easier to read.
  GpVar* ok = NULL;
  if (ctr_ok.getId() != kInvalidValue && cond_ok.getId() != kInvalidValue) {
    c.and_(ctr_ok, cond_ok);
    ok = &ctr_ok;
  } else if (ctr_ok.getId() != kInvalidValue) {
    ok = &ctr_ok;
  } else if (cond_ok.getId() != kInvalidValue) {
    ok = &cond_ok;
  }

  uint32_t nia;
  if (i.B.AA) {
    nia = XEEXTS26(i.B.BD << 2);
  } else {
    nia = i.address + XEEXTS26(i.B.BD << 2);
  }
  if (XeEmitBranchTo(e, c, "bcx", i.address, i.B.LK, ok)) {
    return 1;
  }

  return 0;
}

XEEMITTER(bcctrx,       0x4C000420, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // cond_ok <- BO[0] | (CR[BI+32] ≡ BO[1])
  // if cond_ok then
  //   NIA <- CTR[0:61] || 0b00
  // if LK then
  //   LR <- CIA + 4

  // NOTE: the condition bits are reversed!
  // 01234 (docs)
  // 43210 (real)

  // TODO(benvanik): this may be wrong and overwrite LRs when not desired!
  // The docs say always, though...
  if (i.XL.LK) {
    e.update_lr_value(imm(i.address + 4));
  }

  GpVar cond_ok;
  if (XESELECTBITS(i.XL.BO, 4, 4)) {
    // Ignore cond.
  } else {
    GpVar cr(c.newGpVar());
    c.mov(cr, e.cr_value(i.XL.BI >> 2));
    c.and_(cr, imm(1 << (i.XL.BI & 3)));
    c.cmp(cr, imm(0));
    cond_ok = c.newGpVar();
    if (XESELECTBITS(i.XL.BO, 3, 3)) {
      c.setnz(cond_ok.r8());
    } else {
      c.setz(cond_ok.r8());
    }
  }

  // We do a bit of optimization here to make the llvm assembly easier to read.
  GpVar* ok = NULL;
  if (cond_ok.getId() != kInvalidValue) {
    ok = &cond_ok;
  }

  if (XeEmitBranchTo(e, c, "bcctrx", i.address, i.XL.LK, ok)) {
    return 1;
  }

  return 0;
}

XEEMITTER(bclrx,        0x4C000020, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
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

  // TODO(benvanik): this may be wrong and overwrite LRs when not desired!
  // The docs say always, though...
  if (i.XL.LK) {
    e.update_lr_value(imm(i.address + 4));
  }

  GpVar ctr_ok;
  if (XESELECTBITS(i.XL.BO, 2, 2)) {
    // Ignore ctr.
  } else {
    // Decrement counter.
    GpVar ctr(c.newGpVar());
    c.mov(ctr, e.ctr_value());
    c.dec(ctr);
    e.update_ctr_value(ctr);

    // Ctr check.
    c.cmp(ctr, imm(0));
    ctr_ok = c.newGpVar();
    if (XESELECTBITS(i.XL.BO, 1, 1)) {
      c.setz(ctr_ok.r8());
    } else {
      c.setnz(ctr_ok.r8());
    }
  }

  GpVar cond_ok;
  if (XESELECTBITS(i.XL.BO, 4, 4)) {
    // Ignore cond.
  } else {
    GpVar cr(c.newGpVar());
    c.mov(cr, e.cr_value(i.XL.BI >> 2));
    c.and_(cr, imm(1 << (i.XL.BI & 3)));
    c.cmp(cr, imm(0));
    cond_ok = c.newGpVar();
    if (XESELECTBITS(i.XL.BO, 3, 3)) {
      c.setnz(cond_ok.r8());
    } else {
      c.setz(cond_ok.r8());
    }
  }

  // We do a bit of optimization here to make the llvm assembly easier to read.
  GpVar* ok = NULL;
  if (ctr_ok.getId() != kInvalidValue && cond_ok.getId() != kInvalidValue) {
    c.and_(ctr_ok, cond_ok);
    ok = &ctr_ok;
  } else if (ctr_ok.getId() != kInvalidValue) {
    ok = &ctr_ok;
  } else if (cond_ok.getId() != kInvalidValue) {
    ok = &cond_ok;
  }

  if (XeEmitBranchTo(e, c, "bclrx", i.address, i.XL.LK, ok)) {
    return 1;
  }

  return 0;
}


// Condition register logical (A-23)

XEEMITTER(crand,        0x4C000202, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crandc,       0x4C000102, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(creqv,        0x4C000242, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crnand,       0x4C0001C2, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crnor,        0x4C000042, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(cror,         0x4C000382, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crorc,        0x4C000342, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crxor,        0x4C000182, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mcrf,         0x4C000000, XL )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// System linkage (A-24)

XEEMITTER(sc,           0x44000002, SC )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Trap (A-25)

int XeEmitTrap(X64Emitter& e, X86Compiler& c, InstrData& i,
               GpVar& va, GpVar& vb, uint32_t TO) {
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

  XELOGCPU("twi not implemented - instruction ignored");

  // TODO(benvanik): port from LLVM

  // BasicBlock* after_bb = BasicBlock::Create(*e.context(), "", e.fn(),
  //                                           e.GetNextBasicBlock());
  // BasicBlock* trap_bb = BasicBlock::Create(*e.context(), "", e.fn(),
  //                                          after_bb);

  // // Create the basic blocks (so we can chain).
  // std::vector<BasicBlock*> bbs;
  // if (TO & (1 << 4)) {
  //   bbs.push_back(BasicBlock::Create(*e.context(), "", e.fn(), trap_bb));
  // }
  // if (TO & (1 << 3)) {
  //   bbs.push_back(BasicBlock::Create(*e.context(), "", e.fn(), trap_bb));
  // }
  // if (TO & (1 << 2)) {
  //   bbs.push_back(BasicBlock::Create(*e.context(), "", e.fn(), trap_bb));
  // }
  // if (TO & (1 << 1)) {
  //   bbs.push_back(BasicBlock::Create(*e.context(), "", e.fn(), trap_bb));
  // }
  // if (TO & (1 << 0)) {
  //   bbs.push_back(BasicBlock::Create(*e.context(), "", e.fn(), trap_bb));
  // }
  // bbs.push_back(after_bb);

  // // Jump to the first bb.
  // b.CreateBr(bbs.front());

  // // Setup each basic block.
  // std::vector<BasicBlock*>::iterator it = bbs.begin();
  // if (TO & (1 << 4)) {
  //   // a < b
  //   BasicBlock* bb = *(it++);
  //   b.SetInsertPoint(bb);
  //   GpVar cmp = b.CreateICmpSLT(va, vb);
  //   b.CreateCondBr(cmp, trap_bb, *it);
  // }
  // if (TO & (1 << 3)) {
  //   // a > b
  //   BasicBlock* bb = *(it++);
  //   b.SetInsertPoint(bb);
  //   GpVar cmp = b.CreateICmpSGT(va, vb);
  //   b.CreateCondBr(cmp, trap_bb, *it);
  // }
  // if (TO & (1 << 2)) {
  //   // a = b
  //   BasicBlock* bb = *(it++);
  //   b.SetInsertPoint(bb);
  //   GpVar cmp = b.CreateICmpEQ(va, vb);
  //   b.CreateCondBr(cmp, trap_bb, *it);
  // }
  // if (TO & (1 << 1)) {
  //   // a <u b
  //   BasicBlock* bb = *(it++);
  //   b.SetInsertPoint(bb);
  //   GpVar cmp = b.CreateICmpULT(va, vb);
  //   b.CreateCondBr(cmp, trap_bb, *it);
  // }
  // if (TO & (1 << 0)) {
  //   // a >u b
  //   BasicBlock* bb = *(it++);
  //   b.SetInsertPoint(bb);
  //   GpVar cmp = b.CreateICmpUGT(va, vb);
  //   b.CreateCondBr(cmp, trap_bb, *it);
  // }

  // // Create trap BB.
  // b.SetInsertPoint(trap_bb);
  // e.SpillRegisters();
  // // TODO(benvanik): use @llvm.debugtrap? could make debugging better
  // b.CreateCall2(e.gen_module()->getFunction("XeTrap"),
  //               e.fn()->arg_begin(),
  //               e.get_uint64(i.address));
  // b.CreateBr(after_bb);

  // // Resume.
  // b.SetInsertPoint(after_bb);

  return 1;
}

XEEMITTER(td,           0x7C000088, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // a <- (RA)
  // b <- (RB)
  // if (a < b) & TO[0] then TRAP
  // if (a > b) & TO[1] then TRAP
  // if (a = b) & TO[2] then TRAP
  // if (a <u b) & TO[3] then TRAP
  // if (a >u b) & TO[4] then TRAP
  GpVar va = e.gpr_value(i.X.RA);
  GpVar vb = e.gpr_value(i.X.RA);
  return XeEmitTrap(e, c, i, va, vb, i.X.RT);
}

XEEMITTER(tdi,          0x08000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // a <- (RA)
  // if (a < EXTS(SI)) & TO[0] then TRAP
  // if (a > EXTS(SI)) & TO[1] then TRAP
  // if (a = EXTS(SI)) & TO[2] then TRAP
  // if (a <u EXTS(SI)) & TO[3] then TRAP
  // if (a >u EXTS(SI)) & TO[4] then TRAP
  GpVar va = e.gpr_value(i.D.RA);
  GpVar vb(c.newGpVar());
  c.mov(vb, imm(XEEXTS16(i.D.DS)));
  return XeEmitTrap(e, c, i, va, vb, i.D.RT);
}

XEEMITTER(tw,           0x7C000008, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // a <- EXTS((RA)[32:63])
  // b <- EXTS((RB)[32:63])
  // if (a < b) & TO[0] then TRAP
  // if (a > b) & TO[1] then TRAP
  // if (a = b) & TO[2] then TRAP
  // if (a <u b) & TO[3] then TRAP
  // if (a >u b) & TO[4] then TRAP
  GpVar va = e.sign_extend(e.gpr_value(i.X.RA), 4, 8);
  GpVar vb = e.sign_extend(e.gpr_value(i.X.RA), 4, 8);
  return XeEmitTrap(e, c, i, va, vb, i.X.RT);
}

XEEMITTER(twi,          0x0C000000, D  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // a <- EXTS((RA)[32:63])
  // if (a < EXTS(SI)) & TO[0] then TRAP
  // if (a > EXTS(SI)) & TO[1] then TRAP
  // if (a = EXTS(SI)) & TO[2] then TRAP
  // if (a <u EXTS(SI)) & TO[3] then TRAP
  // if (a >u EXTS(SI)) & TO[4] then TRAP
  GpVar va = e.sign_extend(e.gpr_value(i.D.RA), 4, 8);
  GpVar vb(c.newGpVar());
  c.mov(vb, imm(XEEXTS16(i.D.DS)));
  return XeEmitTrap(e, c, i, va, vb, i.D.RT);
}


// Processor control (A-26)

XEEMITTER(mfcr,         0x7C000026, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mfspr,        0x7C0002A6, XFX)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- spr[5:9] || spr[0:4]
  // if length(SPR(n)) = 64 then
  //   RT <- SPR(n)
  // else
  //   RT <- i32.0 || SPR(n)

  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  GpVar v;
  switch (n) {
  case 1:
    // XER
    v = e.xer_value();
    break;
  case 8:
    // LR
    v = e.lr_value();
    break;
  case 9:
    // CTR
    v = e.ctr_value();
    break;
  default:
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  e.update_gpr_value(i.XFX.RT, v);

  e.clear_constant_gpr_value(i.XFX.RT);

  return 0;
}

XEEMITTER(mftb,         0x7C0002E6, XFX)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtcrf,        0x7C000120, XFX)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtspr,        0x7C0003A6, XFX)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // n <- spr[5:9] || spr[0:4]
  // if length(SPR(n)) = 64 then
  //   SPR(n) <- (RS)
  // else
  //   SPR(n) <- (RS)[32:63]

  GpVar v = e.gpr_value(i.XFX.RT);

  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  switch (n) {
  case 1:
    // XER
    e.update_xer_value(v);
    break;
  case 8:
    // LR
    e.update_lr_value(v);
    break;
  case 9:
    // CTR
    e.update_ctr_value(v);
    break;
  default:
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
}


void X64RegisterEmitCategoryControl() {
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
}


}  // namespace x64
}  // namespace cpu
}  // namespace xe

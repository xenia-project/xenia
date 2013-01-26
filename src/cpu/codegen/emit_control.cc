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
#include <xenia/cpu/ppc/state.h>


using namespace llvm;
using namespace xe::cpu::codegen;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;


namespace xe {
namespace cpu {
namespace codegen {


int XeEmitIndirectBranchTo(
    FunctionGenerator& g, IRBuilder<>& b, const char* src, uint32_t cia,
    bool lk, uint32_t reg) {
  // TODO(benvanik): run a DFA pass to see if we can detect whether this is
  //     a normal function return that is pulling the LR from the stack that
  //     it set in the prolog. If so, we can omit the dynamic check!

  // NOTE: we avoid spilling registers until we know that the target is not
  // a basic block within this function.

  Value* target;
  switch (reg) {
    case kXEPPCRegLR:
      target = g.lr_value();
      break;
    case kXEPPCRegCTR:
      target = g.ctr_value();
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
    BasicBlock* next_block = g.GetNextBasicBlock();
    BasicBlock* mismatch_bb = BasicBlock::Create(*g.context(), "lr_mismatch",
                                                 g.gen_fn(), next_block);
    Value* lr_cmp = b.CreateICmpEQ(target, ++(g.gen_fn()->arg_begin()));
    // The return block will spill registers for us.
    b.CreateCondBr(lr_cmp, g.GetReturnBasicBlock(), mismatch_bb);
    b.SetInsertPoint(mismatch_bb);
  }

  // Defer to the generator, which will do fancy things.
  bool likely_local = !lk && reg == kXEPPCRegCTR;
  return g.GenerateIndirectionBranch(cia, target, lk, likely_local);
}

int XeEmitBranchTo(
    FunctionGenerator& g, IRBuilder<>& b, const char* src, uint32_t cia,
    bool lk) {
  // Get the basic block and switch behavior based on outgoing type.
  FunctionBlock* fn_block = g.fn_block();
  switch (fn_block->outgoing_type) {
    case FunctionBlock::kTargetBlock:
    {
      BasicBlock* target_bb = g.GetBasicBlock(fn_block->outgoing_address);
      XEASSERTNOTNULL(target_bb);
      b.CreateBr(target_bb);
      break;
    }
    case FunctionBlock::kTargetFunction:
    {
      // Spill all registers to memory.
      // TODO(benvanik): only spill ones used by the target function? Use
      //     calling convention flags on the function to not spill temp
      //     registers?
      g.SpillRegisters();

      Function* target_fn = g.GetFunction(fn_block->outgoing_function);
      Function::arg_iterator args = g.gen_fn()->arg_begin();
      Value* state_ptr = args;
      b.CreateCall2(target_fn, state_ptr, b.getInt64(cia + 4));
      BasicBlock* next_bb = g.GetNextBasicBlock();
      if (!lk || !next_bb) {
        // Tail. No need to refill the local register values, just return.
        b.CreateRetVoid();
      } else {
        // Refill registers from state.
        g.FillRegisters();
        b.CreateBr(next_bb);
      }
      break;
    }
    case FunctionBlock::kTargetLR:
    {
      // An indirect jump.
      printf("INDIRECT JUMP VIA LR: %.8X\n", cia);
      return XeEmitIndirectBranchTo(g, b, src, cia, lk, kXEPPCRegLR);
    }
    case FunctionBlock::kTargetCTR:
    {
      // An indirect jump.
      printf("INDIRECT JUMP VIA CTR: %.8X\n", cia);
      return XeEmitIndirectBranchTo(g, b, src, cia, lk, kXEPPCRegCTR);
    }
    default:
    case FunctionBlock::kTargetNone:
      XEASSERTALWAYS();
      return 1;
  }
  return 0;
}

XEEMITTER(bx,           0x48000000, I  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
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
    g.update_lr_value(b.getInt32(i.address + 4));
  }

  return XeEmitBranchTo(g, b, "bx", i.address, i.I.LK);
}

XEEMITTER(bcx,          0x40000000, B  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
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
    g.update_lr_value(b.getInt32(i.address + 4));
  }

  Value* ctr_ok = NULL;
  if (XESELECTBITS(i.B.BO, 2, 2)) {
    // Ignore ctr.
  } else {
    // Decrement counter.
    Value* ctr = g.ctr_value();
    ctr = g.ctr_value();
    ctr = b.CreateSub(ctr, b.getInt64(1));

    // Ctr check.
    if (XESELECTBITS(i.B.BO, 1, 1)) {
      ctr_ok = b.CreateICmpEQ(ctr, b.getInt64(0));
    } else {
      ctr_ok = b.CreateICmpNE(ctr, b.getInt64(0));
    }
  }

  Value* cond_ok = NULL;
  if (XESELECTBITS(i.B.BO, 4, 4)) {
    // Ignore cond.
  } else {
    Value* cr = g.cr_value();
    cr = b.CreateAnd(cr, XEBITMASK(i.B.BI, i.B.BI));
    if (XESELECTBITS(i.B.BO, 3, 3)) {
      cond_ok = b.CreateICmpNE(cr, b.getInt64(0));
    } else {
      cond_ok = b.CreateICmpEQ(cr, b.getInt64(0));
    }
  }

  // We do a bit of optimization here to make the llvm assembly easier to read.
  Value* ok = NULL;
  if (ctr_ok && cond_ok) {
    ok = b.CreateAnd(ctr_ok, cond_ok);
  } else if (ctr_ok) {
    ok = ctr_ok;
  } else if (cond_ok) {
    ok = cond_ok;
  }

  // Handle unconditional branches without extra fluff.
  BasicBlock* original_bb = b.GetInsertBlock();
  if (ok) {
    char name[32];
    xesnprintfa(name, XECOUNT(name), "loc_%.8X_bcx", i.address);
    BasicBlock* next_block = g.GetNextBasicBlock();
    BasicBlock* branch_bb = BasicBlock::Create(*g.context(), name, g.gen_fn(),
                                               next_block);

    b.CreateCondBr(ok, branch_bb, next_block);
    b.SetInsertPoint(branch_bb);
  }

  // Note that this occurs entirely within the branch true block.
  uint32_t nia;
  if (i.B.AA) {
    nia = XEEXTS26(i.B.BD << 2);
  } else {
    nia = i.address + XEEXTS26(i.B.BD << 2);
  }
  if (XeEmitBranchTo(g, b, "bcx", i.address, i.B.LK)) {
    return 1;
  }

  b.SetInsertPoint(original_bb);

  return 0;
}

XEEMITTER(bcctrx,       0x4C000420, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
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
    g.update_lr_value(b.getInt32(i.address + 4));
  }

  Value* cond_ok = NULL;
  if (XESELECTBITS(i.XL.BO, 4, 4)) {
    // Ignore cond.
  } else {
    Value* cr = g.cr_value();
    cr = b.CreateAnd(cr, XEBITMASK(i.XL.BI, i.XL.BI));
    if (XESELECTBITS(i.XL.BO, 3, 3)) {
      cond_ok = b.CreateICmpNE(cr, b.getInt64(0));
    } else {
      cond_ok = b.CreateICmpEQ(cr, b.getInt64(0));
    }
  }

  // We do a bit of optimization here to make the llvm assembly easier to read.
  Value* ok = NULL;
  if (cond_ok) {
    ok = cond_ok;
  }

  // Handle unconditional branches without extra fluff.
  BasicBlock* original_bb = b.GetInsertBlock();
  if (ok) {
    char name[32];
    xesnprintfa(name, XECOUNT(name), "loc_%.8X_bcctrx", i.address);
    BasicBlock* next_block = g.GetNextBasicBlock();
    XEASSERTNOTNULL(next_block);
    BasicBlock* branch_bb = BasicBlock::Create(*g.context(), name, g.gen_fn(),
                                               next_block);

    b.CreateCondBr(ok, branch_bb, next_block);
    b.SetInsertPoint(branch_bb);
  }

  // Note that this occurs entirely within the branch true block.
  if (XeEmitBranchTo(g, b, "bcctrx", i.address, i.XL.LK)) {
    return 1;
  }

  b.SetInsertPoint(original_bb);

  return 0;
}

XEEMITTER(bclrx,        0x4C000020, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
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
    g.update_lr_value(b.getInt32(i.address + 4));
  }

  Value* ctr_ok = NULL;
  if (XESELECTBITS(i.XL.BO, 2, 2)) {
    // Ignore ctr.
  } else {
    // Decrement counter.
    Value* ctr = g.ctr_value();
    ctr = g.ctr_value();
    ctr = b.CreateSub(ctr, b.getInt64(1));

    // Ctr check.
    if (XESELECTBITS(i.XL.BO, 1, 1)) {
      ctr_ok = b.CreateICmpEQ(ctr, b.getInt64(0));
    } else {
      ctr_ok = b.CreateICmpNE(ctr, b.getInt64(0));
    }
  }

  Value* cond_ok = NULL;
  if (XESELECTBITS(i.XL.BO, 4, 4)) {
    // Ignore cond.
  } else {
    Value* cr = g.cr_value();
    cr = b.CreateAnd(cr, XEBITMASK(i.XL.BI, i.XL.BI));
    if (XESELECTBITS(i.XL.BO, 3, 3)) {
      cond_ok = b.CreateICmpNE(cr, b.getInt64(0));
    } else {
      cond_ok = b.CreateICmpEQ(cr, b.getInt64(0));
    }
  }

  // We do a bit of optimization here to make the llvm assembly easier to read.
  Value* ok = NULL;
  if (ctr_ok && cond_ok) {
    ok = b.CreateAnd(ctr_ok, cond_ok);
  } else if (ctr_ok) {
    ok = ctr_ok;
  } else if (cond_ok) {
    ok = cond_ok;
  }

  // Handle unconditional branches without extra fluff.
  BasicBlock* original_bb = b.GetInsertBlock();
  if (ok) {
    char name[32];
    xesnprintfa(name, XECOUNT(name), "loc_%.8X_bclrx", i.address);
    BasicBlock* next_block = g.GetNextBasicBlock();
    XEASSERTNOTNULL(next_block);
    BasicBlock* branch_bb = BasicBlock::Create(*g.context(), name, g.gen_fn(),
                                               next_block);

    b.CreateCondBr(ok, branch_bb, next_block);
    b.SetInsertPoint(branch_bb);
  }

  // Note that this occurs entirely within the branch true block.
  if (XeEmitBranchTo(g, b, "bclrx", i.address, i.XL.LK)) {
    return 1;
  }

  b.SetInsertPoint(original_bb);

  return 0;
}


// Condition register logical (A-23)

XEEMITTER(crand,        0x4C000202, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crandc,       0x4C000102, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(creqv,        0x4C000242, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crnand,       0x4C0001C2, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crnor,        0x4C000042, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(cror,         0x4C000382, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crorc,        0x4C000342, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(crxor,        0x4C000182, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mcrf,         0x4C000000, XL )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// System linkage (A-24)

XEEMITTER(sc,           0x44000002, SC )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Trap (A-25)

XEEMITTER(td,           0x7C000088, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(tdi,          0x08000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(tw,           0x7C000008, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(twi,          0x0C000000, D  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}


// Processor control (A-26)

XEEMITTER(mfcr,         0x7C000026, X  )(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mfspr,        0x7C0002A6, XFX)(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // n <- spr[5:9] || spr[0:4]
  // if length(SPR(n)) = 64 then
  //   RT <- SPR(n)
  // else
  //   RT <- i32.0 || SPR(n)

  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  Value* v = NULL;
  switch (n) {
  case 1:
    // XER
    v = g.xer_value();
    break;
  case 8:
    // LR
    v = g.lr_value();
    break;
  case 9:
    // CTR
    v = g.ctr_value();
    break;
  default:
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  g.update_gpr_value(i.XFX.RT, v);

  return 0;
}

XEEMITTER(mftb,         0x7C0002E6, XFX)(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtcrf,        0x7C000120, XFX)(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtspr,        0x7C0003A6, XFX)(FunctionGenerator& g, IRBuilder<>& b, InstrData& i) {
  // n <- spr[5:9] || spr[0:4]
  // if length(SPR(n)) = 64 then
  //   SPR(n) <- (RS)
  // else
  //   SPR(n) <- (RS)[32:63]

  Value* v = g.gpr_value(i.XFX.RT);

  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  switch (n) {
  case 1:
    // XER
    g.update_xer_value(v);
    break;
  case 8:
    // LR
    g.update_lr_value(v);
    break;
  case 9:
    // CTR
    g.update_ctr_value(v);
    break;
  default:
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }

  return 0;
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

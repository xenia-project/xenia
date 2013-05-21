/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/libjit/libjit_emit.h>


using namespace xe::cpu;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;


namespace xe {
namespace cpu {
namespace libjit {


// int XeEmitIndirectBranchTo(
//     LibjitEmitter& e, jit_function_t f, const char* src, uint32_t cia,
//     bool lk, uint32_t reg) {
//   // TODO(benvanik): run a DFA pass to see if we can detect whether this is
//   //     a normal function return that is pulling the LR from the stack that
//   //     it set in the prolog. If so, we can omit the dynamic check!

//   // NOTE: we avoid spilling registers until we know that the target is not
//   // a basic block within this function.

//   Value* target;
//   switch (reg) {
//     case kXEPPCRegLR:
//       target = e.lr_value();
//       break;
//     case kXEPPCRegCTR:
//       target = e.ctr_value();
//       break;
//     default:
//       XEASSERTALWAYS();
//       return 1;
//   }

//   // Dynamic test when branching to LR, which is usually used for the return.
//   // We only do this if LK=0 as returns wouldn't set LR.
//   // Ideally it's a return and we can just do a simple ret and be done.
//   // If it's not, we fall through to the full indirection logic.
//   if (!lk && reg == kXEPPCRegLR) {
//     BasicBlock* next_block = e.GetNextBasicBlock();
//     BasicBlock* mismatch_bb = BasicBlock::Create(*e.context(), "lr_mismatch",
//                                                  e.gen_fn(), next_block);
//     Value* lr_cmp = b.CreateICmpEQ(target, ++(e.gen_fn()->arg_begin()));
//     // The return block will spill registers for us.
//     b.CreateCondBr(lr_cmp, e.GetReturnBasicBlock(), mismatch_bb);
//     b.SetInsertPoint(mismatch_bb);
//   }

//   // Defer to the generator, which will do fancy things.
//   bool likely_local = !lk && reg == kXEPPCRegCTR;
//   return e.GenerateIndirectionBranch(cia, target, lk, likely_local);
// }

// int XeEmitBranchTo(
//     LibjitEmitter& e, jit_function_t f, const char* src, uint32_t cia,
//     bool lk) {
//   // Get the basic block and switch behavior based on outgoing type.
//   FunctionBlock* fn_block = e.fn_block();
//   switch (fn_block->outgoing_type) {
//     case FunctionBlock::kTargetBlock:
//     {
//       BasicBlock* target_bb = e.GetBasicBlock(fn_block->outgoing_address);
//       XEASSERTNOTNULL(target_bb);
//       b.CreateBr(target_bb);
//       break;
//     }
//     case FunctionBlock::kTargetFunction:
//     {
//       // Spill all registers to memory.
//       // TODO(benvanik): only spill ones used by the target function? Use
//       //     calling convention flags on the function to not spill temp
//       //     registers?
//       e.SpillRegisters();

//       XEASSERTNOTNULL(fn_block->outgoing_function);
//       Function* target_fn = e.GetFunction(fn_block->outgoing_function);
//       Function::arg_iterator args = e.gen_fn()->arg_begin();
//       Value* state_ptr = args;
//       BasicBlock* next_bb = e.GetNextBasicBlock();
//       if (!lk || !next_bb) {
//         // Tail. No need to refill the local register values, just return.
//         // We optimize this by passing in the LR from our parent instead of the
//         // next instruction. This allows the return from our callee to pop
//         // all the way up.
//         b.CreateCall2(target_fn, state_ptr, ++args);
//         b.CreateRetVoid();
//       } else {
//         // Will return here eventually.
//         // Refill registers from state.
//         b.CreateCall2(target_fn, state_ptr, b.getInt64(cia + 4));
//         e.FillRegisters();
//         b.CreateBr(next_bb);
//       }
//       break;
//     }
//     case FunctionBlock::kTargetLR:
//     {
//       // An indirect jump.
//       printf("INDIRECT JUMP VIA LR: %.8X\n", cia);
//       return XeEmitIndirectBranchTo(e, b, src, cia, lk, kXEPPCRegLR);
//     }
//     case FunctionBlock::kTargetCTR:
//     {
//       // An indirect jump.
//       printf("INDIRECT JUMP VIA CTR: %.8X\n", cia);
//       return XeEmitIndirectBranchTo(e, b, src, cia, lk, kXEPPCRegCTR);
//     }
//     default:
//     case FunctionBlock::kTargetNone:
//       XEASSERTALWAYS();
//       return 1;
//   }
//   return 0;
// }


// XEEMITTER(bx,           0x48000000, I  )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   // if AA then
//   //   NIA <- EXTS(LI || 0b00)
//   // else
//   //   NIA <- CIA + EXTS(LI || 0b00)
//   // if LK then
//   //   LR <- CIA + 4

//   uint32_t nia;
//   if (i.I.AA) {
//     nia = XEEXTS26(i.I.LI << 2);
//   } else {
//     nia = i.address + XEEXTS26(i.I.LI << 2);
//   }
//   if (i.I.LK) {
//     e.update_lr_value(b.getInt32(i.address + 4));
//   }

//   return XeEmitBranchTo(e, b, "bx", i.address, i.I.LK);
// }

// XEEMITTER(bcx,          0x40000000, B  )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   // if ¬BO[2] then
//   //   CTR <- CTR - 1
//   // ctr_ok <- BO[2] | ((CTR[0:63] != 0) XOR BO[3])
//   // cond_ok <- BO[0] | (CR[BI+32] ≡ BO[1])
//   // if ctr_ok & cond_ok then
//   //   if AA then
//   //     NIA <- EXTS(BD || 0b00)
//   //   else
//   //     NIA <- CIA + EXTS(BD || 0b00)
//   // if LK then
//   //   LR <- CIA + 4

//   // NOTE: the condition bits are reversed!
//   // 01234 (docs)
//   // 43210 (real)

//   // TODO(benvanik): this may be wrong and overwrite LRs when not desired!
//   // The docs say always, though...
//   if (i.B.LK) {
//     e.update_lr_value(b.getInt32(i.address + 4));
//   }

//   Value* ctr_ok = NULL;
//   if (XESELECTBITS(i.B.BO, 2, 2)) {
//     // Ignore ctr.
//   } else {
//     // Decrement counter.
//     Value* ctr = e.ctr_value();
//     ctr = b.CreateSub(ctr, b.getInt64(1));
//     e.update_ctr_value(ctr);

//     // Ctr check.
//     if (XESELECTBITS(i.B.BO, 1, 1)) {
//       ctr_ok = b.CreateICmpEQ(ctr, b.getInt64(0));
//     } else {
//       ctr_ok = b.CreateICmpNE(ctr, b.getInt64(0));
//     }
//   }

//   Value* cond_ok = NULL;
//   if (XESELECTBITS(i.B.BO, 4, 4)) {
//     // Ignore cond.
//   } else {
//     Value* cr = e.cr_value(i.B.BI >> 2);
//     cr = b.CreateAnd(cr, 1 << (i.B.BI & 3));
//     if (XESELECTBITS(i.B.BO, 3, 3)) {
//       cond_ok = b.CreateICmpNE(cr, b.getInt64(0));
//     } else {
//       cond_ok = b.CreateICmpEQ(cr, b.getInt64(0));
//     }
//   }

//   // We do a bit of optimization here to make the llvm assembly easier to read.
//   Value* ok = NULL;
//   if (ctr_ok && cond_ok) {
//     ok = b.CreateAnd(ctr_ok, cond_ok);
//   } else if (ctr_ok) {
//     ok = ctr_ok;
//   } else if (cond_ok) {
//     ok = cond_ok;
//   }

//   // Handle unconditional branches without extra fluff.
//   BasicBlock* original_bb = b.GetInsertBlock();
//   if (ok) {
//     char name[32];
//     xesnprintfa(name, XECOUNT(name), "loc_%.8X_bcx", i.address);
//     BasicBlock* next_block = e.GetNextBasicBlock();
//     BasicBlock* branch_bb = BasicBlock::Create(*e.context(), name, e.gen_fn(),
//                                                next_block);

//     b.CreateCondBr(ok, branch_bb, next_block);
//     b.SetInsertPoint(branch_bb);
//   }

//   // Note that this occurs entirely within the branch true block.
//   uint32_t nia;
//   if (i.B.AA) {
//     nia = XEEXTS26(i.B.BD << 2);
//   } else {
//     nia = i.address + XEEXTS26(i.B.BD << 2);
//   }
//   if (XeEmitBranchTo(e, b, "bcx", i.address, i.B.LK)) {
//     return 1;
//   }

//   b.SetInsertPoint(original_bb);

//   return 0;
// }

// XEEMITTER(bcctrx,       0x4C000420, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   // cond_ok <- BO[0] | (CR[BI+32] ≡ BO[1])
//   // if cond_ok then
//   //   NIA <- CTR[0:61] || 0b00
//   // if LK then
//   //   LR <- CIA + 4

//   // NOTE: the condition bits are reversed!
//   // 01234 (docs)
//   // 43210 (real)

//   // TODO(benvanik): this may be wrong and overwrite LRs when not desired!
//   // The docs say always, though...
//   if (i.XL.LK) {
//     e.update_lr_value(b.getInt32(i.address + 4));
//   }

//   Value* cond_ok = NULL;
//   if (XESELECTBITS(i.XL.BO, 4, 4)) {
//     // Ignore cond.
//   } else {
//     Value* cr = e.cr_value(i.XL.BI >> 2);
//     cr = b.CreateAnd(cr, 1 << (i.XL.BI & 3));
//     if (XESELECTBITS(i.XL.BO, 3, 3)) {
//       cond_ok = b.CreateICmpNE(cr, b.getInt64(0));
//     } else {
//       cond_ok = b.CreateICmpEQ(cr, b.getInt64(0));
//     }
//   }

//   // We do a bit of optimization here to make the llvm assembly easier to read.
//   Value* ok = NULL;
//   if (cond_ok) {
//     ok = cond_ok;
//   }

//   // Handle unconditional branches without extra fluff.
//   BasicBlock* original_bb = b.GetInsertBlock();
//   if (ok) {
//     char name[32];
//     xesnprintfa(name, XECOUNT(name), "loc_%.8X_bcctrx", i.address);
//     BasicBlock* next_block = e.GetNextBasicBlock();
//     XEASSERTNOTNULL(next_block);
//     BasicBlock* branch_bb = BasicBlock::Create(*e.context(), name, e.gen_fn(),
//                                                next_block);

//     b.CreateCondBr(ok, branch_bb, next_block);
//     b.SetInsertPoint(branch_bb);
//   }

//   // Note that this occurs entirely within the branch true block.
//   if (XeEmitBranchTo(e, b, "bcctrx", i.address, i.XL.LK)) {
//     return 1;
//   }

//   b.SetInsertPoint(original_bb);

//   return 0;
// }

// XEEMITTER(bclrx,        0x4C000020, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   // if ¬BO[2] then
//   //   CTR <- CTR - 1
//   // ctr_ok <- BO[2] | ((CTR[0:63] != 0) XOR BO[3]
//   // cond_ok <- BO[0] | (CR[BI+32] ≡ BO[1])
//   // if ctr_ok & cond_ok then
//   //   NIA <- LR[0:61] || 0b00
//   // if LK then
//   //   LR <- CIA + 4

//   // NOTE: the condition bits are reversed!
//   // 01234 (docs)
//   // 43210 (real)

//   // TODO(benvanik): this may be wrong and overwrite LRs when not desired!
//   // The docs say always, though...
//   if (i.XL.LK) {
//     e.update_lr_value(b.getInt32(i.address + 4));
//   }

//   Value* ctr_ok = NULL;
//   if (XESELECTBITS(i.XL.BO, 2, 2)) {
//     // Ignore ctr.
//   } else {
//     // Decrement counter.
//     Value* ctr = e.ctr_value();
//     ctr = b.CreateSub(ctr, b.getInt64(1));

//     // Ctr check.
//     if (XESELECTBITS(i.XL.BO, 1, 1)) {
//       ctr_ok = b.CreateICmpEQ(ctr, b.getInt64(0));
//     } else {
//       ctr_ok = b.CreateICmpNE(ctr, b.getInt64(0));
//     }
//   }

//   Value* cond_ok = NULL;
//   if (XESELECTBITS(i.XL.BO, 4, 4)) {
//     // Ignore cond.
//   } else {
//     Value* cr = e.cr_value(i.XL.BI >> 2);
//     cr = b.CreateAnd(cr, 1 << (i.XL.BI & 3));
//     if (XESELECTBITS(i.XL.BO, 3, 3)) {
//       cond_ok = b.CreateICmpNE(cr, b.getInt64(0));
//     } else {
//       cond_ok = b.CreateICmpEQ(cr, b.getInt64(0));
//     }
//   }

//   // We do a bit of optimization here to make the llvm assembly easier to read.
//   Value* ok = NULL;
//   if (ctr_ok && cond_ok) {
//     ok = b.CreateAnd(ctr_ok, cond_ok);
//   } else if (ctr_ok) {
//     ok = ctr_ok;
//   } else if (cond_ok) {
//     ok = cond_ok;
//   }

//   // Handle unconditional branches without extra fluff.
//   BasicBlock* original_bb = b.GetInsertBlock();
//   if (ok) {
//     char name[32];
//     xesnprintfa(name, XECOUNT(name), "loc_%.8X_bclrx", i.address);
//     BasicBlock* next_block = e.GetNextBasicBlock();
//     XEASSERTNOTNULL(next_block);
//     BasicBlock* branch_bb = BasicBlock::Create(*e.context(), name, e.gen_fn(),
//                                                next_block);

//     b.CreateCondBr(ok, branch_bb, next_block);
//     b.SetInsertPoint(branch_bb);
//   }

//   // Note that this occurs entirely within the branch true block.
//   if (XeEmitBranchTo(e, b, "bclrx", i.address, i.XL.LK)) {
//     return 1;
//   }

//   b.SetInsertPoint(original_bb);

//   return 0;
// }


// // Condition register logical (A-23)

// XEEMITTER(crand,        0x4C000202, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(crandc,       0x4C000102, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(creqv,        0x4C000242, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(crnand,       0x4C0001C2, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(crnor,        0x4C000042, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(cror,         0x4C000382, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(crorc,        0x4C000342, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(crxor,        0x4C000182, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(mcrf,         0x4C000000, XL )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }


// // System linkage (A-24)

// XEEMITTER(sc,           0x44000002, SC )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }


// // Trap (A-25)

// int XeEmitTrap(LibjitEmitter& e, jit_function_t f, InstrData& i,
//                 Value* va, Value* vb, uint32_t TO) {
//   // if (a < b) & TO[0] then TRAP
//   // if (a > b) & TO[1] then TRAP
//   // if (a = b) & TO[2] then TRAP
//   // if (a <u b) & TO[3] then TRAP
//   // if (a >u b) & TO[4] then TRAP
//   // Bits swapped:
//   // 01234
//   // 43210

//   if (!TO) {
//     return 0;
//   }

//   BasicBlock* after_bb = BasicBlock::Create(*e.context(), "", e.gen_fn(),
//                                             e.GetNextBasicBlock());
//   BasicBlock* trap_bb = BasicBlock::Create(*e.context(), "", e.gen_fn(),
//                                            after_bb);

//   // Create the basic blocks (so we can chain).
//   std::vector<BasicBlock*> bbs;
//   if (TO & (1 << 4)) {
//     bbs.push_back(BasicBlock::Create(*e.context(), "", e.gen_fn(), trap_bb));
//   }
//   if (TO & (1 << 3)) {
//     bbs.push_back(BasicBlock::Create(*e.context(), "", e.gen_fn(), trap_bb));
//   }
//   if (TO & (1 << 2)) {
//     bbs.push_back(BasicBlock::Create(*e.context(), "", e.gen_fn(), trap_bb));
//   }
//   if (TO & (1 << 1)) {
//     bbs.push_back(BasicBlock::Create(*e.context(), "", e.gen_fn(), trap_bb));
//   }
//   if (TO & (1 << 0)) {
//     bbs.push_back(BasicBlock::Create(*e.context(), "", e.gen_fn(), trap_bb));
//   }
//   bbs.push_back(after_bb);

//   // Jump to the first bb.
//   b.CreateBr(bbs.front());

//   // Setup each basic block.
//   std::vector<BasicBlock*>::iterator it = bbs.begin();
//   if (TO & (1 << 4)) {
//     // a < b
//     BasicBlock* bb = *(it++);
//     b.SetInsertPoint(bb);
//     Value* cmp = b.CreateICmpSLT(va, vb);
//     b.CreateCondBr(cmp, trap_bb, *it);
//   }
//   if (TO & (1 << 3)) {
//     // a > b
//     BasicBlock* bb = *(it++);
//     b.SetInsertPoint(bb);
//     Value* cmp = b.CreateICmpSGT(va, vb);
//     b.CreateCondBr(cmp, trap_bb, *it);
//   }
//   if (TO & (1 << 2)) {
//     // a = b
//     BasicBlock* bb = *(it++);
//     b.SetInsertPoint(bb);
//     Value* cmp = b.CreateICmpEQ(va, vb);
//     b.CreateCondBr(cmp, trap_bb, *it);
//   }
//   if (TO & (1 << 1)) {
//     // a <u b
//     BasicBlock* bb = *(it++);
//     b.SetInsertPoint(bb);
//     Value* cmp = b.CreateICmpULT(va, vb);
//     b.CreateCondBr(cmp, trap_bb, *it);
//   }
//   if (TO & (1 << 0)) {
//     // a >u b
//     BasicBlock* bb = *(it++);
//     b.SetInsertPoint(bb);
//     Value* cmp = b.CreateICmpUGT(va, vb);
//     b.CreateCondBr(cmp, trap_bb, *it);
//   }

//   // Create trap BB.
//   b.SetInsertPoint(trap_bb);
//   e.SpillRegisters();
//   // TODO(benvanik): use @llvm.debugtrap? could make debugging better
//   b.CreateCall2(e.gen_module()->getFunction("XeTrap"),
//                 e.gen_fn()->arg_begin(),
//                 b.getInt32(i.address));
//   b.CreateBr(after_bb);

//   // Resume.
//   b.SetInsertPoint(after_bb);

//   return 0;
// }

// XEEMITTER(td,           0x7C000088, X  )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   // a <- (RA)
//   // b <- (RB)
//   // if (a < b) & TO[0] then TRAP
//   // if (a > b) & TO[1] then TRAP
//   // if (a = b) & TO[2] then TRAP
//   // if (a <u b) & TO[3] then TRAP
//   // if (a >u b) & TO[4] then TRAP
//   return XeEmitTrap(e, b, i,
//                     e.gpr_value(i.X.RA),
//                     e.gpr_value(i.X.RB),
//                     i.X.RT);
// }

// XEEMITTER(tdi,          0x08000000, D  )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   // a <- (RA)
//   // if (a < EXTS(SI)) & TO[0] then TRAP
//   // if (a > EXTS(SI)) & TO[1] then TRAP
//   // if (a = EXTS(SI)) & TO[2] then TRAP
//   // if (a <u EXTS(SI)) & TO[3] then TRAP
//   // if (a >u EXTS(SI)) & TO[4] then TRAP
//   return XeEmitTrap(e, b, i,
//                     e.gpr_value(i.D.RA),
//                     b.getInt64(XEEXTS16(i.D.DS)),
//                     i.D.RT);
// }

// XEEMITTER(tw,           0x7C000008, X  )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   // a <- EXTS((RA)[32:63])
//   // b <- EXTS((RB)[32:63])
//   // if (a < b) & TO[0] then TRAP
//   // if (a > b) & TO[1] then TRAP
//   // if (a = b) & TO[2] then TRAP
//   // if (a <u b) & TO[3] then TRAP
//   // if (a >u b) & TO[4] then TRAP
//   return XeEmitTrap(e, b, i,
//                     b.CreateSExt(b.CreateTrunc(e.gpr_value(i.X.RA),
//                                                b.getInt32Ty()),
//                                  b.getInt64Ty()),
//                     b.CreateSExt(b.CreateTrunc(e.gpr_value(i.X.RB),
//                                                b.getInt32Ty()),
//                                  b.getInt64Ty()),
//                     i.X.RT);
// }

// XEEMITTER(twi,          0x0C000000, D  )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   // a <- EXTS((RA)[32:63])
//   // if (a < EXTS(SI)) & TO[0] then TRAP
//   // if (a > EXTS(SI)) & TO[1] then TRAP
//   // if (a = EXTS(SI)) & TO[2] then TRAP
//   // if (a <u EXTS(SI)) & TO[3] then TRAP
//   // if (a >u EXTS(SI)) & TO[4] then TRAP
//   return XeEmitTrap(e, b, i,
//                     b.CreateSExt(b.CreateTrunc(e.gpr_value(i.D.RA),
//                                                b.getInt32Ty()),
//                                  b.getInt64Ty()),
//                     b.getInt64(XEEXTS16(i.D.DS)),
//                     i.D.RT);
// }


// // Processor control (A-26)

// XEEMITTER(mfcr,         0x7C000026, X  )(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(mfspr,        0x7C0002A6, XFX)(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   // n <- spr[5:9] || spr[0:4]
//   // if length(SPR(n)) = 64 then
//   //   RT <- SPR(n)
//   // else
//   //   RT <- i32.0 || SPR(n)

//   const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
//   Value* v = NULL;
//   switch (n) {
//   case 1:
//     // XER
//     v = e.xer_value();
//     break;
//   case 8:
//     // LR
//     v = e.lr_value();
//     break;
//   case 9:
//     // CTR
//     v = e.ctr_value();
//     break;
//   default:
//     XEINSTRNOTIMPLEMENTED();
//     return 1;
//   }

//   e.update_gpr_value(i.XFX.RT, v);

//   return 0;
// }

// XEEMITTER(mftb,         0x7C0002E6, XFX)(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(mtcrf,        0x7C000120, XFX)(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }

// XEEMITTER(mtspr,        0x7C0003A6, XFX)(LibjitEmitter& e, jit_function_t f, InstrData& i) {
//   // n <- spr[5:9] || spr[0:4]
//   // if length(SPR(n)) = 64 then
//   //   SPR(n) <- (RS)
//   // else
//   //   SPR(n) <- (RS)[32:63]

//   Value* v = e.gpr_value(i.XFX.RT);

//   const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
//   switch (n) {
//   case 1:
//     // XER
//     e.update_xer_value(v);
//     break;
//   case 8:
//     // LR
//     e.update_lr_value(v);
//     break;
//   case 9:
//     // CTR
//     e.update_ctr_value(v);
//     break;
//   default:
//     XEINSTRNOTIMPLEMENTED();
//     return 1;
//   }

//   return 0;
// }


void LibjitRegisterEmitCategoryControl() {
  // XEREGISTERINSTR(bx,           0x48000000);
  // XEREGISTERINSTR(bcx,          0x40000000);
  // XEREGISTERINSTR(bcctrx,       0x4C000420);
  // XEREGISTERINSTR(bclrx,        0x4C000020);
  // XEREGISTERINSTR(crand,        0x4C000202);
  // XEREGISTERINSTR(crandc,       0x4C000102);
  // XEREGISTERINSTR(creqv,        0x4C000242);
  // XEREGISTERINSTR(crnand,       0x4C0001C2);
  // XEREGISTERINSTR(crnor,        0x4C000042);
  // XEREGISTERINSTR(cror,         0x4C000382);
  // XEREGISTERINSTR(crorc,        0x4C000342);
  // XEREGISTERINSTR(crxor,        0x4C000182);
  // XEREGISTERINSTR(mcrf,         0x4C000000);
  // XEREGISTERINSTR(sc,           0x44000002);
  // XEREGISTERINSTR(td,           0x7C000088);
  // XEREGISTERINSTR(tdi,          0x08000000);
  // XEREGISTERINSTR(tw,           0x7C000008);
  // XEREGISTERINSTR(twi,          0x0C000000);
  // XEREGISTERINSTR(mfcr,         0x7C000026);
  // XEREGISTERINSTR(mfspr,        0x7C0002A6);
  // XEREGISTERINSTR(mftb,         0x7C0002E6);
  // XEREGISTERINSTR(mtcrf,        0x7C000120);
  // XEREGISTERINSTR(mtspr,        0x7C0003A6);
}


}  // namespace libjit
}  // namespace cpu
}  // namespace xe

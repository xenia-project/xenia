/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lowering/lowering_sequences.h>

#include <alloy/backend/x64/lowering/lowering_table.h>

using namespace alloy;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lir;
using namespace alloy::backend::x64::lowering;
using namespace alloy::hir;

void alloy::backend::x64::lowering::RegisterSequences(LoweringTable* table) {
  // --------------------------------------------------------------------------
  // General
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_COMMENT, [](LIRBuilder& lb, Instr*& instr) {
    char* str = (char*)instr->src1.offset;
    lb.Comment(str);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_NOP, [](LIRBuilder& lb, Instr*& instr) {
    lb.Nop();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Debugging
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_SOURCE_OFFSET, [](LIRBuilder& lb, Instr*& instr) {
    // TODO(benvanik): translate source offsets for mapping? We're just passing
    //     down the original offset - it may be nice to have two.
    lb.SourceOffset(instr->src1.offset);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DEBUG_BREAK, [](LIRBuilder& lb, Instr*& instr) {
    lb.DebugBreak();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DEBUG_BREAK_TRUE, [](LIRBuilder& lb, Instr*& instr) {
    auto skip_label = lb.NewLocalLabel();
    lb.Test(instr->src1.value, instr->src1.value);
    lb.JumpNE(skip_label);
    lb.DebugBreak();
    lb.MarkLabel(skip_label);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_TRAP, [](LIRBuilder& lb, Instr*& instr) {
    lb.Trap();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_TRAP_TRUE, [](LIRBuilder& lb, Instr*& instr) {
    auto skip_label = lb.NewLocalLabel();
    lb.Test(instr->src1.value, instr->src1.value);
    lb.JumpNE(skip_label);
    lb.Trap();
    lb.MarkLabel(skip_label);
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Calls
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_CALL, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CALL_TRUE, [](LIRBuilder& lb, Instr*& instr) {
    auto skip_label = lb.NewLocalLabel();
    lb.Test(instr->src1.value, instr->src1.value);
    lb.JumpNE(skip_label);
    // TODO
    lb.MarkLabel(skip_label);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CALL_INDIRECT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CALL_INDIRECT_TRUE, [](LIRBuilder& lb, Instr*& instr) {
    auto skip_label = lb.NewLocalLabel();
    lb.Test(instr->src1.value, instr->src1.value);
    lb.JumpNE(skip_label);
    // TODO
    lb.MarkLabel(skip_label);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_RETURN, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SET_RETURN_ADDRESS, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Branches
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_BRANCH, [](LIRBuilder& lb, Instr*& instr) {
    auto target = (LIRLabel*)instr->src1.label->tag;
    lb.Jump(target);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_BRANCH_TRUE, [](LIRBuilder& lb, Instr*& instr) {
    lb.Test(instr->src1.value, instr->src1.value);
    auto target = (LIRLabel*)instr->src2.label->tag;
    lb.JumpEQ(target);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_BRANCH_FALSE, [](LIRBuilder& lb, Instr*& instr) {
    lb.Test(instr->src1.value, instr->src1.value);
    auto target = (LIRLabel*)instr->src2.label->tag;
    lb.JumpNE(target);
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Types
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_ASSIGN, [](LIRBuilder& lb, Instr*& instr) {
    lb.Mov(instr->dest, instr->src1.value);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CAST, [](LIRBuilder& lb, Instr*& instr) {
    lb.Mov(instr->dest, instr->src1.value);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ZERO_EXTEND, [](LIRBuilder& lb, Instr*& instr) {
    lb.MovZX(instr->dest, instr->src1.value);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SIGN_EXTEND, [](LIRBuilder& lb, Instr*& instr) {
    lb.MovSX(instr->dest, instr->src1.value);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_TRUNCATE, [](LIRBuilder& lb, Instr*& instr) {
    lb.Mov(instr->dest, instr->src1.value);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CONVERT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ROUND, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_CONVERT_I2F, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_CONVERT_F2I, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Constants
  // --------------------------------------------------------------------------

  // specials for zeroing/etc (xor/etc)

  table->AddSequence(OPCODE_LOAD_VECTOR_SHL, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_LOAD_VECTOR_SHR, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Context
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_LOAD_CONTEXT, [](LIRBuilder& lb, Instr*& instr) {
    lb.Mov(
        instr->dest,
        LIRRegister(LIRRegisterType::REG64, LIRRegisterName::RCX),
        instr->src1.offset);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_STORE_CONTEXT, [](LIRBuilder& lb, Instr*& instr) {
    lb.Mov(
        LIRRegister(LIRRegisterType::REG64, LIRRegisterName::RCX),
        instr->src1.offset,
        instr->src2.value);
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Memory
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_LOAD, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_LOAD_ACQUIRE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_STORE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_STORE_RELEASE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_PREFETCH, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Comparisons
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_MAX, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_MIN, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SELECT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_IS_TRUE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_IS_FALSE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_EQ, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_NE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SLT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SLE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SGT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SGE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_ULT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_ULE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_UGT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_UGE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DID_CARRY, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DID_OVERFLOW, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_EQ, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_SGT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_SGE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_UGT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_UGE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Math
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_ADD, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ADD_CARRY, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SUB, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_MUL, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DIV, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_REM, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_MULADD, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_MULSUB, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_NEG, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ABS, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SQRT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_RSQRT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DOT_PRODUCT_3, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DOT_PRODUCT_4, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_AND, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_OR, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_XOR, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_NOT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SHL, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_SHL, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SHR, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SHA, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ROTATE_LEFT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_BYTE_SWAP, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CNTLZ, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_INSERT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_EXTRACT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SPLAT, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_PERMUTE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SWIZZLE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Atomic
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_COMPARE_EXCHANGE, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ATOMIC_ADD, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ATOMIC_SUB, [](LIRBuilder& lb, Instr*& instr) {
    // TODO
    instr = instr->next;
    return true;
  });
}

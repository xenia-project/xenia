/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lowering/lowering_sequences.h>

#include <alloy/backend/x64/x64_emitter.h>
#include <alloy/backend/x64/lowering/lowering_table.h>

using namespace alloy;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lowering;
using namespace alloy::hir;

using namespace Xbyak;


namespace {

#define UNIMPLEMENTED_SEQ()

// TODO(benvanik): emit traces/printfs/etc

}  // namespace


void alloy::backend::x64::lowering::RegisterSequences(LoweringTable* table) {
  // --------------------------------------------------------------------------
  // General
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_COMMENT, [](X64Emitter& e, Instr*& instr) {
    //char* str = (char*)instr->src1.offset;
    //lb.Comment(str);
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_NOP, [](X64Emitter& e, Instr*& instr) {
    // If we got this, chances are we want it.
    e.nop();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Debugging
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_SOURCE_OFFSET, [](X64Emitter& e, Instr*& instr) {
    // TODO(benvanik): translate source offsets for mapping? We're just passing
    //     down the original offset - it may be nice to have two.
    //lb.SourceOffset(instr->src1.offset);
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DEBUG_BREAK, [](X64Emitter& e, Instr*& instr) {
    // TODO(benvanik): insert a call to the debug break function to let the
    //     debugger know.
    e.db(0xCC);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DEBUG_BREAK_TRUE, [](X64Emitter& e, Instr*& instr) {
    e.inLocalLabel();
    //e.test(e.rax, e.rax);
    e.jne(".x");
    // TODO(benvanik): insert a call to the debug break function to let the
    //     debugger know.
    e.db(0xCC);
    e.L(".x");
    e.outLocalLabel();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_TRAP, [](X64Emitter& e, Instr*& instr) {
    // TODO(benvanik): insert a call to the trap function to let the
    //     debugger know.
    e.db(0xCC);
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_TRAP_TRUE, [](X64Emitter& e, Instr*& instr) {
    e.inLocalLabel();
    //e.test(rax, rax);
    e.jne(".x");
    // TODO(benvanik): insert a call to the trap function to let the
    //     debugger know.
    e.db(0xCC);
    e.L(".x");
    e.outLocalLabel();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Calls
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_CALL, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CALL_TRUE, [](X64Emitter& e, Instr*& instr) {
    //auto skip_label = lb.NewLocalLabel();
    //lb.Test(instr->src1.value, instr->src1.value);
    //lb.JumpNE(skip_label);
    //// TODO
    //lb.MarkLabel(skip_label);
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CALL_INDIRECT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CALL_INDIRECT_TRUE, [](X64Emitter& e, Instr*& instr) {
    //auto skip_label = lb.NewLocalLabel();
    //lb.Test(instr->src1.value, instr->src1.value);
    //lb.JumpNE(skip_label);
    //UNIMPLEMENTED_SEQ();
    //lb.MarkLabel(skip_label);
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_RETURN, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SET_RETURN_ADDRESS, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Branches
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_BRANCH, [](X64Emitter& e, Instr*& instr) {
    /*auto target = (LIRLabel*)instr->src1.label->tag;
    lb.Jump(target);*/
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_BRANCH_TRUE, [](X64Emitter& e, Instr*& instr) {
    /*lb.Test(instr->src1.value, instr->src1.value);
    auto target = (LIRLabel*)instr->src2.label->tag;
    lb.JumpEQ(target);*/
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_BRANCH_FALSE, [](X64Emitter& e, Instr*& instr) {
    /*lb.Test(instr->src1.value, instr->src1.value);
    auto target = (LIRLabel*)instr->src2.label->tag;
    lb.JumpNE(target);*/
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Types
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_ASSIGN, [](X64Emitter& e, Instr*& instr) {
    //lb.Mov(instr->dest, instr->src1.value);
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CAST, [](X64Emitter& e, Instr*& instr) {
    //lb.Mov(instr->dest, instr->src1.value);
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ZERO_EXTEND, [](X64Emitter& e, Instr*& instr) {
    //lb.MovZX(instr->dest, instr->src1.value);
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SIGN_EXTEND, [](X64Emitter& e, Instr*& instr) {
    //lb.MovSX(instr->dest, instr->src1.value);
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_TRUNCATE, [](X64Emitter& e, Instr*& instr) {
    //lb.Mov(instr->dest, instr->src1.value);
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CONVERT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ROUND, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_CONVERT_I2F, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_CONVERT_F2I, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Constants
  // --------------------------------------------------------------------------

  // specials for zeroing/etc (xor/etc)

  table->AddSequence(OPCODE_LOAD_VECTOR_SHL, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_LOAD_VECTOR_SHR, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_LOAD_CLOCK, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Context
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_LOAD_CONTEXT, [](X64Emitter& e, Instr*& instr) {
    /*lb.Mov(
        instr->dest,
        LIRRegister(LIRRegisterType::REG64, LIRRegisterName::RCX),
        instr->src1.offset);*/
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_STORE_CONTEXT, [](X64Emitter& e, Instr*& instr) {
    /*lb.Mov(
        LIRRegister(LIRRegisterType::REG64, LIRRegisterName::RCX),
        instr->src1.offset,
        instr->src2.value);*/
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Memory
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_LOAD, [](X64Emitter& e, Instr*& instr) {
    // TODO(benvanik): dynamic register access check
    // mov reg, [membase + address.32]
    // TODO(benvanik): special for f32/f64/v128
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_STORE, [](X64Emitter& e, Instr*& instr) {
    // TODO(benvanik): dynamic register access check
    // mov [membase + address.32], reg
    // TODO(benvanik): special for f32/f64/v128
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_PREFETCH, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Comparisons
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_MAX, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_MIN, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SELECT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_IS_TRUE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_IS_FALSE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_EQ, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_NE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SLT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SLE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SGT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SGE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_ULT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_ULE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_UGT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_UGE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DID_CARRY, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DID_OVERFLOW, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DID_SATURATE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_EQ, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_SGT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_SGE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_UGT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_UGE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Math
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_ADD, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ADD_CARRY, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SUB, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_MUL, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_MUL_HI, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DIV, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_MUL_ADD, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_MUL_SUB, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_NEG, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ABS, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SQRT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_RSQRT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_POW2, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_LOG2, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DOT_PRODUCT_3, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_DOT_PRODUCT_4, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_AND, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_OR, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_XOR, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_NOT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SHL, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_SHL, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SHR, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_SHR, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SHA, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_SHA, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ROTATE_LEFT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_BYTE_SWAP, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_CNTLZ, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_INSERT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_EXTRACT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SPLAT, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_PERMUTE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_SWIZZLE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_PACK, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_UNPACK, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  // --------------------------------------------------------------------------
  // Atomic
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_COMPARE_EXCHANGE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ATOMIC_EXCHANGE, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ATOMIC_ADD, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });

  table->AddSequence(OPCODE_ATOMIC_SUB, [](X64Emitter& e, Instr*& instr) {
    UNIMPLEMENTED_SEQ();
    instr = instr->next;
    return true;
  });
}

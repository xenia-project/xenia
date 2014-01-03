/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_LIR_LIR_BUILDER_H_
#define ALLOY_BACKEND_X64_LIR_LIR_BUILDER_H_

#include <alloy/core.h>
#include <alloy/backend/x64/lir/lir_block.h>
#include <alloy/backend/x64/lir/lir_instr.h>
#include <alloy/backend/x64/lir/lir_label.h>
#include <alloy/backend/x64/lir/lir_opcodes.h>
#include <alloy/hir/value.h>


namespace alloy {
namespace backend {
namespace x64 {
class X64Backend;
namespace lir {


class LIRBuilder {
public:
  LIRBuilder(X64Backend* backend);
  ~LIRBuilder();

  void Reset();
  int Finalize();

  void Dump(StringBuffer* str);

  Arena* arena() const { return arena_; }

  LIRBlock* first_block() const { return block_head_; }
  LIRBlock* current_block() const;
  LIRInstr* last_instr() const;

  LIRLabel* NewLabel(const char* name = 0, bool local = false);
  LIRLabel* NewLocalLabel() { return NewLabel(0, true); }
  void MarkLabel(LIRLabel* label, LIRBlock* block = 0);

  // TODO(benvanik): allocations

  LIRBlock* AppendBlock();
  void EndBlock();

  void Comment(const char* format, ...);
  void Nop();
  void SourceOffset(uint64_t offset);

  void DebugBreak();
  void Trap();

  void Mov();

  void Test(int8_t a, int8_t b);
  void Test(int16_t a, int16_t b);
  void Test(int32_t a, int32_t b);
  void Test(int64_t a, int64_t b);
  void Test(hir::Value* a, hir::Value* b);

  void JumpEQ(LIRLabel* label);
  void JumpNE(LIRLabel* label);

private:
  LIRInstr* AppendInstr(const LIROpcodeInfo& opcode, uint16_t flags = 0);

private:
  X64Backend* backend_;
  Arena*      arena_;

  uint32_t    next_label_id_;

  LIRBlock*   block_head_;
  LIRBlock*   block_tail_;
  LIRBlock*   current_block_;
};


}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_LIR_LIR_BUILDER_H_

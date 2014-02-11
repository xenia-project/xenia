/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASSES_REGISTER_ALLOCATION_PASS_H_
#define ALLOY_COMPILER_PASSES_REGISTER_ALLOCATION_PASS_H_

#include <alloy/backend/machine_info.h>
#include <alloy/compiler/compiler_pass.h>


namespace alloy {
namespace compiler {
namespace passes {


class RegisterAllocationPass : public CompilerPass {
public:
  RegisterAllocationPass(const backend::MachineInfo* machine_info);
  virtual ~RegisterAllocationPass();

  virtual int Run(hir::HIRBuilder* builder);

private:
  struct Interval;
  struct Intervals;
  void ComputeLastUse(hir::Value* value);
  bool TryAllocateFreeReg(Interval* current, Intervals& intervals);
  void AllocateBlockedReg(hir::HIRBuilder* builder,
                          Interval* current, Intervals& intervals);

private:
  const backend::MachineInfo* machine_info_;

  struct RegisterFreeUntilSet {
    uint32_t count;
    uint32_t pos[32];
    const backend::MachineInfo::RegisterSet* set;
  };
  struct RegisterFreeUntilSets {
    RegisterFreeUntilSet* int_set;
    RegisterFreeUntilSet* float_set;
    RegisterFreeUntilSet* vec_set;
    RegisterFreeUntilSet* all_sets[3];
  };
  RegisterFreeUntilSets free_until_sets_;
};


}  // namespace passes
}  // namespace compiler
}  // namespace alloy


#endif  // ALLOY_COMPILER_PASSES_REGISTER_ALLOCATION_PASS_H_

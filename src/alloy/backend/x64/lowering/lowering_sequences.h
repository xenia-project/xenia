/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_X64_LOWERING_LOWERING_SEQUENCES_H_
#define ALLOY_BACKEND_X64_X64_LOWERING_LOWERING_SEQUENCES_H_

#include <alloy/core.h>
#include <alloy/hir/instr.h>


namespace alloy {
namespace backend {
namespace x64 {
namespace lowering {

class LoweringTable;

void RegisterSequences(LoweringTable* table);


}  // namespace lowering
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_X64_LOWERING_LOWERING_SEQUENCES_H_

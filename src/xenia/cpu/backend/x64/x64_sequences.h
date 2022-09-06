/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_X64_X64_SEQUENCES_H_
#define XENIA_CPU_BACKEND_X64_X64_SEQUENCES_H_

#include "xenia/cpu/hir/instr.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class X64Emitter;

bool SelectSequence(X64Emitter* e, const hir::Instr* i,
                    const hir::Instr** new_tail);

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_X64_X64_SEQUENCES_H_

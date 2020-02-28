/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/function_debug_info.h"

#include <cstdio>
#include <cstdlib>

#include "xenia/base/logging.h"

namespace xe {
namespace cpu {

FunctionDebugInfo::FunctionDebugInfo()
    : source_disasm_(nullptr),
      raw_hir_disasm_(nullptr),
      hir_disasm_(nullptr),
      machine_code_disasm_(nullptr) {}

FunctionDebugInfo::~FunctionDebugInfo() {
  free(source_disasm_);
  free(raw_hir_disasm_);
  free(hir_disasm_);
  free(machine_code_disasm_);
}

void FunctionDebugInfo::Dump() {
  if (source_disasm_) {
    XELOGD("PPC:\n{}\n", source_disasm_);
  }
  if (raw_hir_disasm_) {
    XELOGD("Unoptimized HIR:\n{}\n", raw_hir_disasm_);
  }
  if (hir_disasm_) {
    XELOGD("Optimized HIR:\n{}\n", hir_disasm_);
  }
  if (machine_code_disasm_) {
    XELOGD("Machine Code:\n{}\n", machine_code_disasm_);
  }
}

}  // namespace cpu
}  // namespace xe

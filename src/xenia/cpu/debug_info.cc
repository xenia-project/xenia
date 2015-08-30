/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/debug_info.h"

#include <cstdio>
#include <cstdlib>

#include "xenia/base/logging.h"

namespace xe {
namespace cpu {

DebugInfo::DebugInfo()
    : source_disasm_(nullptr),
      raw_hir_disasm_(nullptr),
      hir_disasm_(nullptr),
      machine_code_disasm_(nullptr) {}

DebugInfo::~DebugInfo() {
  free(source_disasm_);
  free(raw_hir_disasm_);
  free(hir_disasm_);
  free(machine_code_disasm_);
}

void DebugInfo::Dump() {
  if (source_disasm_) {
    XELOGD("PPC:\n%s\n", source_disasm_);
  }
  if (raw_hir_disasm_) {
    XELOGD("Unoptimized HIR:\n%s\n", raw_hir_disasm_);
  }
  if (hir_disasm_) {
    XELOGD("Optimized HIR:\n%s\n", hir_disasm_);
  }
  if (machine_code_disasm_) {
    XELOGD("Machine Code:\n%s\n", machine_code_disasm_);
  }
}

}  // namespace cpu
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/debug_info.h>

using namespace alloy;
using namespace alloy::runtime;


DebugInfo::DebugInfo() :
    source_disasm_(0),
    raw_hir_disasm_(0),
    hir_disasm_(0),
    machine_code_disasm_(0) {
}

DebugInfo::~DebugInfo() {
  xe_free(source_disasm_);
  xe_free(raw_hir_disasm_);
  xe_free(hir_disasm_);
  xe_free(machine_code_disasm_);
}

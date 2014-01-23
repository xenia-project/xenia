/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_DEBUG_INFO_H_
#define ALLOY_RUNTIME_DEBUG_INFO_H_

#include <alloy/core.h>


namespace alloy {
namespace runtime {


enum DebugInfoFlags {
  DEBUG_INFO_NONE                 = 0,

  DEBUG_INFO_SOURCE_DISASM        = (1 << 1),
  DEBUG_INFO_RAW_HIR_DISASM       = (1 << 2),
  DEBUG_INFO_HIR_DISASM           = (1 << 3),
  DEBUG_INFO_RAW_LIR_DISASM       = (1 << 4),
  DEBUG_INFO_LIR_DISASM           = (1 << 5),
  DEBUG_INFO_MACHINE_CODE_DISASM  = (1 << 6),

  DEBUG_INFO_ALL_DISASM           = 0xFFFF,
};


class DebugInfo {
public:
  DebugInfo();
  ~DebugInfo();

  const char* source_disasm() const { return source_disasm_; }
  void set_source_disasm(char* value) { source_disasm_ = value; }
  const char* raw_hir_disasm() const { return raw_hir_disasm_; }
  void set_raw_hir_disasm(char* value) { raw_hir_disasm_ = value; }
  const char* hir_disasm() const { return hir_disasm_; }
  void set_hir_disasm(char* value) { hir_disasm_ = value; }
  const char* raw_lir_disasm() const { return raw_lir_disasm_; }
  void set_raw_lir_disasm(char* value) { raw_lir_disasm_ = value; }
  const char* lir_disasm() const { return lir_disasm_; }
  void set_lir_disasm(char* value) { lir_disasm_ = value; }
  const char* machine_code_disasm() const { return machine_code_disasm_; }
  void set_machine_code_disasm(char* value) { machine_code_disasm_ = value; }

  // map functions: source addr -> hir index (raw?)
  //                hir index (raw?) to lir index (raw?)
  //                lir index (raw?) to machine code offset
  //                source -> machine code offset

private:
  char* source_disasm_;
  char* raw_hir_disasm_;
  char* hir_disasm_;
  char* raw_lir_disasm_;
  char* lir_disasm_;
  char* machine_code_disasm_;
};


}  // namespace runtime
}  // namespace alloy


#endif  // ALLOY_RUNTIME_DEBUG_INFO_H_

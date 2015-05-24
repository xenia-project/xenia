/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_DEBUG_INFO_H_
#define XENIA_CPU_DEBUG_INFO_H_

#include <cstddef>
#include <cstdint>

#include "xenia/debug/function_trace_data.h"

namespace xe {
namespace cpu {

enum DebugInfoFlags {
  kDebugInfoNone = 0,
  kDebugInfoDisasmSource = (1 << 1),
  kDebugInfoDisasmRawHir = (1 << 2),
  kDebugInfoDisasmHir = (1 << 3),
  kDebugInfoDisasmMachineCode = (1 << 4),
  kDebugInfoAllDisasm = kDebugInfoDisasmSource | kDebugInfoDisasmRawHir |
                        kDebugInfoDisasmHir | kDebugInfoDisasmMachineCode,
  kDebugInfoSourceMap = (1 << 5),
  kDebugInfoTraceFunctions = (1 << 6),
  kDebugInfoTraceFunctionCoverage = (1 << 7) | kDebugInfoTraceFunctions,
  kDebugInfoTraceFunctionReferences = (1 << 8) | kDebugInfoTraceFunctions,
  kDebugInfoTraceFunctionData = (1 << 9) | kDebugInfoTraceFunctions,
  kDebugInfoAll = 0xFFFFFFFF,
};

typedef struct SourceMapEntry_s {
  uint32_t source_offset;  // Original source address/offset.
  uint32_t hir_offset;     // Block ordinal (16b) | Instr ordinal (16b)
  uint32_t code_offset;    // Offset from emitted code start.
} SourceMapEntry;

class DebugInfo {
 public:
  DebugInfo();
  ~DebugInfo();

  uint32_t address_reference_count() const { return address_reference_count_; }
  void set_address_reference_count(uint32_t value) {
    address_reference_count_ = value;
  }
  uint32_t instruction_result_count() const {
    return instruction_result_count_;
  }
  void set_instruction_result_count(uint32_t value) {
    instruction_result_count_ = value;
  }

  debug::FunctionTraceData& trace_data() { return trace_data_; }

  const char* source_disasm() const { return source_disasm_; }
  void set_source_disasm(char* value) { source_disasm_ = value; }
  const char* raw_hir_disasm() const { return raw_hir_disasm_; }
  void set_raw_hir_disasm(char* value) { raw_hir_disasm_ = value; }
  const char* hir_disasm() const { return hir_disasm_; }
  void set_hir_disasm(char* value) { hir_disasm_ = value; }
  const char* machine_code_disasm() const { return machine_code_disasm_; }
  void set_machine_code_disasm(char* value) { machine_code_disasm_ = value; }

  void InitializeSourceMap(size_t source_map_count, SourceMapEntry* source_map);
  SourceMapEntry* LookupSourceOffset(uint32_t offset);
  SourceMapEntry* LookupHIROffset(uint32_t offset);
  SourceMapEntry* LookupCodeOffset(uint32_t offset);

 private:
  uint32_t address_reference_count_;
  uint32_t instruction_result_count_;

  debug::FunctionTraceData trace_data_;

  char* source_disasm_;
  char* raw_hir_disasm_;
  char* hir_disasm_;
  char* machine_code_disasm_;

  size_t source_map_count_;
  SourceMapEntry* source_map_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_DEBUG_INFO_H_

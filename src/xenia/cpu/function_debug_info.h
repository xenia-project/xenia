/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_FUNCTION_DEBUG_INFO_H_
#define XENIA_CPU_FUNCTION_DEBUG_INFO_H_

#include <cstddef>
#include <cstdint>

namespace xe {
namespace cpu {

enum DebugInfoFlags : uint32_t {
  kDebugInfoNone = 0,
  kDebugInfoDisasmSource = (1 << 1),
  kDebugInfoDisasmRawHir = (1 << 2),
  kDebugInfoDisasmHir = (1 << 3),
  kDebugInfoDisasmMachineCode = (1 << 4),
  kDebugInfoAllDisasm = kDebugInfoDisasmSource | kDebugInfoDisasmRawHir |
                        kDebugInfoDisasmHir | kDebugInfoDisasmMachineCode,
  kDebugInfoTraceFunctions = (1 << 6),
  kDebugInfoTraceFunctionCoverage = (1 << 7) | kDebugInfoTraceFunctions,
  kDebugInfoTraceFunctionReferences = (1 << 8) | kDebugInfoTraceFunctions,
  kDebugInfoTraceFunctionData = (1 << 9) | kDebugInfoTraceFunctions,

  kDebugInfoAllTracing =
      kDebugInfoTraceFunctions | kDebugInfoTraceFunctionCoverage |
      kDebugInfoTraceFunctionReferences | kDebugInfoTraceFunctionData,
  kDebugInfoAll = 0xFFFFFFFF,
};

// DEPRECATED
// This will be getting removed or refactored to contain only on-demand
// disassembly data.
class FunctionDebugInfo {
 public:
  FunctionDebugInfo();
  ~FunctionDebugInfo();

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

  const char* source_disasm() const { return source_disasm_; }
  void set_source_disasm(char* value) { source_disasm_ = value; }
  const char* raw_hir_disasm() const { return raw_hir_disasm_; }
  void set_raw_hir_disasm(char* value) { raw_hir_disasm_ = value; }
  const char* hir_disasm() const { return hir_disasm_; }
  void set_hir_disasm(char* value) { hir_disasm_ = value; }
  const char* machine_code_disasm() const { return machine_code_disasm_; }
  void set_machine_code_disasm(char* value) { machine_code_disasm_ = value; }

  void Dump();

 private:
  uint32_t address_reference_count_;
  uint32_t instruction_result_count_;

  char* source_disasm_;
  char* raw_hir_disasm_;
  char* hir_disasm_;
  char* machine_code_disasm_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_FUNCTION_DEBUG_INFO_H_

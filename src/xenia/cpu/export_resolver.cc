/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/export_resolver.h"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"

namespace xe {
namespace cpu {

ExportResolver::ExportResolver() = default;

ExportResolver::~ExportResolver() = default;

void ExportResolver::RegisterTable(
    const std::string& library_name,
    const std::vector<xe::cpu::Export*>* exports) {
  tables_.emplace_back(library_name, exports);
}

Export* ExportResolver::GetExportByOrdinal(const std::string& library_name,
                                           uint16_t ordinal) {
  for (const auto& table : tables_) {
    if (table.name == library_name || table.simple_name == library_name) {
      if (ordinal > table.exports->size()) {
        return nullptr;
      }
      return table.exports->at(ordinal);
    }
  }
  return nullptr;
}

void ExportResolver::SetVariableMapping(const std::string& library_name,
                                        uint16_t ordinal, uint32_t value) {
  auto export_entry = GetExportByOrdinal(library_name, ordinal);
  assert_not_null(export_entry);
  export_entry->tags |= ExportTag::kImplemented;
  export_entry->variable_ptr = value;
}

void ExportResolver::SetFunctionMapping(const std::string& library_name,
                                        uint16_t ordinal,
                                        xe_kernel_export_shim_fn shim) {
  auto export_entry = GetExportByOrdinal(library_name, ordinal);
  assert_not_null(export_entry);
  export_entry->tags |= ExportTag::kImplemented;
  export_entry->function_data.shim = shim;
}

void ExportResolver::SetFunctionMapping(const std::string& library_name,
                                        uint16_t ordinal,
                                        ExportTrampoline trampoline) {
  auto export_entry = GetExportByOrdinal(library_name, ordinal);
  assert_not_null(export_entry);
  export_entry->tags |= ExportTag::kImplemented;
  export_entry->function_data.trampoline = trampoline;
}

}  // namespace cpu
}  // namespace xe

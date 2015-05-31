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

ExportResolver::ExportResolver() {}

ExportResolver::~ExportResolver() {}

void ExportResolver::RegisterTable(const std::string& library_name,
                                   Export* exports, const size_t count) {
  tables_.emplace_back(library_name, exports, count);
}

Export* ExportResolver::GetExportByOrdinal(const std::string& library_name,
                                           uint16_t ordinal) {
  for (const auto& table : tables_) {
    if (table.name == library_name || table.simple_name == library_name) {
      // TODO(benvanik): binary search?
      for (size_t n = 0; n < table.count; n++) {
        if (table.exports[n].ordinal == ordinal) {
          return &table.exports[n];
        }
      }
      return nullptr;
    }
  }
  return nullptr;
}

void ExportResolver::SetVariableMapping(const std::string& library_name,
                                        uint16_t ordinal, uint32_t value) {
  auto export = GetExportByOrdinal(library_name, ordinal);
  assert_not_null(export);
  export->tags |= ExportTag::kImplemented;
  export->variable_ptr = value;
}

void ExportResolver::SetFunctionMapping(const std::string& library_name,
                                        uint16_t ordinal,
                                        xe_kernel_export_shim_fn shim) {
  auto export = GetExportByOrdinal(library_name, ordinal);
  assert_not_null(export);
  export->tags |= ExportTag::kImplemented;
  export->function_data.shim = shim;
}

void ExportResolver::SetFunctionMapping(const std::string& library_name,
                                        uint16_t ordinal,
                                        ExportTrampoline trampoline) {
  auto export = GetExportByOrdinal(library_name, ordinal);
  assert_not_null(export);
  export->tags |= ExportTag::kImplemented;
  export->function_data.trampoline = trampoline;
}

}  // namespace cpu
}  // namespace xe

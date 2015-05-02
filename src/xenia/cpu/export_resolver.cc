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
                                   KernelExport* exports, const size_t count) {
  tables_.emplace_back(library_name, exports, count);

  for (size_t n = 0; n < count; n++) {
    exports[n].is_implemented = false;
    exports[n].variable_ptr = 0;
    exports[n].function_data.shim_data = nullptr;
    exports[n].function_data.shim = nullptr;
  }
}

KernelExport* ExportResolver::GetExportByOrdinal(
    const std::string& library_name, const uint32_t ordinal) {
  for (const auto& table : tables_) {
    if (table.name == library_name) {
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
                                        const uint32_t ordinal,
                                        uint32_t value) {
  auto kernel_export = GetExportByOrdinal(library_name, ordinal);
  assert_not_null(kernel_export);
  kernel_export->is_implemented = true;
  kernel_export->variable_ptr = value;
}

void ExportResolver::SetFunctionMapping(const std::string& library_name,
                                        const uint32_t ordinal, void* shim_data,
                                        xe_kernel_export_shim_fn shim) {
  auto kernel_export = GetExportByOrdinal(library_name, ordinal);
  assert_not_null(kernel_export);
  kernel_export->is_implemented = true;
  kernel_export->function_data.shim_data = shim_data;
  kernel_export->function_data.shim = shim;
}

}  // namespace cpu
}  // namespace xe

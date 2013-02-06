/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/export.h>


using namespace xe;
using namespace xe::kernel;


ExportResolver::ExportResolver() {
}

ExportResolver::~ExportResolver() {
}

void ExportResolver::RegisterTable(
    const char* library_name, KernelExport* exports, const size_t count) {
  ExportTable table;
  XEIGNORE(xestrcpya(table.name, XECOUNT(table.name), library_name));
  table.exports = exports;
  table.count = count;
  tables_.push_back(table);

  for (size_t n = 0; n < count; n++) {
    exports[n].is_implemented = false;
    exports[n].variable_ptr = 0;
    exports[n].function_data.shim_data = NULL;
    exports[n].function_data.shim = NULL;
    exports[n].function_data.impl = NULL;
  }
}

KernelExport* ExportResolver::GetExportByOrdinal(const char* library_name,
                                                 const uint32_t ordinal) {
  for (std::vector<ExportTable>::iterator it = tables_.begin();
       it != tables_.end(); ++it) {
    if (!xestrcmpa(library_name, it->name)) {
      // TODO(benvanik): binary search?
      for (size_t n = 0; n < it->count; n++) {
        if (it->exports[n].ordinal == ordinal) {
          return &it->exports[n];
        }
      }
      return NULL;
    }
  }
  return NULL;
}

KernelExport* ExportResolver::GetExportByName(const char* library_name,
                                              const char* name) {
  // TODO(benvanik): lookup by name.
  XEASSERTALWAYS();
  return NULL;
}

void ExportResolver::SetVariableMapping(const char* library_name,
                                        const uint32_t ordinal,
                                        uint32_t value) {
  KernelExport* kernel_export = GetExportByOrdinal(library_name, ordinal);
  XEASSERTNOTNULL(kernel_export);
  kernel_export->is_implemented = true;
  kernel_export->variable_ptr = value;
}

void ExportResolver::SetFunctionMapping(
    const char* library_name, const uint32_t ordinal,
    void* shim_data, xe_kernel_export_shim_fn shim,
    xe_kernel_export_impl_fn impl) {
  KernelExport* kernel_export = GetExportByOrdinal(library_name, ordinal);
  XEASSERTNOTNULL(kernel_export);
  kernel_export->is_implemented = true;
  kernel_export->function_data.shim_data = shim_data;
  kernel_export->function_data.shim = shim;
  kernel_export->function_data.impl = impl;
}

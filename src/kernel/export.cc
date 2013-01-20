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


bool KernelExport::IsImplemented() {
  switch (type) {
  case Function:
    if (function_data.impl) {
      return true;
    }
    break;
  case Variable:
    if (variable_data) {
      return true;
    }
    break;
  }
  return false;
}


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
}

KernelExport* ExportResolver::GetExportByOrdinal(const char* library_name,
                                                 const uint32_t ordinal) {
  for (std::vector<ExportTable>::iterator it = tables_.begin();
       it != tables_.end(); ++it) {
    if (!xestrcmp(library_name, it->name)) {
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

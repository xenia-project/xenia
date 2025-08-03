/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_module.h"

#include <vector>

#include "xenia/base/math.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_private.h"

namespace xe {
namespace kernel {
namespace xam {

std::atomic<int> xam_dialogs_shown_ = {0};

bool xeXamIsUIActive() { return xam_dialogs_shown_ > 0; }

XamModule::XamModule(Emulator* emulator, KernelState* kernel_state)
    : KernelModule(kernel_state, "xe:\\xam.xex"), loader_data_() {
  RegisterExportTable(export_resolver_);

  // Register all exported functions.
#define XE_MODULE_EXPORT_GROUP(m, n) \
  Register##n##Exports(export_resolver_, kernel_state_);
#include "xam_module_export_groups.inc"
#undef XE_MODULE_EXPORT_GROUP
}

std::vector<xe::cpu::Export*> xam_exports(4096);

xe::cpu::Export* RegisterExport_xam(xe::cpu::Export* export_entry) {
  // Debug output to help diagnose the crash
//  printf("DEBUG: RegisterExport_xam called with ordinal %u (0x%X), xam_exports.size() = %zu\n", 
//         export_entry->ordinal, export_entry->ordinal, xam_exports.size());
  
  if (export_entry->ordinal >= xam_exports.size()) {
    printf("ERROR: Ordinal %u (0x%X) is >= xam_exports.size() (%zu)\n", 
           export_entry->ordinal, export_entry->ordinal, xam_exports.size());
    printf("ERROR: Export name: %s\n", export_entry->name ? export_entry->name : "unknown");
    // Resize the vector to accommodate the ordinal
    xam_exports.resize(export_entry->ordinal + 1);
    printf("DEBUG: Resized xam_exports to %zu\n", xam_exports.size());
  }
  
  xam_exports[export_entry->ordinal] = export_entry;
  return export_entry;
}

void XamModule::RegisterExportTable(xe::cpu::ExportResolver* export_resolver) {
  assert_not_null(export_resolver);

// Build the export table used for resolution.
#include "xenia/kernel/util/export_table_pre.inc"
  static xe::cpu::Export xam_export_table[] = {
#include "xenia/kernel/xam/xam_table.inc"
  };
#include "xenia/kernel/util/export_table_post.inc"
  for (size_t i = 0; i < xe::countof(xam_export_table); ++i) {
    auto& export_entry = xam_export_table[i];
    assert_true(export_entry.ordinal < xam_exports.size());
    if (!xam_exports[export_entry.ordinal]) {
      xam_exports[export_entry.ordinal] = &export_entry;
    }
  }
  export_resolver->RegisterTable("xam.xex", &xam_exports);
}

XamModule::~XamModule() {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

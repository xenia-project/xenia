/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xbdm/xbdm_module.h"

#include <vector>

#include "xenia/base/math.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xbdm/xbdm_private.h"

namespace xe {
namespace kernel {
namespace xbdm {

XbdmModule::XbdmModule(Emulator* emulator, KernelState* kernel_state)
    : KernelModule(kernel_state, "xe:\\xbdm.xex") {
  RegisterExportTable(export_resolver_);

  // Register all exported functions.
#define XE_MODULE_EXPORT_GROUP(m, n) \
  Register##n##Exports(export_resolver_, kernel_state_);
#include "xbdm_module_export_groups.inc"
#undef XE_MODULE_EXPORT_GROUP
}

// Function to ensure xbdm_exports is properly initialized
static std::vector<xe::cpu::Export*>& GetXbdmExports() {
  static std::vector<xe::cpu::Export*> xbdm_exports(4096);
  return xbdm_exports;
}

xe::cpu::Export* RegisterExport_xbdm(xe::cpu::Export* export_entry) {
  auto& xbdm_exports = GetXbdmExports();
  assert_true(export_entry->ordinal < xbdm_exports.size());
  xbdm_exports[export_entry->ordinal] = export_entry;
  return export_entry;
}

void XbdmModule::RegisterExportTable(xe::cpu::ExportResolver* export_resolver) {
  assert_not_null(export_resolver);

// Build the export table used for resolution.
#include "xenia/kernel/util/export_table_pre.inc"
  static xe::cpu::Export xbdm_export_table[] = {
#include "xenia/kernel/xbdm/xbdm_table.inc"
  };
#include "xenia/kernel/util/export_table_post.inc"
  auto& xbdm_exports = GetXbdmExports();
  for (size_t i = 0; i < xe::countof(xbdm_export_table); ++i) {
    auto& export_entry = xbdm_export_table[i];
    assert_true(export_entry.ordinal < xbdm_exports.size());
    if (!xbdm_exports[export_entry.ordinal]) {
      xbdm_exports[export_entry.ordinal] = &export_entry;
    }
  }
  export_resolver->RegisterTable("xbdm.xex", &xbdm_exports);
}

XbdmModule::~XbdmModule() {}

}  // namespace xbdm
}  // namespace kernel
}  // namespace xe

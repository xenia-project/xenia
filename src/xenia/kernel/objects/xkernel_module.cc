/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/objects/xkernel_module.h"

#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/objects/xthread.h"

namespace xe {
namespace kernel {

XKernelModule::XKernelModule(KernelState* kernel_state, const char* path)
    : XModule(kernel_state, ModuleType::kKernelModule, path) {
  emulator_ = kernel_state->emulator();
  memory_ = emulator_->memory();
  export_resolver_ = kernel_state->emulator()->export_resolver();

  OnLoad();
}

XKernelModule::~XKernelModule() {}

uint32_t XKernelModule::GetProcAddressByOrdinal(uint16_t ordinal) {
  auto export_entry = export_resolver_->GetExportByOrdinal(name(), ordinal);
  if (!export_entry) {
    // Export (or its parent library) not found.
    return 0;
  }
  if (export_entry->type == cpu::Export::Type::kVariable) {
    if (export_entry->variable_ptr) {
      return export_entry->variable_ptr;
    } else {
      XELOGW(
          "ERROR: var export referenced GetProcAddressByOrdinal(%.4X(%s)) is "
          "not implemented",
          ordinal, export_entry->name);
      return 0;
    }
  } else {
    if (export_entry->function_data.shim) {
      // Implemented. Dynamically generate trampoline.
      XELOGE("GetProcAddressByOrdinal not implemented");
      return 0;
    } else {
      // Not implemented.
      XELOGW(
          "ERROR: fn export referenced GetProcAddressByOrdinal(%.4X(%s)) is "
          "not implemented",
          ordinal, export_entry->name);
      return 0;
    }
  }
}

uint32_t XKernelModule::GetProcAddressByName(const char* name) {
  XELOGE("GetProcAddressByName not implemented");
  return 0;
}

}  // namespace kernel
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xam/xam_module.h"

#include "kernel/modules/xam/xam_info.h"

#include "kernel/modules/xam/xam_table.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


XamModule::XamModule(Runtime* runtime) :
    KernelModule(runtime) {
  export_resolver_->RegisterTable(
      "xam.xex", xam_export_table, XECOUNT(xam_export_table));

  // Setup the xam state instance.
  xam_state = auto_ptr<XamState>(new XamState(pal_, memory_, export_resolver_));

  // Register all exported functions.
  RegisterInfoExports(export_resolver_.get(), xam_state.get());
}

XamModule::~XamModule() {
}

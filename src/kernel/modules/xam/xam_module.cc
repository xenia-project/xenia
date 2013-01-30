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


XamModule::XamModule(xe_pal_ref pal, xe_memory_ref memory,
                     shared_ptr<ExportResolver> resolver) :
    KernelModule(pal, memory, resolver) {
  resolver->RegisterTable(
      "xam.xex", xam_export_table, XECOUNT(xam_export_table));

  // Setup the xam state instance.
  xam_state = auto_ptr<XamState>(new XamState(pal, memory, resolver));

  // Register all exported functions.
  RegisterInfoExports(resolver.get(), xam_state.get());
}

XamModule::~XamModule() {
}

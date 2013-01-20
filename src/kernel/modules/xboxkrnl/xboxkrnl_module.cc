/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/xboxkrnl_module.h"

#include "kernel/modules/xboxkrnl/xboxkrnl_table.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


XboxkrnlModule::XboxkrnlModule(xe_pal_ref pal, xe_memory_ref memory,
                               shared_ptr<ExportResolver> resolver) :
    KernelModule(pal, memory, resolver) {
  resolver->RegisterTable(
      "xboxkrnl.exe", xboxkrnl_export_table, XECOUNT(xboxkrnl_export_table));
}

XboxkrnlModule::~XboxkrnlModule() {
}

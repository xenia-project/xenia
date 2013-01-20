/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xbdm/xbdm_module.h"

#include "kernel/modules/xbdm/xbdm_table.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xbdm;


XbdmModule::XbdmModule(xe_pal_ref pal, xe_memory_ref memory,
                       shared_ptr<ExportResolver> resolver) :
    KernelModule(pal, memory, resolver) {
  resolver->RegisterTable(
      "xbdm.exe", xbdm_export_table, XECOUNT(xbdm_export_table));
}

XbdmModule::~XbdmModule() {
}

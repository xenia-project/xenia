/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam_module.h>

#include <xenia/export_resolver.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xam_private.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


XamModule::XamModule(Emulator* emulator, KernelState* kernel_state) :
    XKernelModule(kernel_state, "xe:\\xam.xex") {
  // Build the export table used for resolution.
  #include <xenia/kernel/util/export_table_pre.inc>
  static KernelExport xam_export_table[] = {
    #include <xenia/kernel/xam_table.inc>
  };
  #include <xenia/kernel/util/export_table_post.inc>
  export_resolver_->RegisterTable(
      "xam.xex", xam_export_table, XECOUNT(xam_export_table));

  // Register all exported functions.
  RegisterContentExports(export_resolver_, kernel_state);
  RegisterInfoExports(export_resolver_, kernel_state);
  RegisterInputExports(export_resolver_, kernel_state);
  RegisterMsgExports(export_resolver_, kernel_state);
  RegisterNetExports(export_resolver_, kernel_state);
  RegisterNotifyExports(export_resolver_, kernel_state);
  RegisterUIExports(export_resolver_, kernel_state);
  RegisterUserExports(export_resolver_, kernel_state);
  RegisterVideoExports(export_resolver_, kernel_state);
  RegisterVoiceExports(export_resolver_, kernel_state);
}

XamModule::~XamModule() {
}

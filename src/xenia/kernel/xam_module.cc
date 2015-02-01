/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam_module.h"

#include "poly/math.h"
#include "xenia/export_resolver.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam_private.h"

namespace xe {
namespace kernel {

XamModule::XamModule(Emulator* emulator, KernelState* kernel_state)
    : XKernelModule(kernel_state, "xe:\\xam.xex") {
// Build the export table used for resolution.
#include "xenia/kernel/util/export_table_pre.inc"
  static KernelExport xam_export_table[] = {
#include "xenia/kernel/xam_table.inc"
  };
#include "xenia/kernel/util/export_table_post.inc"
  export_resolver_->RegisterTable("xam.xex", xam_export_table,
                                  poly::countof(xam_export_table));

  // Register all exported functions.
  xam::RegisterContentExports(export_resolver_, kernel_state);
  xam::RegisterInfoExports(export_resolver_, kernel_state);
  xam::RegisterInputExports(export_resolver_, kernel_state);
  xam::RegisterMsgExports(export_resolver_, kernel_state);
  xam::RegisterNetExports(export_resolver_, kernel_state);
  xam::RegisterNotifyExports(export_resolver_, kernel_state);
  xam::RegisterUIExports(export_resolver_, kernel_state);
  xam::RegisterUserExports(export_resolver_, kernel_state);
  xam::RegisterVideoExports(export_resolver_, kernel_state);
  xam::RegisterVoiceExports(export_resolver_, kernel_state);
}

XamModule::~XamModule() {}

}  // namespace kernel
}  // namespace xe

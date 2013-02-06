/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xam/xam_state.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace {

}


XamState::XamState(xe_pal_ref pal, xe_memory_ref memory,
                   shared_ptr<ExportResolver> export_resolver) {
  this->pal = xe_pal_retain(pal);
  this->memory = xe_memory_retain(memory);
  export_resolver_ = export_resolver;
}

XamState::~XamState() {
  xe_memory_release(memory);
  xe_pal_release(pal);
}

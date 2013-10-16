/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/kernel_module.h>

#include <xenia/kernel/export.h>
#include <xenia/kernel/runtime.h>


using namespace xe;
using namespace xe::kernel;


KernelModule::KernelModule(Runtime* runtime) {
  runtime_  = runtime;
  memory_   = runtime->memory();
  export_resolver_ = runtime->export_resolver();
}

KernelModule::~KernelModule() {
  export_resolver_.reset();
  xe_memory_release(memory_);
}

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/kernel_module.h>

#include <xenia/emulator.h>
#include <xenia/export_resolver.h>


using namespace xe;
using namespace xe::kernel;


KernelModule::KernelModule(Emulator* emulator) :
    emulator_(emulator), memory_(emulator->memory()) {
  export_resolver_ = emulator->export_resolver();
}

KernelModule::~KernelModule() {
}

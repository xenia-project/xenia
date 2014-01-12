/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/objects/xkernel_module.h>

#include <xenia/emulator.h>
#include <xenia/cpu/cpu.h>
#include <xenia/kernel/objects/xthread.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::kernel;


XKernelModule::XKernelModule(KernelState* kernel_state, const char* path) :
    XModule(kernel_state, path) {
  emulator_ = kernel_state->emulator();
  memory_ = emulator_->memory();
  export_resolver_ = kernel_state->emulator()->export_resolver();
}

XKernelModule::~XKernelModule() {
}

void* XKernelModule::GetProcAddressByOrdinal(uint16_t ordinal) {
  // TODO(benvanik): check export tables.
  XELOGE("GetProcAddressByOrdinal not implemented");
  return NULL;
}

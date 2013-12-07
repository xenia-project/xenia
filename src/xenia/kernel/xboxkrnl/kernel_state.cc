/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/kernel_state.h>

#include <xenia/emulator.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/xboxkrnl/xobject.h>
#include <xenia/kernel/xboxkrnl/objects/xmodule.h>
#include <xenia/kernel/xboxkrnl/objects/xthread.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


KernelState::KernelState(Emulator* emulator) :
    emulator_(emulator), memory_(emulator->memory()),
    executable_module_(NULL) {
  processor_    = emulator->processor();
  file_system_  = emulator->file_system();

  object_table_ = new ObjectTable();
  object_mutex_ = xe_mutex_alloc(10000);
}

KernelState::~KernelState() {
  SetExecutableModule(NULL);

  // Delete all objects.
  xe_mutex_free(object_mutex_);
  delete object_table_;
}

KernelState* KernelState::shared() {
  return shared_kernel_state_;
}

XModule* KernelState::GetModule(const char* name) {
  // TODO(benvanik): implement lookup. Most games seem to look for xam.xex/etc.
  XEASSERTALWAYS();
  return NULL;
}

XModule* KernelState::GetExecutableModule() {
  if (!executable_module_) {
    return NULL;
  }

  executable_module_->Retain();
  return executable_module_;
}

void KernelState::SetExecutableModule(XModule* module) {
  if (module == executable_module_) {
    return;
  }

  if (executable_module_) {
    executable_module_->Release();
  }
  executable_module_ = module;
  if (executable_module_) {
    executable_module_->Retain();
  }
}

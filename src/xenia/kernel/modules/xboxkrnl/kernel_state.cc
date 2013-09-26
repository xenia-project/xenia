/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/kernel_state.h>

#include <xenia/kernel/modules/xboxkrnl/xobject.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xmodule.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xthread.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {

}


KernelState::KernelState(Runtime* runtime) :
    runtime_(runtime),
    executable_module_(NULL) {
  memory_     = runtime->memory();
  processor_  = runtime->processor();
  filesystem_ = runtime->filesystem();

  object_table_ = new ObjectTable();
  object_mutex_ = xe_mutex_alloc(10000);
}

KernelState::~KernelState() {
  SetExecutableModule(NULL);

  // Delete all objects.
  xe_mutex_free(object_mutex_);
  delete object_table_;

  filesystem_.reset();
  processor_.reset();
  xe_memory_release(memory_);
}

Runtime* KernelState::runtime() {
  return runtime_;
}

xe_memory_ref KernelState::memory() {
  return memory_;
}

cpu::Processor* KernelState::processor() {
  return processor_.get();
}

fs::FileSystem* KernelState::filesystem() {
  return filesystem_.get();
}

ObjectTable* KernelState::object_table() const {
  return object_table_;
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

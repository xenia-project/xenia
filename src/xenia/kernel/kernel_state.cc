/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/kernel_state.h>

#include <xenia/emulator.h>
#include <xenia/kernel/dispatcher.h>
#include <xenia/kernel/xam_module.h>
#include <xenia/kernel/xboxkrnl_module.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/xobject.h>
#include <xenia/kernel/objects/xmodule.h>
#include <xenia/kernel/objects/xthread.h>
#include <xenia/kernel/objects/xuser_module.h>


using namespace xe;
using namespace xe::kernel;


// This is a global object initialized with the XboxkrnlModule.
// It references the current kernel state object that all kernel methods should
// be using to stash their variables.
KernelState* xe::kernel::shared_kernel_state_ = NULL;


KernelState::KernelState(Emulator* emulator) :
    emulator_(emulator), memory_(emulator->memory()),
    executable_module_(NULL) {
  processor_    = emulator->processor();
  file_system_  = emulator->file_system();

  dispatcher_   = new Dispatcher(this);

  object_table_ = new ObjectTable();
  object_mutex_ = xe_mutex_alloc(10000);

  XEASSERTNULL(shared_kernel_state_);
  shared_kernel_state_ = this;
}

KernelState::~KernelState() {
  SetExecutableModule(NULL);

  // Delete all objects.
  xe_mutex_free(object_mutex_);
  delete object_table_;

  delete dispatcher_;

  XEASSERT(shared_kernel_state_ == this);
  shared_kernel_state_ = NULL;
}

KernelState* KernelState::shared() {
  return shared_kernel_state_;
}

XModule* KernelState::GetModule(const char* name) {
  if (!name) {
    // NULL name = self.
    // TODO(benvanik): lookup module from caller address.
    return GetExecutableModule();
  } else if (xestrcasecmpa(name, "xam.xex") == 0) {
    auto module = emulator_->xam();
    module->Retain();
    return module;
  } else if (xestrcasecmpa(name, "xboxkrnl.exe") == 0) {
    auto module = emulator_->xboxkrnl();
    module->Retain();
    return module;
  } else if (xestrcasecmpa(name, "kernel32.dll") == 0) {
    // Some games request this, for some reason. wtf.
    return NULL;
  } else {
    // TODO(benvanik): support user modules/loading/etc.
    XEASSERTALWAYS();
    return NULL;
  }
}

XUserModule* KernelState::GetExecutableModule() {
  if (!executable_module_) {
    return NULL;
  }

  executable_module_->Retain();
  return executable_module_;
}

void KernelState::SetExecutableModule(XUserModule* module) {
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

void KernelState::RegisterThread(XThread* thread) {
  xe_mutex_lock(object_mutex_);
  threads_by_id_[thread->thread_id()] = thread;
  xe_mutex_unlock(object_mutex_);
}

void KernelState::UnregisterThread(XThread* thread) {
  xe_mutex_lock(object_mutex_);
  auto it = threads_by_id_.find(thread->thread_id());
  if (it != threads_by_id_.end()) {
    threads_by_id_.erase(it);
  }
  xe_mutex_unlock(object_mutex_);
}

XThread* KernelState::GetThreadByID(uint32_t thread_id) {
  XThread* thread = NULL;
  xe_mutex_lock(object_mutex_);
  auto it = threads_by_id_.find(thread_id);
  if (it != threads_by_id_.end()) {
    thread = it->second;
    // Caller must release.
    thread->Retain();
  }
  xe_mutex_unlock(object_mutex_);
  return thread;
}

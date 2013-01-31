/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/xobject.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


XObject::XObject(KernelState* kernel_state, Type type) :
    kernel_state_(kernel_state),
    ref_count_(1),
    type_(type), handle_(X_INVALID_HANDLE_VALUE) {
  handle_ = kernel_state->InsertObject(this);
}

XObject::~XObject() {
  XEASSERTZERO(ref_count_);

  if (handle_ != X_INVALID_HANDLE_VALUE) {
    // Remove from state table.
    kernel_state_->RemoveObject(this);
  }
}

Runtime* XObject::runtime() {
  return kernel_state_->runtime_;
}

KernelState* XObject::kernel_state() {
  return kernel_state_;
}

xe_memory_ref XObject::memory() {
  return kernel_state_->memory();
}

XObject::Type XObject::type() {
  return type_;
}

X_HANDLE XObject::handle() {
  return handle_;
}

void XObject::Retain() {
  xe_atomic_inc_32(&ref_count_);
}

void XObject::Release() {
  if (!xe_atomic_dec_32(&ref_count_)) {
    delete this;
  }
}

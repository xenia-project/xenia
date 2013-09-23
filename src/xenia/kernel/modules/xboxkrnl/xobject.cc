/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/xobject.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


XObject::XObject(KernelState* kernel_state, Type type) :
    kernel_state_(kernel_state),
    handle_ref_count_(0),
    pointer_ref_count_(0),
    type_(type), handle_(X_INVALID_HANDLE_VALUE) {
  kernel_state->object_table()->AddHandle(this, &handle_);
}

XObject::~XObject() {
  XEASSERTZERO(handle_ref_count_);
  XEASSERTZERO(pointer_ref_count_);
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

X_HANDLE XObject::handle() const {
  return handle_;
}

void XObject::RetainHandle() {
  xe_atomic_inc_32(&handle_ref_count_);
}

bool XObject::ReleaseHandle() {
  if (!xe_atomic_dec_32(&handle_ref_count_)) {
    return true;
  }
  return false;
}

void XObject::Retain() {
  xe_atomic_inc_32(&pointer_ref_count_);
}

void XObject::Release() {
  if (!xe_atomic_dec_32(&pointer_ref_count_)) {
    XEASSERT(pointer_ref_count_ >= handle_ref_count_);
    delete this;
  }
}

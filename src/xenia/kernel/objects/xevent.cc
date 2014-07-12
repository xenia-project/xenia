/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/objects/xevent.h>


using namespace xe;
using namespace xe::kernel;


XEvent::XEvent(KernelState* kernel_state) :
    XObject(kernel_state, kTypeEvent),
    handle_(NULL) {
}

XEvent::~XEvent() {
  if (handle_) {
    CloseHandle(handle_);
  }
}

void XEvent::Initialize(bool manual_reset, bool initial_state) {
  assert_null(handle_);

  handle_ = CreateEvent(NULL, manual_reset, initial_state, NULL);
}

void XEvent::InitializeNative(void* native_ptr, DISPATCH_HEADER& header) {
  assert_null(handle_);

  bool manual_reset;
  switch (header.type_flags >> 24) {
  case 0x00: // EventNotificationObject (manual reset)
    manual_reset = true;
    break;
  case 0x01: // EventSynchronizationObject (auto reset)
    manual_reset = false;
    break;
  default:
    assert_always();
    return;
  }

  bool initial_state = header.signal_state ? true : false;

  handle_ = CreateEvent(NULL, manual_reset, initial_state, NULL);
}

int32_t XEvent::Set(uint32_t priority_increment, bool wait) {
  return SetEvent(handle_) ? 1 : 0;
}

int32_t XEvent::Pulse(uint32_t priority_increment, bool wait) {
  return PulseEvent(handle_) ? 1 : 0;
}

int32_t XEvent::Reset() {
  return ResetEvent(handle_) ? 1 : 0;
}

void XEvent::Clear() {
  ResetEvent(handle_);
}

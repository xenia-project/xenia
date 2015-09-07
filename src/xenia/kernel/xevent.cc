/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/xevent.h"

namespace xe {
namespace kernel {

XEvent::XEvent(KernelState* kernel_state) : XObject(kernel_state, kTypeEvent) {}

XEvent::~XEvent() = default;

void XEvent::Initialize(bool manual_reset, bool initial_state) {
  assert_false(event_);

  this->CreateNative<X_KEVENT>();

  if (manual_reset) {
    event_ = xe::threading::Event::CreateManualResetEvent(initial_state);
  } else {
    event_ = xe::threading::Event::CreateAutoResetEvent(initial_state);
  }
}

void XEvent::InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header) {
  assert_false(event_);

  bool manual_reset;
  switch (header->type) {
    case 0x00:  // EventNotificationObject (manual reset)
      manual_reset = true;
      break;
    case 0x01:  // EventSynchronizationObject (auto reset)
      manual_reset = false;
      break;
    default:
      assert_always();
      return;
  }

  bool initial_state = header->signal_state ? true : false;
  if (manual_reset) {
    event_ = xe::threading::Event::CreateManualResetEvent(initial_state);
  } else {
    event_ = xe::threading::Event::CreateAutoResetEvent(initial_state);
  }
}

int32_t XEvent::Set(uint32_t priority_increment, bool wait) {
  event_->Set();
  return 1;
}

int32_t XEvent::Pulse(uint32_t priority_increment, bool wait) {
  event_->Pulse();
  return 1;
}

int32_t XEvent::Reset() {
  event_->Reset();
  return 1;
}

void XEvent::Clear() { event_->Reset(); }

}  // namespace kernel
}  // namespace xe

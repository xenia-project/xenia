/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xsession.h"

namespace xe {
namespace kernel {

XSession::XSession(KernelState* kernel_state)
    : XObject(kernel_state, Type::Session) {}

bool XSession::Initialize() {
  // FIXME: ! UNKNOWN SIZE !
  CreateNative(16);

  return true;
}

}  // namespace kernel
}  // namespace xe
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/objects/xenumerator.h>

namespace xe {
namespace kernel {

XEnumerator::XEnumerator(KernelState* kernel_state)
    : XObject(kernel_state, kTypeEnumerator) {}

XEnumerator::~XEnumerator() {}

void XEnumerator::Initialize() {}

}  // namespace kernel
}  // namespace xe

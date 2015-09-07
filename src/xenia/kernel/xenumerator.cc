/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xenumerator.h"

namespace xe {
namespace kernel {

XEnumerator::XEnumerator(KernelState* kernel_state, size_t item_capacity,
                         size_t item_size)
    : XObject(kernel_state, kTypeEnumerator),
      item_capacity_(item_capacity),
      item_size_(item_size) {}

XEnumerator::~XEnumerator() = default;

void XEnumerator::Initialize() {}

}  // namespace kernel
}  // namespace xe

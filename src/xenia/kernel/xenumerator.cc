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

XEnumerator::XEnumerator(KernelState* kernel_state, size_t items_per_enumerate,
                         size_t item_size)
    : XObject(kernel_state, kTypeEnumerator),
      items_per_enumerate_(items_per_enumerate),
      item_size_(item_size) {}

XEnumerator::~XEnumerator() = default;

void XEnumerator::Initialize() {}

}  // namespace kernel
}  // namespace xe

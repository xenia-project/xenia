/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/stack_walker.h"

#include "xenia/base/logging.h"

namespace xe {
namespace cpu {

std::unique_ptr<StackWalker> StackWalker::Create(
    backend::CodeCache* code_cache) {
  XELOGD("Stack walker unimplemented on Mac");
  return nullptr;
}

}  // namespace cpu
}  // namespace xe

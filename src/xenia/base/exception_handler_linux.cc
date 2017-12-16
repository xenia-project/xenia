/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/exception_handler.h"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/platform_linux.h"

namespace xe {

// This can be as large as needed, but isn't often needed.
// As we will be sometimes firing many exceptions we want to avoid having to
// scan the table too much or invoke many custom handlers.
constexpr size_t kMaxHandlerCount = 8;

// All custom handlers, left-aligned and null terminated.
// Executed in order.
std::pair<ExceptionHandler::Handler, void*> handlers_[kMaxHandlerCount];

void ExceptionHandler::Install(Handler fn, void* data) {
  // TODO(dougvj) stub
}

void ExceptionHandler::Uninstall(Handler fn, void* data) {
  // TODO(dougvj) stub
}

}  // namespace xe

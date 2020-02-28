/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/debugging.h"

#include <signal.h>
#include <cstdarg>

#include "xenia/base/string_buffer.h"

namespace xe {
namespace debugging {

bool IsDebuggerAttached() { return false; }
void Break() { raise(SIGTRAP); }

namespace internal {
void DebugPrint(const char* s) {
  // TODO: proper implementation.
}
}  // namespace internal

}  // namespace debugging
}  // namespace xe

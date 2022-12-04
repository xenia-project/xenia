/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_DEBUGGING_H_
#define XENIA_BASE_DEBUGGING_H_

#include <cstdint>

#include "third_party/fmt/include/fmt/format.h"

namespace xe {
namespace debugging {

// Returns true if a debugger is attached to this process.
// The state may change at any time (attach after launch, etc), so do not
// cache this value. Determining if the debugger is attached is expensive,
// though, so avoid calling it frequently.
// chrispy: no longer expensive to check on windows, we're directly using the
// teb/peb now to find this

bool IsDebuggerAttached();

// Breaks into the debugger if it is attached.
// If no debugger is present, a signal will be raised.
void Break();

namespace internal {
void DebugPrint(const char* s);
}

// Prints a message to the attached debugger.
// This bypasses the normal logging mechanism. If no debugger is attached it's
// likely to no-op.
template <typename... Args>
void DebugPrint(fmt::string_view format, const Args&... args) {
  internal::DebugPrint(
      fmt::vformat(format, fmt::make_format_args(args...)).c_str());
}

}  // namespace debugging
}  // namespace xe

#endif  // XENIA_BASE_DEBUGGING_H_

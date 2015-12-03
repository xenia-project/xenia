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

namespace xe {
namespace debugging {

// Returns true if a debugger is attached to this process.
// The state may change at any time (attach after launch, etc), so do not
// cache this value. Determining if the debugger is attached is expensive,
// though, so avoid calling it frequently.
bool IsDebuggerAttached();

// Breaks into the debugger if it is attached.
// If no debugger is present, a signal will be raised.
void Break();

// Prints a message to the attached debugger.
// This bypasses the normal logging mechanism. If no debugger is attached it's
// likely to no-op.
void DebugPrint(const char* fmt, ...);

}  // namespace debugging
}  // namespace xe

#endif  // XENIA_BASE_DEBUGGING_H_

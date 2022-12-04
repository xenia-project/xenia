/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/debugging.h"

#include "xenia/base/platform_win.h"
#include "xenia/base/string_buffer.h"

namespace xe {
namespace debugging {

bool IsDebuggerAttached() {
  return reinterpret_cast<const bool*>(
      __readgsqword(0x60))[2];  // get BeingDebugged field of PEB
}

void Break() { __debugbreak(); }

namespace internal {
void DebugPrint(const char* s) { OutputDebugStringA(s); }
}  // namespace internal

}  // namespace debugging
}  // namespace xe

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

void DebugPrint(const char* fmt, ...) {
  StringBuffer buff;

  va_list va;
  va_start(va, fmt);
  buff.AppendVarargs(fmt, va);
  va_end(va);

  // OutputDebugStringA(buff.GetString());
}

}  // namespace debugging
}  // namespace xe

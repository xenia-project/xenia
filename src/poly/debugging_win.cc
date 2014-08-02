/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/debugging.h>

#include <Windows.h>

namespace poly {
namespace debugging {

bool IsDebuggerAttached() {
  return IsDebuggerPresent() ? true : false;
}

void Break() {
  __debugbreak();
}

}  // namespace debugging
}  // namespace poly

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_MAIN_H_
#define POLY_MAIN_H_

#include <string>
#include <vector>

#include "poly/platform.h"

namespace poly {

// Returns true if there is a user-visible console attached to receive stdout.
bool has_console_attached();

// Extern defined by user code. This must be present for the application to
// launch.
struct EntryInfo {
  std::wstring name;
  std::wstring usage;
  int (*entry_point)(std::vector<std::wstring>& args);
};
EntryInfo GetEntryInfo();

#define DEFINE_ENTRY_POINT(name, usage, entry_point)    \
  poly::EntryInfo poly::GetEntryInfo() {                \
    return poly::EntryInfo({name, usage, entry_point}); \
  }

}  // namespace poly

#endif  // POLY_MAIN_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MAIN_H_
#define XENIA_BASE_MAIN_H_

#include <string>
#include <vector>

#include "xenia/base/platform.h"

namespace xe {

// Returns true if there is a user-visible console attached to receive stdout.
bool has_console_attached();

// Extern defined by user code. This must be present for the application to
// launch.
struct EntryInfo {
  std::wstring name;
  std::wstring usage;
  int (*entry_point)(const std::vector<std::wstring>& args);
};
EntryInfo GetEntryInfo();

#define DEFINE_ENTRY_POINT(name, usage, entry_point)  \
  xe::EntryInfo xe::GetEntryInfo() {                  \
    return xe::EntryInfo({name, usage, entry_point}); \
  }

}  // namespace xe

#endif  // XENIA_BASE_MAIN_H_

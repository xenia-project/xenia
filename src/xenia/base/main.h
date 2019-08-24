/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MAIN_H_
#define XENIA_BASE_MAIN_H_

#include <string>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/platform.h"

namespace xe {

// Returns true if there is a user-visible console attached to receive stdout.
bool has_console_attached();

// Extern defined by user code. This must be present for the application to
// launch.
struct EntryInfo {
  std::wstring name;
  std::string positional_usage;
  std::vector<std::string> positional_options;
  int (*entry_point)(const std::vector<std::wstring>& args);
};
EntryInfo GetEntryInfo();

#define DEFINE_ENTRY_POINT(name, entry_point, positional_usage, ...)       \
  xe::EntryInfo xe::GetEntryInfo() {                                       \
    std::initializer_list<std::string> positional_options = {__VA_ARGS__}; \
    return xe::EntryInfo(                                                  \
        {name, positional_usage,                                           \
         std::vector<std::string>(std::move(positional_options)),          \
         entry_point});                                                    \
  }

}  // namespace xe

#endif  // XENIA_BASE_MAIN_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MAIN_H_
#define XENIA_BASE_MAIN_H_

#include <optional>
#include <string>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/platform.h"

namespace xe {

// Returns true if there is a user-visible console attached to receive stdout.
bool has_console_attached();

void AttachConsole();

// Extern defined by user code. This must be present for the application to
// launch.
struct EntryInfo {
  std::string name;
  int (*entry_point)(const std::vector<std::string>& args);
  bool transparent_options;  // no argument parsing
  std::optional<std::string> positional_usage;
  std::optional<std::vector<std::string>> positional_options;
};
EntryInfo GetEntryInfo();

#define DEFINE_ENTRY_POINT(name, entry_point, positional_usage, ...)       \
  xe::EntryInfo xe::GetEntryInfo() {                                       \
    std::initializer_list<std::string> positional_options = {__VA_ARGS__}; \
    return xe::EntryInfo{                                                  \
        name, entry_point, false, positional_usage,                        \
        std::vector<std::string>(std::move(positional_options))};          \
  }

// TODO(Joel Linn): Add some way to filter consumed arguments in
// cvar::ParseLaunchArguments()
#define DEFINE_ENTRY_POINT_TRANSPARENT(name, entry_point)                      \
  xe::EntryInfo xe::GetEntryInfo() {                                           \
    return xe::EntryInfo{name, entry_point, true, std::nullopt, std::nullopt}; \
  }

}  // namespace xe

#endif  // XENIA_BASE_MAIN_H_

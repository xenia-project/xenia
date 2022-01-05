/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MAIN_WIN_H_
#define XENIA_BASE_MAIN_WIN_H_

#include <string>
#include <vector>

namespace xe {

// Functions for calling in both windowed and console entry points.
bool ParseWin32LaunchArguments(
    bool transparent_options, const std::string_view positional_usage,
    const std::vector<std::string>& positional_options,
    std::vector<std::string>* args_out);
// InitializeWin32App uses cvars, call ParseWin32LaunchArguments before.
int InitializeWin32App(const std::string_view app_name);
void ShutdownWin32App();

}  // namespace xe

#endif  // XENIA_BASE_MAIN_WIN_H_

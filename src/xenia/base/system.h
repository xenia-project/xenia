/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_SYSTEM_H_
#define XENIA_BASE_SYSTEM_H_

#include <filesystem>
#include <string>

#include "xenia/base/string.h"

namespace xe {

void LaunchWebBrowser(const std::string& url);
void LaunchFileExplorer(const std::filesystem::path& path);

}  // namespace xe

#endif  // XENIA_BASE_SYSTEM_H_

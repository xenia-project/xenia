/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdlib>

#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/platform_linux.h"
#include "xenia/base/string.h"
#include "xenia/base/system.h"

namespace xe {

void LaunchWebBrowser(const std::string& url) {
  auto cmd = "xdg-open " + url;
  system(cmd.c_str());
}

void LaunchFileExplorer(const std::filesystem::path& path) { assert_always(); }

}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/platform_linux.h"
#include <stdlib.h>
#include <string>
#include "xenia/base/string.h"

namespace xe {

void LaunchBrowser(const wchar_t* url) {
  auto cmd = std::string("xdg-open " + xe::to_string(url));
  system(cmd.c_str());
}

}  // namespace xe

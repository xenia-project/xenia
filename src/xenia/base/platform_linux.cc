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

namespace xe {

void LaunchBrowser(const char* url) {
  auto cmd = std::string("xdg-open " + std::string(url));
  system(cmd.c_str());
}

}  // namespace xe

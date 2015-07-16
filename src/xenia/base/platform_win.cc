/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/platform_win.h"

namespace xe {

void LaunchBrowser(const char* url) {
  ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdlib>

#include "xenia/base/main.h"
#include "xenia/ui/windowed_app_context_mac.h"

extern "C" int main(int argc, char** argv) {
  // Call the normal Xenia main with macOS app context
  return xe::main(argc, argv, [](WindowedAppContext& app_context) -> int {
    auto& mac_app_context = static_cast<xe::ui::MacWindowedAppContext&>(app_context);
    mac_app_context.RunMainCocoaLoop();
    return 0;
  });
}

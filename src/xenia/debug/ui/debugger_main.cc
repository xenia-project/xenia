/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>

#include <cstring>

#include "xenia/base/main.h"
#include "xenia/debug/ui/application.h"

namespace xe {
namespace debug {
namespace ui {

int main(std::vector<std::wstring>& args) {
  auto app = Application::Create();
  app->loop()->AwaitQuit();
  return 0;
}

}  // namespace ui
}  // namespace debug
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-debug-ui", L"xenia-debug-ui ??",
                   xe::debug::ui::main);

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>
#include <vector>

#include "xenia/base/main.h"
#include "xenia/ui/gl/gl_provider.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

int window_demo_main(const std::vector<std::wstring>& args);

std::unique_ptr<GraphicsProvider> CreateDemoGraphicsProvider(Window* window) {
  return xe::ui::gl::GLProvider::Create(window);
}

}  // namespace ui
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-ui-window-gl-demo", L"xenia-ui-window-gl-demo",
                   xe::ui::window_demo_main);

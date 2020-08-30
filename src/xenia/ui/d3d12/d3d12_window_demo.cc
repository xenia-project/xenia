/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>
#include <vector>

#include "xenia/base/main.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

int window_demo_main(const std::vector<std::string>& args);

std::unique_ptr<GraphicsProvider> CreateDemoGraphicsProvider(Window* window) {
  return xe::ui::d3d12::D3D12Provider::Create(window);
}

}  // namespace ui
}  // namespace xe

DEFINE_ENTRY_POINT("xenia-ui-window-d3d12-demo", xe::ui::window_demo_main, "");

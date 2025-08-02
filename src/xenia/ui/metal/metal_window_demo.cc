/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <memory>
#include <string>

#include "xenia/ui/metal/metal_provider.h"
#include "xenia/ui/window_demo.h"
#include "xenia/ui/windowed_app.h"

namespace xe {
namespace ui {
namespace metal {

class MetalWindowDemoApp final : public WindowDemoApp {
 public:
  static std::unique_ptr<WindowedApp> Create(WindowedAppContext& app_context) {
    return std::unique_ptr<WindowedApp>(new MetalWindowDemoApp(app_context));
  }

 protected:
  std::unique_ptr<GraphicsProvider> CreateGraphicsProvider() const override;

 private:
  explicit MetalWindowDemoApp(WindowedAppContext& app_context)
      : WindowDemoApp(app_context, "xenia-ui-window-metal-demo") {}
};

std::unique_ptr<GraphicsProvider> MetalWindowDemoApp::CreateGraphicsProvider()
    const {
  return MetalProvider::Create();
}

}  // namespace metal
}  // namespace ui
}  // namespace xe

XE_DEFINE_WINDOWED_APP(xenia_ui_window_metal_demo,
                       xe::ui::metal::MetalWindowDemoApp::Create);

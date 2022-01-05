/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <memory>
#include <string>

#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/window_demo.h"
#include "xenia/ui/windowed_app.h"

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12WindowDemoApp final : public WindowDemoApp {
 public:
  static std::unique_ptr<WindowedApp> Create(WindowedAppContext& app_context) {
    return std::unique_ptr<WindowedApp>(new D3D12WindowDemoApp(app_context));
  }

 protected:
  std::unique_ptr<GraphicsProvider> CreateGraphicsProvider() const override;

 private:
  explicit D3D12WindowDemoApp(WindowedAppContext& app_context)
      : WindowDemoApp(app_context, "xenia-ui-window-d3d12-demo") {}
};

std::unique_ptr<GraphicsProvider> D3D12WindowDemoApp::CreateGraphicsProvider()
    const {
  return D3D12Provider::Create();
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

XE_DEFINE_WINDOWED_APP(xenia_ui_window_d3d12_demo,
                       xe::ui::d3d12::D3D12WindowDemoApp::Create);

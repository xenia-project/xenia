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

#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/window_demo.h"
#include "xenia/ui/windowed_app.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanWindowDemoApp final : public WindowDemoApp {
 public:
  static std::unique_ptr<WindowedApp> Create(WindowedAppContext& app_context) {
    return std::unique_ptr<WindowedApp>(new VulkanWindowDemoApp(app_context));
  }

 protected:
  std::unique_ptr<GraphicsProvider> CreateGraphicsProvider() const override;

 private:
  explicit VulkanWindowDemoApp(WindowedAppContext& app_context)
      : WindowDemoApp(app_context, "xenia-ui-window-vulkan-demo") {}
};

std::unique_ptr<GraphicsProvider> VulkanWindowDemoApp::CreateGraphicsProvider()
    const {
  return VulkanProvider::Create(false, true);
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

XE_DEFINE_WINDOWED_APP(xenia_ui_window_vulkan_demo,
                       xe::ui::vulkan::VulkanWindowDemoApp::Create);

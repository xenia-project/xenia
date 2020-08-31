/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_PROVIDER_H_
#define XENIA_UI_VULKAN_VULKAN_PROVIDER_H_

#include <memory>

#include "xenia/ui/graphics_provider.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanProvider : public GraphicsProvider {
 public:
  static std::unique_ptr<VulkanProvider> Create(Window* main_window);

  std::unique_ptr<GraphicsContext> CreateContext(
      Window* target_window) override;
  std::unique_ptr<GraphicsContext> CreateOffscreenContext() override;

 private:
  explicit VulkanProvider(Window* main_window);

  bool Initialize();
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_PROVIDER_H_

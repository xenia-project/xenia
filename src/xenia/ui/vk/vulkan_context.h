/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VK_VULKAN_CONTEXT_H_
#define XENIA_UI_VK_VULKAN_CONTEXT_H_

#include <memory>

#include "xenia/ui/graphics_context.h"
#include "xenia/ui/vk/vulkan_provider.h"

#define FINE_GRAINED_DRAW_SCOPES 1

namespace xe {
namespace ui {
namespace vk {

class VulkanImmediateDrawer;

class VulkanContext : public GraphicsContext {
 public:
  ~VulkanContext() override;

  ImmediateDrawer* immediate_drawer() override;

  bool is_current() override;
  bool MakeCurrent() override;
  void ClearCurrent() override;

  bool WasLost() override { return context_lost_; }

  void BeginSwap() override;
  void EndSwap() override;

  std::unique_ptr<RawImage> Capture() override;

 private:
  friend class VulkanProvider;

  explicit VulkanContext(VulkanProvider* provider, Window* target_window);

 private:
  bool Initialize();
  void Shutdown();

  bool initialized_fully_ = false;

  bool context_lost_ = false;

  std::unique_ptr<VulkanImmediateDrawer> immediate_drawer_ = nullptr;
};

}  // namespace vk
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VK_VULKAN_CONTEXT_H_

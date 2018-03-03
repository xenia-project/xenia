/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_CONTEXT_H_
#define XENIA_UI_VULKAN_VULKAN_CONTEXT_H_

#include <memory>

#include "xenia/ui/graphics_context.h"
#include "xenia/ui/vulkan/vulkan.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanDevice;
class VulkanImmediateDrawer;
class VulkanInstance;
class VulkanProvider;
class VulkanSwapChain;

class VulkanContext : public GraphicsContext {
 public:
  ~VulkanContext() override;

  ImmediateDrawer* immediate_drawer() override;
  VulkanSwapChain* swap_chain() const { return swap_chain_.get(); }
  VulkanInstance* instance() const;
  VulkanDevice* device() const;

  bool is_current() override;
  bool MakeCurrent() override;
  void ClearCurrent() override;

  bool WasLost() override { return context_lost_; }

  void BeginSwap() override;
  void EndSwap() override;

  std::unique_ptr<RawImage> Capture() override;

 protected:
  bool context_lost_ = false;

 private:
  friend class VulkanProvider;

  explicit VulkanContext(VulkanProvider* provider, Window* target_window);

 private:
  bool Initialize();

  std::unique_ptr<VulkanSwapChain> swap_chain_;
  std::unique_ptr<VulkanImmediateDrawer> immediate_drawer_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_CONTEXT_H_

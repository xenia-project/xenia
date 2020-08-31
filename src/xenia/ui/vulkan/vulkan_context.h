/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_CONTEXT_H_
#define XENIA_UI_VULKAN_VULKAN_CONTEXT_H_

#include <memory>

#include "xenia/ui/graphics_context.h"
#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanContext : public GraphicsContext {
 public:
  ImmediateDrawer* immediate_drawer() override;

  // Returns true if the OS took away our context because we caused a TDR or
  // some other outstanding error. When this happens, this context, as well as
  // any other shared contexts are junk.
  // This context must be made current in order for this call to work properly.
  bool WasLost() override { return false; }

  void BeginSwap() override;
  void EndSwap() override;

  std::unique_ptr<RawImage> Capture() override;

  VulkanProvider* GetVulkanProvider() const {
    return static_cast<VulkanProvider*>(provider_);
  }

 private:
  friend class VulkanProvider;
  explicit VulkanContext(VulkanProvider* provider, Window* target_window);
  bool Initialize();

 private:
  std::unique_ptr<VulkanImmediateDrawer> immediate_drawer_ = nullptr;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_CONTEXT_H_

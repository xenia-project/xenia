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
#include "xenia/ui/vk/vulkan_immediate_drawer.h"
#include "xenia/ui/vk/vulkan_provider.h"

#define FINE_GRAINED_DRAW_SCOPES 1

namespace xe {
namespace ui {
namespace vk {

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

  VulkanProvider* GetVulkanProvider() const {
    return static_cast<VulkanProvider*>(provider_);
  }

  // The count of copies of transient objects (like command buffers, dynamic
  // descriptor pools) that must be kept when rendering with this context.
  static constexpr uint32_t kQueuedFrames = 3;
  // The current absolute frame number.
  uint64_t GetCurrentFrame() { return current_frame_; }
  // The last completed frame - it's fine to destroy objects used in it.
  uint64_t GetLastCompletedFrame() { return last_completed_frame_; }
  uint32_t GetCurrentQueueFrame() { return current_queue_frame_; }
  void AwaitAllFramesCompletion();

 private:
  friend class VulkanProvider;

  explicit VulkanContext(VulkanProvider* provider, Window* target_window);

 private:
  bool Initialize();
  void Shutdown();

  bool initialized_fully_ = false;

  bool context_lost_ = false;

  uint64_t current_frame_ = 1;
  uint64_t last_completed_frame_ = 0;
  uint32_t current_queue_frame_ = 1;
  VkFence fences_[kQueuedFrames] = {};

  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  uint32_t surface_min_image_count_ = 3;
  VkSurfaceFormatKHR surface_format_ = {VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  VkPresentModeKHR surface_present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
  VkCompositeAlphaFlagBitsKHR surface_composite_alpha_ =
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

  std::unique_ptr<VulkanImmediateDrawer> immediate_drawer_ = nullptr;
};

}  // namespace vk
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VK_VULKAN_CONTEXT_H_

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

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "xenia/ui/graphics_context.h"
#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

#define FINE_GRAINED_DRAW_SCOPES 1

namespace xe {
namespace ui {
namespace vulkan {

class VulkanContext : public GraphicsContext {
 public:
  ~VulkanContext() override;

  ImmediateDrawer* immediate_drawer() override;

  bool WasLost() override;

  bool BeginSwap() override;
  void EndSwap() override;

  std::unique_ptr<RawImage> Capture() override;

  VulkanProvider& GetVulkanProvider() const {
    return static_cast<VulkanProvider&>(*provider_);
  }

  void RequestSurfaceRecreation();

  VkCommandBuffer GetSwapCommandBuffer() const {
    return swap_submissions_[swap_submission_current_ % kSwapchainMaxImageCount]
        .command_buffer;
  }
  VkCommandBuffer AcquireSwapSetupCommandBuffer();
  uint64_t swap_submission_current() const { return swap_submission_current_; }
  uint64_t swap_submission_completed() const {
    return swap_submission_completed_;
  }

  const VkSurfaceFormatKHR& swap_surface_format() const {
    return swap_surface_format_;
  }
  VkRenderPass swap_render_pass() const { return swap_render_pass_; }

 private:
  friend class VulkanProvider;
  explicit VulkanContext(VulkanProvider* provider, Window* target_window);
  bool Initialize();

 private:
  void Shutdown();

  bool AwaitSwapSubmissionsCompletion(uint64_t awaited_submission,
                                      bool ignore_result);
  void AwaitAllSwapSubmissionsCompletion() {
    // Current starts from 1, so subtracting 1 can't result in a negative value.
    AwaitSwapSubmissionsCompletion(swap_submission_current_ - 1, true);
  }

  // AwaitAllSwapSubmissionsCompletion must be called before. As this can be
  // used in swapchain creation or in shutdown,
  // swap_swapchain_or_surface_recreation_needed_ won't be set by this.
  void DestroySwapchainFramebuffers();

  bool context_lost_ = false;

  // Actual image count may be less, depending on what the surface can provide.
  static constexpr uint32_t kSwapchainMaxImageCount = 3;

  // Because of the nature of Vulkan fences (that they belong only to their
  // specific submission, not the submission and all prior submissions), ALL
  // fences since the last completed submission to the needed submission should
  // individually be checked, not just the last one. However, this submission
  // number abstraction hides the loosely ordered design of Vulkan submissions
  // (it's okay to wait first for completion of A, then of B, no matter if they
  // are actually completed in AB or in BA order).

  // May be used infrequently, so allocated on demand (to only keep 1 rather
  // than 3).
  std::pair<VkCommandPool, VkCommandBuffer>
      swap_setup_command_buffers_[kSwapchainMaxImageCount];
  uint32_t swap_setup_command_buffers_allocated_count_ = 0;
  uint32_t swap_setup_command_buffers_free_bits_ = 0;

  struct SwapSubmission {
    // One pool per frame, with resetting the pool itself rather than individual
    // command buffers (resetting command buffers themselves is not recommended
    // by Arm since it makes the pool unable to use a single big allocation), as
    // recommended by Nvidia (Direct3D 12-like way):
    // https://developer.nvidia.com/sites/default/files/akamai/gameworks/blog/munich/mschott_vulkan_multi_threading.pdf
    VkFence fence = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer;
    uint32_t setup_command_buffer_index = UINT32_MAX;
  };
  SwapSubmission swap_submissions_[kSwapchainMaxImageCount];
  uint64_t swap_submission_current_ = 1;
  uint64_t swap_submission_completed_ = 0;

  VkSemaphore swap_image_acquisition_semaphore_ = VK_NULL_HANDLE;
  VkSemaphore swap_render_completion_semaphore_ = VK_NULL_HANDLE;

  VkSurfaceKHR swap_surface_ = VK_NULL_HANDLE;
  VkSurfaceFormatKHR swap_surface_format_ = {VK_FORMAT_UNDEFINED,
                                             VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  VkPresentModeKHR swap_surface_present_mode_;
  VkRenderPass swap_render_pass_ = VK_NULL_HANDLE;
  VkSwapchainKHR swap_swapchain_ = VK_NULL_HANDLE;
  VkExtent2D swap_swapchain_extent_;
  std::vector<VkImageView> swap_swapchain_image_views_;
  std::vector<VkFramebuffer> swap_swapchain_framebuffers_;

  uint32_t swap_swapchain_image_current_ = UINT32_MAX;

  // Attempts to recreate the swapchain will only be made in BeginSwap if this
  // is true (set when something relevant is changed), so if creation fails,
  // there won't be attempts every frame again and again.
  bool swap_swapchain_or_surface_recreation_needed_ = false;

  std::unique_ptr<VulkanImmediateDrawer> immediate_drawer_ = nullptr;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_CONTEXT_H_

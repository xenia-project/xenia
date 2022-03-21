/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_RENDER_TARGET_CACHE_H_
#define XENIA_GPU_VULKAN_VULKAN_RENDER_TARGET_CACHE_H_

#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "xenia/base/hash.h"
#include "xenia/gpu/render_target_cache.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;

class VulkanRenderTargetCache final : public RenderTargetCache {
 public:
  union RenderPassKey {
    struct {
      // If emulating 2x as 4x, set this to 4x for 2x not to create unnecessary
      // render pass objects.
      xenos::MsaaSamples msaa_samples : xenos::kMsaaSamplesBits;  // 2
      // << 0 is depth, << 1...4 is color.
      uint32_t depth_and_color_used : 1 + xenos::kMaxColorRenderTargets;  // 7
      // 0 for unused attachments.
      // If VK_FORMAT_D24_UNORM_S8_UINT is not supported, this must be kD24FS8
      // even for kD24S8.
      xenos::DepthRenderTargetFormat depth_format
          : xenos::kDepthRenderTargetFormatBits;  // 8
      // Linear or sRGB included if host sRGB is used.
      xenos::ColorRenderTargetFormat color_0_view_format
          : xenos::kColorRenderTargetFormatBits;  // 12
      xenos::ColorRenderTargetFormat color_1_view_format
          : xenos::kColorRenderTargetFormatBits;  // 16
      xenos::ColorRenderTargetFormat color_2_view_format
          : xenos::kColorRenderTargetFormatBits;  // 20
      xenos::ColorRenderTargetFormat color_3_view_format
          : xenos::kColorRenderTargetFormatBits;  // 24
    };
    uint32_t key = 0;
    struct Hasher {
      size_t operator()(const RenderPassKey& key) const {
        return std::hash<uint32_t>{}(key.key);
      }
    };
    bool operator==(const RenderPassKey& other_key) const {
      return key == other_key.key;
    }
    bool operator!=(const RenderPassKey& other_key) const {
      return !(*this == other_key);
    }
  };
  static_assert_size(RenderPassKey, sizeof(uint32_t));

  struct Framebuffer {
    VkFramebuffer framebuffer;
    VkExtent2D host_extent;
    Framebuffer(VkFramebuffer framebuffer, const VkExtent2D& host_extent)
        : framebuffer(framebuffer), host_extent(host_extent) {}
  };

  VulkanRenderTargetCache(VulkanCommandProcessor& command_processor,
                          const RegisterFile& register_file);
  ~VulkanRenderTargetCache();

  bool Initialize();
  void Shutdown(bool from_destructor = false);
  void ClearCache() override;

  // TOOD(Triang3l): Fragment shader interlock.
  Path GetPath() const override { return Path::kHostRenderTargets; }

  // TODO(Triang3l): Resolution scaling.
  uint32_t GetResolutionScaleX() const override { return 1; }
  uint32_t GetResolutionScaleY() const override { return 1; }

  bool Update(bool is_rasterization_done,
              uint32_t shader_writes_color_targets) override;
  // Binding information for the last successful update.
  RenderPassKey last_update_render_pass_key() const {
    return last_update_render_pass_key_;
  }
  VkRenderPass last_update_render_pass() const {
    return last_update_render_pass_;
  }
  const Framebuffer* last_update_framebuffer() const {
    return last_update_framebuffer_;
  }

  // Returns the render pass object, or VK_NULL_HANDLE if failed to create.
  // A render pass managed by the render target cache may be ended and resumed
  // at any time (to allow for things like copying and texture loading).
  VkRenderPass GetRenderPass(RenderPassKey key);

  VkFormat GetDepthVulkanFormat(xenos::DepthRenderTargetFormat format) const;
  VkFormat GetColorVulkanFormat(xenos::ColorRenderTargetFormat format) const;
  VkFormat GetColorOwnershipTransferVulkanFormat(
      xenos::ColorRenderTargetFormat format,
      bool* is_integer_out = nullptr) const;

 protected:
  // Can only be destroyed when framebuffers referencing it are destroyed!
  class VulkanRenderTarget final : public RenderTarget {
   public:
    static constexpr VkPipelineStageFlags kColorDrawStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    static constexpr VkAccessFlags kColorDrawAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    static constexpr VkImageLayout kColorDrawLayout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    static constexpr VkPipelineStageFlags kDepthDrawStageMask =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    static constexpr VkAccessFlags kDepthDrawAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    static constexpr VkImageLayout kDepthDrawLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Takes ownership of the Vulkan objects passed to the constructor.
    VulkanRenderTarget(RenderTargetKey key,
                       const ui::vulkan::VulkanProvider& provider,
                       VkImage image, VkDeviceMemory memory,
                       VkImageView view_depth_color,
                       VkImageView view_depth_stencil, VkImageView view_stencil,
                       VkImageView view_srgb,
                       VkImageView view_color_transfer_separate)
        : RenderTarget(key),
          provider_(provider),
          image_(image),
          memory_(memory),
          view_depth_color_(view_depth_color),
          view_depth_stencil_(view_depth_stencil),
          view_stencil_(view_stencil),
          view_srgb_(view_srgb),
          view_color_transfer_separate_(view_color_transfer_separate) {}
    ~VulkanRenderTarget();

    VkImage image() const { return image_; }

    VkImageView view_depth_color() const { return view_depth_color_; }
    VkImageView view_depth_stencil() const { return view_depth_stencil_; }

    static void GetDrawUsage(bool is_depth,
                             VkPipelineStageFlags* stage_mask_out,
                             VkAccessFlags* access_mask_out,
                             VkImageLayout* layout_out) {
      if (stage_mask_out) {
        *stage_mask_out = is_depth ? kDepthDrawStageMask : kColorDrawStageMask;
      }
      if (access_mask_out) {
        *access_mask_out =
            is_depth ? kDepthDrawAccessMask : kColorDrawAccessMask;
      }
      if (layout_out) {
        *layout_out = is_depth ? kDepthDrawLayout : kColorDrawLayout;
      }
    }
    void GetDrawUsage(VkPipelineStageFlags* stage_mask_out,
                      VkAccessFlags* access_mask_out,
                      VkImageLayout* layout_out) const {
      GetDrawUsage(key().is_depth, stage_mask_out, access_mask_out, layout_out);
    }
    VkPipelineStageFlags current_stage_mask() const {
      return current_stage_mask_;
    }
    VkAccessFlags current_access_mask() const { return current_access_mask_; }
    VkImageLayout current_layout() const { return current_layout_; }
    void SetUsage(VkPipelineStageFlags stage_mask, VkAccessFlags access_mask,
                  VkImageLayout layout) {
      current_stage_mask_ = stage_mask;
      current_access_mask_ = access_mask;
      current_layout_ = layout;
    }

   private:
    const ui::vulkan::VulkanProvider& provider_;

    VkImage image_;
    VkDeviceMemory memory_;

    // TODO(Triang3l): Per-format drawing views for mutable formats with EDRAM
    // aliasing without transfers.
    VkImageView view_depth_color_;
    // Optional views.
    VkImageView view_depth_stencil_;
    VkImageView view_stencil_;
    VkImageView view_srgb_;
    VkImageView view_color_transfer_separate_;

    VkPipelineStageFlags current_stage_mask_ = 0;
    VkAccessFlags current_access_mask_ = 0;
    VkImageLayout current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
  };

  uint32_t GetMaxRenderTargetWidth() const override;
  uint32_t GetMaxRenderTargetHeight() const override;

  RenderTarget* CreateRenderTarget(RenderTargetKey key) override;

  // TODO(Triang3l): Check actual unorm24 support.
  bool IsHostDepthEncodingDifferent(
      xenos::DepthRenderTargetFormat format) const override {
    return true;
  }

 private:
  VulkanCommandProcessor& command_processor_;

  // RenderPassKey::key -> VkRenderPass.
  std::unordered_map<uint32_t, VkRenderPass> render_passes_;

  // For host render targets.

  struct FramebufferKey {
    RenderPassKey render_pass_key;

    // Same as RenderTargetKey::pitch_tiles_at_32bpp.
    uint32_t pitch_tiles_at_32bpp : 8;  // 8
    // [0, 2047].
    uint32_t depth_base_tiles : xenos::kEdramBaseTilesBits - 1;    // 19
    uint32_t color_0_base_tiles : xenos::kEdramBaseTilesBits - 1;  // 30

    uint32_t color_1_base_tiles : xenos::kEdramBaseTilesBits - 1;  // 43
    uint32_t color_2_base_tiles : xenos::kEdramBaseTilesBits - 1;  // 54

    uint32_t color_3_base_tiles : xenos::kEdramBaseTilesBits - 1;  // 75

    // Including all the padding, for a stable hash.
    FramebufferKey() { Reset(); }
    FramebufferKey(const FramebufferKey& key) {
      std::memcpy(this, &key, sizeof(*this));
    }
    FramebufferKey& operator=(const FramebufferKey& key) {
      std::memcpy(this, &key, sizeof(*this));
      return *this;
    }
    bool operator==(const FramebufferKey& key) const {
      return std::memcmp(this, &key, sizeof(*this)) == 0;
    }
    using Hasher = xe::hash::XXHasher<FramebufferKey>;
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  };

  // Returns the framebuffer object, or VK_NULL_HANDLE if failed to create.
  const Framebuffer* GetFramebuffer(
      RenderPassKey render_pass_key, uint32_t pitch_tiles_at_32bpp,
      const RenderTarget* const* depth_and_color_render_targets);

  bool gamma_render_target_as_srgb_ = false;

  std::unordered_map<FramebufferKey, Framebuffer, FramebufferKey::Hasher>
      framebuffers_;

  RenderPassKey last_update_render_pass_key_;
  VkRenderPass last_update_render_pass_ = VK_NULL_HANDLE;
  uint32_t last_update_framebuffer_pitch_tiles_at_32bpp_ = 0;
  const RenderTarget* const*
      last_update_framebuffer_attachments_[1 + xenos::kMaxColorRenderTargets] =
          {};
  const Framebuffer* last_update_framebuffer_ = VK_NULL_HANDLE;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_RENDER_TARGET_CACHE_H_

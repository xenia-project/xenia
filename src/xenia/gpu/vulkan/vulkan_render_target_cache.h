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

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <unordered_map>

#include "xenia/base/hash.h"
#include "xenia/base/xxhash.h"
#include "xenia/gpu/render_target_cache.h"
#include "xenia/gpu/vulkan/vulkan_shared_memory.h"
#include "xenia/gpu/vulkan/vulkan_texture_cache.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/single_layout_descriptor_set_pool.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_upload_buffer_pool.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;

class VulkanRenderTargetCache final : public RenderTargetCache {
 public:
  union RenderPassKey {
    struct {
      // If emulating 2x as 4x, this is still 2x for simplicity of using this
      // field to make guest-related decisions. Render pass objects are not very
      // expensive, and their dependencies can't be shared between 2x-as-4x and
      // true 4x MSAA passes (framebuffers because render target cache render
      // targets are different for 2x and 4x guest MSAA, pipelines because the
      // sample mask will have 2 samples excluded for 2x-as-4x).
      // This has effect only on the attachments, but even in cases when there
      // are no attachments, it can be used to pass the sample count between
      // subsystems, for instance, to specify the desired number of samples to
      // use when there are no attachments in pipelines.
      // Also, without attachments, using separate render passes for different
      // sample counts ensures that if the variableMultisampleRate feature is
      // not supported, no draws with different rasterization sample counts end
      // up in one render pass.
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
          : xenos::kColorRenderTargetFormatBits;    // 24
      uint32_t color_rts_use_transfer_formats : 1;  // 25
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
    bool operator<(const RenderPassKey& other_key) const {
      return key < other_key.key;
    }
  };
  static_assert_size(RenderPassKey, sizeof(uint32_t));

  struct Framebuffer {
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkExtent2D host_extent{};
    Framebuffer() = default;
    Framebuffer(VkFramebuffer framebuffer, const VkExtent2D& host_extent)
        : framebuffer(framebuffer), host_extent(host_extent) {}
  };

  VulkanRenderTargetCache(const RegisterFile& register_file,
                          const Memory& memory, TraceWriter& trace_writer,
                          uint32_t draw_resolution_scale_x,
                          uint32_t draw_resolution_scale_y,
                          VulkanCommandProcessor& command_processor);
  ~VulkanRenderTargetCache();

  // Transient descriptor set layouts must be initialized in the command
  // processor.
  bool Initialize(uint32_t shared_memory_binding_count);
  void Shutdown(bool from_destructor = false);
  void ClearCache() override;

  void CompletedSubmissionUpdated();
  void EndSubmission();

  Path GetPath() const override { return path_; }

  VkBuffer edram_buffer() const { return edram_buffer_; }

  // Performs the resolve to a shared memory area according to the current
  // register values, and also clears the render targets if needed. Must be in a
  // frame for calling.
  bool Resolve(const Memory& memory, VulkanSharedMemory& shared_memory,
               VulkanTextureCache& texture_cache, uint32_t& written_address_out,
               uint32_t& written_length_out);

  bool Update(bool is_rasterization_done,
              reg::RB_DEPTHCONTROL normalized_depth_control,
              uint32_t normalized_color_mask,
              const Shader& vertex_shader) override;
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

  // Using R16G16[B16A16]_SNORM, which are -1...1, not the needed -32...32.
  // Persistent data doesn't depend on this, so can be overriden by per-game
  // configuration.
  bool IsFixedRG16TruncatedToMinus1To1() const {
    // TODO(Triang3l): Not float16 condition.
    return GetPath() == Path::kHostRenderTargets &&
           !cvars::snorm16_render_target_full_range;
  }
  bool IsFixedRGBA16TruncatedToMinus1To1() const {
    // TODO(Triang3l): Not float16 condition.
    return GetPath() == Path::kHostRenderTargets &&
           !cvars::snorm16_render_target_full_range;
  }

  bool depth_unorm24_vulkan_format_supported() const {
    return depth_unorm24_vulkan_format_supported_;
  }
  bool depth_float24_round() const { return depth_float24_round_; }

  bool msaa_2x_attachments_supported() const {
    return msaa_2x_attachments_supported_;
  }
  bool msaa_2x_no_attachments_supported() const {
    return msaa_2x_no_attachments_supported_;
  }
  bool IsMsaa2xSupported(bool subpass_has_attachments) const {
    return subpass_has_attachments ? msaa_2x_attachments_supported_
                                   : msaa_2x_no_attachments_supported_;
  }

  // Returns the render pass object, or VK_NULL_HANDLE if failed to create.
  // A render pass managed by the render target cache may be ended and resumed
  // at any time (to allow for things like copying and texture loading).
  VkRenderPass GetHostRenderTargetsRenderPass(RenderPassKey key);
  VkRenderPass GetFragmentShaderInterlockRenderPass() const {
    assert_true(GetPath() == Path::kPixelShaderInterlock);
    return fsi_render_pass_;
  }

  VkFormat GetDepthVulkanFormat(xenos::DepthRenderTargetFormat format) const;
  VkFormat GetColorVulkanFormat(xenos::ColorRenderTargetFormat format) const;
  VkFormat GetColorOwnershipTransferVulkanFormat(
      xenos::ColorRenderTargetFormat format,
      bool* is_integer_out = nullptr) const;

 protected:
  uint32_t GetMaxRenderTargetWidth() const override;
  uint32_t GetMaxRenderTargetHeight() const override;

  RenderTarget* CreateRenderTarget(RenderTargetKey key) override;

  bool IsHostDepthEncodingDifferent(
      xenos::DepthRenderTargetFormat format) const override;

  void RequestPixelShaderInterlockBarrier() override;

 private:
  enum class EdramBufferUsage {
    // There's no need for combined fragment and compute usages.
    // With host render targets, the usual usage sequence is as follows:
    // - Optionally compute writes - host depth copy storing for EDRAM range
    //   ownership transfers.
    // - Optionally fragment reads - host depth copy storing for EDRAM range
    //   ownership transfers.
    // - Compute writes - copying from host render targets during resolving.
    // - Compute reads - writing to the shared memory during resolving.
    // With the render backend implementation based on fragment shader
    // interlocks, it's:
    // - Fragment reads and writes - depth / stencil and color operations.
    // - Compute reads - writing to the shared memory during resolving.
    // So, fragment reads and compute reads normally don't follow each other,
    // and there's no need to amortize the cost of a read > read barrier in an
    // exceptional situation by using a wider barrier in the normal scenario.

    // Host depth copy storing.
    kFragmentRead,
    // Fragment shader interlock depth / stencil and color operations.
    kFragmentReadWrite,
    // Resolve - copying to the shared memory.
    kComputeRead,
    // Resolve - copying from host render targets.
    kComputeWrite,
    // Trace recording.
    kTransferRead,
    // Trace playback.
    kTransferWrite,
  };

  enum class EdramBufferModificationStatus {
    // The values are ordered by how strong the barrier conditions are.
    // No uncommitted shader writes.
    kUnmodified,
    // Need to commit before the next fragment shader interlock usage with
    // overlap.
    kViaFragmentShaderInterlock,
    // Need to commit before any next fragment shader interlock usage.
    kViaUnordered,
  };

  enum ResolveCopyDescriptorSet : uint32_t {
    // Never changes.
    kResolveCopyDescriptorSetEdram,
    // Shared memory or a region in it.
    kResolveCopyDescriptorSetDest,

    kResolveCopyDescriptorSetCount,
  };

  struct ResolveCopyShaderCode {
    const uint32_t* unscaled;
    size_t unscaled_size_bytes;
    const uint32_t* scaled;
    size_t scaled_size_bytes;
  };

  static void GetEdramBufferUsageMasks(EdramBufferUsage usage,
                                       VkPipelineStageFlags& stage_mask_out,
                                       VkAccessFlags& access_mask_out);
  void UseEdramBuffer(EdramBufferUsage new_usage);
  void MarkEdramBufferModified(
      EdramBufferModificationStatus modification_status =
          EdramBufferModificationStatus::kViaUnordered);
  void CommitEdramBufferShaderWrites(
      EdramBufferModificationStatus commit_status =
          EdramBufferModificationStatus::kViaFragmentShaderInterlock);

  VulkanCommandProcessor& command_processor_;
  TraceWriter& trace_writer_;

  Path path_ = Path::kHostRenderTargets;

  // Accessible in fragment and compute shaders.
  VkDescriptorSetLayout descriptor_set_layout_storage_buffer_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_sampled_image_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_sampled_image_x2_ =
      VK_NULL_HANDLE;

  std::unique_ptr<ui::vulkan::SingleLayoutDescriptorSetPool>
      descriptor_set_pool_sampled_image_;
  std::unique_ptr<ui::vulkan::SingleLayoutDescriptorSetPool>
      descriptor_set_pool_sampled_image_x2_;

  VkDeviceMemory edram_buffer_memory_ = VK_NULL_HANDLE;
  VkBuffer edram_buffer_ = VK_NULL_HANDLE;
  EdramBufferUsage edram_buffer_usage_;
  EdramBufferModificationStatus edram_buffer_modification_status_ =
      EdramBufferModificationStatus::kUnmodified;
  VkDescriptorPool edram_storage_buffer_descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet edram_storage_buffer_descriptor_set_;

  VkPipelineLayout resolve_copy_pipeline_layout_ = VK_NULL_HANDLE;
  static const ResolveCopyShaderCode
      kResolveCopyShaders[size_t(draw_util::ResolveCopyShaderIndex::kCount)];
  std::array<VkPipeline, size_t(draw_util::ResolveCopyShaderIndex::kCount)>
      resolve_copy_pipelines_{};

  // On the fragment shader interlock path, the render pass key is used purely
  // for passing parameters to pipeline setup - there's always only one render
  // pass.
  RenderPassKey last_update_render_pass_key_;
  VkRenderPass last_update_render_pass_ = VK_NULL_HANDLE;
  // The pitch is not used on the fragment shader interlock path.
  uint32_t last_update_framebuffer_pitch_tiles_at_32bpp_ = 0;
  // The attachments are not used on the fragment shader interlock path.
  const RenderTarget* const*
      last_update_framebuffer_attachments_[1 + xenos::kMaxColorRenderTargets] =
          {};
  const Framebuffer* last_update_framebuffer_ = VK_NULL_HANDLE;

  // For host render targets.

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
                       VulkanRenderTargetCache& render_target_cache,
                       VkImage image, VkDeviceMemory memory,
                       VkImageView view_depth_color,
                       VkImageView view_depth_stencil, VkImageView view_stencil,
                       VkImageView view_srgb,
                       VkImageView view_color_transfer_separate,
                       size_t descriptor_set_index_transfer_source)
        : RenderTarget(key),
          render_target_cache_(render_target_cache),
          image_(image),
          memory_(memory),
          view_depth_color_(view_depth_color),
          view_depth_stencil_(view_depth_stencil),
          view_stencil_(view_stencil),
          view_srgb_(view_srgb),
          view_color_transfer_separate_(view_color_transfer_separate),
          descriptor_set_index_transfer_source_(
              descriptor_set_index_transfer_source) {}
    ~VulkanRenderTarget();

    VkImage image() const { return image_; }

    VkImageView view_depth_color() const { return view_depth_color_; }
    VkImageView view_depth_stencil() const { return view_depth_stencil_; }
    VkImageView view_color_transfer_separate() const {
      return view_color_transfer_separate_;
    }
    VkImageView view_color_transfer() const {
      return view_color_transfer_separate_ != VK_NULL_HANDLE
                 ? view_color_transfer_separate_
                 : view_depth_color_;
    }
    VkDescriptorSet GetDescriptorSetTransferSource() const {
      ui::vulkan::SingleLayoutDescriptorSetPool& descriptor_set_pool =
          key().is_depth
              ? *render_target_cache_.descriptor_set_pool_sampled_image_x2_
              : *render_target_cache_.descriptor_set_pool_sampled_image_;
      return descriptor_set_pool.Get(descriptor_set_index_transfer_source_);
    }

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

    uint32_t temporary_sort_index() const { return temporary_sort_index_; }
    void SetTemporarySortIndex(uint32_t index) {
      temporary_sort_index_ = index;
    }

   private:
    VulkanRenderTargetCache& render_target_cache_;

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

    // 2 sampled images for depth / stencil, 1 sampled image for color.
    size_t descriptor_set_index_transfer_source_;

    VkPipelineStageFlags current_stage_mask_ = 0;
    VkAccessFlags current_access_mask_ = 0;
    VkImageLayout current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    // Temporary storage for indices in operations like transfers and dumps.
    uint32_t temporary_sort_index_ = 0;
  };

  struct FramebufferKey {
    RenderPassKey render_pass_key;

    // Same as RenderTargetKey::pitch_tiles_at_32bpp.
    uint32_t pitch_tiles_at_32bpp : 8;  // 8
    // [0, 2047].
    uint32_t depth_base_tiles : xenos::kEdramBaseTilesBits;    // 19
    uint32_t color_0_base_tiles : xenos::kEdramBaseTilesBits;  // 30

    uint32_t color_1_base_tiles : xenos::kEdramBaseTilesBits;  // 43
    uint32_t color_2_base_tiles : xenos::kEdramBaseTilesBits;  // 54

    uint32_t color_3_base_tiles : xenos::kEdramBaseTilesBits;  // 75

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

  enum TransferUsedDescriptorSet : uint32_t {
    // Ordered from the least to the most frequently changed.
    kTransferUsedDescriptorSetHostDepthBuffer,
    kTransferUsedDescriptorSetHostDepthStencilTextures,
    kTransferUsedDescriptorSetDepthStencilTextures,
    // Mutually exclusive with kTransferUsedDescriptorSetDepthStencilTextures.
    kTransferUsedDescriptorSetColorTexture,

    kTransferUsedDescriptorSetCount,

    kTransferUsedDescriptorSetHostDepthBufferBit =
        uint32_t(1) << kTransferUsedDescriptorSetHostDepthBuffer,
    kTransferUsedDescriptorSetHostDepthStencilTexturesBit =
        uint32_t(1) << kTransferUsedDescriptorSetHostDepthStencilTextures,
    kTransferUsedDescriptorSetDepthStencilTexturesBit =
        uint32_t(1) << kTransferUsedDescriptorSetDepthStencilTextures,
    kTransferUsedDescriptorSetColorTextureBit =
        uint32_t(1) << kTransferUsedDescriptorSetColorTexture,
  };

  // 32-bit push constants (for simplicity of size calculation and to avoid
  // std140 packing issues).
  enum TransferUsedPushConstantDword : uint32_t {
    kTransferUsedPushConstantDwordHostDepthAddress,
    kTransferUsedPushConstantDwordAddress,
    // Changed 8 times per transfer.
    kTransferUsedPushConstantDwordStencilMask,

    kTransferUsedPushConstantDwordCount,

    kTransferUsedPushConstantDwordHostDepthAddressBit =
        uint32_t(1) << kTransferUsedPushConstantDwordHostDepthAddress,
    kTransferUsedPushConstantDwordAddressBit =
        uint32_t(1) << kTransferUsedPushConstantDwordAddress,
    kTransferUsedPushConstantDwordStencilMaskBit =
        uint32_t(1) << kTransferUsedPushConstantDwordStencilMask,
  };

  enum class TransferPipelineLayoutIndex {
    kColor,
    kDepth,
    kColorToStencilBit,
    kDepthToStencilBit,
    kColorAndHostDepthTexture,
    kColorAndHostDepthBuffer,
    kDepthAndHostDepthTexture,
    kDepthAndHostDepthBuffer,

    kCount,
  };

  struct TransferPipelineLayoutInfo {
    uint32_t used_descriptor_sets;
    uint32_t used_push_constant_dwords;
  };

  static const TransferPipelineLayoutInfo
      kTransferPipelineLayoutInfos[size_t(TransferPipelineLayoutIndex::kCount)];

  enum class TransferMode : uint32_t {
    kColorToDepth,
    kColorToColor,

    kDepthToDepth,
    kDepthToColor,

    kColorToStencilBit,
    kDepthToStencilBit,

    // Two-source modes, using the host depth if it, when converted to the guest
    // format, matches what's in the owner source (not modified, keep host
    // precision), or the guest data otherwise (significantly modified, possibly
    // cleared). Stencil for FragStencilRef is always taken from the guest
    // source.

    kColorAndHostDepthToDepth,
    // When using different source and destination depth formats.
    kDepthAndHostDepthToDepth,

    // If host depth is fetched, but it's the same image as the destination,
    // it's copied to the EDRAM buffer (but since it's just a scratch buffer,
    // with tiles laid out linearly with the same pitch as in the original
    // render target; also no swapping of 40-sample columns as opposed to the
    // host render target - this is done only for the color source) and fetched
    // from there instead of the host depth texture.
    kColorAndHostDepthCopyToDepth,
    kDepthAndHostDepthCopyToDepth,

    kCount,
  };

  enum class TransferOutput {
    kColor,
    kDepth,
    kStencilBit,
  };

  struct TransferModeInfo {
    TransferOutput output;
    TransferPipelineLayoutIndex pipeline_layout;
  };

  static const TransferModeInfo kTransferModes[size_t(TransferMode::kCount)];

  union TransferShaderKey {
    uint32_t key;
    struct {
      xenos::MsaaSamples dest_msaa_samples : xenos::kMsaaSamplesBits;
      uint32_t dest_color_rt_index : xenos::kColorRenderTargetIndexBits;
      uint32_t dest_resource_format : xenos::kRenderTargetFormatBits;
      xenos::MsaaSamples source_msaa_samples : xenos::kMsaaSamplesBits;
      // Always 1x when the host depth is a copy from a buffer rather than an
      // image, not to create the same pipeline for different MSAA sample counts
      // as it doesn't matter in this case.
      xenos::MsaaSamples host_depth_source_msaa_samples
          : xenos::kMsaaSamplesBits;
      uint32_t source_resource_format : xenos::kRenderTargetFormatBits;

      // Last bits because this affects the pipeline layout - after sorting,
      // only change it as fewer times as possible. Depth buffers have an
      // additional stencil texture.
      static_assert(size_t(TransferMode::kCount) <= (size_t(1) << 4));
      TransferMode mode : 4;
    };

    TransferShaderKey() : key(0) { static_assert_size(*this, sizeof(key)); }

    struct Hasher {
      size_t operator()(const TransferShaderKey& key) const {
        return std::hash<uint32_t>{}(key.key);
      }
    };
    bool operator==(const TransferShaderKey& other_key) const {
      return key == other_key.key;
    }
    bool operator!=(const TransferShaderKey& other_key) const {
      return !(*this == other_key);
    }
    bool operator<(const TransferShaderKey& other_key) const {
      return key < other_key.key;
    }
  };

  struct TransferPipelineKey {
    RenderPassKey render_pass_key;
    TransferShaderKey shader_key;

    TransferPipelineKey(RenderPassKey render_pass_key,
                        TransferShaderKey shader_key)
        : render_pass_key(render_pass_key), shader_key(shader_key) {}

    struct Hasher {
      size_t operator()(const TransferPipelineKey& key) const {
        XXH3_state_t hash_state;
        XXH3_64bits_reset(&hash_state);
        XXH3_64bits_update(&hash_state, &key.render_pass_key,
                           sizeof(key.render_pass_key));
        XXH3_64bits_update(&hash_state, &key.shader_key,
                           sizeof(key.shader_key));
        return static_cast<size_t>(XXH3_64bits_digest(&hash_state));
      }
    };
    bool operator==(const TransferPipelineKey& other_key) const {
      return render_pass_key == other_key.render_pass_key &&
             shader_key == other_key.shader_key;
    }
    bool operator!=(const TransferPipelineKey& other_key) const {
      return !(*this == other_key);
    }
    bool operator<(const TransferPipelineKey& other_key) const {
      if (render_pass_key != other_key.render_pass_key) {
        return render_pass_key < other_key.render_pass_key;
      }
      return shader_key < other_key.shader_key;
    }
  };

  union TransferAddressConstant {
    uint32_t constant;
    struct {
      // All in tiles.
      uint32_t dest_pitch : xenos::kEdramPitchTilesBits;
      uint32_t source_pitch : xenos::kEdramPitchTilesBits;
      // Destination base in tiles minus source base in tiles (not vice versa
      // because this is a transform of the coordinate system, not addresses
      // themselves).
      // + 1 bit because this is a signed difference between two EDRAM bases.
      // 0 for host_depth_source_is_copy (ignored in this case anyway as
      // destination == source anyway).
      int32_t source_to_dest : xenos::kEdramBaseTilesBits + 1;
    };
    TransferAddressConstant() : constant(0) {
      static_assert_size(*this, sizeof(constant));
    }
    bool operator==(const TransferAddressConstant& other_constant) const {
      return constant == other_constant.constant;
    }
    bool operator!=(const TransferAddressConstant& other_constant) const {
      return !(*this == other_constant);
    }
  };

  struct TransferInvocation {
    Transfer transfer;
    TransferShaderKey shader_key;
    TransferInvocation(const Transfer& transfer,
                       const TransferShaderKey& shader_key)
        : transfer(transfer), shader_key(shader_key) {}
    bool operator<(const TransferInvocation& other_invocation) const {
      // TODO(Triang3l): See if it may be better to sort by the source in the
      // first place, especially when reading the same data multiple times (like
      // to write the stencil bits after depth) for better read locality.
      // Sort by the shader key primarily to reduce pipeline state (context)
      // switches.
      if (shader_key != other_invocation.shader_key) {
        return shader_key < other_invocation.shader_key;
      }
      // Host depth render targets are changed rarely if they exist, won't save
      // many binding changes, ignore them for simplicity (their existence is
      // caught by the shader key change).
      assert_not_null(transfer.source);
      assert_not_null(other_invocation.transfer.source);
      uint32_t source_index =
          static_cast<const VulkanRenderTarget*>(transfer.source)
              ->temporary_sort_index();
      uint32_t other_source_index = static_cast<const VulkanRenderTarget*>(
                                        other_invocation.transfer.source)
                                        ->temporary_sort_index();
      if (source_index != other_source_index) {
        return source_index < other_source_index;
      }
      return transfer.start_tiles < other_invocation.transfer.start_tiles;
    }
    bool CanBeMergedIntoOneDraw(
        const TransferInvocation& other_invocation) const {
      return shader_key == other_invocation.shader_key &&
             transfer.AreSourcesSame(other_invocation.transfer);
    }
  };

  union DumpPipelineKey {
    uint32_t key;
    struct {
      xenos::MsaaSamples msaa_samples : 2;
      uint32_t resource_format : 4;
      // Last bit because this affects the pipeline - after sorting, only change
      // it at most once. Depth buffers have an additional stencil SRV.
      uint32_t is_depth : 1;
    };

    DumpPipelineKey() : key(0) { static_assert_size(*this, sizeof(key)); }

    struct Hasher {
      size_t operator()(const DumpPipelineKey& key) const {
        return std::hash<uint32_t>{}(key.key);
      }
    };
    bool operator==(const DumpPipelineKey& other_key) const {
      return key == other_key.key;
    }
    bool operator!=(const DumpPipelineKey& other_key) const {
      return !(*this == other_key);
    }
    bool operator<(const DumpPipelineKey& other_key) const {
      return key < other_key.key;
    }

    xenos::ColorRenderTargetFormat GetColorFormat() const {
      assert_false(is_depth);
      return xenos::ColorRenderTargetFormat(resource_format);
    }
    xenos::DepthRenderTargetFormat GetDepthFormat() const {
      assert_true(is_depth);
      return xenos::DepthRenderTargetFormat(resource_format);
    }
  };

  // There's no strict dependency on the group size in dumping, for simplicity
  // calculations especially with resolution scaling, dividing manually (as the
  // group size is not unlimited). The only restriction is that an integer
  // multiple of it must be 80x16 samples (and no larger than that) for 32bpp,
  // or 40x16 samples for 64bpp (because only a half of the pair of tiles may
  // need to be dumped). Using 8x16 since that's 128 - the minimum required
  // group size on Vulkan, and the maximum number of lanes in a subgroup on
  // Vulkan.
  static constexpr uint32_t kDumpSamplesPerGroupX = 8;
  static constexpr uint32_t kDumpSamplesPerGroupY = 16;

  union DumpPitches {
    uint32_t pitches;
    struct {
      // Both in tiles.
      uint32_t dest_pitch : xenos::kEdramPitchTilesBits;
      uint32_t source_pitch : xenos::kEdramPitchTilesBits;
    };
    DumpPitches() : pitches(0) { static_assert_size(*this, sizeof(pitches)); }
    bool operator==(const DumpPitches& other_pitches) const {
      return pitches == other_pitches.pitches;
    }
    bool operator!=(const DumpPitches& other_pitches) const {
      return !(*this == other_pitches);
    }
  };

  union DumpOffsets {
    uint32_t offsets;
    struct {
      // May be beyond the EDRAM tile count in case of EDRAM addressing
      // wrapping, thus + 1 bit.
      uint32_t dispatch_first_tile : xenos::kEdramBaseTilesBits + 1;
      uint32_t source_base_tiles : xenos::kEdramBaseTilesBits;
    };
    DumpOffsets() : offsets(0) { static_assert_size(*this, sizeof(offsets)); }
    bool operator==(const DumpOffsets& other_offsets) const {
      return offsets == other_offsets.offsets;
    }
    bool operator!=(const DumpOffsets& other_offsets) const {
      return !(*this == other_offsets);
    }
  };

  enum DumpDescriptorSet : uint32_t {
    // Never changes. Same in both color and depth pipeline layouts, keep the
    // first for pipeline layout compatibility, to only have to set it once.
    kDumpDescriptorSetEdram,
    // One resolve may need multiple sources. Different descriptor set layouts
    // for color and depth.
    kDumpDescriptorSetSource,

    kDumpDescriptorSetCount,
  };

  enum DumpPushConstant : uint32_t {
    // May be different for different sources.
    kDumpPushConstantPitches,
    // May be changed multiple times for the same source.
    kDumpPushConstantOffsets,

    kDumpPushConstantCount,
  };

  struct DumpInvocation {
    ResolveCopyDumpRectangle rectangle;
    DumpPipelineKey pipeline_key;
    DumpInvocation(const ResolveCopyDumpRectangle& rectangle,
                   const DumpPipelineKey& pipeline_key)
        : rectangle(rectangle), pipeline_key(pipeline_key) {}
    bool operator<(const DumpInvocation& other_invocation) const {
      // Sort by the pipeline key primarily to reduce pipeline state (context)
      // switches.
      if (pipeline_key != other_invocation.pipeline_key) {
        return pipeline_key < other_invocation.pipeline_key;
      }
      assert_not_null(rectangle.render_target);
      uint32_t render_target_index =
          static_cast<const VulkanRenderTarget*>(rectangle.render_target)
              ->temporary_sort_index();
      const ResolveCopyDumpRectangle& other_rectangle =
          other_invocation.rectangle;
      uint32_t other_render_target_index =
          static_cast<const VulkanRenderTarget*>(other_rectangle.render_target)
              ->temporary_sort_index();
      if (render_target_index != other_render_target_index) {
        return render_target_index < other_render_target_index;
      }
      if (rectangle.row_first != other_rectangle.row_first) {
        return rectangle.row_first < other_rectangle.row_first;
      }
      return rectangle.row_first_start < other_rectangle.row_first_start;
    }
  };

  // Returns the framebuffer object, or VK_NULL_HANDLE if failed to create.
  const Framebuffer* GetHostRenderTargetsFramebuffer(
      RenderPassKey render_pass_key, uint32_t pitch_tiles_at_32bpp,
      const RenderTarget* const* depth_and_color_render_targets);

  VkShaderModule GetTransferShader(TransferShaderKey key);
  // With sample-rate shading, returns a pointer to one pipeline. Without
  // sample-rate shading, returns a pointer to as many pipelines as there are
  // samples. If there was a failure to create a pipeline, returns nullptr.
  VkPipeline const* GetTransferPipelines(TransferPipelineKey key);

  // Do ownership transfers for render targets - each render target / vector may
  // be null / empty in case there's nothing to do for them.
  // resolve_clear_rectangle is expected to be provided by
  // PrepareHostRenderTargetsResolveClear which should do all the needed size
  // bound checks.
  void PerformTransfersAndResolveClears(
      uint32_t render_target_count, RenderTarget* const* render_targets,
      const std::vector<Transfer>* render_target_transfers,
      const uint64_t* render_target_resolve_clear_values = nullptr,
      const Transfer::Rectangle* resolve_clear_rectangle = nullptr);

  VkPipeline GetDumpPipeline(DumpPipelineKey key);

  // Writes contents of host render targets within rectangles from
  // ResolveInfo::GetCopyEdramTileSpan to edram_buffer_.
  void DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used,
                         uint32_t dump_rows, uint32_t dump_pitch);

  bool gamma_render_target_as_srgb_ = false;

  bool depth_unorm24_vulkan_format_supported_ = false;
  bool depth_float24_round_ = false;

  bool msaa_2x_attachments_supported_ = false;
  bool msaa_2x_no_attachments_supported_ = false;

  // VK_NULL_HANDLE if failed to create.
  std::unordered_map<RenderPassKey, VkRenderPass, RenderPassKey::Hasher>
      render_passes_;

  std::unordered_map<FramebufferKey, Framebuffer, FramebufferKey::Hasher>
      framebuffers_;

  // Set 0 - EDRAM storage buffer, set 1 - source depth sampled image (and
  // unused stencil from the transfer descriptor set), HostDepthStoreConstants
  // passed via push constants.
  VkPipelineLayout host_depth_store_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline host_depth_store_pipelines_[size_t(xenos::MsaaSamples::k4X) + 1] =
      {};

  std::unique_ptr<ui::vulkan::VulkanUploadBufferPool>
      transfer_vertex_buffer_pool_;
  VkShaderModule transfer_passthrough_vertex_shader_ = VK_NULL_HANDLE;
  VkPipelineLayout transfer_pipeline_layouts_[size_t(
      TransferPipelineLayoutIndex::kCount)] = {};
  // VK_NULL_HANDLE if failed to create.
  std::unordered_map<TransferShaderKey, VkShaderModule,
                     TransferShaderKey::Hasher>
      transfer_shaders_;
  // With sample-rate shading, one pipeline per entry. Without sample-rate
  // shading, one pipeline per sample per entry. VK_NULL_HANDLE if failed to
  // create.
  std::unordered_map<TransferPipelineKey, std::array<VkPipeline, 4>,
                     TransferPipelineKey::Hasher>
      transfer_pipelines_;

  VkPipelineLayout dump_pipeline_layout_color_ = VK_NULL_HANDLE;
  VkPipelineLayout dump_pipeline_layout_depth_ = VK_NULL_HANDLE;
  // Compute pipelines for copying host render target contents to the EDRAM
  // buffer. VK_NULL_HANDLE if failed to create.
  std::unordered_map<DumpPipelineKey, VkPipeline, DumpPipelineKey::Hasher>
      dump_pipelines_;

  // Temporary storage for Resolve.
  std::vector<Transfer> clear_transfers_[2];

  // Temporary storage for PerformTransfersAndResolveClears.
  std::vector<TransferInvocation> current_transfer_invocations_;

  // Temporary storage for DumpRenderTargets.
  std::vector<ResolveCopyDumpRectangle> dump_rectangles_;
  std::vector<DumpInvocation> dump_invocations_;

  // For pixel (fragment) shader interlock.

  VkRenderPass fsi_render_pass_ = VK_NULL_HANDLE;
  Framebuffer fsi_framebuffer_;

  VkPipelineLayout resolve_fsi_clear_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline resolve_fsi_clear_32bpp_pipeline_ = VK_NULL_HANDLE;
  VkPipeline resolve_fsi_clear_64bpp_pipeline_ = VK_NULL_HANDLE;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_RENDER_TARGET_CACHE_H_

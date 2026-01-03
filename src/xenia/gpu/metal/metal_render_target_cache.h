/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_RENDER_TARGET_CACHE_H_
#define XENIA_GPU_METAL_METAL_RENDER_TARGET_CACHE_H_

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/render_target_cache.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"

#include "third_party/metal-cpp/Metal/Metal.hpp"

namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor;

class MetalRenderTargetCache final : public gpu::RenderTargetCache {
 public:
  // Metal-specific render target - defined inside cache class to access
  // protected RenderTarget
  class MetalRenderTarget final : public RenderTarget {
   public:
    ~MetalRenderTarget() override;

    MTL::Texture* texture() const { return texture_; }
    MTL::Texture* msaa_texture() const { return msaa_texture_; }
    MTL::Texture* draw_texture() const {
      return draw_texture_ ? draw_texture_ : texture_;
    }
    MTL::Texture* transfer_texture() const {
      return transfer_texture_ ? transfer_texture_ : texture_;
    }
    MTL::Texture* msaa_draw_texture() const {
      return msaa_draw_texture_ ? msaa_draw_texture_ : msaa_texture_;
    }
    MTL::Texture* msaa_transfer_texture() const {
      return msaa_transfer_texture_ ? msaa_transfer_texture_ : msaa_texture_;
    }

    void SetTemporarySortIndex(uint32_t index) { temporary_sort_index_ = index; }
    uint32_t temporary_sort_index() const { return temporary_sort_index_; }

    void SetTexture(MTL::Texture* texture) { texture_ = texture; }
    void SetMsaaTexture(MTL::Texture* texture) { msaa_texture_ = texture; }
    void SetDrawTexture(MTL::Texture* texture) { draw_texture_ = texture; }
    void SetTransferTexture(MTL::Texture* texture) {
      transfer_texture_ = texture;
    }
    void SetMsaaDrawTexture(MTL::Texture* texture) {
      msaa_draw_texture_ = texture;
    }
    void SetMsaaTransferTexture(MTL::Texture* texture) {
      msaa_transfer_texture_ = texture;
    }
    bool needs_initial_clear() const { return needs_initial_clear_; }
    void SetNeedsInitialClear(bool needs_initial_clear) {
      needs_initial_clear_ = needs_initial_clear;
    }

    // Public constructor for creating render targets
    MetalRenderTarget(RenderTargetKey key) : RenderTarget(key) {}

   private:
    MTL::Texture* texture_ = nullptr;
    MTL::Texture* msaa_texture_ = nullptr;  // If MSAA is enabled
    MTL::Texture* draw_texture_ = nullptr;
    MTL::Texture* transfer_texture_ = nullptr;
    MTL::Texture* msaa_draw_texture_ = nullptr;
    MTL::Texture* msaa_transfer_texture_ = nullptr;
    uint32_t temporary_sort_index_ = UINT32_MAX;
    bool needs_initial_clear_ = true;
  };

 public:
  MetalRenderTargetCache(const RegisterFile& register_file,
                         const Memory& memory, TraceWriter* trace_writer,
                         uint32_t draw_resolution_scale_x,
                         uint32_t draw_resolution_scale_y,
                         MetalCommandProcessor& command_processor);
  ~MetalRenderTargetCache() override;

  bool Initialize();
  void Shutdown(bool from_destructor = false);

  // RenderTargetCache implementation
  Path GetPath() const override;

  // Fixed-point render targets (k_16_16 / k_16_16_16_16) are backed by *_SNORM
  // formats in the host render targets path, which are -1...1 rather than the
  // Xbox 360's -32...32 range. When this is true, resolve/copy must compensate
  // to match the guest packing expectations.
  bool IsFixedRG16TruncatedToMinus1To1() const {
    return GetPath() == Path::kHostRenderTargets &&
           !cvars::snorm16_render_target_full_range;
  }
  bool IsFixedRGBA16TruncatedToMinus1To1() const {
    return GetPath() == Path::kHostRenderTargets &&
           !cvars::snorm16_render_target_full_range;
  }

  void ClearCache() override;
  void BeginFrame() override;

  bool Update(bool is_rasterization_done,
              reg::RB_DEPTHCONTROL normalized_depth_control,
              uint32_t normalized_color_mask,
              const Shader& vertex_shader) override;

  // Metal-specific methods
  MTL::RenderPassDescriptor* GetRenderPassDescriptor(
      uint32_t expected_sample_count = 1);

  bool IsRenderPassDescriptorDirty() const { return render_pass_descriptor_dirty_; }

  // Get current render targets for capture
  MTL::Texture* GetColorTarget(uint32_t index) const;
  MTL::Texture* GetDepthTarget() const;
  MTL::Texture* GetDummyColorTarget() const;
  MetalRenderTarget* GetColorRenderTarget(uint32_t index) const;
  // Get current render targets for pipeline attachment formats.
  MTL::Texture* GetColorTargetForDraw(uint32_t index) const;
  MTL::Texture* GetDepthTargetForDraw() const;
  MTL::Texture* GetDummyColorTargetForDraw() const;

  // Get the last REAL (non-dummy) render targets for capture
  MTL::Texture* GetLastRealColorTarget(uint32_t index) const;
  MTL::Texture* GetLastRealDepthTarget() const;

  // Look up a render target texture by key for debug/trace viewer use.
  MTL::Texture* GetRenderTargetTexture(RenderTargetKey key) const;
  // Look up a color render target texture by key components for the trace
  // viewer without exposing RenderTargetKey.
  MTL::Texture* GetColorRenderTargetTexture(
      uint32_t pitch, xenos::MsaaSamples samples, uint32_t base,
      xenos::ColorRenderTargetFormat format) const;

  // Restore EDRAM contents from snapshot (for trace playback), matching
  // D3D12RenderTargetCache::RestoreEdramSnapshot.
  void RestoreEdramSnapshot(const void* snapshot);

  MTL::Buffer* GetEdramBuffer() const { return edram_buffer_; }

  // Debug/slow-path: dump current host RT contents into EDRAM.
  void FlushEdramFromHostRenderTargets();

  // Per-draw ordered blending fallback: blend a scissor region from a specific
  // color RT into the EDRAM buffer.
  void BlendRenderTargetToEdramRect(MetalRenderTarget* render_target,
                                    uint32_t rt_index,
                                    uint32_t rt_write_mask,
                                    uint32_t scissor_x,
                                    uint32_t scissor_y,
                                    uint32_t scissor_width,
                                    uint32_t scissor_height,
                                    MTL::CommandBuffer* command_buffer);
  void ReloadRenderTargetFromEdramTiles(MetalRenderTarget* render_target,
                                        uint32_t tile_x0, uint32_t tile_y0,
                                        uint32_t tile_x1, uint32_t tile_y1,
                                        MTL::CommandBuffer* command_buffer);
  void SetOrderedBlendCoverageActive(bool active);
  bool IsOrderedBlendCoverageActive() const {
    return ordered_blend_coverage_active_;
  }
  MTL::Texture* GetOrderedBlendCoverageTexture() const {
    return ordered_blend_coverage_texture_;
  }
  static constexpr uint32_t kOrderedBlendCoverageAttachmentIndex = 4;

  // Resolve (copy) render targets to shared memory
  bool Resolve(Memory& memory, uint32_t& written_address,
               uint32_t& written_length,
               MTL::CommandBuffer* command_buffer = nullptr);

 protected:
  // Virtual methods from RenderTargetCache
  uint32_t GetMaxRenderTargetWidth() const override;
  uint32_t GetMaxRenderTargetHeight() const override;

  RenderTarget* CreateRenderTarget(RenderTargetKey key) override;

  bool IsHostDepthEncodingDifferent(
      xenos::DepthRenderTargetFormat format) const override;

 private:
  static uint32_t GetMetalEdramDumpFormat(RenderTargetKey key);
  bool EnsureOrderedBlendCoverageTexture(uint32_t width, uint32_t height,
                                         uint32_t sample_count);
  MTL::Library* GetOrCreateEdramLoadLibrary(bool msaa);
  MTL::RenderPipelineState* GetOrCreateEdramLoadPipeline(
      MTL::PixelFormat dest_format, uint32_t sample_count);

  MetalCommandProcessor& command_processor_;
  TraceWriter* trace_writer_;

  // Metal device reference
  MTL::Device* device_ = nullptr;
  bool raster_order_groups_supported_ = false;
  bool gamma_render_target_as_srgb_ = false;

  // EDRAM buffer (10MB embedded DRAM)
  MTL::Buffer* edram_buffer_ = nullptr;

  // EDRAM compute shaders for tile operations
  MTL::ComputePipelineState* edram_load_pipeline_ = nullptr;   // Tiled → Linear
  MTL::ComputePipelineState* edram_store_pipeline_ = nullptr;  // Linear → Tiled
  std::unordered_map<uint64_t, MTL::RenderPipelineState*>
      edram_load_pipelines_;
  MTL::Library* edram_load_library_ = nullptr;
  MTL::Library* edram_load_library_msaa_ = nullptr;

  // EDRAM dump compute shaders for host render target → EDRAM copies.
  // Color, 32bpp.
  MTL::ComputePipelineState* edram_dump_color_32bpp_1xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_dump_color_32bpp_2xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_dump_color_32bpp_4xmsaa_pipeline_ = nullptr;
  // EDRAM blend compute shaders (host RT -> EDRAM with blend/keep mask).
  MTL::ComputePipelineState* edram_blend_32bpp_1xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_blend_32bpp_2xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_blend_32bpp_4xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_blend_64bpp_1xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_blend_64bpp_2xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_blend_64bpp_4xmsaa_pipeline_ = nullptr;
  // Color, 64bpp.
  MTL::ComputePipelineState* edram_dump_color_64bpp_1xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_dump_color_64bpp_2xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_dump_color_64bpp_4xmsaa_pipeline_ = nullptr;
  // Depth (D24x / D24FS8 encoded as 32bpp in EDRAM snapshot).
  MTL::ComputePipelineState* edram_dump_depth_32bpp_1xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_dump_depth_32bpp_2xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_dump_depth_32bpp_4xmsaa_pipeline_ = nullptr;

  // Resolve compute shaders (Metal XeSL → MSL metallib)
  MTL::ComputePipelineState* resolve_full_8bpp_pipeline_ = nullptr;
  MTL::ComputePipelineState* resolve_full_16bpp_pipeline_ = nullptr;
  MTL::ComputePipelineState* resolve_full_32bpp_pipeline_ = nullptr;
  MTL::ComputePipelineState* resolve_full_64bpp_pipeline_ = nullptr;
  MTL::ComputePipelineState* resolve_full_128bpp_pipeline_ = nullptr;
  MTL::ComputePipelineState* resolve_fast_32bpp_1x2xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* resolve_fast_32bpp_4xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* resolve_fast_64bpp_1x2xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* resolve_fast_64bpp_4xmsaa_pipeline_ = nullptr;

  // Host depth store compute shaders (1x/2x/4x MSAA).
  MTL::ComputePipelineState* host_depth_store_pipelines_[3] = {};

  // Per-draw ordered-blend coverage attachment.
  bool ordered_blend_coverage_active_ = false;
  MTL::Texture* ordered_blend_coverage_texture_ = nullptr;
  uint32_t ordered_blend_coverage_width_ = 0;
  uint32_t ordered_blend_coverage_height_ = 0;
  uint32_t ordered_blend_coverage_samples_ = 0;

  // Transfer shaders (host RT ownership transfers) - modeled after D3D12.

  // TransferMode list mirrors D3D12RenderTargetCache::TransferMode so logs and
  // structure stay in sync, even if many modes are not implemented yet.
  enum class TransferMode {
    kColorToColor,
    kColorToDepth,
    kDepthToColor,
    kDepthToDepth,
    kColorToStencilBit,
    kDepthToStencilBit,
    kColorAndHostDepthToDepth,
    kDepthAndHostDepthToDepth,
  };

  struct TransferShaderKey {
    TransferMode mode;
    xenos::MsaaSamples source_msaa_samples;
    xenos::MsaaSamples dest_msaa_samples;
    xenos::MsaaSamples host_depth_source_msaa_samples;
    uint32_t source_resource_format;
    uint32_t dest_resource_format;
    uint32_t host_depth_source_is_copy;

    bool operator==(const TransferShaderKey& other) const {
      return mode == other.mode &&
             source_msaa_samples == other.source_msaa_samples &&
             dest_msaa_samples == other.dest_msaa_samples &&
             host_depth_source_msaa_samples ==
                 other.host_depth_source_msaa_samples &&
             source_resource_format == other.source_resource_format &&
             dest_resource_format == other.dest_resource_format &&
             host_depth_source_is_copy == other.host_depth_source_is_copy;
    }
    bool operator!=(const TransferShaderKey& other) const {
      return !(*this == other);
    }
    bool operator<(const TransferShaderKey& other) const {
      if (mode != other.mode) {
        return mode < other.mode;
      }
      if (source_msaa_samples != other.source_msaa_samples) {
        return source_msaa_samples < other.source_msaa_samples;
      }
      if (dest_msaa_samples != other.dest_msaa_samples) {
        return dest_msaa_samples < other.dest_msaa_samples;
      }
      if (host_depth_source_msaa_samples !=
          other.host_depth_source_msaa_samples) {
        return host_depth_source_msaa_samples <
               other.host_depth_source_msaa_samples;
      }
      if (source_resource_format != other.source_resource_format) {
        return source_resource_format < other.source_resource_format;
      }
      if (dest_resource_format != other.dest_resource_format) {
        return dest_resource_format < other.dest_resource_format;
      }
      return host_depth_source_is_copy < other.host_depth_source_is_copy;
    }

    struct Hasher {
      size_t operator()(const TransferShaderKey& key) const {
        size_t h = size_t(key.mode);
        h ^= (size_t(key.source_msaa_samples) << 4);
        h ^= (size_t(key.dest_msaa_samples) << 8);
        h ^= (size_t(key.host_depth_source_msaa_samples) << 12);
        h ^= (size_t(key.source_resource_format) << 16);
        h ^= (size_t(key.dest_resource_format) << 24);
        h ^= (size_t(key.host_depth_source_is_copy) << 28);
        return h ^ (h >> 16);
      }
    };
  };

  struct TransferInvocation {
    Transfer transfer;
    TransferShaderKey shader_key;
    TransferInvocation(const Transfer& transfer,
                       const TransferShaderKey& shader_key)
        : transfer(transfer), shader_key(shader_key) {}
    bool operator<(const TransferInvocation& other) const {
      if (shader_key != other.shader_key) {
        return shader_key < other.shader_key;
      }
      assert_not_null(transfer.source);
      assert_not_null(other.transfer.source);
      uint32_t source_index =
          static_cast<const MetalRenderTarget*>(transfer.source)
              ->temporary_sort_index();
      uint32_t other_source_index =
          static_cast<const MetalRenderTarget*>(other.transfer.source)
              ->temporary_sort_index();
      if (source_index != other_source_index) {
        return source_index < other_source_index;
      }
      return transfer.start_tiles < other.transfer.start_tiles;
    }
    bool CanBeMergedIntoOneDraw(
        const TransferInvocation& other) const {
      return shader_key == other.shader_key &&
             transfer.AreSourcesSame(other.transfer);
    }
  };

  std::unordered_map<TransferShaderKey, MTL::RenderPipelineState*,
                     TransferShaderKey::Hasher>
      transfer_pipelines_;
  std::vector<TransferInvocation> transfer_invocations_;
  MTL::Library* transfer_library_ = nullptr;
  std::unordered_map<uint32_t, MTL::RenderPipelineState*>
      transfer_clear_pipelines_;
  MTL::DepthStencilState* transfer_depth_state_ = nullptr;
  MTL::DepthStencilState* transfer_depth_state_none_ = nullptr;
  MTL::DepthStencilState* transfer_depth_clear_state_ = nullptr;
  MTL::DepthStencilState* transfer_stencil_clear_state_ = nullptr;
  MTL::DepthStencilState* transfer_stencil_bit_states_[8] = {};
  MTL::Buffer* transfer_dummy_buffer_ = nullptr;
  MTL::Texture* transfer_dummy_color_float_[3] = {};
  MTL::Texture* transfer_dummy_color_uint_[3] = {};
  MTL::Texture* transfer_dummy_depth_[3] = {};
  MTL::Texture* transfer_dummy_stencil_[3] = {};
  bool msaa_2x_supported_ = true;

  // Current render targets - updated by base class Update() call

  MetalRenderTarget* current_color_targets_[4] = {};
  MetalRenderTarget* current_depth_target_ = nullptr;

  // Track the last REAL (non-dummy) render targets for capture
  MetalRenderTarget* last_real_color_targets_[4] = {};
  MetalRenderTarget* last_real_depth_target_ = nullptr;

  // Track all created render targets so we can find them
  std::unordered_map<uint32_t, MetalRenderTarget*> render_target_map_;

  // Render pass descriptor cache
  MTL::RenderPassDescriptor* cached_render_pass_descriptor_ = nullptr;
  bool render_pass_descriptor_dirty_ = true;

  // Dummy render target for when no render targets are bound
  mutable std::unique_ptr<MetalRenderTarget> dummy_color_target_;
  mutable bool dummy_color_target_needs_clear_ = true;

  // Track which render targets have been cleared this frame
  std::unordered_set<uint32_t> cleared_render_targets_this_frame_;

  // Debug helper to log a small region of the current color RT0.
  void LogCurrentColorRT0Pixels(const char* tag);

  // Helper methods
  MTL::Texture* CreateColorTexture(uint32_t width, uint32_t height,
                                   xenos::ColorRenderTargetFormat format,
                                   uint32_t samples);
  MTL::Texture* CreateDepthTexture(uint32_t width, uint32_t height,
                                   xenos::DepthRenderTargetFormat format,
                                   uint32_t samples);

  MTL::PixelFormat GetColorResourcePixelFormat(
      xenos::ColorRenderTargetFormat format) const;
  MTL::PixelFormat GetColorDrawPixelFormat(
      xenos::ColorRenderTargetFormat format) const;
  MTL::PixelFormat GetColorOwnershipTransferPixelFormat(
      xenos::ColorRenderTargetFormat format, bool* is_integer_out) const;
  MTL::PixelFormat GetDepthPixelFormat(
      xenos::DepthRenderTargetFormat format) const;

  // EDRAM compute shader setup
  bool InitializeEdramComputeShaders();
  void ShutdownEdramComputeShaders();

  // Transfer pipeline setup (host RT ownership transfers) - Metal analogue of
  // D3D12RenderTargetCache::GetOrCreateTransferPipelines.
  MTL::RenderPipelineState* GetOrCreateTransferPipelines(
      const TransferShaderKey& key, MTL::PixelFormat dest_format,
      bool dest_is_uint);
  MTL::RenderPipelineState* GetOrCreateTransferClearPipeline(
      MTL::PixelFormat dest_format, bool dest_is_uint, bool is_depth,
      uint32_t sample_count);
  MTL::Library* GetOrCreateTransferLibrary();
  MTL::Texture* GetTransferDummyTexture(MTL::PixelFormat format,
                                        uint32_t sample_count);
  MTL::Texture* GetTransferDummyColorFloatTexture(uint32_t sample_count);
  MTL::Texture* GetTransferDummyColorUintTexture(uint32_t sample_count);
  MTL::Texture* GetTransferDummyDepthTexture(uint32_t sample_count);
  MTL::Texture* GetTransferDummyStencilTexture(uint32_t sample_count);
  MTL::Buffer* GetTransferDummyBuffer();
  MTL::DepthStencilState* GetTransferDepthStencilState(bool depth_write);
  MTL::DepthStencilState* GetTransferNoDepthStencilState();
  MTL::DepthStencilState* GetTransferDepthClearState();
  MTL::DepthStencilState* GetTransferStencilClearState();
  MTL::DepthStencilState* GetTransferStencilBitState(uint32_t bit);

  // EDRAM tile operations

  void LoadTiledData(MTL::CommandBuffer* command_buffer, MTL::Texture* texture,
                     uint32_t edram_base, uint32_t pitch_tiles,
                     uint32_t height_tiles, bool is_depth);

  void StoreTiledData(MTL::CommandBuffer* command_buffer, MTL::Texture* texture,
                      uint32_t edram_base, uint32_t pitch_tiles,
                      uint32_t height_tiles, bool is_depth);

  // Ownership transfer support - copies data between render targets when
  // EDRAM regions are aliased between different RT configurations.
  // This mirrors D3D12/Vulkan's PerformTransfersAndResolveClears.
  void PerformTransfersAndResolveClears(
      uint32_t render_target_count, RenderTarget* const* render_targets,
      const std::vector<Transfer>* render_target_transfers,
      const uint64_t* render_target_resolve_clear_values = nullptr,
      const Transfer::Rectangle* resolve_clear_rectangle = nullptr,
      MTL::CommandBuffer* command_buffer = nullptr);

  // Writes contents of host render targets within rectangles from
  // ResolveInfo::GetCopyEdramTileSpan to edram_buffer_.
  void DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used,
                         uint32_t dump_rows, uint32_t dump_pitch,
                         MTL::CommandBuffer* command_buffer = nullptr);

  // Blends and packs host render targets into EDRAM for resolve paths.
  void BlendRenderTargetsToEdram(uint32_t dump_base,
                                 uint32_t dump_row_length_used,
                                 uint32_t dump_rows, uint32_t dump_pitch,
                                 MTL::CommandBuffer* command_buffer = nullptr);
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_RENDER_TARGET_CACHE_H_

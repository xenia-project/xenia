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

    void SetTexture(MTL::Texture* texture) { texture_ = texture; }
    void SetMsaaTexture(MTL::Texture* texture) { msaa_texture_ = texture; }

    // Public constructor for creating render targets
    MetalRenderTarget(RenderTargetKey key) : RenderTarget(key) {}

   private:
    MTL::Texture* texture_ = nullptr;
    MTL::Texture* msaa_texture_ = nullptr;  // If MSAA is enabled
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
  Path GetPath() const override { return Path::kHostRenderTargets; }

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

  // Get the last REAL (non-dummy) render targets for capture
  MTL::Texture* GetLastRealColorTarget(uint32_t index) const;
  MTL::Texture* GetLastRealDepthTarget() const;

  // Restore EDRAM contents from snapshot (for trace playback), matching
  // D3D12RenderTargetCache::RestoreEdramSnapshot.
  void RestoreEdramSnapshot(const void* snapshot);

  // Resolve (copy) render targets to shared memory
  bool Resolve(Memory& memory, uint32_t& written_address,
               uint32_t& written_length);

 protected:
  // Virtual methods from RenderTargetCache
  uint32_t GetMaxRenderTargetWidth() const override;
  uint32_t GetMaxRenderTargetHeight() const override;

  RenderTarget* CreateRenderTarget(RenderTargetKey key) override;

  bool IsHostDepthEncodingDifferent(
      xenos::DepthRenderTargetFormat format) const override;

 private:
  MetalCommandProcessor& command_processor_;
  TraceWriter* trace_writer_;

  // Metal device reference
  MTL::Device* device_ = nullptr;

  // EDRAM buffer (10MB embedded DRAM)
  MTL::Buffer* edram_buffer_ = nullptr;

  // EDRAM compute shaders for tile operations
  MTL::ComputePipelineState* edram_load_pipeline_ = nullptr;   // Tiled → Linear
  MTL::ComputePipelineState* edram_store_pipeline_ = nullptr;  // Linear → Tiled

  // EDRAM dump compute shaders for host render target → EDRAM copies.
  // Color, 32bpp.
  MTL::ComputePipelineState* edram_dump_color_32bpp_1xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_dump_color_32bpp_2xmsaa_pipeline_ = nullptr;
  MTL::ComputePipelineState* edram_dump_color_32bpp_4xmsaa_pipeline_ = nullptr;
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
    uint32_t source_resource_format;
    uint32_t dest_resource_format;

    bool operator==(const TransferShaderKey& other) const {
      return mode == other.mode &&
             source_msaa_samples == other.source_msaa_samples &&
             dest_msaa_samples == other.dest_msaa_samples &&
             source_resource_format == other.source_resource_format &&
             dest_resource_format == other.dest_resource_format;
    }

    struct Hasher {
      size_t operator()(const TransferShaderKey& key) const {
        size_t h = size_t(key.mode);
        h ^= (size_t(key.source_msaa_samples) << 4);
        h ^= (size_t(key.dest_msaa_samples) << 8);
        h ^= (size_t(key.source_resource_format) << 16);
        h ^= (size_t(key.dest_resource_format) << 24);
        return h;
      }
    };
  };

  std::unordered_map<TransferShaderKey, MTL::RenderPipelineState*,
                     TransferShaderKey::Hasher>
      transfer_pipelines_;

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

  MTL::PixelFormat GetColorPixelFormat(
      xenos::ColorRenderTargetFormat format) const;
  MTL::PixelFormat GetDepthPixelFormat(
      xenos::DepthRenderTargetFormat format) const;

  // EDRAM compute shader setup
  bool InitializeEdramComputeShaders();
  void ShutdownEdramComputeShaders();

  // Transfer pipeline setup (host RT ownership transfers) - Metal analogue of
  // D3D12RenderTargetCache::GetOrCreateTransferPipelines. Currently only
  // returns a pipeline for a very small subset of keys and logs whenever a
  // mode / key combination is not yet implemented.
  MTL::RenderPipelineState* GetOrCreateTransferPipelines(
      const TransferShaderKey& key, MTL::PixelFormat dest_format);

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
      const std::vector<Transfer>* render_target_transfers);

  // Writes contents of host render targets within rectangles from
  // ResolveInfo::GetCopyEdramTileSpan to edram_buffer_.
  void DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used,
                         uint32_t dump_rows, uint32_t dump_pitch);
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_RENDER_TARGET_CACHE_H_

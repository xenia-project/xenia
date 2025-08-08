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

#include "xenia/gpu/render_target_cache.h"
#include "xenia/gpu/register_file.h"
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
  // Metal-specific render target - defined inside cache class to access protected RenderTarget
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
                         const Memory& memory,
                         TraceWriter* trace_writer,
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
  MTL::RenderPassDescriptor* GetRenderPassDescriptor(uint32_t expected_sample_count = 1);
  
  // Get current render targets for capture
  MTL::Texture* GetColorTarget(uint32_t index) const;
  MTL::Texture* GetDepthTarget() const;
  MTL::Texture* GetDummyColorTarget() const;
  
  // Restore EDRAM contents from snapshot (for trace playback)
  void RestoreEdram(const void* snapshot);
  
  // Resolve (copy) render targets to shared memory
  bool Resolve(Memory& memory, uint32_t& written_address, uint32_t& written_length);

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
  
  // Current render targets - updated by base class Update() call
  MetalRenderTarget* current_color_targets_[4] = {};
  MetalRenderTarget* current_depth_target_ = nullptr;
  
  // Track all created render targets so we can find them
  std::unordered_map<uint32_t, MetalRenderTarget*> render_target_map_;
  
  // Render pass descriptor cache
  MTL::RenderPassDescriptor* cached_render_pass_descriptor_ = nullptr;
  bool render_pass_descriptor_dirty_ = true;
  
  // Dummy render target for when no render targets are bound
  mutable std::unique_ptr<MetalRenderTarget> dummy_color_target_;
  mutable bool dummy_color_target_needs_clear_ = true;
  
  // Helper methods
  MTL::Texture* CreateColorTexture(uint32_t width, uint32_t height,
                                   xenos::ColorRenderTargetFormat format,
                                   uint32_t samples);
  MTL::Texture* CreateDepthTexture(uint32_t width, uint32_t height,
                                   xenos::DepthRenderTargetFormat format,
                                   uint32_t samples);
  
  MTL::PixelFormat GetColorPixelFormat(xenos::ColorRenderTargetFormat format) const;
  MTL::PixelFormat GetDepthPixelFormat(xenos::DepthRenderTargetFormat format) const;
  
  // EDRAM tile operations
  void LoadTiledData(MTL::CommandBuffer* command_buffer,
                     MTL::Texture* texture,
                     uint32_t edram_base,
                     uint32_t pitch_tiles,
                     uint32_t height_tiles,
                     bool is_depth);
  
  void StoreTiledData(MTL::CommandBuffer* command_buffer,
                      MTL::Texture* texture,
                      uint32_t edram_base,
                      uint32_t pitch_tiles,
                      uint32_t height_tiles,
                      bool is_depth);
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_RENDER_TARGET_CACHE_H_
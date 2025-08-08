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

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"

#include "third_party/metal-cpp/Metal/Metal.hpp"

namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor;

class MetalRenderTargetCache {
 public:
  MetalRenderTargetCache(MetalCommandProcessor* command_processor,
                         const RegisterFile* register_file,
                         Memory* memory);
  ~MetalRenderTargetCache();

  bool Initialize();
  void Shutdown();
  void ClearCache();

  // Render target management
  bool SetRenderTargets(uint32_t rt_count, const uint32_t* color_targets,
                       uint32_t depth_target);
  
  // Resolve (copy) render targets to shared memory
  bool Resolve(Memory& memory, uint32_t& written_address, uint32_t& written_length);
  
  // Copy render targets to/from EDRAM buffer
  void LoadRenderTargetsFromEDRAM(MTL::CommandBuffer* command_buffer = nullptr);
  void StoreRenderTargetsToEDRAM(MTL::CommandBuffer* command_buffer = nullptr);
  
  // Get current render targets for Metal render pass
  MTL::Texture* GetColorTarget(uint32_t index) const;
  MTL::Texture* GetDepthTarget() const;
  MTL::Texture* GetDummyColorTarget() const;
  
  // Reset for new frame (clears dummy target on next use)
  void BeginFrame() {
    dummy_color_target_needs_clear_ = true;
  }
  
  // Restore EDRAM contents from snapshot (for trace playback)
  void RestoreEdram(const void* snapshot);
  
  // Get render pass descriptor for current targets
  // expected_sample_count: The sample count expected by the pipeline (for dummy target creation)
  MTL::RenderPassDescriptor* GetRenderPassDescriptor(uint32_t expected_sample_count = 1);

 private:
  struct RenderTargetDescriptor {
    uint32_t base_address;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t msaa_samples;
    bool is_depth;
    
    bool operator==(const RenderTargetDescriptor& other) const;
  };
  
  struct RenderTargetDescriptorHasher {
    size_t operator()(const RenderTargetDescriptor& desc) const;
  };

  struct MetalRenderTarget {
    MTL::Texture* texture;
    MTL::Texture* resolve_texture;  // For MSAA resolve
    uint32_t width;
    uint32_t height;
    MTL::PixelFormat format;
    uint32_t samples;
    bool is_depth;
    uint32_t edram_base;  // EDRAM address offset for this render target
    bool needs_clear;  // Track if this target needs initial clear
    
    MetalRenderTarget() : texture(nullptr), resolve_texture(nullptr),
                         width(0), height(0), format(MTL::PixelFormatInvalid),
                         samples(1), is_depth(false), edram_base(0), needs_clear(true) {}
    ~MetalRenderTarget() {
      if (texture) {
        texture->release();
      }
      if (resolve_texture) {
        resolve_texture->release();
      }
    }
  };

  // Metal render target creation helpers
  MTL::Texture* CreateColorTarget(uint32_t width, uint32_t height,
                                 MTL::PixelFormat format, uint32_t samples = 1);
  MTL::Texture* CreateDepthTarget(uint32_t width, uint32_t height,
                                 MTL::PixelFormat format, uint32_t samples = 1);
  
  // Xbox 360 format conversion
  MTL::PixelFormat ConvertColorFormat(xenos::ColorRenderTargetFormat format);
  MTL::PixelFormat ConvertDepthFormat(xenos::DepthRenderTargetFormat format);

  MetalCommandProcessor* command_processor_;
  const RegisterFile* register_file_;
  Memory* memory_;

  // EDRAM buffer - 10MB of embedded DRAM that Xbox 360 uses for render targets
  MTL::Buffer* edram_buffer_;
  
  // Current render targets (pointers to cached targets)
  MetalRenderTarget* current_color_targets_[4];
  MetalRenderTarget* current_depth_target_;
  
  // Render target cache
  std::unordered_map<RenderTargetDescriptor, std::unique_ptr<MetalRenderTarget>,
                     RenderTargetDescriptorHasher> render_target_cache_;
                     
  // Cached render pass descriptor
  mutable MTL::RenderPassDescriptor* cached_render_pass_descriptor_;
  mutable bool render_pass_descriptor_dirty_;
  
  // Dummy render target for when no render targets are bound
  mutable std::unique_ptr<MetalRenderTarget> dummy_color_target_;
  mutable bool dummy_color_target_needs_clear_ = true;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_RENDER_TARGET_CACHE_H_

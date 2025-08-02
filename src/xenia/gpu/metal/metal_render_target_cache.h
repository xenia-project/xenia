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

#if XE_PLATFORM_MAC
#ifdef METAL_CPP_AVAILABLE
#include "third_party/metal-cpp/Metal/Metal.hpp"
#endif  // METAL_CPP_AVAILABLE
#endif  // XE_PLATFORM_MAC

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
  
#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Get current render targets for Metal render pass
  MTL::Texture* GetColorTarget(uint32_t index) const;
  MTL::Texture* GetDepthTarget() const;
  
  // Get render pass descriptor for current targets
  MTL::RenderPassDescriptor* GetRenderPassDescriptor() const;
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

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

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  struct MetalRenderTarget {
    MTL::Texture* texture;
    MTL::Texture* resolve_texture;  // For MSAA resolve
    uint32_t width;
    uint32_t height;
    MTL::PixelFormat format;
    uint32_t samples;
    bool is_depth;
    
    MetalRenderTarget() : texture(nullptr), resolve_texture(nullptr),
                         width(0), height(0), format(MTL::PixelFormatInvalid),
                         samples(1), is_depth(false) {}
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
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  MetalCommandProcessor* command_processor_;
  const RegisterFile* register_file_;
  Memory* memory_;

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Current render targets
  std::unique_ptr<MetalRenderTarget> current_color_targets_[4];
  std::unique_ptr<MetalRenderTarget> current_depth_target_;
  
  // Render target cache
  std::unordered_map<RenderTargetDescriptor, std::unique_ptr<MetalRenderTarget>,
                     RenderTargetDescriptorHasher> render_target_cache_;
                     
  // Cached render pass descriptor
  mutable MTL::RenderPassDescriptor* cached_render_pass_descriptor_;
  mutable bool render_pass_descriptor_dirty_;
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_RENDER_TARGET_CACHE_H_

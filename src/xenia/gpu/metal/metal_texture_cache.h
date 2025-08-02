/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_TEXTURE_CACHE_H_
#define XENIA_GPU_METAL_METAL_TEXTURE_CACHE_H_

#include <memory>
#include <unordered_map>

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/texture_info.h"
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

class MetalTextureCache {
 public:
  MetalTextureCache(MetalCommandProcessor* command_processor,
                    const RegisterFile* register_file,
                    Memory* memory);
  ~MetalTextureCache();

  bool Initialize();
  void Shutdown();
  void ClearCache();

  // Texture management
  bool UploadTexture2D(const TextureInfo& texture_info);
  bool UploadTextureCube(const TextureInfo& texture_info);

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Get Metal textures for rendering
  MTL::Texture* GetTexture2D(const TextureInfo& texture_info);
  MTL::Texture* GetTextureCube(const TextureInfo& texture_info);
  
  // Pixel format conversion
  MTL::PixelFormat ConvertXenosFormat(xenos::TextureFormat format, 
                                      xenos::Endian endian = xenos::Endian::k8in32);
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

 private:
  struct TextureDescriptor {
    uint32_t guest_base;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t format;
    uint32_t endian;
    uint32_t mip_levels;
    bool is_cube;
    
    bool operator==(const TextureDescriptor& other) const;
  };
  
  struct TextureDescriptorHasher {
    size_t operator()(const TextureDescriptor& desc) const;
  };

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  struct MetalTexture {
    MTL::Texture* texture;
    uint32_t size;
    bool is_dynamic;
    
    MetalTexture() : texture(nullptr), size(0), is_dynamic(false) {}
    ~MetalTexture() {
      if (texture) {
        texture->release();
      }
    }
  };

  // Metal texture creation helpers
  MTL::Texture* CreateTexture2D(uint32_t width, uint32_t height, 
                                MTL::PixelFormat format, uint32_t mip_levels = 1);
  MTL::Texture* CreateTextureCube(uint32_t width, MTL::PixelFormat format, 
                                  uint32_t mip_levels = 1);
  bool UpdateTexture2D(MTL::Texture* texture, const TextureInfo& texture_info);
  bool UpdateTextureCube(MTL::Texture* texture, const TextureInfo& texture_info);
  
  // Format conversion helpers
  bool ConvertTextureData(const void* src_data, void* dst_data, 
                         uint32_t width, uint32_t height,
                         xenos::TextureFormat src_format, 
                         MTL::PixelFormat dst_format);
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  MetalCommandProcessor* command_processor_;
  const RegisterFile* register_file_;
  Memory* memory_;

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Texture cache
  std::unordered_map<TextureDescriptor, std::unique_ptr<MetalTexture>,
                     TextureDescriptorHasher> texture_cache_;
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_TEXTURE_CACHE_H_

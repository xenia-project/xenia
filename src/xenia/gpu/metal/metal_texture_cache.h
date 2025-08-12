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
#include <string>
#include <unordered_map>

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"

#include "third_party/metal-cpp/Metal/Metal.hpp"

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

  // Get Metal textures for rendering
  MTL::Texture* GetTexture2D(const TextureInfo& texture_info);
  MTL::Texture* GetTextureCube(const TextureInfo& texture_info);
  
  // Pixel format conversion
  MTL::PixelFormat ConvertXenosFormat(xenos::TextureFormat format, 
                                      xenos::Endian endian = xenos::Endian::k8in32);
  
  // Debug texture creation
  MTL::Texture* CreateDebugTexture(uint32_t width = 256, uint32_t height = 256);

  // Null texture accessors for invalid bindings (following D3D12/Vulkan pattern)
  MTL::Texture* GetNullTexture2D() const { return null_texture_2d_; }
  MTL::Texture* GetNullTexture3D() const { return null_texture_3d_; }
  MTL::Texture* GetNullTextureCube() const { return null_texture_cube_; }

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
  void DumpTextureToFile(MTL::Texture* texture, const std::string& filename,
                        uint32_t width, uint32_t height);
  
  // Format conversion helpers
  bool ConvertTextureData(const void* src_data, void* dst_data, 
                         uint32_t width, uint32_t height,
                         xenos::TextureFormat src_format, 
                         MTL::PixelFormat dst_format);

  // Null texture factory methods (following existing CreateTexture pattern)
  MTL::Texture* CreateNullTexture2D();
  MTL::Texture* CreateNullTexture3D();
  MTL::Texture* CreateNullTextureCube();

  MetalCommandProcessor* command_processor_;
  const RegisterFile* register_file_;
  Memory* memory_;

  // Pre-created null textures for invalid bindings (following existing patterns)
  MTL::Texture* null_texture_2d_ = nullptr;
  MTL::Texture* null_texture_3d_ = nullptr;
  MTL::Texture* null_texture_cube_ = nullptr;

  // Texture cache
  std::unordered_map<TextureDescriptor, std::unique_ptr<MetalTexture>,
                     TextureDescriptorHasher> texture_cache_;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_TEXTURE_CACHE_H_

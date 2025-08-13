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
#include "xenia/gpu/texture_cache.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"

#include "third_party/metal-cpp/Metal/Metal.hpp"

namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor;
class MetalSharedMemory;

class MetalTextureCache : public TextureCache {
 public:
  MetalTextureCache(MetalCommandProcessor* command_processor,
                    const RegisterFile& register_file,
                    MetalSharedMemory& shared_memory,
                    uint32_t draw_resolution_scale_x,
                    uint32_t draw_resolution_scale_y);
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

  // TextureCache virtual method overrides
  void RequestTextures(uint32_t used_texture_mask) override;
  uint32_t GetHostFormatSwizzle(TextureKey key) const override;
  uint32_t GetMaxHostTextureWidthHeight(xenos::DataDimension dimension) const override;
  uint32_t GetMaxHostTextureDepthOrArraySize(xenos::DataDimension dimension) const override;
  std::unique_ptr<Texture> CreateTexture(TextureKey key) override;
  bool LoadTextureDataFromResidentMemoryImpl(Texture& texture, bool load_base, bool load_mips) override;

  // Metal-specific Texture implementation
  class MetalTexture : public Texture {
   public:
    MetalTexture(MetalTextureCache& texture_cache, const TextureKey& key, MTL::Texture* metal_texture);
    ~MetalTexture() override;

    MTL::Texture* metal_texture() const { return metal_texture_; }

   private:
    MTL::Texture* metal_texture_;
  };

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
  
  // Legacy texture cache for old methods (will be migrated to base class)
  std::unordered_map<TextureDescriptor, std::unique_ptr<MetalTexture>, TextureDescriptorHasher> texture_cache_;

  // Pre-created null textures for invalid bindings (following existing patterns)
  MTL::Texture* null_texture_2d_ = nullptr;
  MTL::Texture* null_texture_3d_ = nullptr;
  MTL::Texture* null_texture_cube_ = nullptr;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_TEXTURE_CACHE_H_

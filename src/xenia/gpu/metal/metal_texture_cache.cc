/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_texture_cache.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/metal/metal_command_processor.h"


namespace xe {
namespace gpu {
namespace metal {

MetalTextureCache::MetalTextureCache(MetalCommandProcessor* command_processor,
                                     const RegisterFile* register_file,
                                     Memory* memory)
    : command_processor_(command_processor),
      register_file_(register_file),
      memory_(memory) {}

MetalTextureCache::~MetalTextureCache() {
  Shutdown();
}

bool MetalTextureCache::Initialize() {
  SCOPE_profile_cpu_f("gpu");

  XELOGD("Metal texture cache: Initialized successfully");
  
  return true;
}

void MetalTextureCache::Shutdown() {
  SCOPE_profile_cpu_f("gpu");

  ClearCache();

  XELOGD("Metal texture cache: Shutdown complete");
}

void MetalTextureCache::ClearCache() {
  SCOPE_profile_cpu_f("gpu");

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  texture_cache_.clear();
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  XELOGD("Metal texture cache: Cache cleared");
}

bool MetalTextureCache::UploadTexture2D(const TextureInfo& texture_info) {
  SCOPE_profile_cpu_f("gpu");

  if (!texture_info.memory.base_address || !texture_info.width || !texture_info.height) {
    return false;
  }

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Create texture descriptor for cache lookup
  TextureDescriptor desc = {};
  desc.guest_base = texture_info.memory.base_address;
  desc.width = texture_info.width;
  desc.height = texture_info.height;
  desc.depth = 1;
  desc.format = static_cast<uint32_t>(texture_info.format_info()->format);
  desc.endian = static_cast<uint32_t>(texture_info.endianness);
  desc.mip_levels = texture_info.mip_levels();
  desc.is_cube = false;

  // Check if texture already exists in cache
  auto it = texture_cache_.find(desc);
  if (it != texture_cache_.end()) {
    return true;  // Already uploaded
  }

  // Convert Xbox 360 format to Metal format
  MTL::PixelFormat metal_format = ConvertXenosFormat(
      texture_info.format_info()->format, texture_info.endianness);
  if (metal_format == MTL::PixelFormatInvalid) {
    XELOGE("Metal texture cache: Unsupported texture format {}", 
           static_cast<uint32_t>(texture_info.format_info()->format));
    return false;
  }

  // Create Metal texture
  MTL::Texture* metal_texture = CreateTexture2D(
      texture_info.width, texture_info.height, metal_format, texture_info.mip_levels());
  if (!metal_texture) {
    XELOGE("Metal texture cache: Failed to create 2D texture");
    return false;
  }

  // Upload texture data
  if (!UpdateTexture2D(metal_texture, texture_info)) {
    metal_texture->release();
    return false;
  }

  // Add to cache
  auto metal_tex = std::make_unique<MetalTexture>();
  metal_tex->texture = metal_texture;
  metal_tex->size = texture_info.memory.base_size;
  texture_cache_[desc] = std::move(metal_tex);

  XELOGD("Metal texture cache: Created 2D texture {}x{} format={} (total: {})",
         texture_info.width, texture_info.height, 
         static_cast<uint32_t>(texture_info.format_info()->format),
         texture_cache_.size());
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  return true;
}

bool MetalTextureCache::UploadTextureCube(const TextureInfo& texture_info) {
  SCOPE_profile_cpu_f("gpu");

  if (!texture_info.memory.base_address || !texture_info.width || !texture_info.height) {
    return false;
  }

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Create texture descriptor for cache lookup
  TextureDescriptor desc = {};
  desc.guest_base = texture_info.memory.base_address;
  desc.width = texture_info.width;
  desc.height = texture_info.height;
  desc.depth = 1;
  desc.format = static_cast<uint32_t>(texture_info.format_info()->format);
  desc.endian = static_cast<uint32_t>(texture_info.endianness);
  desc.mip_levels = texture_info.mip_levels();
  desc.is_cube = true;

  // Check if texture already exists in cache
  auto it = texture_cache_.find(desc);
  if (it != texture_cache_.end()) {
    return true;  // Already uploaded
  }

  // Convert Xbox 360 format to Metal format
  MTL::PixelFormat metal_format = ConvertXenosFormat(
      texture_info.format_info()->format, texture_info.endianness);
  if (metal_format == MTL::PixelFormatInvalid) {
    XELOGE("Metal texture cache: Unsupported cube texture format {}", 
           static_cast<uint32_t>(texture_info.format_info()->format));
    return false;
  }

  // Create Metal cube texture
  MTL::Texture* metal_texture = CreateTextureCube(
      texture_info.width, metal_format, texture_info.mip_levels());
  if (!metal_texture) {
    XELOGE("Metal texture cache: Failed to create cube texture");
    return false;
  }

  // Upload texture data
  if (!UpdateTextureCube(metal_texture, texture_info)) {
    metal_texture->release();
    return false;
  }

  // Add to cache
  auto metal_tex = std::make_unique<MetalTexture>();
  metal_tex->texture = metal_texture;
  metal_tex->size = texture_info.memory.base_size;
  texture_cache_[desc] = std::move(metal_tex);

  XELOGD("Metal texture cache: Created cube texture {}x{} format={} (total: {})",
         texture_info.width, texture_info.height, 
         static_cast<uint32_t>(texture_info.format_info()->format),
         texture_cache_.size());
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  return true;
}

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)

MTL::Texture* MetalTextureCache::GetTexture2D(const TextureInfo& texture_info) {
  TextureDescriptor desc = {};
  desc.guest_base = texture_info.memory.base_address;
  desc.width = texture_info.width;
  desc.height = texture_info.height;
  desc.depth = 1;
  desc.format = static_cast<uint32_t>(texture_info.format_info()->format);
  desc.endian = static_cast<uint32_t>(texture_info.endianness);
  desc.mip_levels = texture_info.mip_levels();
  desc.is_cube = false;

  auto it = texture_cache_.find(desc);
  if (it != texture_cache_.end()) {
    return it->second->texture;
  }

  return nullptr;
}

MTL::Texture* MetalTextureCache::GetTextureCube(const TextureInfo& texture_info) {
  TextureDescriptor desc = {};
  desc.guest_base = texture_info.memory.base_address;
  desc.width = texture_info.width;
  desc.height = texture_info.height;
  desc.depth = 1;
  desc.format = static_cast<uint32_t>(texture_info.format_info()->format);
  desc.endian = static_cast<uint32_t>(texture_info.endianness);
  desc.mip_levels = texture_info.mip_levels();
  desc.is_cube = true;

  auto it = texture_cache_.find(desc);
  if (it != texture_cache_.end()) {
    return it->second->texture;
  }

  return nullptr;
}

MTL::PixelFormat MetalTextureCache::ConvertXenosFormat(xenos::TextureFormat format, 
                                                       xenos::Endian endian) {
  // Convert Xbox 360 texture formats to Metal pixel formats
  // This is a simplified mapping - the full implementation would handle all Xbox 360 formats
  switch (format) {
    case xenos::TextureFormat::k_8_8_8_8:
      return MTL::PixelFormatRGBA8Unorm;
    case xenos::TextureFormat::k_1_5_5_5:
      return MTL::PixelFormatRGB5A1Unorm;
    case xenos::TextureFormat::k_5_6_5:
      return MTL::PixelFormatB5G6R5Unorm;
    case xenos::TextureFormat::k_8:
      return MTL::PixelFormatR8Unorm;
    case xenos::TextureFormat::k_8_8:
      return MTL::PixelFormatRG8Unorm;
    case xenos::TextureFormat::k_DXT1:
      return MTL::PixelFormatBC1_RGBA;
    case xenos::TextureFormat::k_DXT3:
      return MTL::PixelFormatBC2_RGBA;
    case xenos::TextureFormat::k_DXT5:
      return MTL::PixelFormatBC3_RGBA;
    default:
      XELOGW("Metal texture cache: Unsupported Xbox 360 texture format {}", 
             static_cast<uint32_t>(format));
      return MTL::PixelFormatInvalid;
  }
}

MTL::Texture* MetalTextureCache::CreateTexture2D(uint32_t width, uint32_t height, 
                                                  MTL::PixelFormat format, uint32_t mip_levels) {
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal texture cache: Failed to get Metal device from command processor");
    return nullptr;
  }

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  descriptor->setTextureType(MTL::TextureType2D);
  descriptor->setPixelFormat(format);
  descriptor->setWidth(width);
  descriptor->setHeight(height);
  descriptor->setMipmapLevelCount(mip_levels);
  descriptor->setUsage(MTL::TextureUsageShaderRead);
  descriptor->setStorageMode(MTL::StorageModeShared);

  MTL::Texture* texture = device->newTexture(descriptor);
  
  descriptor->release();

  if (!texture) {
    XELOGE("Metal texture cache: Failed to create 2D texture {}x{}", width, height);
    return nullptr;
  }

  return texture;
}

MTL::Texture* MetalTextureCache::CreateTextureCube(uint32_t width, MTL::PixelFormat format, 
                                                   uint32_t mip_levels) {
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal texture cache: Failed to get Metal device from command processor");
    return nullptr;
  }

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  descriptor->setTextureType(MTL::TextureTypeCube);
  descriptor->setPixelFormat(format);
  descriptor->setWidth(width);
  descriptor->setHeight(width);  // Cube faces are square
  descriptor->setMipmapLevelCount(mip_levels);
  descriptor->setUsage(MTL::TextureUsageShaderRead);
  descriptor->setStorageMode(MTL::StorageModeShared);

  MTL::Texture* texture = device->newTexture(descriptor);
  
  descriptor->release();

  if (!texture) {
    XELOGE("Metal texture cache: Failed to create cube texture {}", width);
    return nullptr;
  }

  return texture;
}

bool MetalTextureCache::UpdateTexture2D(MTL::Texture* texture, const TextureInfo& texture_info) {
  // Get guest data
  const void* guest_data = memory_->TranslatePhysical(texture_info.memory.base_address);
  if (!guest_data) {
    XELOGE("Metal texture cache: Invalid guest texture address 0x{:08X}", texture_info.memory.base_address);
    return false;
  }

  // For now, assume simple format conversion or direct copy
  // TODO: Implement proper Xbox 360 texture format conversion
  
  MTL::Region region = MTL::Region(0, 0, 0, texture_info.width, texture_info.height, 1);
  
  // Calculate bytes per row (simplified)
  uint32_t bytes_per_pixel = 4;  // Assume RGBA8 for now
  uint32_t bytes_per_row = texture_info.width * bytes_per_pixel;
  
  texture->replaceRegion(region, 0, guest_data, bytes_per_row);
  
  return true;
}

bool MetalTextureCache::UpdateTextureCube(MTL::Texture* texture, const TextureInfo& texture_info) {
  // Get guest data
  const void* guest_data = memory_->TranslatePhysical(texture_info.memory.base_address);
  if (!guest_data) {
    XELOGE("Metal texture cache: Invalid guest cube texture address 0x{:08X}", texture_info.memory.base_address);
    return false;
  }

  // For now, assume simple format conversion
  // TODO: Implement proper Xbox 360 cube texture format conversion
  
  MTL::Region region = MTL::Region(0, 0, 0, texture_info.width, texture_info.width, 1);
  
  // Calculate bytes per row (simplified)
  uint32_t bytes_per_pixel = 4;  // Assume RGBA8 for now
  uint32_t bytes_per_row = texture_info.width * bytes_per_pixel;
  uint32_t face_size = bytes_per_row * texture_info.width;
  
  // Upload all 6 cube faces
  const uint8_t* face_data = static_cast<const uint8_t*>(guest_data);
  for (uint32_t face = 0; face < 6; ++face) {
    texture->replaceRegion(region, 0, face, face_data + (face * face_size), bytes_per_row, 0);
  }
  
  return true;
}

bool MetalTextureCache::ConvertTextureData(const void* src_data, void* dst_data, 
                                           uint32_t width, uint32_t height,
                                           xenos::TextureFormat src_format, 
                                           MTL::PixelFormat dst_format) {
  // TODO: Implement proper Xbox 360 texture format conversion
  // For now, just copy data directly
  if (src_data && dst_data) {
    uint32_t size = width * height * 4;  // Assume 4 bytes per pixel
    std::memcpy(dst_data, src_data, size);
    return true;
  }
  return false;
}

#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

bool MetalTextureCache::TextureDescriptor::operator==(const TextureDescriptor& other) const {
  return guest_base == other.guest_base &&
         width == other.width &&
         height == other.height &&
         depth == other.depth &&
         format == other.format &&
         endian == other.endian &&
         mip_levels == other.mip_levels &&
         is_cube == other.is_cube;
}

size_t MetalTextureCache::TextureDescriptorHasher::operator()(const TextureDescriptor& desc) const {
  size_t hash = 0;
  hash ^= std::hash<uint32_t>{}(desc.guest_base) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.width) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.height) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.format) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<bool>{}(desc.is_cube) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  return hash;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe

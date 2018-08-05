/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/texture_cache.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/texture_util.h"

namespace xe {
namespace gpu {
namespace d3d12 {

TextureCache::HostFormat TextureCache::host_formats_[64] = {
    {DXGI_FORMAT_UNKNOWN},    // k_1_REVERSE
    {DXGI_FORMAT_UNKNOWN},    // k_1
    {DXGI_FORMAT_UNKNOWN},    // k_8
    {DXGI_FORMAT_UNKNOWN},    // k_1_5_5_5
    {DXGI_FORMAT_UNKNOWN},    // k_5_6_5
    {DXGI_FORMAT_UNKNOWN},    // k_6_5_5
    {DXGI_FORMAT_UNKNOWN},    // k_8_8_8_8
    {DXGI_FORMAT_UNKNOWN},    // k_2_10_10_10
    {DXGI_FORMAT_UNKNOWN},    // k_8_A
    {DXGI_FORMAT_UNKNOWN},    // k_8_B
    {DXGI_FORMAT_UNKNOWN},    // k_8_8
    {DXGI_FORMAT_UNKNOWN},    // k_Cr_Y1_Cb_Y0
    {DXGI_FORMAT_UNKNOWN},    // k_Y1_Cr_Y0_Cb
    {DXGI_FORMAT_UNKNOWN},    // k_Shadow
    {DXGI_FORMAT_UNKNOWN},    // k_8_8_8_8_A
    {DXGI_FORMAT_UNKNOWN},    // k_4_4_4_4
    {DXGI_FORMAT_UNKNOWN},    // k_10_11_11
    {DXGI_FORMAT_UNKNOWN},    // k_11_11_10
    {DXGI_FORMAT_BC1_UNORM},  // k_DXT1
    {DXGI_FORMAT_UNKNOWN},    // k_DXT2_3
    {DXGI_FORMAT_UNKNOWN},    // k_DXT4_5
    {DXGI_FORMAT_UNKNOWN},    // k_DXV
    {DXGI_FORMAT_UNKNOWN},    // k_24_8
    {DXGI_FORMAT_UNKNOWN},    // k_24_8_FLOAT
    {DXGI_FORMAT_UNKNOWN},    // k_16
    {DXGI_FORMAT_UNKNOWN},    // k_16_16
    {DXGI_FORMAT_UNKNOWN},    // k_16_16_16_16
    {DXGI_FORMAT_UNKNOWN},    // k_16_EXPAND
    {DXGI_FORMAT_UNKNOWN},    // k_16_16_EXPAND
    {DXGI_FORMAT_UNKNOWN},    // k_16_16_16_16_EXPAND
    {DXGI_FORMAT_UNKNOWN},    // k_16_FLOAT
    {DXGI_FORMAT_UNKNOWN},    // k_16_16_FLOAT
    {DXGI_FORMAT_UNKNOWN},    // k_16_16_16_16_FLOAT
    {DXGI_FORMAT_UNKNOWN},    // k_32
    {DXGI_FORMAT_UNKNOWN},    // k_32_32
    {DXGI_FORMAT_UNKNOWN},    // k_32_32_32_32
    {DXGI_FORMAT_UNKNOWN},    // k_32_FLOAT
    {DXGI_FORMAT_UNKNOWN},    // k_32_32_FLOAT
    {DXGI_FORMAT_UNKNOWN},    // k_32_32_32_32_FLOAT
    {DXGI_FORMAT_UNKNOWN},    // k_32_AS_8
    {DXGI_FORMAT_UNKNOWN},    // k_32_AS_8_8
    {DXGI_FORMAT_UNKNOWN},    // k_16_MPEG
    {DXGI_FORMAT_UNKNOWN},    // k_16_16_MPEG
    {DXGI_FORMAT_UNKNOWN},    // k_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN},    // k_32_AS_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN},    // k_32_AS_8_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN},    // k_16_INTERLACED
    {DXGI_FORMAT_UNKNOWN},    // k_16_MPEG_INTERLACED
    {DXGI_FORMAT_UNKNOWN},    // k_16_16_MPEG_INTERLACED
    {DXGI_FORMAT_UNKNOWN},    // k_DXN
    {DXGI_FORMAT_UNKNOWN},    // k_8_8_8_8_AS_16_16_16_16
    {DXGI_FORMAT_BC1_UNORM},  // k_DXT1_AS_16_16_16_16
    {DXGI_FORMAT_UNKNOWN},    // k_DXT2_3_AS_16_16_16_16
    {DXGI_FORMAT_UNKNOWN},    // k_DXT4_5_AS_16_16_16_16
    {DXGI_FORMAT_UNKNOWN},    // k_2_10_10_10_AS_16_16_16_16
    {DXGI_FORMAT_UNKNOWN},    // k_10_11_11_AS_16_16_16_16
    {DXGI_FORMAT_UNKNOWN},    // k_11_11_10_AS_16_16_16_16
    {DXGI_FORMAT_UNKNOWN},    // k_32_32_32_FLOAT
    {DXGI_FORMAT_UNKNOWN},    // k_DXT3A
    {DXGI_FORMAT_UNKNOWN},    // k_DXT5A
    {DXGI_FORMAT_UNKNOWN},    // k_CTX1
    {DXGI_FORMAT_UNKNOWN},    // k_DXT3A_AS_1_1_1_1
    {DXGI_FORMAT_UNKNOWN},    // k_8_8_8_8_GAMMA
    {DXGI_FORMAT_UNKNOWN},    // k_2_10_10_10_FLOAT
};

const char* TextureCache::dimension_names_[4] = {"1D", "2D", "3D", "cube"};

TextureCache::TextureCache(D3D12CommandProcessor* command_processor,
                           RegisterFile* register_file,
                           SharedMemory* shared_memory)
    : command_processor_(command_processor),
      register_file_(register_file),
      shared_memory_(shared_memory) {}

TextureCache::~TextureCache() { Shutdown(); }

bool TextureCache::Initialize() {
  ClearBindings();
  return true;
}

void TextureCache::Shutdown() { ClearCache(); }

void TextureCache::TextureFetchConstantWritten(uint32_t index) {
  texture_keys_in_sync_ &= ~(1u << index);
}

void TextureCache::BeginFrame() {
  // In case there was a failure creating something in the previous frame, make
  // sure bindings are reset so a new attempt will surely be made if the texture
  // is requested again.
  ClearBindings();
}

void TextureCache::RequestTextures(uint32_t used_vertex_texture_mask,
                                   uint32_t used_pixel_texture_mask) {
  auto command_list = command_processor_->GetCurrentCommandList();
  assert_not_null(command_list);
  if (command_list == nullptr) {
    return;
  }
  auto& regs = *register_file_;

  // Update the texture keys and the textures.
  uint32_t used_texture_mask =
      used_vertex_texture_mask | used_pixel_texture_mask;
  uint32_t index = 0;
  while (xe::bit_scan_forward(used_texture_mask, &index)) {
    uint32_t index_bit = 1u << index;
    used_texture_mask &= ~index_bit;
    if (texture_keys_in_sync_ & index_bit) {
      continue;
    }
    TextureBinding& binding = texture_bindings_[index];

    uint32_t r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + index * 6;
    auto group =
        reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(&regs.values[r]);
    TextureKey old_key = binding.key;
    TextureKeyFromFetchConstant(group->texture_fetch, binding.key,
                                binding.swizzle);
    texture_keys_in_sync_ |= index_bit;
    if (binding.key.IsInvalid() || binding.key == old_key) {
      continue;
    }

    binding.texture = FindOrCreateTexture(binding.key);
    if (binding.texture == nullptr) {
      continue;
    }

    // TODO(Triang3l): Untile the texture.
  }

  // Transition the textures to the needed usage.
  D3D12_RESOURCE_BARRIER barriers[32];
  uint32_t barrier_count = 0;
  used_texture_mask = used_vertex_texture_mask | used_pixel_texture_mask;
  while (xe::bit_scan_forward(used_texture_mask, &index)) {
    uint32_t index_bit = 1u << index;
    used_texture_mask &= ~index_bit;
    Texture* texture = texture_bindings_[index].texture;
    if (texture == nullptr) {
      continue;
    }
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATES(0);
    if (used_vertex_texture_mask & index_bit) {
      state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    if (used_pixel_texture_mask & index_bit) {
      state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (texture->state != state) {
      D3D12_RESOURCE_BARRIER& barrier = barriers[barrier_count];
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Transition.pResource = texture->resource;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barrier.Transition.StateBefore = texture->state;
      barrier.Transition.StateAfter = state;
      ++barrier_count;
      texture->state = state;
    }
  }
  if (barrier_count != 0) {
    command_list->ResourceBarrier(barrier_count, barriers);
  }
}

void TextureCache::ClearCache() {
  ClearBindings();

  // Destroy all the textures.
  for (auto texture_pair : textures_) {
    Texture* texture = texture_pair.second;
    if (texture->resource != nullptr) {
      texture->resource->Release();
    }
    delete texture;
  }
  textures_.clear();
}

void TextureCache::WriteTextureSRV(uint32_t fetch_constant,
                                   TextureDimension shader_dimension,
                                   D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  const TextureBinding& binding = texture_bindings_[fetch_constant];
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;
  desc.Format = host_formats_[uint32_t(binding.key.format)].dxgi_format;
  if (desc.Format == DXGI_FORMAT_UNKNOWN) {
    // A null descriptor must still have a valid format.
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  }
  // XE_GPU_SWIZZLE and D3D12_SHADER_COMPONENT_MAPPING are the same except for
  // one bit.
  desc.Shader4ComponentMapping =
      binding.swizzle |
      D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES;
  ID3D12Resource* resource =
      binding.texture != nullptr ? binding.texture->resource : nullptr;
  switch (shader_dimension) {
    case TextureDimension::k3D:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      desc.Texture3D.MostDetailedMip = 0;
      desc.Texture3D.MipLevels = binding.key.mip_max_level + 1;
      desc.Texture3D.ResourceMinLODClamp = 0.0f;
      if (binding.key.dimension != Dimension::k3D) {
        // Create a null descriptor so it's safe to sample this texture even
        // though it has different dimensions.
        resource = nullptr;
      }
      break;
    case TextureDimension::kCube:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      desc.TextureCube.MostDetailedMip = 0;
      desc.TextureCube.MipLevels = binding.key.mip_max_level + 1;
      desc.TextureCube.ResourceMinLODClamp = 0.0f;
      if (binding.key.dimension != Dimension::kCube) {
        resource = nullptr;
      }
      break;
    default:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      desc.Texture2DArray.MostDetailedMip = 0;
      desc.Texture2DArray.MipLevels = binding.key.mip_max_level + 1;
      desc.Texture2DArray.FirstArraySlice = 0;
      desc.Texture2DArray.ArraySize = binding.key.depth;
      desc.Texture2DArray.PlaneSlice = 0;
      desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
      if (binding.key.dimension == Dimension::k3D ||
          binding.key.dimension == Dimension::kCube) {
        resource = nullptr;
      }
      break;
  }
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  device->CreateShaderResourceView(resource, &desc, handle);
}

void TextureCache::WriteSampler(uint32_t fetch_constant,
                                D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  // TODO(Triang3l): Real samplers instead of this dummy.
  D3D12_SAMPLER_DESC desc;
  desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.MipLODBias = 0.0f;
  desc.MaxAnisotropy = 1;
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  desc.BorderColor[0] = 0.0f;
  desc.BorderColor[1] = 0.0f;
  desc.BorderColor[2] = 0.0f;
  desc.BorderColor[3] = 0.0f;
  desc.MinLOD = 0.0f;
  desc.MaxLOD = 0.0f;
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  device->CreateSampler(&desc, handle);
}

void TextureCache::TextureKeyFromFetchConstant(
    const xenos::xe_gpu_texture_fetch_t& fetch, TextureKey& key_out,
    uint32_t& swizzle_out) {
  // Reset the key and the swizzle.
  key_out.MakeInvalid();
  swizzle_out = xenos::XE_GPU_SWIZZLE_0 | (xenos::XE_GPU_SWIZZLE_0 << 3) |
                (xenos::XE_GPU_SWIZZLE_0 << 6) | (xenos::XE_GPU_SWIZZLE_0 << 9);

  if (fetch.type != 2) {
    XELOGGPU("Texture fetch type is not 2 - ignoring!");
    return;
  }

  // Validate the dimensions, get the size and clamp the maximum mip level.
  Dimension dimension = Dimension(fetch.dimension);
  uint32_t width, height, depth;
  switch (dimension) {
    case Dimension::k1D:
      if (fetch.tiled || fetch.stacked || fetch.packed_mips) {
        assert_always();
        XELOGGPU(
            "1D texture has unsupported properties - ignoring! "
            "Report the game to Xenia developers");
        return;
      }
      width = fetch.size_1d.width + 1;
      if (width > 8192) {
        assert_always();
        XELOGGPU(
            "1D texture is too wide (%u) - ignoring! "
            "Report the game to Xenia developers",
            width);
      }
      height = 1;
      depth = 1;
      break;
    case Dimension::k2D:
      width = fetch.size_stack.width + 1;
      height = fetch.size_stack.height + 1;
      depth = fetch.stacked ? fetch.size_stack.depth + 1 : 1;
      break;
    case Dimension::k3D:
      width = fetch.size_3d.width + 1;
      height = fetch.size_3d.height + 1;
      depth = fetch.size_3d.depth + 1;
      break;
    case Dimension::kCube:
      width = fetch.size_2d.width + 1;
      height = fetch.size_2d.height + 1;
      depth = 6;
      break;
  }
  uint32_t mip_max_level = texture_util::GetSmallestMipLevel(
      width, height, dimension == Dimension::k3D ? depth : 1, false);
  mip_max_level = std::min(mip_max_level, fetch.mip_max_level);

  // Normalize and check the addresses.
  uint32_t base_page = fetch.base_address & 0x1FFFF;
  uint32_t mip_page = mip_max_level != 0 ? fetch.mip_address & 0x1FFFF : 0;
  // Special case for streaming. Games such as Banjo-Kazooie: Nuts & Bolts
  // specify the same address for both the base level and the mips and set
  // mip_min_index to 1 until the texture is actually loaded - this is the way
  // recommended by a GPU hang error message found in game executables. In this
  // case we assume that the base level is not loaded yet.
  // TODO(Triang3l): Ignore the base level completely if min_mip_level is not 0
  // once we start reusing textures with zero base address to reduce memory
  // usage.
  if (base_page == mip_page) {
    base_page = 0;
  }
  if (base_page == 0 && mip_page == 0) {
    // No texture data at all.
    return;
  }

  key_out.base_page = base_page;
  key_out.mip_page = mip_page;
  key_out.dimension = dimension;
  key_out.width = width;
  key_out.height = height;
  key_out.depth = depth;
  key_out.mip_max_level = mip_max_level;
  key_out.tiled = fetch.tiled;
  key_out.packed_mips = fetch.packed_mips;
  key_out.format = TextureFormat(fetch.format);
  key_out.endianness = Endian(fetch.endianness);
  // Get rid of 6 and 7 values (to prevent device losses if the game has
  // something broken) the quick and dirty way - by changing them to 4 and 5.
  swizzle_out = fetch.swizzle &
                ~((fetch.swizzle & (4 | (4 << 3) | (4 << 6) | (4 << 9))) >> 1);
}

void TextureCache::LogTextureKeyAction(TextureKey key, const char* action) {
  XELOGGPU(
      "%s %s %ux%ux%u %s %s texture with %u %spacked mip level%s, "
      "base at 0x%.8X, mips at 0x%.8X", action, key.tiled ? "tiled" : "linear",
      key.width, key.height, key.depth,
      dimension_names_[uint32_t(key.dimension)],
      FormatInfo::Get(key.format)->name, key.mip_max_level + 1,
      key.packed_mips ? "" : "un", key.mip_max_level != 0 ? "s" : "",
      key.base_page << 12, key.mip_page << 12);
}

void TextureCache::LogTextureAction(const Texture& texture,
                                    const char* action) {
  XELOGGPU(
      "%s %s %ux%ux%u %s %s texture with %u %spacked mip level%s, "
      "base at 0x%.8X (size %u), mips at 0x%.8X (size %u)", action,
      texture.key.tiled ? "tiled" : "linear", texture.key.width,
      texture.key.height, texture.key.depth,
      dimension_names_[uint32_t(texture.key.dimension)],
      FormatInfo::Get(texture.key.format)->name,
      texture.key.mip_max_level + 1, texture.key.packed_mips ? "" : "un",
      texture.key.mip_max_level != 0 ? "s" : "", texture.key.base_page << 12,
      texture.base_size, texture.key.mip_page << 12, texture.mip_size);
}

TextureCache::Texture* TextureCache::FindOrCreateTexture(TextureKey key) {
  uint64_t map_key = key.GetMapKey();

  // Try to find an existing texture.
  auto found_range = textures_.equal_range(map_key);
  for (auto iter = found_range.first; iter != found_range.second; ++iter) {
    Texture* found_texture = iter->second;
    if (found_texture->key.bucket_key == key.bucket_key) {
      return found_texture;
    }
  }

  // Create the resource. If failed to create one, don't create a texture object
  // at all so it won't be in indeterminate state.
  D3D12_RESOURCE_DESC desc;
  desc.Format = host_formats_[uint32_t(key.format)].dxgi_format;
  if (desc.Format == DXGI_FORMAT_UNKNOWN) {
    return nullptr;
  }
  if (key.dimension == Dimension::k3D) {
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  } else {
    // 1D textures are treated as 2D for simplicity.
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  }
  desc.Alignment = 0;
  desc.Width = key.width;
  desc.Height = key.height;
  desc.DepthOrArraySize = key.depth;
  desc.MipLevels = key.mip_max_level + 1;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  // Untiling through a buffer instead of using unordered access because copying
  // is not done that often.
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  D3D12_HEAP_PROPERTIES heap_properties = {};
  heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  // Assuming untiling will be the next operation.
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
  ID3D12Resource* resource;
  if (FAILED(device->CreateCommittedResource(
          &heap_properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
          IID_PPV_ARGS(&resource)))) {
    LogTextureKeyAction(key, "Failed to create");
    return nullptr;
  }

  // Create the texture object and add it to the map.
  Texture* texture = new Texture;
  texture->key = key;
  texture->resource = resource;
  texture->state = state;
  uint32_t width_blocks, height_blocks, depth_blocks;
  if (key.base_page != 0) {
    texture_util::GetGuestMipBlocks(key.dimension, key.width, key.height,
                                    key.depth, key.format, 0, width_blocks,
                                    height_blocks, depth_blocks);
    texture->base_size = texture_util::GetGuestMipStorageSize(
        width_blocks, height_blocks, depth_blocks, key.tiled, key.format,
        nullptr);
    texture->base_in_sync = false;
  } else {
    // Never try to upload the base level if there is none.
    texture->base_in_sync = true;
  }
  if (key.mip_page != 0) {
    uint32_t mip_max_storage_level = key.mip_max_level;
    if (key.packed_mips) {
      mip_max_storage_level =
          std::min(mip_max_storage_level,
                   texture_util::GetPackedMipLevel(key.width, key.height));
    }
    texture->mip_size = 0;
    for (uint32_t i = 1; i <= mip_max_storage_level; ++i) {
      texture_util::GetGuestMipBlocks(key.dimension, key.width, key.height,
                                      key.depth, key.format, i, width_blocks,
                                      height_blocks, depth_blocks);
      texture->mip_size += texture_util::GetGuestMipStorageSize(
          width_blocks, height_blocks, depth_blocks, key.tiled, key.format,
          nullptr);
    }
    texture->mips_in_sync = false;
  } else {
    // Never try to upload the mipmaps if there are none.
    texture->mips_in_sync = true;
  }
  textures_.insert(std::make_pair(map_key, texture));
  LogTextureAction(*texture, "Created");

  return texture;
}

void TextureCache::ClearBindings() {
  std::memset(texture_bindings_, 0, sizeof(texture_bindings_));
  texture_keys_in_sync_ = 0;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

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
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "third_party/stb/stb_image_write.h"
#include "xenia/base/assert.h"
#include "xenia/base/autorelease_pool.h"
#include "xenia/base/bit_stream.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/metal/metal_shared_memory.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_128bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_128bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_16bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_16bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_32bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_32bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_64bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_64bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_8bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_8bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_bgrg8_rgb8_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_bgrg8_rgbg8_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_ctx1_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_depth_float_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_depth_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_depth_unorm_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_depth_unorm_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_dxn_rg8_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_dxt1_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_dxt3_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_dxt5_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_gbgr8_grgb8_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_gbgr8_rgb8_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_r4g4b4a4_a4r4g4b4_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_r4g4b4a4_a4r4g4b4_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_r4g4b4a4_b4g4r4a4_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_r4g4b4a4_b4g4r4a4_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_r5g5b5a1_b5g5r5a1_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_r5g5b5a1_b5g5r5a1_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_r5g5b6_b5g6r5_swizzle_rbga_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_r5g6b5_b5g6r5_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_r5g6b5_b5g6r5_scaled_cs.h"
#include "xenia/gpu/texture_conversion.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/gpu/xenos.h"

DEFINE_bool(metal_texture_gpu_load, false,
            "Use GPU texture_load_* shaders for Metal texture loading.", "GPU");
DEFINE_bool(metal_texture_dump_png, false,
            "Dump some loaded Metal textures as PNG to scratch/gpu (debug).",
            "GPU");

namespace xe {
namespace gpu {
namespace metal {
namespace {

struct MetalLoadConstants {
  uint32_t is_tiled_3d_endian_scale;
  uint32_t guest_offset;
  uint32_t guest_pitch_aligned;
  uint32_t guest_z_stride_block_rows_aligned;
  uint32_t size_blocks[3];
  uint32_t padding;  // Pad to 16-byte boundary for uint3 in MSL.
  uint32_t host_offset;
  uint32_t host_pitch;
  uint32_t height_texels;
};

bool AreDimensionsCompatible(xenos::FetchOpDimension shader_dimension,
                             xenos::DataDimension texture_dimension) {
  switch (shader_dimension) {
    case xenos::FetchOpDimension::k3DOrStacked:
      return texture_dimension == xenos::DataDimension::k3D ||
             texture_dimension == xenos::DataDimension::k2DOrStacked;
    case xenos::FetchOpDimension::kCube:
      return texture_dimension == xenos::DataDimension::kCube;
    default:
      return texture_dimension == xenos::DataDimension::k2DOrStacked ||
             texture_dimension == xenos::DataDimension::k1D;
  }
}

}  // namespace

MetalTextureCache::MetalTextureCache(MetalCommandProcessor* command_processor,
                                     const RegisterFile& register_file,
                                     MetalSharedMemory& shared_memory,
                                     uint32_t draw_resolution_scale_x,
                                     uint32_t draw_resolution_scale_y)
    : TextureCache(register_file, shared_memory, draw_resolution_scale_x,
                   draw_resolution_scale_y),
      command_processor_(command_processor) {}

MetalTextureCache::~MetalTextureCache() { Shutdown(); }

bool MetalTextureCache::IsDecompressionNeededForKey(TextureKey key) const {
  switch (key.format) {
    case xenos::TextureFormat::k_DXT1:
    case xenos::TextureFormat::k_DXT2_3:
    case xenos::TextureFormat::k_DXT4_5:
    case xenos::TextureFormat::k_DXN:
    case xenos::TextureFormat::k_CTX1:
      break;
    default:
      return false;
  }

  const FormatInfo* format_info = FormatInfo::Get(key.format);
  if (!format_info) {
    return false;
  }
  uint32_t width = key.GetWidth();
  uint32_t height = key.GetHeight();
  if (!(width & (format_info->block_width - 1)) &&
      !(height & (format_info->block_height - 1))) {
    return false;
  }

  switch (key.format) {
    case xenos::TextureFormat::k_DXT1:
    case xenos::TextureFormat::k_DXT2_3:
    case xenos::TextureFormat::k_DXT4_5:
    case xenos::TextureFormat::k_DXN:
      return true;
    default:
      return false;
  }
}

TextureCache::LoadShaderIndex MetalTextureCache::GetLoadShaderIndexForKey(
    TextureKey key) const {
  bool decompress = IsDecompressionNeededForKey(key);
  switch (key.format) {
    case xenos::TextureFormat::k_8:
    case xenos::TextureFormat::k_8_A:
    case xenos::TextureFormat::k_8_B:
      return kLoadShaderIndex8bpb;
    case xenos::TextureFormat::k_8_8:
      return kLoadShaderIndex16bpb;
    case xenos::TextureFormat::k_1_5_5_5:
      return kLoadShaderIndexR5G5B5A1ToB5G5R5A1;
    case xenos::TextureFormat::k_5_6_5:
      return kLoadShaderIndexR5G6B5ToB5G6R5;
    case xenos::TextureFormat::k_6_5_5:
      return kLoadShaderIndexR5G5B6ToB5G6R5WithRBGASwizzle;
    case xenos::TextureFormat::k_8_8_8_8:
    case xenos::TextureFormat::k_8_8_8_8_A:
      return kLoadShaderIndex32bpb;
    case xenos::TextureFormat::k_2_10_10_10:
      return kLoadShaderIndex32bpb;
    case xenos::TextureFormat::k_4_4_4_4:
      return kLoadShaderIndexRGBA4ToBGRA4;

    case xenos::TextureFormat::k_DXT1:
      return decompress ? kLoadShaderIndexDXT1ToRGBA8 : kLoadShaderIndex64bpb;
    case xenos::TextureFormat::k_DXT2_3:
      return decompress ? kLoadShaderIndexDXT3ToRGBA8 : kLoadShaderIndex128bpb;
    case xenos::TextureFormat::k_DXT4_5:
      return decompress ? kLoadShaderIndexDXT5ToRGBA8 : kLoadShaderIndex128bpb;
    case xenos::TextureFormat::k_DXN:
      return decompress ? kLoadShaderIndexDXNToRG8 : kLoadShaderIndex128bpb;
    case xenos::TextureFormat::k_CTX1:
      return kLoadShaderIndexCTX1;

    case xenos::TextureFormat::k_24_8:
      return kLoadShaderIndexDepthUnorm;
    case xenos::TextureFormat::k_24_8_FLOAT:
      return kLoadShaderIndexDepthFloat;

    default:
      return kLoadShaderIndexUnknown;
  }
}

MTL::PixelFormat MetalTextureCache::GetPixelFormatForKey(TextureKey key) const {
  bool decompress = IsDecompressionNeededForKey(key);
  switch (key.format) {
    case xenos::TextureFormat::k_8:
    case xenos::TextureFormat::k_8_A:
    case xenos::TextureFormat::k_8_B:
    case xenos::TextureFormat::k_DXT5A:
      return MTL::PixelFormatR8Unorm;
    case xenos::TextureFormat::k_8_8:
      return MTL::PixelFormatRG8Unorm;
    case xenos::TextureFormat::k_1_5_5_5:
      return MTL::PixelFormatA1BGR5Unorm;
    case xenos::TextureFormat::k_5_6_5:
    case xenos::TextureFormat::k_6_5_5:
      return MTL::PixelFormatB5G6R5Unorm;
    case xenos::TextureFormat::k_4_4_4_4:
      return MTL::PixelFormatABGR4Unorm;
    case xenos::TextureFormat::k_8_8_8_8:
    case xenos::TextureFormat::k_8_8_8_8_A:
      return MTL::PixelFormatBGRA8Unorm;
    case xenos::TextureFormat::k_2_10_10_10:
      return MTL::PixelFormatRGB10A2Unorm;

    case xenos::TextureFormat::k_DXT1:
      return decompress ? MTL::PixelFormatRGBA8Unorm : MTL::PixelFormatBC1_RGBA;
    case xenos::TextureFormat::k_DXT2_3:
      return decompress ? MTL::PixelFormatRGBA8Unorm : MTL::PixelFormatBC2_RGBA;
    case xenos::TextureFormat::k_DXT4_5:
      return decompress ? MTL::PixelFormatRGBA8Unorm : MTL::PixelFormatBC3_RGBA;
    case xenos::TextureFormat::k_DXN:
      return decompress ? MTL::PixelFormatRG8Unorm
                        : MTL::PixelFormatBC5_RGUnorm;

    case xenos::TextureFormat::k_24_8:
    case xenos::TextureFormat::k_24_8_FLOAT:
      return MTL::PixelFormatR32Float;

    default:
      return MTL::PixelFormatInvalid;
  }
}

bool MetalTextureCache::TryGpuLoadTexture(Texture& texture, bool load_base,
                                          bool load_mips) {
  if (!cvars::metal_texture_gpu_load) {
    return false;
  }

  MetalTexture* metal_texture = static_cast<MetalTexture*>(&texture);
  if (!metal_texture || !metal_texture->metal_texture()) {
    return false;
  }

  const TextureKey& key = texture.key();
  if (key.scaled_resolve) {
    return false;
  }

  const texture_util::TextureGuestLayout& guest_layout = texture.guest_layout();
  xenos::DataDimension dimension = key.dimension;
  bool is_3d = dimension == xenos::DataDimension::k3D;

  uint32_t width = key.GetWidth();
  uint32_t height = key.GetHeight();
  uint32_t depth_or_array_size = key.GetDepthOrArraySize();
  uint32_t depth = is_3d ? depth_or_array_size : 1;
  uint32_t array_size = is_3d ? 1 : depth_or_array_size;

  const FormatInfo* guest_format_info = FormatInfo::Get(key.format);
  if (!guest_format_info) {
    return false;
  }
  uint32_t block_width = guest_format_info->block_width;
  uint32_t block_height = guest_format_info->block_height;
  uint32_t bytes_per_block = guest_format_info->bytes_per_block();

  uint32_t level_first = load_base ? 0 : 1;
  uint32_t level_last = load_mips ? key.mip_max_level : 0;
  if (level_first > level_last) {
    return false;
  }

  bool decompress = IsDecompressionNeededForKey(key);
  TextureCache::LoadShaderIndex load_shader = GetLoadShaderIndexForKey(key);
  if (load_shader == TextureCache::kLoadShaderIndexUnknown) {
    return false;
  }

  MTL::ComputePipelineState* pipeline =
      load_pipelines_[static_cast<size_t>(load_shader)];
  if (!pipeline) {
    return false;
  }

  const TextureCache::LoadShaderInfo& load_shader_info =
      GetLoadShaderInfo(load_shader);

  bool is_block_compressed_format =
      key.format == xenos::TextureFormat::k_DXT1 ||
      key.format == xenos::TextureFormat::k_DXT2_3 ||
      key.format == xenos::TextureFormat::k_DXT4_5 ||
      key.format == xenos::TextureFormat::k_DXN;
  bool host_block_compressed = is_block_compressed_format && !decompress;
  uint32_t host_block_width = host_block_compressed ? block_width : 1;
  uint32_t host_block_height = host_block_compressed ? block_height : 1;
  uint32_t host_x_blocks_per_thread =
      UINT32_C(1) << load_shader_info.guest_x_blocks_per_thread_log2;
  if (!host_block_compressed) {
    host_x_blocks_per_thread *= block_width;
  }

  struct StoredLevelHostLayout {
    bool is_base;
    uint32_t level;
    uint32_t dest_offset_bytes;
    uint32_t slice_size_bytes;
    uint32_t row_pitch_bytes;
    uint32_t height_blocks;
    uint32_t depth_slices;
    uint32_t width_texels;
    uint32_t height_texels;
  };

  uint32_t level_packed = guest_layout.packed_level;
  uint32_t level_stored_first = std::min(level_first, level_packed);
  uint32_t level_stored_last = std::min(level_last, level_packed);

  uint32_t loop_level_first, loop_level_last;
  if (level_packed == 0) {
    loop_level_first = uint32_t(level_first != 0);
    loop_level_last = uint32_t(level_last != 0);
  } else {
    loop_level_first = level_stored_first;
    loop_level_last = level_stored_last;
  }

  std::vector<StoredLevelHostLayout> stored_levels;
  stored_levels.reserve(loop_level_last - loop_level_first + 1);
  uint64_t dest_buffer_size = 0;

  for (uint32_t loop_level = loop_level_first; loop_level <= loop_level_last;
       ++loop_level) {
    bool is_base = loop_level == 0;
    uint32_t level = (level_packed == 0) ? 0 : loop_level;
    const texture_util::TextureGuestLayout::Level& level_guest_layout =
        is_base ? guest_layout.base : guest_layout.mips[level];
    if (!level_guest_layout.level_data_extent_bytes) {
      continue;
    }

    uint32_t level_width, level_height, level_depth;
    if (level == level_packed) {
      level_width = level_guest_layout.x_extent_blocks * block_width;
      level_height = level_guest_layout.y_extent_blocks * block_height;
      level_depth = level_guest_layout.z_extent;
    } else {
      level_width = std::max(width >> level, uint32_t(1));
      level_height = std::max(height >> level, uint32_t(1));
      level_depth = std::max(depth >> level, uint32_t(1));
    }

    uint32_t width_texels_aligned = xe::round_up(level_width, host_block_width);
    uint32_t height_texels_aligned =
        xe::round_up(level_height, host_block_height);
    uint32_t width_blocks = width_texels_aligned / host_block_width;
    uint32_t height_blocks = height_texels_aligned / host_block_height;

    uint32_t row_pitch_bytes =
        xe::align(xe::round_up(width_blocks, host_x_blocks_per_thread) *
                      load_shader_info.bytes_per_host_block,
                  uint32_t(16));
    uint32_t slice_size_bytes =
        xe::align(row_pitch_bytes * height_blocks * level_depth, uint32_t(16));

    StoredLevelHostLayout host_layout = {};
    host_layout.is_base = is_base;
    host_layout.level = level;
    host_layout.dest_offset_bytes = uint32_t(dest_buffer_size);
    host_layout.slice_size_bytes = slice_size_bytes;
    host_layout.row_pitch_bytes = row_pitch_bytes;
    host_layout.height_blocks = height_blocks;
    host_layout.depth_slices = level_depth;
    host_layout.width_texels = level_width;
    host_layout.height_texels = level_height;
    stored_levels.push_back(host_layout);

    dest_buffer_size += uint64_t(slice_size_bytes) * uint64_t(array_size);
  }

  if (stored_levels.empty()) {
    return false;
  }
  if (dest_buffer_size > SIZE_MAX) {
    return false;
  }

  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    return false;
  }
  MTL::CommandQueue* queue = command_processor_->GetMetalCommandQueue();
  if (!queue) {
    return false;
  }

  MetalSharedMemory& metal_shared_memory =
      static_cast<MetalSharedMemory&>(shared_memory());
  MTL::Buffer* shared_buffer = metal_shared_memory.GetBuffer();
  if (!shared_buffer) {
    return false;
  }

  MTL::Buffer* dest_buffer = device->newBuffer(size_t(dest_buffer_size),
                                               MTL::ResourceStorageModeShared);
  if (!dest_buffer) {
    return false;
  }

  uint32_t base_guest_address = key.base_page << 12;
  uint32_t mips_guest_address = key.mip_page << 12;
  size_t constants_size = xe::align(sizeof(MetalLoadConstants), size_t(16));
  size_t dispatch_count = stored_levels.size() * size_t(array_size);
  size_t constants_buffer_size = constants_size * dispatch_count;
  MTL::Buffer* constants_buffer =
      device->newBuffer(constants_buffer_size, MTL::ResourceStorageModeShared);
  if (!constants_buffer) {
    dest_buffer->release();
    return false;
  }

  MTL::CommandBuffer* cmd = queue->commandBuffer();
  if (!cmd) {
    constants_buffer->release();
    dest_buffer->release();
    return false;
  }
  MTL::ComputeCommandEncoder* encoder = cmd->computeCommandEncoder();
  if (!encoder) {
    constants_buffer->release();
    dest_buffer->release();
    return false;
  }

  encoder->setComputePipelineState(pipeline);
  encoder->setBuffer(shared_buffer, 0, 2);

  uint32_t guest_x_blocks_per_group_log2 =
      load_shader_info.GetGuestXBlocksPerGroupLog2();
  MTL::Size threads_per_group =
      MTL::Size::Make(UINT32_C(1) << kLoadGuestXThreadsPerGroupLog2,
                      UINT32_C(1) << kLoadGuestYBlocksPerGroupLog2, 1);

  size_t dispatch_index = 0;
  for (const StoredLevelHostLayout& stored_level : stored_levels) {
    bool is_base_storage = stored_level.is_base;
    const texture_util::TextureGuestLayout::Level& level_guest_layout =
        is_base_storage ? guest_layout.base
                        : guest_layout.mips[stored_level.level];

    uint32_t level_guest_offset =
        is_base_storage ? base_guest_address
                        : (mips_guest_address +
                           guest_layout.mip_offsets_bytes[stored_level.level]);

    uint32_t guest_pitch_aligned = level_guest_layout.row_pitch_bytes;
    if (key.tiled) {
      guest_pitch_aligned /= bytes_per_block;
    }

    uint32_t size_blocks_x =
        (stored_level.width_texels + (block_width - 1)) / block_width;
    uint32_t size_blocks_y =
        (stored_level.height_texels + (block_height - 1)) / block_height;

    uint32_t group_count_x =
        (size_blocks_x +
         ((UINT32_C(1) << guest_x_blocks_per_group_log2) - 1)) >>
        guest_x_blocks_per_group_log2;
    uint32_t group_count_y =
        (size_blocks_y +
         ((UINT32_C(1) << kLoadGuestYBlocksPerGroupLog2) - 1)) >>
        kLoadGuestYBlocksPerGroupLog2;
    MTL::Size threadgroups = MTL::Size::Make(group_count_x, group_count_y,
                                             stored_level.depth_slices);

    for (uint32_t slice = 0; slice < array_size; ++slice) {
      MetalLoadConstants constants = {};
      constants.is_tiled_3d_endian_scale = uint32_t(key.tiled) |
                                           (uint32_t(is_3d) << 1) |
                                           (uint32_t(key.endianness) << 2);
      constants.guest_offset = level_guest_offset;
      if (!is_3d) {
        constants.guest_offset +=
            slice * level_guest_layout.array_slice_stride_bytes;
      }
      constants.guest_pitch_aligned = guest_pitch_aligned;
      constants.guest_z_stride_block_rows_aligned =
          level_guest_layout.z_slice_stride_block_rows;
      constants.size_blocks[0] = size_blocks_x;
      constants.size_blocks[1] = size_blocks_y;
      constants.size_blocks[2] = stored_level.depth_slices;
      constants.padding = 0;
      constants.host_offset = 0;
      constants.host_pitch = stored_level.row_pitch_bytes;
      constants.height_texels = stored_level.height_texels;

      uint8_t* constants_ptr =
          static_cast<uint8_t*>(constants_buffer->contents()) +
          dispatch_index * constants_size;
      std::memcpy(constants_ptr, &constants, sizeof(constants));

      encoder->setBuffer(constants_buffer, dispatch_index * constants_size, 0);
      encoder->setBuffer(dest_buffer,
                         stored_level.dest_offset_bytes +
                             slice * stored_level.slice_size_bytes,
                         1);
      encoder->dispatchThreadgroups(threadgroups, threads_per_group);
      ++dispatch_index;
    }
  }

  encoder->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  uint8_t* dest_data = static_cast<uint8_t*>(dest_buffer->contents());
  if (!dest_data) {
    constants_buffer->release();
    dest_buffer->release();
    return false;
  }

  auto find_stored_level =
      [&](bool is_base_storage,
          uint32_t stored_level) -> const StoredLevelHostLayout* {
    for (const StoredLevelHostLayout& layout : stored_levels) {
      if (layout.is_base == is_base_storage && layout.level == stored_level) {
        return &layout;
      }
    }
    return nullptr;
  };

  MTL::Texture* mtl_texture = metal_texture->metal_texture();
  uint32_t bytes_per_host_block = load_shader_info.bytes_per_host_block;

  for (uint32_t level = level_first; level <= level_last; ++level) {
    uint32_t stored_level = std::min(level, level_packed);
    bool is_base_storage =
        stored_level == 0 && (level_packed != 0 || level == 0);
    const StoredLevelHostLayout* stored_layout =
        find_stored_level(is_base_storage, stored_level);
    if (!stored_layout) {
      continue;
    }

    uint32_t level_width = std::max(width >> level, uint32_t(1));
    uint32_t level_height = std::max(height >> level, uint32_t(1));
    uint32_t level_depth = std::max(depth >> level, uint32_t(1));

    uint32_t upload_width = level_width;
    uint32_t upload_height = level_height;
    if (host_block_compressed) {
      upload_width = xe::round_up(upload_width, host_block_width);
      upload_height = xe::round_up(upload_height, host_block_height);
    }

    uint32_t packed_offset_blocks_x = 0;
    uint32_t packed_offset_blocks_y = 0;
    uint32_t packed_offset_z = 0;
    if (level >= level_packed) {
      texture_util::GetPackedMipOffset(width, height, depth, key.format, level,
                                       packed_offset_blocks_x,
                                       packed_offset_blocks_y, packed_offset_z);
    }

    for (uint32_t slice = 0; slice < array_size; ++slice) {
      const uint8_t* slice_base = dest_data + stored_layout->dest_offset_bytes +
                                  slice * stored_layout->slice_size_bytes;

      const uint8_t* source_ptr = slice_base;
      size_t bytes_per_image =
          size_t(stored_layout->row_pitch_bytes) * stored_layout->height_blocks;
      if (level >= level_packed) {
        if (host_block_compressed) {
          source_ptr += packed_offset_z * bytes_per_image;
          source_ptr += packed_offset_blocks_y * stored_layout->row_pitch_bytes;
          source_ptr += packed_offset_blocks_x * bytes_per_block;
        } else {
          uint32_t packed_offset_texels_x =
              packed_offset_blocks_x * block_width;
          uint32_t packed_offset_texels_y =
              packed_offset_blocks_y * block_height;
          source_ptr += packed_offset_z * bytes_per_image;
          source_ptr += packed_offset_texels_y * stored_layout->row_pitch_bytes;
          source_ptr += packed_offset_texels_x * bytes_per_host_block;
        }
      }

      if (dimension == xenos::DataDimension::k3D) {
        MTL::Region region = MTL::Region::Make3D(0, 0, 0, upload_width,
                                                 upload_height, level_depth);
        mtl_texture->replaceRegion(region, level, 0, source_ptr,
                                   stored_layout->row_pitch_bytes,
                                   bytes_per_image);
      } else {
        MTL::Region region =
            MTL::Region::Make2D(0, 0, upload_width, upload_height);
        mtl_texture->replaceRegion(region, level, slice, source_ptr,
                                   stored_layout->row_pitch_bytes, 0);
      }
    }
  }

  constants_buffer->release();
  dest_buffer->release();

  return true;
}

void MetalTextureCache::DumpTextureToFile(MTL::Texture* texture,
                                          const std::string& filename,
                                          uint32_t width, uint32_t height) {
  if (!texture) {
    XELOGE("DumpTextureToFile: null texture");
    return;
  }

  // Calculate bytes per row
  size_t bytes_per_pixel = 4;  // Assuming BGRA8
  size_t bytes_per_row = width * bytes_per_pixel;

  // Allocate buffer for texture data
  std::vector<uint8_t> data(bytes_per_row * height);

  // Read texture data from GPU
  MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
  texture->getBytes(data.data(), bytes_per_row, region, 0);

  // Convert BGRA to RGBA for stb_image_write
  for (size_t i = 0; i < data.size(); i += 4) {
    std::swap(data[i], data[i + 2]);  // Swap B and R
  }

  // Write PNG file
  if (stbi_write_png(filename.c_str(), width, height, 4, data.data(),
                     bytes_per_row)) {
    XELOGI("Dumped texture to: {}", filename);
  } else {
    XELOGE("Failed to write texture to: {}", filename);
  }
}

bool MetalTextureCache::Initialize() {
  SCOPE_profile_cpu_f("gpu");
  XE_SCOPED_AUTORELEASE_POOL("MetalTextureCache::Initialize");

  // Create null textures following existing factory pattern
  null_texture_2d_ = CreateNullTexture2D();
  null_texture_3d_ = CreateNullTexture3D();
  null_texture_cube_ = CreateNullTextureCube();

  if (!null_texture_2d_ || !null_texture_3d_ || !null_texture_cube_) {
    XELOGE("Failed to create null textures");
    return false;
  }

  if (!InitializeLoadPipelines()) {
    XELOGE("Metal texture cache: Failed to initialize texture_load pipelines");
    return false;
  }

  XELOGD(
      "Metal texture cache: Initialized successfully (null textures + GPU "
      "texture_load pipelines)");

  return true;
}

bool MetalTextureCache::InitializeLoadPipelines() {
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    return false;
  }

  NS::Error* error = nullptr;

  auto create_pipeline_from_metallib =
      [&](const uint8_t* data, size_t size) -> MTL::ComputePipelineState* {
    if (!data || !size) {
      return nullptr;
    }
    dispatch_data_t dispatch_data = dispatch_data_create(
        data, size, nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    MTL::Library* lib = device->newLibrary(dispatch_data, &error);
    dispatch_release(dispatch_data);
    if (!lib) {
      XELOGE("MetalTextureCache: failed to create texture_load library: {}",
             error ? error->localizedDescription()->utf8String() : "unknown");
      return nullptr;
    }
    NS::String* fn_name =
        NS::String::string("xesl_entry", NS::UTF8StringEncoding);
    MTL::Function* fn = lib->newFunction(fn_name);
    if (!fn) {
      XELOGE(
          "MetalTextureCache: texture_load metallib missing xesl_entry "
          "function");
      lib->release();
      return nullptr;
    }
    MTL::ComputePipelineState* pipeline =
        device->newComputePipelineState(fn, &error);
    fn->release();
    lib->release();
    if (!pipeline) {
      XELOGE("MetalTextureCache: failed to create texture_load pipeline: {}",
             error ? error->localizedDescription()->utf8String() : "unknown");
      return nullptr;
    }
    return pipeline;
  };

  auto init_pipeline = [&](TextureCache::LoadShaderIndex index,
                           const uint8_t* data, size_t size) -> void {
    load_pipelines_[index] = create_pipeline_from_metallib(data, size);
  };
  auto init_pipeline_scaled = [&](TextureCache::LoadShaderIndex index,
                                  const uint8_t* data, size_t size) -> void {
    load_pipelines_scaled_[index] = create_pipeline_from_metallib(data, size);
  };

  init_pipeline(TextureCache::kLoadShaderIndex8bpb,
                texture_load_8bpb_cs_metallib,
                sizeof(texture_load_8bpb_cs_metallib));
  init_pipeline_scaled(TextureCache::kLoadShaderIndex8bpb,
                       texture_load_8bpb_scaled_cs_metallib,
                       sizeof(texture_load_8bpb_scaled_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndex16bpb,
                texture_load_16bpb_cs_metallib,
                sizeof(texture_load_16bpb_cs_metallib));
  init_pipeline_scaled(TextureCache::kLoadShaderIndex16bpb,
                       texture_load_16bpb_scaled_cs_metallib,
                       sizeof(texture_load_16bpb_scaled_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndex32bpb,
                texture_load_32bpb_cs_metallib,
                sizeof(texture_load_32bpb_cs_metallib));
  init_pipeline_scaled(TextureCache::kLoadShaderIndex32bpb,
                       texture_load_32bpb_scaled_cs_metallib,
                       sizeof(texture_load_32bpb_scaled_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndex64bpb,
                texture_load_64bpb_cs_metallib,
                sizeof(texture_load_64bpb_cs_metallib));
  init_pipeline_scaled(TextureCache::kLoadShaderIndex64bpb,
                       texture_load_64bpb_scaled_cs_metallib,
                       sizeof(texture_load_64bpb_scaled_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndex128bpb,
                texture_load_128bpb_cs_metallib,
                sizeof(texture_load_128bpb_cs_metallib));
  init_pipeline_scaled(TextureCache::kLoadShaderIndex128bpb,
                       texture_load_128bpb_scaled_cs_metallib,
                       sizeof(texture_load_128bpb_scaled_cs_metallib));

  init_pipeline(TextureCache::kLoadShaderIndexR5G5B5A1ToB5G5R5A1,
                texture_load_r5g5b5a1_b5g5r5a1_cs_metallib,
                sizeof(texture_load_r5g5b5a1_b5g5r5a1_cs_metallib));
  init_pipeline_scaled(
      TextureCache::kLoadShaderIndexR5G5B5A1ToB5G5R5A1,
      texture_load_r5g5b5a1_b5g5r5a1_scaled_cs_metallib,
      sizeof(texture_load_r5g5b5a1_b5g5r5a1_scaled_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexR5G6B5ToB5G6R5,
                texture_load_r5g6b5_b5g6r5_cs_metallib,
                sizeof(texture_load_r5g6b5_b5g6r5_cs_metallib));
  init_pipeline_scaled(TextureCache::kLoadShaderIndexR5G6B5ToB5G6R5,
                       texture_load_r5g6b5_b5g6r5_scaled_cs_metallib,
                       sizeof(texture_load_r5g6b5_b5g6r5_scaled_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexR5G5B6ToB5G6R5WithRBGASwizzle,
                texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs_metallib,
                sizeof(texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs_metallib));
  init_pipeline_scaled(
      TextureCache::kLoadShaderIndexR5G5B6ToB5G6R5WithRBGASwizzle,
      texture_load_r5g5b6_b5g6r5_swizzle_rbga_scaled_cs_metallib,
      sizeof(texture_load_r5g5b6_b5g6r5_swizzle_rbga_scaled_cs_metallib));

  init_pipeline(TextureCache::kLoadShaderIndexRGBA4ToBGRA4,
                texture_load_r4g4b4a4_b4g4r4a4_cs_metallib,
                sizeof(texture_load_r4g4b4a4_b4g4r4a4_cs_metallib));
  init_pipeline_scaled(
      TextureCache::kLoadShaderIndexRGBA4ToBGRA4,
      texture_load_r4g4b4a4_b4g4r4a4_scaled_cs_metallib,
      sizeof(texture_load_r4g4b4a4_b4g4r4a4_scaled_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexRGBA4ToARGB4,
                texture_load_r4g4b4a4_a4r4g4b4_cs_metallib,
                sizeof(texture_load_r4g4b4a4_a4r4g4b4_cs_metallib));
  init_pipeline_scaled(
      TextureCache::kLoadShaderIndexRGBA4ToARGB4,
      texture_load_r4g4b4a4_a4r4g4b4_scaled_cs_metallib,
      sizeof(texture_load_r4g4b4a4_a4r4g4b4_scaled_cs_metallib));

  init_pipeline(TextureCache::kLoadShaderIndexGBGR8ToGRGB8,
                texture_load_gbgr8_grgb8_cs_metallib,
                sizeof(texture_load_gbgr8_grgb8_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexGBGR8ToRGB8,
                texture_load_gbgr8_rgb8_cs_metallib,
                sizeof(texture_load_gbgr8_rgb8_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexBGRG8ToRGBG8,
                texture_load_bgrg8_rgbg8_cs_metallib,
                sizeof(texture_load_bgrg8_rgbg8_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexBGRG8ToRGB8,
                texture_load_bgrg8_rgb8_cs_metallib,
                sizeof(texture_load_bgrg8_rgb8_cs_metallib));

  init_pipeline(TextureCache::kLoadShaderIndexDXT1ToRGBA8,
                texture_load_dxt1_rgba8_cs_metallib,
                sizeof(texture_load_dxt1_rgba8_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexDXT3ToRGBA8,
                texture_load_dxt3_rgba8_cs_metallib,
                sizeof(texture_load_dxt3_rgba8_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexDXT5ToRGBA8,
                texture_load_dxt5_rgba8_cs_metallib,
                sizeof(texture_load_dxt5_rgba8_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexDXNToRG8,
                texture_load_dxn_rg8_cs_metallib,
                sizeof(texture_load_dxn_rg8_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexCTX1,
                texture_load_ctx1_cs_metallib,
                sizeof(texture_load_ctx1_cs_metallib));

  init_pipeline(TextureCache::kLoadShaderIndexDepthUnorm,
                texture_load_depth_unorm_cs_metallib,
                sizeof(texture_load_depth_unorm_cs_metallib));
  init_pipeline_scaled(TextureCache::kLoadShaderIndexDepthUnorm,
                       texture_load_depth_unorm_scaled_cs_metallib,
                       sizeof(texture_load_depth_unorm_scaled_cs_metallib));
  init_pipeline(TextureCache::kLoadShaderIndexDepthFloat,
                texture_load_depth_float_cs_metallib,
                sizeof(texture_load_depth_float_cs_metallib));
  init_pipeline_scaled(TextureCache::kLoadShaderIndexDepthFloat,
                       texture_load_depth_float_scaled_cs_metallib,
                       sizeof(texture_load_depth_float_scaled_cs_metallib));

  // Require at least the common loaders.
  return load_pipelines_[TextureCache::kLoadShaderIndex32bpb] != nullptr &&
         load_pipelines_[TextureCache::kLoadShaderIndex16bpb] != nullptr &&
         load_pipelines_[TextureCache::kLoadShaderIndex8bpb] != nullptr;
}

void MetalTextureCache::Shutdown() {
  SCOPE_profile_cpu_f("gpu");

  ClearCache();

  for (size_t i = 0; i < kLoadShaderCount; ++i) {
    if (load_pipelines_[i]) {
      load_pipelines_[i]->release();
      load_pipelines_[i] = nullptr;
    }
    if (load_pipelines_scaled_[i]) {
      load_pipelines_scaled_[i]->release();
      load_pipelines_scaled_[i] = nullptr;
    }
  }

  // Follow existing shutdown pattern - explicit null checks and release
  if (null_texture_2d_) {
    null_texture_2d_->release();
    null_texture_2d_ = nullptr;
  }
  if (null_texture_3d_) {
    null_texture_3d_->release();
    null_texture_3d_ = nullptr;
  }
  if (null_texture_cube_) {
    null_texture_cube_->release();
    null_texture_cube_ = nullptr;
  }

  XELOGD("Metal texture cache: Shutdown complete");
}

void MetalTextureCache::ClearCache() {
  SCOPE_profile_cpu_f("gpu");

  for (auto& sampler_pair : sampler_cache_) {
    if (sampler_pair.second) {
      sampler_pair.second->release();
    }
  }
  sampler_cache_.clear();

  XELOGD("Metal texture cache: Cache cleared");
}

// Legacy method - kept for compatibility but now uses base class texture
// management
bool MetalTextureCache::UploadTexture2D(const TextureInfo& texture_info) {
  XELOGD("UploadTexture2D: Legacy method called - delegating to base class");
  // The base class RequestTextures will handle texture creation and loading
  return true;
}

// Legacy method - kept for compatibility but now uses base class texture
// management
bool MetalTextureCache::UploadTextureCube(const TextureInfo& texture_info) {
  XELOGD("UploadTextureCube: Legacy method called - delegating to base class");
  // The base class RequestTextures will handle texture creation and loading
  return true;
}

MTL::Texture* MetalTextureCache::GetTexture2D(const TextureInfo& texture_info) {
  // Legacy method - now uses base class texture management
  // This method is kept for compatibility but should be migrated to use
  // the standard texture binding flow via RequestTextures
  XELOGD("GetTexture2D: Legacy method called - use RequestTextures instead");
  return null_texture_2d_;
}

MTL::Texture* MetalTextureCache::GetTextureCube(
    const TextureInfo& texture_info) {
  // Legacy method - now uses base class texture management
  // This method is kept for compatibility but should be migrated to use
  // the standard texture binding flow via RequestTextures
  XELOGD("GetTextureCube: Legacy method called - use RequestTextures instead");
  return null_texture_cube_;
}

MTL::PixelFormat MetalTextureCache::ConvertXenosFormat(
    xenos::TextureFormat format, xenos::Endian endian) {
  // Convert Xbox 360 texture formats to Metal pixel formats
  // This is a simplified mapping - the full implementation would handle all
  // Xbox 360 formats
  switch (format) {
    case xenos::TextureFormat::k_8_8_8_8:
      // Xbox 360 k_8_8_8_8 is stored as ARGB in big-endian. After k_8in32
      // endian swap on little-endian, we get BGRA byte order.
      // Metal's BGRA8Unorm matches this layout directly.
      return MTL::PixelFormatBGRA8Unorm;
    case xenos::TextureFormat::k_1_5_5_5:
      return MTL::PixelFormatA1BGR5Unorm;
    case xenos::TextureFormat::k_5_6_5:
      return MTL::PixelFormatB5G6R5Unorm;
    case xenos::TextureFormat::k_8:
      return MTL::PixelFormatR8Unorm;
    case xenos::TextureFormat::k_8_8:
      return MTL::PixelFormatRG8Unorm;
    case xenos::TextureFormat::k_DXT1:
      return MTL::PixelFormatBC1_RGBA;
    case xenos::TextureFormat::k_DXT2_3:
      return MTL::PixelFormatBC2_RGBA;
    case xenos::TextureFormat::k_DXT4_5:
      return MTL::PixelFormatBC3_RGBA;
    case xenos::TextureFormat::k_16_16_16_16:
      return MTL::PixelFormatRGBA16Unorm;
    case xenos::TextureFormat::k_2_10_10_10:
      return MTL::PixelFormatRGB10A2Unorm;
    case xenos::TextureFormat::k_16_FLOAT:
      return MTL::PixelFormatR16Float;
    case xenos::TextureFormat::k_16_16_FLOAT:
      return MTL::PixelFormatRG16Float;
    case xenos::TextureFormat::k_16_16_16_16_FLOAT:
      return MTL::PixelFormatRGBA16Float;
    case xenos::TextureFormat::k_32_FLOAT:
      return MTL::PixelFormatR32Float;
    case xenos::TextureFormat::k_32_32_FLOAT:
      return MTL::PixelFormatRG32Float;
    case xenos::TextureFormat::k_32_32_32_32_FLOAT:
      return MTL::PixelFormatRGBA32Float;
    case xenos::TextureFormat::k_DXN:  // BC5
      return MTL::PixelFormatBC5_RGUnorm;
    default:
      // Don't log here - caller will log the error with more context
      return MTL::PixelFormatInvalid;
  }
}

MTL::Texture* MetalTextureCache::CreateTexture2D(uint32_t width,
                                                 uint32_t height,
                                                 uint32_t array_length,
                                                 MTL::PixelFormat format,
                                                 uint32_t mip_levels) {
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE(
        "Metal texture cache: Failed to get Metal device from command "
        "processor");
    return nullptr;
  }

  // Always create 2D array textures (even with a single layer) so that the
  // Metal texture type matches the shader expectation of texture2d_array,
  // mirroring the D3D12 backend which uses TEXTURE2DARRAY SRVs for 1D/2D
  // textures.
  array_length = std::max(array_length, 1u);

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  descriptor->setTextureType(MTL::TextureType2DArray);
  descriptor->setPixelFormat(format);
  descriptor->setWidth(width);
  descriptor->setHeight(std::max(height, 1u));
  descriptor->setDepth(1);
  descriptor->setArrayLength(array_length);
  descriptor->setMipmapLevelCount(mip_levels);
  descriptor->setUsage(MTL::TextureUsageShaderRead);
  descriptor->setStorageMode(MTL::StorageModeShared);

  MTL::Texture* texture = device->newTexture(descriptor);

  descriptor->release();

  if (!texture) {
    XELOGE(
        "Metal texture cache: Failed to create 2D array texture {}x{} (layers "
        "{})",
        width, height, array_length);
    return nullptr;
  }

  return texture;
}

MTL::Texture* MetalTextureCache::CreateTexture3D(uint32_t width,
                                                 uint32_t height,
                                                 uint32_t depth,
                                                 MTL::PixelFormat format,
                                                 uint32_t mip_levels) {
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE(
        "Metal texture cache: Failed to get Metal device from command "
        "processor");
    return nullptr;
  }

  depth = std::max(depth, 1u);

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  descriptor->setTextureType(MTL::TextureType3D);
  descriptor->setPixelFormat(format);
  descriptor->setWidth(width);
  descriptor->setHeight(std::max(height, 1u));
  descriptor->setDepth(depth);
  descriptor->setArrayLength(1);
  descriptor->setMipmapLevelCount(mip_levels);
  descriptor->setUsage(MTL::TextureUsageShaderRead);
  descriptor->setStorageMode(MTL::StorageModeShared);

  MTL::Texture* texture = device->newTexture(descriptor);

  descriptor->release();

  if (!texture) {
    XELOGE("Metal texture cache: Failed to create 3D texture {}x{}x{}", width,
           height, depth);
    return nullptr;
  }

  return texture;
}

MTL::Texture* MetalTextureCache::CreateTextureCube(uint32_t width,
                                                   MTL::PixelFormat format,
                                                   uint32_t mip_levels,
                                                   uint32_t cube_count) {
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE(
        "Metal texture cache: Failed to get Metal device from command "
        "processor");
    return nullptr;
  }

  cube_count = std::max(cube_count, 1u);

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  bool is_array = cube_count > 1;
  descriptor->setTextureType(is_array ? MTL::TextureTypeCubeArray
                                      : MTL::TextureTypeCube);
  descriptor->setPixelFormat(format);
  descriptor->setWidth(width);
  descriptor->setHeight(width);  // Cube faces are square
  descriptor->setDepth(1);
  descriptor->setArrayLength(cube_count);
  descriptor->setMipmapLevelCount(mip_levels);
  descriptor->setUsage(MTL::TextureUsageShaderRead);
  descriptor->setStorageMode(MTL::StorageModeShared);

  MTL::Texture* texture = device->newTexture(descriptor);

  descriptor->release();

  if (!texture) {
    XELOGE("Metal texture cache: Failed to create cube{} texture {} (count {})",
           is_array ? " array" : "", width, cube_count);
    return nullptr;
  }

  return texture;
}

bool MetalTextureCache::UpdateTexture2D(MTL::Texture* texture,
                                        const TextureInfo& texture_info) {
  // Legacy method - memory access will be handled by base class during
  // RequestTextures For now, return success to avoid build errors. Real texture
  // loading happens in LoadTextureDataFromResidentMemoryImpl which is called by
  // the base class.
  XELOGD(
      "UpdateTexture2D: Legacy method called - base class handles memory "
      "access");
  return true;
}

bool MetalTextureCache::UpdateTextureCube(MTL::Texture* texture,
                                          const TextureInfo& texture_info) {
  // Legacy method - memory access will be handled by base class during
  // RequestTextures For now, return success to avoid build errors. Real texture
  // loading happens in LoadTextureDataFromResidentMemoryImpl which is called by
  // the base class.
  XELOGD(
      "UpdateTextureCube: Legacy method called - base class handles memory "
      "access");
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

MTL::Texture* MetalTextureCache::CreateDebugTexture(uint32_t width,
                                                    uint32_t height) {
  MTL::Texture* texture =
      CreateTexture2D(width, height, 1, MTL::PixelFormatRGBA8Unorm);
  if (!texture) {
    XELOGE("Failed to create debug texture");
    return nullptr;
  }

  // Create checkerboard pattern data
  std::vector<uint32_t> pixels(width * height);
  uint32_t checker_size = 32;  // Size of each checker square

  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      bool is_white = ((x / checker_size) + (y / checker_size)) % 2 == 0;
      // Use green/purple pattern to distinguish from pink error color
      if (is_white) {
        pixels[y * width + x] = 0xFF00FF00;  // Green (RGBA)
      } else {
        pixels[y * width + x] = 0xFF8000FF;  // Purple (RGBA)
      }
    }
  }

  // Upload texture data
  MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
  texture->replaceRegion(region, 0, pixels.data(), width * 4);

  XELOGI("Created debug checkerboard texture {}x{} (green/purple pattern)",
         width, height);
  return texture;
}

MTL::Texture* MetalTextureCache::CreateNullTexture2D() {
  SCOPE_profile_cpu_f("gpu");

  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal texture cache: Failed to get Metal device for null texture");
    return nullptr;
  }

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  // Null 2D textures are created as 2D arrays with a single layer so they can
  // be bound wherever shaders expect texture2d_array.
  descriptor->setTextureType(MTL::TextureType2DArray);
  descriptor->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  descriptor->setWidth(1);
  descriptor->setHeight(1);
  descriptor->setArrayLength(1);
  descriptor->setUsage(MTL::TextureUsageShaderRead);
  descriptor->setStorageMode(MTL::StorageModeShared);

  MTL::Texture* texture = device->newTexture(descriptor);
  descriptor->release();  // Immediate release following pattern

  if (texture) {
    // Initialize with black color (0xFF000000 for RGBA8)
    uint32_t default_color = 0xFF000000;
    MTL::Region region = MTL::Region::Make2D(0, 0, 1, 1);
    texture->replaceRegion(region, 0, &default_color, 4);
    XELOGI("Created null 2D texture (1x1 black)");
  } else {
    XELOGE("Failed to create null 2D texture");
  }

  return texture;  // No retain needed - newTexture returns retained object
}

MTL::Texture* MetalTextureCache::CreateNullTexture3D() {
  SCOPE_profile_cpu_f("gpu");

  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE(
        "Metal texture cache: Failed to get Metal device for null 3D texture");
    return nullptr;
  }

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  descriptor->setTextureType(MTL::TextureType3D);
  descriptor->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  descriptor->setWidth(1);
  descriptor->setHeight(1);
  descriptor->setDepth(1);
  descriptor->setUsage(MTL::TextureUsageShaderRead);
  descriptor->setStorageMode(MTL::StorageModeShared);

  MTL::Texture* texture = device->newTexture(descriptor);
  descriptor->release();  // Immediate release following pattern

  if (texture) {
    // Initialize with black color (0xFF000000 for RGBA8)
    uint32_t default_color = 0xFF000000;
    MTL::Region region = MTL::Region::Make3D(0, 0, 0, 1, 1, 1);
    texture->replaceRegion(region, 0, 0, &default_color, 4, 4);
    XELOGI("Created null 3D texture (1x1x1 black)");
  } else {
    XELOGE("Failed to create null 3D texture");
  }

  return texture;  // No retain needed - newTexture returns retained object
}

MTL::Texture* MetalTextureCache::CreateNullTextureCube() {
  SCOPE_profile_cpu_f("gpu");

  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE(
        "Metal texture cache: Failed to get Metal device for null cube "
        "texture");
    return nullptr;
  }

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  descriptor->setTextureType(MTL::TextureTypeCube);
  descriptor->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  descriptor->setWidth(1);
  descriptor->setHeight(1);
  descriptor->setDepth(1);
  descriptor->setArrayLength(1);
  descriptor->setUsage(MTL::TextureUsageShaderRead);
  descriptor->setStorageMode(MTL::StorageModeShared);

  MTL::Texture* texture = device->newTexture(descriptor);
  descriptor->release();  // Immediate release following pattern

  if (texture) {
    // Initialize all 6 faces with black color (0xFF000000 for RGBA8)
    uint32_t default_color = 0xFF000000;
    MTL::Region region = MTL::Region::Make2D(0, 0, 1, 1);
    for (uint32_t face = 0; face < 6; ++face) {
      texture->replaceRegion(region, 0, face, &default_color, 4, 0);
    }
    XELOGI("Created null cube texture (1x1 black on all 6 faces)");
  } else {
    XELOGE("Failed to create null cube texture");
  }

  return texture;  // No retain needed - newTexture returns retained object
}

// RequestTextures override - integrates with standard texture binding pipeline
void MetalTextureCache::RequestTextures(uint32_t used_texture_mask) {
  SCOPE_profile_cpu_f("gpu");

  // Call base class implementation first
  TextureCache::RequestTextures(used_texture_mask);

  // Process each requested texture in the mask
  uint32_t index;
  while (xe::bit_scan_forward(used_texture_mask, &index)) {
    used_texture_mask &= ~(uint32_t(1) << index);

    const TextureBinding* binding = GetValidTextureBinding(index);
    if (binding && binding->texture) {
      // Get the MetalTexture from the base class texture
      MetalTexture* metal_texture =
          static_cast<MetalTexture*>(binding->texture);
      if (metal_texture && metal_texture->metal_texture()) {
        XELOGD("RequestTextures: Texture at index {} ready for binding", index);
      }
    } else {
      XELOGW("RequestTextures: No valid texture binding at index {}", index);
    }
  }
}

MTL::Texture* MetalTextureCache::GetTextureForBinding(
    uint32_t fetch_constant, xenos::FetchOpDimension dimension,
    bool is_signed) {
  const TextureBinding* binding = GetValidTextureBinding(fetch_constant);
  if (!binding) {
    XELOGW("GetTextureForBinding: No valid binding for fetch constant {}",
           fetch_constant);
    return nullptr;
  }
  XELOGI(
      "GetTextureForBinding: Found binding for fetch {} - texture={}, "
      "key.valid={}",
      fetch_constant, binding->texture != nullptr, binding->key.is_valid);

  if (!AreDimensionsCompatible(dimension, binding->key.dimension)) {
    return nullptr;
  }

  Texture* texture = nullptr;
  if (is_signed) {
    bool needs_signed_components =
        texture_util::IsAnySignSigned(binding->swizzled_signs);
    if (needs_signed_components &&
        IsSignedVersionSeparateForFormat(binding->key)) {
      texture =
          binding->texture_signed ? binding->texture_signed : binding->texture;
    } else {
      texture = binding->texture;
    }
  } else {
    bool has_unsigned_components =
        texture_util::IsAnySignNotSigned(binding->swizzled_signs);
    if (has_unsigned_components) {
      texture = binding->texture;
    } else if (!has_unsigned_components && binding->texture_signed != nullptr) {
      texture = binding->texture_signed;
    } else {
      texture = binding->texture;
    }
  }

  if (!texture) {
    XELOGW("GetTextureForBinding: No texture object for fetch {}",
           fetch_constant);
    return nullptr;
  }

  texture->MarkAsUsed();
  auto* metal_texture = static_cast<MetalTexture*>(texture);
  MTL::Texture* result =
      metal_texture ? metal_texture->metal_texture() : nullptr;
  XELOGI("GetTextureForBinding: fetch {} -> MetalTexture={}, MTL::Texture={}",
         fetch_constant, metal_texture != nullptr, result != nullptr);
  return result;
}

MetalTextureCache::SamplerParameters MetalTextureCache::GetSamplerParameters(
    const DxbcShader::SamplerBinding& binding) const {
  const RegisterFile& regs = register_file();
  xenos::xe_gpu_texture_fetch_t fetch =
      regs.GetTextureFetch(binding.fetch_constant);

  SamplerParameters parameters;

  xenos::ClampMode fetch_clamp_x, fetch_clamp_y, fetch_clamp_z;
  texture_util::GetClampModesForDimension(fetch, fetch_clamp_x, fetch_clamp_y,
                                          fetch_clamp_z);
  parameters.clamp_x = NormalizeClampMode(fetch_clamp_x);
  parameters.clamp_y = NormalizeClampMode(fetch_clamp_y);
  parameters.clamp_z = NormalizeClampMode(fetch_clamp_z);

  if (xenos::ClampModeUsesBorder(parameters.clamp_x) ||
      xenos::ClampModeUsesBorder(parameters.clamp_y) ||
      xenos::ClampModeUsesBorder(parameters.clamp_z)) {
    parameters.border_color = fetch.border_color;
  } else {
    parameters.border_color = xenos::BorderColor::k_ABGR_Black;
  }

  uint32_t mip_min_level;
  texture_util::GetSubresourcesFromFetchConstant(fetch, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr,
                                                 &mip_min_level, nullptr);
  parameters.mip_min_level = mip_min_level;

  xenos::AnisoFilter aniso_filter =
      binding.aniso_filter == xenos::AnisoFilter::kUseFetchConst
          ? fetch.aniso_filter
          : binding.aniso_filter;
  aniso_filter = std::min(aniso_filter, xenos::AnisoFilter::kMax_16_1);
  parameters.aniso_filter = aniso_filter;

  xenos::TextureFilter mip_filter =
      binding.mip_filter == xenos::TextureFilter::kUseFetchConst
          ? fetch.mip_filter
          : binding.mip_filter;

  if (aniso_filter != xenos::AnisoFilter::kDisabled) {
    parameters.mag_linear = 1;
    parameters.min_linear = 1;
    parameters.mip_linear = 1;
  } else {
    xenos::TextureFilter mag_filter =
        binding.mag_filter == xenos::TextureFilter::kUseFetchConst
            ? fetch.mag_filter
            : binding.mag_filter;
    parameters.mag_linear = mag_filter == xenos::TextureFilter::kLinear;

    xenos::TextureFilter min_filter =
        binding.min_filter == xenos::TextureFilter::kUseFetchConst
            ? fetch.min_filter
            : binding.min_filter;
    parameters.min_linear = min_filter == xenos::TextureFilter::kLinear;

    parameters.mip_linear = mip_filter == xenos::TextureFilter::kLinear;
  }

  parameters.mip_base_map =
      mip_filter == xenos::TextureFilter::kBaseMap ? 1 : 0;

  return parameters;
}

MTL::SamplerState* MetalTextureCache::GetOrCreateSampler(
    SamplerParameters parameters) {
  auto it = sampler_cache_.find(parameters.value);
  if (it != sampler_cache_.end()) {
    return it->second;
  }

  MTL::SamplerDescriptor* desc = MTL::SamplerDescriptor::alloc()->init();
  if (!desc) {
    XELOGE("Failed to allocate Metal sampler descriptor");
    return nullptr;
  }

  auto convert_clamp = [](xenos::ClampMode mode) {
    switch (mode) {
      case xenos::ClampMode::kRepeat:
        return MTL::SamplerAddressModeRepeat;
      case xenos::ClampMode::kMirroredRepeat:
        return MTL::SamplerAddressModeMirrorRepeat;
      case xenos::ClampMode::kClampToEdge:
        return MTL::SamplerAddressModeClampToEdge;
      case xenos::ClampMode::kMirrorClampToEdge:
        return MTL::SamplerAddressModeMirrorClampToEdge;
      case xenos::ClampMode::kClampToBorder:
        return MTL::SamplerAddressModeClampToBorderColor;
      default:
        return MTL::SamplerAddressModeClampToEdge;
    }
  };

  if (parameters.aniso_filter != xenos::AnisoFilter::kDisabled) {
    desc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    desc->setMagFilter(MTL::SamplerMinMagFilterLinear);
    desc->setMipFilter(MTL::SamplerMipFilterLinear);
    desc->setMaxAnisotropy(1u << (uint32_t(parameters.aniso_filter) - 1));
  } else {
    desc->setMinFilter(parameters.min_linear ? MTL::SamplerMinMagFilterLinear
                                             : MTL::SamplerMinMagFilterNearest);
    desc->setMagFilter(parameters.mag_linear ? MTL::SamplerMinMagFilterLinear
                                             : MTL::SamplerMinMagFilterNearest);
    desc->setMipFilter(parameters.mip_linear ? MTL::SamplerMipFilterLinear
                                             : MTL::SamplerMipFilterNearest);
    desc->setMaxAnisotropy(1);
  }

  desc->setSAddressMode(convert_clamp(xenos::ClampMode(parameters.clamp_x)));
  desc->setTAddressMode(convert_clamp(xenos::ClampMode(parameters.clamp_y)));
  desc->setRAddressMode(convert_clamp(xenos::ClampMode(parameters.clamp_z)));

  switch (parameters.border_color) {
    case xenos::BorderColor::k_ABGR_White:
      desc->setBorderColor(MTL::SamplerBorderColorOpaqueWhite);
      break;
    case xenos::BorderColor::k_ABGR_Black:
      desc->setBorderColor(MTL::SamplerBorderColorTransparentBlack);
      break;
    default:
      desc->setBorderColor(MTL::SamplerBorderColorOpaqueBlack);
      break;
  }

  desc->setLodMinClamp(static_cast<float>(parameters.mip_min_level));
  float max_lod = parameters.mip_base_map
                      ? static_cast<float>(parameters.mip_min_level)
                      : FLT_MAX;
  if (parameters.mip_base_map &&
      parameters.aniso_filter == xenos::AnisoFilter::kDisabled &&
      !parameters.mip_linear) {
    max_lod += 0.25f;
  }
  desc->setLodMaxClamp(max_lod);
  desc->setLodAverage(false);
  desc->setSupportArgumentBuffers(true);

  MTL::SamplerState* sampler_state =
      command_processor_->GetMetalDevice()->newSamplerState(desc);
  desc->release();

  if (!sampler_state) {
    XELOGE("Failed to create Metal sampler state");
    return nullptr;
  }

  sampler_cache_.emplace(parameters.value, sampler_state);
  return sampler_state;
}

xenos::ClampMode MetalTextureCache::NormalizeClampMode(
    xenos::ClampMode clamp_mode) const {
  if (clamp_mode == xenos::ClampMode::kClampToHalfway) {
    return xenos::ClampMode::kClampToEdge;
  }
  if (clamp_mode == xenos::ClampMode::kMirrorClampToHalfway ||
      clamp_mode == xenos::ClampMode::kMirrorClampToBorder) {
    return xenos::ClampMode::kMirrorClampToEdge;
  }
  return clamp_mode;
}

// GetHostFormatSwizzle implementation
uint32_t MetalTextureCache::GetHostFormatSwizzle(TextureKey key) const {
  switch (key.format) {
    case xenos::TextureFormat::k_8:
    case xenos::TextureFormat::k_8_A:
    case xenos::TextureFormat::k_8_B:
    case xenos::TextureFormat::k_16:
    case xenos::TextureFormat::k_16_EXPAND:
    case xenos::TextureFormat::k_16_FLOAT:
    case xenos::TextureFormat::k_DXT3A:
    case xenos::TextureFormat::k_DXT5A:
    case xenos::TextureFormat::k_24_8:
    case xenos::TextureFormat::k_24_8_FLOAT:
      return xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR;

    case xenos::TextureFormat::k_8_8:
    case xenos::TextureFormat::k_16_16:
    case xenos::TextureFormat::k_16_16_EXPAND:
    case xenos::TextureFormat::k_16_16_FLOAT:
    case xenos::TextureFormat::k_DXN:
      return xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG;

    case xenos::TextureFormat::k_5_6_5:
    case xenos::TextureFormat::k_10_11_11:
    case xenos::TextureFormat::k_11_11_10:
      return xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB;

    default:
      return xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
  }
}

// GetMaxHostTextureWidthHeight implementation
uint32_t MetalTextureCache::GetMaxHostTextureWidthHeight(
    xenos::DataDimension dimension) const {
  // Metal supports up to 16384x16384 for 2D textures on most devices
  switch (dimension) {
    case xenos::DataDimension::k1D:
      return 16384;
    case xenos::DataDimension::k2DOrStacked:
      return 16384;
    case xenos::DataDimension::k3D:
      return 2048;  // 3D textures have lower limits
    case xenos::DataDimension::kCube:
      return 16384;
    default:
      return 16384;
  }
}

// GetMaxHostTextureDepthOrArraySize implementation
uint32_t MetalTextureCache::GetMaxHostTextureDepthOrArraySize(
    xenos::DataDimension dimension) const {
  // Metal array and 3D texture limits
  switch (dimension) {
    case xenos::DataDimension::k1D:
      return 2048;  // Array size limit
    case xenos::DataDimension::k2DOrStacked:
      return 2048;  // Array size limit
    case xenos::DataDimension::k3D:
      return 2048;  // Depth limit for 3D textures
    case xenos::DataDimension::kCube:
      return 2048;  // Array size for cube arrays
    default:
      return 2048;
  }
}

// CreateTexture implementation - creates MetalTexture from TextureKey
std::unique_ptr<TextureCache::Texture> MetalTextureCache::CreateTexture(
    TextureKey key) {
  SCOPE_profile_cpu_f("gpu");

  MTL::PixelFormat metal_format = GetPixelFormatForKey(key);
  if (metal_format == MTL::PixelFormatInvalid) {
    XELOGE("CreateTexture: Unsupported texture format {}",
           static_cast<uint32_t>(key.format));
    return nullptr;
  }

  MTL::Texture* metal_texture = nullptr;

  // Create Metal texture based on dimension
  switch (key.dimension) {
    case xenos::DataDimension::k1D: {
      metal_texture = CreateTexture2D(key.GetWidth(), key.GetHeight(),
                                      key.GetDepthOrArraySize(), metal_format,
                                      key.mip_max_level + 1);
      break;
    }
    case xenos::DataDimension::k2DOrStacked: {
      metal_texture = CreateTexture2D(key.GetWidth(), key.GetHeight(),
                                      key.GetDepthOrArraySize(), metal_format,
                                      key.mip_max_level + 1);
      break;
    }
    case xenos::DataDimension::k3D: {
      metal_texture = CreateTexture3D(key.GetWidth(), key.GetHeight(),
                                      key.GetDepthOrArraySize(), metal_format,
                                      key.mip_max_level + 1);
      break;
    }
    case xenos::DataDimension::kCube: {
      uint32_t array_size = key.GetDepthOrArraySize();
      if (array_size % 6 != 0) {
        XELOGW(
            "CreateTexture: Cube texture array size {} is not divisible by 6",
            array_size);
      }
      uint32_t cube_count = std::max(1u, array_size / 6);
      metal_texture = CreateTextureCube(key.GetWidth(), metal_format,
                                        key.mip_max_level + 1, cube_count);
      break;
    }
    default: {
      XELOGE("CreateTexture: Unsupported texture dimension {}",
             static_cast<uint32_t>(key.dimension));
      return nullptr;
    }
  }

  if (!metal_texture) {
    XELOGE("CreateTexture: Failed to create Metal texture");
    return nullptr;
  }

  // Create MetalTexture wrapper
  return std::make_unique<MetalTexture>(*this, key, metal_texture);
}

// LoadTextureDataFromResidentMemoryImpl implementation
bool MetalTextureCache::LoadTextureDataFromResidentMemoryImpl(Texture& texture,
                                                              bool load_base,
                                                              bool load_mips) {
  SCOPE_profile_cpu_f("gpu");

  MetalTexture* metal_texture = static_cast<MetalTexture*>(&texture);
  if (!metal_texture || !metal_texture->metal_texture()) {
    XELOGE("LoadTextureDataFromResidentMemoryImpl: Invalid Metal texture");
    return false;
  }

  // GPU-based loading path for Metal texture_load_* shaders. For formats not
  // yet supported by the Metal path, fall back to the existing CPU untile path
  // as a safety net.
  if (TryGpuLoadTexture(texture, load_base, load_mips)) {
    return true;
  }

  const TextureKey& key = texture.key();
  const texture_util::TextureGuestLayout& guest_layout = texture.guest_layout();

  MTL::Texture* mtl_texture = metal_texture->metal_texture();

  // Get format info
  const FormatInfo* format_info = FormatInfo::Get(key.format);
  if (!format_info) {
    XELOGE("LoadTextureDataFromResidentMemoryImpl: Unknown format {}",
           static_cast<uint32_t>(key.format));
    return false;
  }

  uint32_t bytes_per_block = format_info->bytes_per_block();
  uint32_t bytes_per_block_log2 = xe::log2_floor(bytes_per_block);

  // Get guest memory address
  uint32_t base_guest_address = key.base_page << 12;
  uint32_t mips_guest_address = key.mip_page << 12;

  // Get the shared memory buffer - this contains a copy of Xbox memory
  MetalSharedMemory& metal_shared_memory =
      static_cast<MetalSharedMemory&>(shared_memory());
  MTL::Buffer* shared_buffer = metal_shared_memory.GetBuffer();
  if (!shared_buffer) {
    XELOGE(
        "LoadTextureDataFromResidentMemoryImpl: Shared memory buffer is null");
    return false;
  }

  // Get pointer to buffer contents - this is the Xbox memory copy
  const uint8_t* buffer_data =
      static_cast<const uint8_t*>(shared_buffer->contents());
  if (!buffer_data) {
    XELOGE(
        "LoadTextureDataFromResidentMemoryImpl: Failed to get buffer contents");
    return false;
  }

  // The guest_address is a physical address offset into the 512MB Xbox memory
  auto load_level =
      [&](uint32_t mip_level,
          const texture_util::TextureGuestLayout::Level& level_layout,
          uint32_t level_guest_address) -> bool {
    if (!level_layout.level_data_extent_bytes) {
      return true;
    }
    if (!level_guest_address) {
      return false;
    }

    uint32_t width_texels = std::max(key.GetWidth() >> mip_level, uint32_t(1));
    uint32_t height_texels =
        std::max(key.GetHeight() >> mip_level, uint32_t(1));
    uint32_t depth_or_array_size =
        std::max(key.GetDepthOrArraySize() >> mip_level, uint32_t(1));
    if (key.dimension != xenos::DataDimension::k3D) {
      depth_or_array_size = key.GetDepthOrArraySize();
    }

    uint32_t width_blocks = xe::align(width_texels, format_info->block_width) /
                            format_info->block_width;
    uint32_t height_blocks =
        xe::align(height_texels, format_info->block_height) /
        format_info->block_height;

    uint32_t dst_row_pitch_bytes = width_blocks * bytes_per_block;
    uint32_t depth_slices =
        key.dimension == xenos::DataDimension::k3D ? depth_or_array_size : 1;
    uint32_t array_slices = key.dimension == xenos::DataDimension::k3D
                                ? 1
                                : guest_layout.array_size;

    uint64_t slice_bytes = uint64_t(dst_row_pitch_bytes) *
                           uint64_t(height_blocks) * uint64_t(depth_slices);
    uint64_t total_bytes = slice_bytes * uint64_t(array_slices);
    if (total_bytes > SIZE_MAX) {
      return false;
    }

    const uint8_t* level_src = buffer_data + level_guest_address;

    auto untiled_data = std::vector<uint8_t>(size_t(total_bytes));
    uint32_t pitch_blocks = level_layout.row_pitch_bytes / bytes_per_block;

    if (key.tiled) {
      if (key.dimension == xenos::DataDimension::k3D) {
        uint32_t height_stride_blocks = level_layout.z_slice_stride_block_rows;
        for (uint32_t z = 0; z < depth_slices; ++z) {
          for (uint32_t y = 0; y < height_blocks; ++y) {
            for (uint32_t x = 0; x < width_blocks; ++x) {
              int32_t tiled_offset = texture_util::GetTiledOffset3D(
                  int32_t(x), int32_t(y), int32_t(z), pitch_blocks,
                  height_stride_blocks, bytes_per_block_log2);
              size_t dst_offset = (size_t(z) * height_blocks * width_blocks +
                                   size_t(y) * width_blocks + size_t(x)) *
                                  bytes_per_block;
              if (tiled_offset < 0 ||
                  dst_offset + bytes_per_block > untiled_data.size()) {
                continue;
              }
              texture_conversion::CopySwapBlock(
                  key.endianness, untiled_data.data() + dst_offset,
                  level_src + tiled_offset, bytes_per_block);
            }
          }
        }
      } else {
        for (uint32_t slice = 0; slice < array_slices; ++slice) {
          const uint8_t* slice_src =
              level_src + slice * level_layout.array_slice_stride_bytes;
          for (uint32_t y = 0; y < height_blocks; ++y) {
            for (uint32_t x = 0; x < width_blocks; ++x) {
              int32_t tiled_offset = texture_util::GetTiledOffset2D(
                  int32_t(x), int32_t(y), pitch_blocks, bytes_per_block_log2);
              size_t dst_offset =
                  (size_t(slice) * height_blocks * width_blocks +
                   size_t(y) * width_blocks + size_t(x)) *
                  bytes_per_block;
              if (tiled_offset < 0 ||
                  dst_offset + bytes_per_block > untiled_data.size()) {
                continue;
              }
              texture_conversion::CopySwapBlock(
                  key.endianness, untiled_data.data() + dst_offset,
                  slice_src + tiled_offset, bytes_per_block);
            }
          }
        }
      }
    } else {
      uint32_t z_slice_stride_bytes =
          level_layout.row_pitch_bytes * level_layout.z_slice_stride_block_rows;
      if (key.dimension == xenos::DataDimension::k3D) {
        for (uint32_t z = 0; z < depth_slices; ++z) {
          for (uint32_t y = 0; y < height_blocks; ++y) {
            uint32_t src_offset =
                z * z_slice_stride_bytes + y * level_layout.row_pitch_bytes;
            size_t dst_offset =
                (size_t(z) * height_blocks + size_t(y)) * dst_row_pitch_bytes;
            texture_conversion::CopySwapBlock(
                key.endianness, untiled_data.data() + dst_offset,
                level_src + src_offset, dst_row_pitch_bytes);
          }
        }
      } else {
        for (uint32_t slice = 0; slice < array_slices; ++slice) {
          const uint8_t* slice_src =
              level_src + slice * level_layout.array_slice_stride_bytes;
          for (uint32_t y = 0; y < height_blocks; ++y) {
            uint32_t src_offset = y * level_layout.row_pitch_bytes;
            size_t dst_offset = (size_t(slice) * height_blocks + size_t(y)) *
                                dst_row_pitch_bytes;
            texture_conversion::CopySwapBlock(
                key.endianness, untiled_data.data() + dst_offset,
                slice_src + src_offset, dst_row_pitch_bytes);
          }
        }
      }
    }

    // Upload to Metal texture.
    if (key.dimension == xenos::DataDimension::k3D) {
      MTL::Region region = MTL::Region::Make3D(0, 0, 0, width_texels,
                                               height_texels, depth_slices);
      mtl_texture->replaceRegion(region, mip_level, 0, untiled_data.data(),
                                 dst_row_pitch_bytes,
                                 dst_row_pitch_bytes * height_blocks);
    } else {
      for (uint32_t slice = 0; slice < array_slices; ++slice) {
        MTL::Region region =
            MTL::Region::Make2D(0, 0, width_texels, height_texels);
        const uint8_t* slice_data = untiled_data.data() + slice * slice_bytes;
        mtl_texture->replaceRegion(region, mip_level, slice, slice_data,
                                   dst_row_pitch_bytes, 0);
      }
    }

    return true;
  };

  // For k_8_8_8_8, the untiled data is BGRA after endian swap.
  // Metal textures now use MTL::PixelFormatBGRA8Unorm which matches this
  // layout directly, so no swizzle is needed.
  // (Previously we swizzled BGRA->RGBA for RGBA8Unorm format.)

  bool ok = true;
  if (load_base && guest_layout.base.level_data_extent_bytes) {
    ok &= load_level(0, guest_layout.base, base_guest_address);
  }
  if (load_mips && guest_layout.mips_total_extent_bytes) {
    for (uint32_t level = 1; level <= guest_layout.max_level; ++level) {
      const auto& level_layout = guest_layout.mips[level];
      if (!level_layout.level_data_extent_bytes) {
        continue;
      }
      ok &= load_level(
          level, level_layout,
          mips_guest_address + guest_layout.mip_offsets_bytes[level]);
    }
  }

  return ok;
}

// MetalTexture implementation
MetalTextureCache::MetalTexture::MetalTexture(MetalTextureCache& texture_cache,
                                              const TextureKey& key,
                                              MTL::Texture* metal_texture)
    : Texture(texture_cache, key), metal_texture_(metal_texture) {
  if (metal_texture_) {
    // Calculate host memory usage for base class tracking
    uint32_t bytes_per_pixel =
        4;  // Simplified - could be more accurate based on format
    uint32_t width = key.GetWidth();
    uint32_t height = key.GetHeight();
    uint32_t depth = key.GetDepthOrArraySize();
    uint64_t memory_usage = width * height * depth * bytes_per_pixel;

    SetHostMemoryUsage(memory_usage);
  }
}

MetalTextureCache::MetalTexture::~MetalTexture() {
  if (metal_texture_) {
    metal_texture_->release();
    metal_texture_ = nullptr;
  }
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe

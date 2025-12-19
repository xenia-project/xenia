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
#include "xenia/gpu/shaders/bytecode/metal/texture_load_16bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_32bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_64bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/texture_load_8bpb_cs.h"
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

bool MetalTextureCache::TryGpuLoadTexture(Texture& texture, bool load_base,
                                          bool load_mips) {
  XELOGI(
      "MetalTextureCache::TryGpuLoadTexture: called (load_base={}, "
      "load_mips={})",
      load_base, load_mips);

  MetalTexture* metal_texture = static_cast<MetalTexture*>(&texture);

  if (!metal_texture || !metal_texture->metal_texture()) {
    return false;
  }

  const TextureKey& key = texture.key();
  xenos::DataDimension dimension = key.dimension;

  // For now, implement the GPU path for the primary A-Train case:
  // 2D, non-scaled, k_8_8_8_8 textures (32bpp). Other formats and dimensions
  // will fall back to the existing CPU implementation.
  if (key.scaled_resolve || dimension != xenos::DataDimension::k2DOrStacked ||
      key.format != xenos::TextureFormat::k_8_8_8_8) {
    return false;
  }
  if (!load_base) {
    // Mip-only load not supported yet on Metal.
    return false;
  }

  // Require a 1x draw resolution scale for the initial implementation.
  if (draw_resolution_scale_x() != 1 || draw_resolution_scale_y() != 1) {
    XELOGW(
        "MetalTextureCache::TryGpuLoadTexture: resolution scaling not yet "
        "supported in Metal texture_load path");
    return false;
  }

  TextureCache::LoadShaderIndex load_shader =
      TextureCache::kLoadShaderIndex32bpb;
  MTL::ComputePipelineState* pipeline =
      load_pipelines_[static_cast<size_t>(load_shader)];
  if (!pipeline) {
    XELOGW(
        "MetalTextureCache::TryGpuLoadTexture: missing 32bpp texture_load "
        "pipeline");
    return false;
  }

  const TextureCache::LoadShaderInfo& load_shader_info =
      GetLoadShaderInfo(load_shader);

  // Guest layout and basic parameters.
  const texture_util::TextureGuestLayout& guest_layout = texture.guest_layout();
  bool is_3d = false;
  uint32_t width = key.GetWidth();
  uint32_t height = key.GetHeight();
  uint32_t depth_or_array_size = key.GetDepthOrArraySize();
  uint32_t depth = 1;
  uint32_t array_size = depth_or_array_size;

  const FormatInfo* guest_format_info = FormatInfo::Get(key.format);
  if (!guest_format_info) {
    return false;
  }
  uint32_t block_width = guest_format_info->block_width;
  uint32_t block_height = guest_format_info->block_height;
  uint32_t bytes_per_block = guest_format_info->bytes_per_block();

  // Only support base level for now.
  uint32_t level = 0;
  uint32_t level_width = width;
  uint32_t level_height = height;
  uint32_t level_depth = depth;

  // Build LoadConstants matching texture_load.xesli /
  // TextureCache::LoadConstants.
  TextureCache::LoadConstants load_constants = {};
  load_constants.is_tiled_3d_endian_scale =
      uint32_t(key.tiled) | (uint32_t(is_3d) << 1) |
      (uint32_t(key.endianness) << 2) | (1u << 4) | (1u << 7);

  uint32_t guest_base_address = key.base_page << 12;
  load_constants.guest_offset = guest_base_address;

  const texture_util::TextureGuestLayout::Level& level_guest_layout =
      guest_layout.base;
  uint32_t level_guest_pitch = level_guest_layout.row_pitch_bytes;
  if (key.tiled) {
    level_guest_pitch /= bytes_per_block;
  }
  load_constants.guest_pitch_aligned = level_guest_pitch;
  load_constants.guest_z_stride_block_rows_aligned =
      level_guest_layout.z_slice_stride_block_rows;

  // Size in blocks (no scaling for now).
  load_constants.size_blocks[0] =
      (level_width + (block_width - 1)) / block_width;
  load_constants.size_blocks[1] =
      (level_height + (block_height - 1)) / block_height;
  load_constants.size_blocks[2] = level_depth;
  load_constants.height_texels = level_height;

  // Host layout for an uncompressed 32bpp guest format.
  uint32_t host_block_width = 1;
  uint32_t host_block_height = 1;
  uint32_t host_x_blocks_per_thread =
      UINT32_C(1) << load_shader_info.guest_x_blocks_per_thread_log2;
  uint32_t host_width_blocks =
      (level_width + (host_block_width - 1)) / host_block_width;
  uint32_t host_height_blocks =
      (level_height + (host_block_height - 1)) / host_block_height;
  host_width_blocks = xe::round_up(host_width_blocks, host_x_blocks_per_thread);

  uint32_t host_row_pitch =
      host_width_blocks * load_shader_info.bytes_per_host_block;
  load_constants.host_pitch = host_row_pitch;
  load_constants.host_offset = 0;

  uint32_t host_slice_size_bytes = host_row_pitch * host_height_blocks;
  uint64_t host_buffer_size =
      uint64_t(host_slice_size_bytes) * uint64_t(array_size);

  // Acquire Metal resources.
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    return false;
  }

  MetalSharedMemory& metal_shared_memory =
      static_cast<MetalSharedMemory&>(shared_memory());
  MTL::Buffer* shared_buffer = metal_shared_memory.GetBuffer();
  if (!shared_buffer) {
    return false;
  }

  // Debug: log a small slice of the source shared memory backing this texture.
  const uint8_t* shared_bytes =
      static_cast<const uint8_t*>(shared_buffer->contents());
  if (shared_bytes) {
    const uint8_t* src = shared_bytes + guest_base_address;
    XELOGI(
        "MetalTexLoad SRC[0..15] @0x{:08X}: "
        "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}  "
        "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}",
        guest_base_address, src[0], src[1], src[2], src[3], src[4], src[5],
        src[6], src[7], src[8], src[9], src[10], src[11], src[12], src[13],
        src[14], src[15]);
  }

  MTL::Buffer* dest_buffer =
      device->newBuffer(host_buffer_size, MTL::ResourceStorageModeShared);
  if (!dest_buffer) {
    return false;
  }

  MetalLoadConstants metal_constants = {};
  metal_constants.is_tiled_3d_endian_scale =
      load_constants.is_tiled_3d_endian_scale;
  metal_constants.guest_offset = load_constants.guest_offset;
  metal_constants.guest_pitch_aligned = load_constants.guest_pitch_aligned;
  metal_constants.guest_z_stride_block_rows_aligned =
      load_constants.guest_z_stride_block_rows_aligned;
  metal_constants.size_blocks[0] = load_constants.size_blocks[0];
  metal_constants.size_blocks[1] = load_constants.size_blocks[1];
  metal_constants.size_blocks[2] = load_constants.size_blocks[2];
  metal_constants.padding = 0;
  metal_constants.host_offset = load_constants.host_offset;
  metal_constants.host_pitch = load_constants.host_pitch;
  metal_constants.height_texels = load_constants.height_texels;

  // Section 8.2: Log LoadConstants for cross-backend comparison.
  // This format matches the D3D12/Vulkan LoadConstants to help identify
  // discrepancies in tiling/pitch/offset calculations.
  XELOGI(
      "LoadConstants 0x{:08X}: is_tiled_3d_endian_scale=0x{:08X} "
      "guest_offset=0x{:08X} "
      "guest_pitch_aligned={} guest_z_stride_block_rows_aligned={} "
      "size_blocks=({},{},{}) host_offset={} host_pitch={} height_texels={}",
      guest_base_address, metal_constants.is_tiled_3d_endian_scale,
      metal_constants.guest_offset, metal_constants.guest_pitch_aligned,
      metal_constants.guest_z_stride_block_rows_aligned,
      metal_constants.size_blocks[0], metal_constants.size_blocks[1],
      metal_constants.size_blocks[2], metal_constants.host_offset,
      metal_constants.host_pitch, metal_constants.height_texels);

  size_t constants_size = xe::align(sizeof(metal_constants), size_t(16));
  MTL::Buffer* constants_buffer =
      device->newBuffer(constants_size, MTL::ResourceStorageModeShared);
  if (!constants_buffer) {
    dest_buffer->release();
    return false;
  }
  std::memcpy(constants_buffer->contents(), &metal_constants,
              sizeof(metal_constants));

  MTL::CommandQueue* queue = command_processor_->GetMetalCommandQueue();
  if (!queue) {
    constants_buffer->release();
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
    // cmd is autoreleased from commandBuffer() - do not release
    constants_buffer->release();
    dest_buffer->release();
    return false;
  }

  encoder->setComputePipelineState(pipeline);
  encoder->setBuffer(constants_buffer, 0, 0);
  encoder->setBuffer(dest_buffer, 0, 1);
  encoder->setBuffer(shared_buffer, 0, 2);

  uint32_t guest_x_blocks_per_group_log2 =
      load_shader_info.GetGuestXBlocksPerGroupLog2();
  uint32_t group_count_x =
      (load_constants.size_blocks[0] +
       ((UINT32_C(1) << guest_x_blocks_per_group_log2) - 1)) >>
      guest_x_blocks_per_group_log2;
  uint32_t group_count_y =
      (load_constants.size_blocks[1] +
       ((UINT32_C(1) << kLoadGuestYBlocksPerGroupLog2) - 1)) >>
      kLoadGuestYBlocksPerGroupLog2;

  MTL::Size threads_per_group =
      MTL::Size::Make(UINT32_C(1) << kLoadGuestXThreadsPerGroupLog2,
                      UINT32_C(1) << kLoadGuestYBlocksPerGroupLog2, 1);
  MTL::Size threadgroups = MTL::Size::Make(group_count_x, group_count_y,
                                           load_constants.size_blocks[2]);

  encoder->dispatchThreadgroups(threadgroups, threads_per_group);
  encoder->endEncoding();
  // encoder is autoreleased - do not release

  cmd->commit();
  cmd->waitUntilCompleted();
  // cmd is autoreleased from commandBuffer() - do not release

  constants_buffer->release();

  // Upload from the GPU-populated linear buffer to the Metal texture via CPU;
  // this avoids CPU untile/format work while still using Metal's texture APIs.
  MTL::Texture* mtl_texture = metal_texture->metal_texture();
  uint8_t* dest_data = static_cast<uint8_t*>(dest_buffer->contents());
  if (!dest_data) {
    dest_buffer->release();
    return false;
  }

  // Debug: log the first few bytes of the GPU-populated destination buffer.
  XELOGI(
      "MetalTexLoad DEST[0..15]: "
      "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}  "
      "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}",
      dest_data[0], dest_data[1], dest_data[2], dest_data[3], dest_data[4],
      dest_data[5], dest_data[6], dest_data[7], dest_data[8], dest_data[9],
      dest_data[10], dest_data[11], dest_data[12], dest_data[13], dest_data[14],
      dest_data[15]);

  for (uint32_t slice = 0; slice < array_size; ++slice) {
    uint8_t* slice_data = dest_data + slice * host_slice_size_bytes;
    MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
    mtl_texture->replaceRegion(region, 0, slice, slice_data, host_row_pitch, 0);
  }

  // Debug: Verify texture upload by reading back first pixel
  static int upload_verify_count = 0;
  if (upload_verify_count < 5) {
    upload_verify_count++;
    uint8_t readback[4] = {0};
    MTL::Region verify_region = MTL::Region::Make2D(0, 0, 1, 1);
    mtl_texture->getBytes(readback, 4, verify_region, 0);
    XELOGI(
        "TEXTURE_UPLOAD_VERIFY[{}]: mtl_texture={:p} uploaded to slice 0, "
        "readback first pixel: {:02X} {:02X} {:02X} {:02X} (expected: {:02X} "
        "{:02X} {:02X} {:02X})",
        upload_verify_count, (void*)mtl_texture, readback[0], readback[1],
        readback[2], readback[3], dest_data[0], dest_data[1], dest_data[2],
        dest_data[3]);
  }

  if (cvars::metal_texture_dump_png) {
    static int gpu_tex_dump_count = 0;
    if (gpu_tex_dump_count < 20) {
      gpu_tex_dump_count++;
      std::filesystem::path out_dir = std::filesystem::path("scratch") / "gpu";
      std::error_code ec;
      std::filesystem::create_directories(out_dir, ec);

      char filename_buf[256];
      std::snprintf(filename_buf, sizeof(filename_buf),
                    "gpu_texture_%02d_0x%08X_%ux%u.png", gpu_tex_dump_count,
                    guest_base_address, width, height);
      std::filesystem::path filename = out_dir / filename_buf;
      // dest_data is BGRA after GPU texture_load (endian swapped from Xbox
      // ARGB). stb_image_write expects RGBA, so swap R<->B for correct dump.
      size_t total_pixels = size_t(width) * height;
      std::vector<uint8_t> rgba_data(total_pixels * 4);
      for (size_t i = 0; i < total_pixels; ++i) {
        size_t src_offset = (i / width) * host_row_pitch + (i % width) * 4;
        rgba_data[i * 4 + 0] = dest_data[src_offset + 2];  // R from B
        rgba_data[i * 4 + 1] = dest_data[src_offset + 1];  // G
        rgba_data[i * 4 + 2] = dest_data[src_offset + 0];  // B from R
        rgba_data[i * 4 + 3] = dest_data[src_offset + 3];  // A
      }
      int ok = stbi_write_png(filename.string().c_str(), int(width),
                              int(height), 4, rgba_data.data(), int(width * 4));
      XELOGI(
          "MetalTextureCache: dumped GPU-loaded texture #{} to {} (ok={}, "
          "{}x{}, host_pitch={})",
          gpu_tex_dump_count, filename.string(), ok, width, height,
          host_row_pitch);
    }
  }

  dest_buffer->release();

  XELOGD(
      "MetalTextureCache::TryGpuLoadTexture: GPU texture_load_32bpb path used "
      "for 0x{:08X} ({}x{})",
      guest_base_address, width, height);

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

  // Only initialize the core 8/16/32/64/128bpb loaders for now; format-
  // specific loaders can be added as needed.
  load_pipelines_[TextureCache::kLoadShaderIndex8bpb] =
      create_pipeline_from_metallib(texture_load_8bpb_cs_metallib,
                                    sizeof(texture_load_8bpb_cs_metallib));
  load_pipelines_[TextureCache::kLoadShaderIndex16bpb] =
      create_pipeline_from_metallib(texture_load_16bpb_cs_metallib,
                                    sizeof(texture_load_16bpb_cs_metallib));
  load_pipelines_[TextureCache::kLoadShaderIndex32bpb] =
      create_pipeline_from_metallib(texture_load_32bpb_cs_metallib,
                                    sizeof(texture_load_32bpb_cs_metallib));
  load_pipelines_[TextureCache::kLoadShaderIndex64bpb] =
      create_pipeline_from_metallib(texture_load_64bpb_cs_metallib,
                                    sizeof(texture_load_64bpb_cs_metallib));
  load_pipelines_[TextureCache::kLoadShaderIndex128bpb] =
      create_pipeline_from_metallib(texture_load_128bpb_cs_metallib,
                                    sizeof(texture_load_128bpb_cs_metallib));

  // For now, texture_load_*_scaled_cs are not required because the Metal
  // texture_load path only runs for non-scaled textures.

  if (!load_pipelines_[TextureCache::kLoadShaderIndex32bpb]) {
    return false;
  }

  return true;
}

void MetalTextureCache::Shutdown() {
  SCOPE_profile_cpu_f("gpu");

  ClearCache();

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
  // Metal textures generally match the expected swizzle patterns
  // For most formats, return RGBA order
  return xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
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

  // Convert TextureKey to Metal format
  MTL::PixelFormat metal_format =
      ConvertXenosFormat(key.format, key.endianness);
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

  MTL::Texture* mtl_texture = metal_texture->metal_texture();

  // Get texture dimensions
  uint32_t width = key.GetWidth();
  uint32_t height = key.GetHeight();
  uint32_t depth = key.GetDepthOrArraySize();
  bool is_tiled = key.tiled != 0;

  // Get format info
  const FormatInfo* format_info = FormatInfo::Get(key.format);
  if (!format_info) {
    XELOGE("LoadTextureDataFromResidentMemoryImpl: Unknown format {}",
           static_cast<uint32_t>(key.format));
    return false;
  }

  uint32_t bytes_per_block = format_info->bytes_per_block();
  uint32_t block_width = format_info->block_width;
  uint32_t block_height = format_info->block_height;

  // Calculate block dimensions
  uint32_t width_blocks = (width + block_width - 1) / block_width;
  uint32_t height_blocks = (height + block_height - 1) / block_height;

  // Get guest memory address
  uint32_t guest_address = key.base_page << 12;

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
  const uint8_t* src_data = buffer_data + guest_address;

  // Debug: Log texture source data from Metal buffer
  static int tex_src_log_count = 0;
  if (tex_src_log_count < 5) {
    tex_src_log_count++;
    XELOGI("TEX_SRC_DATA[{}]: guest_addr=0x{:08X}, format={}, {}x{}, tiled={}",
           tex_src_log_count, guest_address, static_cast<uint32_t>(key.format),
           width, height, is_tiled);
    XELOGI(
        "  Metal buffer[0..15]: {:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} "
        "{:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} "
        "{:02X}",
        src_data[0], src_data[1], src_data[2], src_data[3], src_data[4],
        src_data[5], src_data[6], src_data[7], src_data[8], src_data[9],
        src_data[10], src_data[11], src_data[12], src_data[13], src_data[14],
        src_data[15]);
  }

  // For the A-Train background texture at 0x1BFD5000, scan the raw shared
  // memory backing this texture before any untile/endian/swatch work, to see
  // if any non-zero data is present in guest memory.
  if (guest_address == 0x1BFD5000 &&
      key.format == xenos::TextureFormat::k_8_8_8_8 && width == 1280 &&
      height == 720) {
    size_t scan_size = static_cast<size_t>(width) * height * 4;
    size_t first_nz = scan_size;
    uint8_t first_val = 0;
    for (size_t i = 0; i < scan_size; ++i) {
      if (src_data[i] != 0) {
        first_nz = i;
        first_val = src_data[i];
        break;
      }
    }
    XELOGI(
        "BG_RAW_DEBUG: guest 0x1BFD5000 raw scan size={} first_non_zero={} "
        "value=0x{:02X}",
        scan_size, first_nz == scan_size ? -1 : static_cast<int>(first_nz),
        first_val);
  }

  // Calculate the pitch for tiled textures (aligned to 32 blocks)
  uint32_t pitch_blocks =
      is_tiled ? xe::align(width_blocks, xenos::kTextureTileWidthHeight)
               : width_blocks;

  // Allocate buffer for untiled/converted data
  uint32_t row_pitch_bytes = width_blocks * bytes_per_block;
  uint32_t total_size = row_pitch_bytes * height_blocks * depth;

  std::vector<uint8_t> untiled_data(total_size);

  // Get bytes per block log2 for tiling calculations
  uint32_t bytes_per_block_log2 = xe::log2_floor(bytes_per_block);

  // Process the texture data
  if (is_tiled) {
    // Untile the texture data using the Xbox 360 tiling algorithm
    for (uint32_t z = 0; z < depth; ++z) {
      for (uint32_t y = 0; y < height_blocks; ++y) {
        for (uint32_t x = 0; x < width_blocks; ++x) {
          // Calculate the tiled source offset
          int32_t tiled_offset;
          if (key.dimension == xenos::DataDimension::k3D) {
            tiled_offset = texture_util::GetTiledOffset3D(
                static_cast<int32_t>(x), static_cast<int32_t>(y),
                static_cast<int32_t>(z), pitch_blocks, height_blocks,
                bytes_per_block_log2);
          } else {
            tiled_offset = texture_util::GetTiledOffset2D(
                static_cast<int32_t>(x), static_cast<int32_t>(y), pitch_blocks,
                bytes_per_block_log2);
          }

          // Calculate linear destination offset
          uint32_t linear_offset =
              (z * height_blocks * width_blocks + y * width_blocks + x) *
              bytes_per_block;

          // Copy and endian-swap the block
          if (tiled_offset >= 0 &&
              linear_offset + bytes_per_block <= total_size) {
            const uint8_t* src_block = src_data + tiled_offset;
            uint8_t* dst_block = untiled_data.data() + linear_offset;

            // Debug: log first pixel offset
            static int tiled_offset_log = 0;
            if (tiled_offset_log < 3 && x == 0 && y == 0 && z == 0) {
              tiled_offset_log++;
              XELOGI(
                  "TILED_OFFSET[{}]: (0,0) -> tiled_offset={}, endian={}, "
                  "src[0..3]={:02X} {:02X} {:02X} {:02X}",
                  tiled_offset_log, tiled_offset,
                  static_cast<uint32_t>(key.endianness), src_block[0],
                  src_block[1], src_block[2], src_block[3]);
            }

            // Perform endian swap based on the texture's endianness
            texture_conversion::CopySwapBlock(key.endianness, dst_block,
                                              src_block, bytes_per_block);

            // Debug: log dst after swap
            static int swap_log = 0;
            if (swap_log < 3 && x == 0 && y == 0 && z == 0) {
              swap_log++;
              XELOGI(
                  "AFTER_SWAP[{}]: linear_offset={}, bytes_per_block={}, "
                  "dst[0..3]={:02X} {:02X} {:02X} {:02X}",
                  swap_log, linear_offset, bytes_per_block, dst_block[0],
                  dst_block[1], dst_block[2], dst_block[3]);
              // Also check what's actually at untiled_data[0]
              XELOGI("  untiled_data[0..3]={:02X} {:02X} {:02X} {:02X}",
                     untiled_data[0], untiled_data[1], untiled_data[2],
                     untiled_data[3]);
            }
          }
        }
      }
    }
  } else {
    // Linear texture - just copy with endian swap
    uint32_t src_pitch = pitch_blocks * bytes_per_block;
    // Align to 256 bytes for linear textures
    src_pitch = xe::align(src_pitch, uint32_t(256));

    for (uint32_t z = 0; z < depth; ++z) {
      for (uint32_t y = 0; y < height_blocks; ++y) {
        uint32_t src_offset = z * src_pitch * height_blocks + y * src_pitch;
        uint32_t dst_offset = (z * height_blocks + y) * row_pitch_bytes;

        // Copy row with endian swap
        texture_conversion::CopySwapBlock(
            key.endianness, untiled_data.data() + dst_offset,
            src_data + src_offset, row_pitch_bytes);
      }
    }
  }

  // Section 8.1: Log CPU untile DEST for comparison with GPU path.
  // Log the first 16 bytes of untiled data BEFORE the BGRA->RGBA swizzle, so
  // we can directly compare with GPU texture_load output (which produces RGBA
  // directly for k_8_8_8_8 after endian swap).
  if (key.format == xenos::TextureFormat::k_8_8_8_8 && total_size >= 16) {
    XELOGI(
        "MetalTexLoadCPU DEST[0..15] @0x{:08X}: "
        "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}  "
        "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}",
        guest_address, untiled_data[0], untiled_data[1], untiled_data[2],
        untiled_data[3], untiled_data[4], untiled_data[5], untiled_data[6],
        untiled_data[7], untiled_data[8], untiled_data[9], untiled_data[10],
        untiled_data[11], untiled_data[12], untiled_data[13], untiled_data[14],
        untiled_data[15]);
  }

  // For k_8_8_8_8, the untiled data is BGRA after endian swap.
  // Metal textures now use MTL::PixelFormatBGRA8Unorm which matches this
  // layout directly, so no swizzle is needed.
  // (Previously we swizzled BGRA->RGBA for RGBA8Unorm format.)

  // Avoid ad-hoc per-trace texture dumps here. Use `--metal_texture_dump_png`
  // for controlled PNG dumping, and the generic `--texture_dump` for DDS.

  // Upload to Metal texture
  if (key.dimension == xenos::DataDimension::kCube) {
    // Cube texture - upload each face
    uint32_t face_size = row_pitch_bytes * height_blocks;
    for (uint32_t face = 0; face < 6 && face < depth; ++face) {
      MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
      mtl_texture->replaceRegion(region, 0, face,
                                 untiled_data.data() + face * face_size,
                                 row_pitch_bytes, 0);
    }
  } else if (key.dimension == xenos::DataDimension::k3D) {
    // 3D texture
    MTL::Region region = MTL::Region::Make3D(0, 0, 0, width, height, depth);
    mtl_texture->replaceRegion(region, 0, 0, untiled_data.data(),
                               row_pitch_bytes,
                               row_pitch_bytes * height_blocks);
  } else {
    // 2D texture (possibly array)
    if (depth > 1) {
      // 2D array
      uint32_t slice_size = row_pitch_bytes * height_blocks;
      for (uint32_t slice = 0; slice < depth; ++slice) {
        MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
        mtl_texture->replaceRegion(region, 0, slice,
                                   untiled_data.data() + slice * slice_size,
                                   row_pitch_bytes, 0);
      }
    } else {
      // Simple 2D texture
      MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
      mtl_texture->replaceRegion(region, 0, untiled_data.data(),
                                 row_pitch_bytes);
    }
  }

  XELOGD(
      "LoadTextureDataFromResidentMemoryImpl: Loaded {}x{}x{} {} texture "
      "(tiled={}, format={}, {} bytes)",
      width, height, depth,
      key.dimension == xenos::DataDimension::kCube ? "cube"
      : key.dimension == xenos::DataDimension::k3D ? "3D"
                                                   : "2D",
      is_tiled, static_cast<uint32_t>(key.format), total_size);

  // Debug: Dump first few pixels of texture to verify data
  static int texture_dump_count = 0;
  if (texture_dump_count < 3 && total_size >= 16) {
    texture_dump_count++;
    XELOGI("TEXTURE_DEBUG[{}]: {}x{} format={} tiled={}", texture_dump_count,
           width, height, static_cast<uint32_t>(key.format), is_tiled);
    XELOGI(
        "  First 16 bytes (BGRA): {:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} "
        "{:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} "
        "{:02X}",
        untiled_data[0], untiled_data[1], untiled_data[2], untiled_data[3],
        untiled_data[4], untiled_data[5], untiled_data[6], untiled_data[7],
        untiled_data[8], untiled_data[9], untiled_data[10], untiled_data[11],
        untiled_data[12], untiled_data[13], untiled_data[14], untiled_data[15]);
  }

  return true;
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

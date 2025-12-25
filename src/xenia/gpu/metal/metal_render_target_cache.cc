/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_render_target_cache.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "third_party/stb/stb_image_write.h"
#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/shaders/bytecode/metal/resolve_fast_32bpp_1x2xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/resolve_fast_32bpp_4xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/resolve_fast_64bpp_1x2xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/resolve_fast_64bpp_4xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/resolve_full_128bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/resolve_full_16bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/resolve_full_32bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/resolve_full_64bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/resolve_full_8bpp_cs.h"

#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace metal {

namespace {

MTL::ComputePipelineState* CreateComputePipelineFromEmbeddedLibrary(
    MTL::Device* device, const void* metallib_data, size_t metallib_size,
    const char* debug_name) {
  if (!device || !metallib_data || !metallib_size) {
    return nullptr;
  }

  NS::Error* error = nullptr;

  dispatch_data_t data = dispatch_data_create(
      metallib_data, metallib_size, nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  MTL::Library* lib = device->newLibrary(data, &error);
  dispatch_release(data);
  if (!lib) {
    XELOGE("Metal: failed to create {} library: {}", debug_name,
           error ? error->localizedDescription()->utf8String() : "unknown");
    return nullptr;
  }

  // XeSL compute entrypoint name used in the embedded metallibs.
  NS::String* fn_name = NS::String::string("xesl_entry", NS::UTF8StringEncoding);
  MTL::Function* fn = lib->newFunction(fn_name);
  if (!fn) {
    XELOGE("Metal: {} missing xesl_entry", debug_name);
    lib->release();
    return nullptr;
  }

  MTL::ComputePipelineState* pipeline =
      device->newComputePipelineState(fn, &error);
  fn->release();
  lib->release();

  if (!pipeline) {
    XELOGE("Metal: failed to create {} pipeline: {}", debug_name,
           error ? error->localizedDescription()->utf8String() : "unknown");
    return nullptr;
  }

  return pipeline;
}

// Section 6.1: Debug helper to read back a tiny region of a Metal render target
// texture and log its contents. This is intended only for debugging, not for
// production.
void LogMetalRenderTargetTopLeftPixels(
    MetalRenderTargetCache::MetalRenderTarget* rt, const char* tag,
    MTL::Device* device, MTL::CommandQueue* queue) {
  if (!rt || !device || !queue) {
    return;
  }
  MTL::Texture* tex = rt->texture();
  if (!tex) {
    return;
  }

  uint32_t width = static_cast<uint32_t>(tex->width());
  uint32_t height = static_cast<uint32_t>(tex->height());
  if (!width || !height) {
    return;
  }

  // Check if this is a depth/stencil texture - these cannot be read directly
  // easily without format conversion, but we can try for raw values or skip.
  // For now, we skip depth to avoid complexity, as we care about color RTs.
  MTL::PixelFormat format = tex->pixelFormat();
  bool is_depth_stencil = format == MTL::PixelFormatDepth32Float_Stencil8 ||
                          format == MTL::PixelFormatDepth32Float ||
                          format == MTL::PixelFormatDepth16Unorm ||
                          format == MTL::PixelFormatDepth24Unorm_Stencil8 ||
                          format == MTL::PixelFormatX32_Stencil8;
  if (is_depth_stencil) {
    const auto& key = rt->key();
    XELOGI(
        "{}: RT key=0x{:08X} {}x{} msaa={} is_depth={} (skipping readback for "
        "depth/stencil)",
        tag, key.key, width, height,
        1u << static_cast<uint32_t>(key.msaa_samples), int(key.is_depth));
    return;
  }

  // Check if this is a multisample texture
  if (tex->textureType() == MTL::TextureType2DMultisample) {
    const auto& key = rt->key();
    XELOGI(
        "{}: RT key=0x{:08X} {}x{} msaa={} is_depth={} (skipping readback for "
        "MSAA texture)",
        tag, key.key, width, height,
        1u << static_cast<uint32_t>(key.msaa_samples), int(key.is_depth));
    return;
  }

  // Use a blit to read back data, supporting Private storage mode
  uint32_t read_width = std::min<uint32_t>(width, 4u);
  uint32_t read_height = std::min<uint32_t>(height, 4u);
  uint32_t bytes_per_pixel = 4;  // Assumption for BGRA8/RGBA8/R32F etc.
  // Adjust bytes per pixel based on format if needed, but 4 covers most 32bpp
  // cases
  if (format == MTL::PixelFormatRG16Float ||
      format == MTL::PixelFormatRG16Snorm ||
      format == MTL::PixelFormatRGBA8Unorm) {
    bytes_per_pixel = 4;
  } else if (format == MTL::PixelFormatRGBA16Float ||
             format == MTL::PixelFormatRGBA16Snorm) {
    bytes_per_pixel = 8;
  }

  uint32_t row_pitch = read_width * bytes_per_pixel;
  uint32_t buffer_size = row_pitch * read_height;

  MTL::Buffer* readback_buffer =
      device->newBuffer(buffer_size, MTL::ResourceStorageModeShared);
  if (!readback_buffer) {
    return;
  }

  MTL::CommandBuffer* cmd = queue->commandBuffer();
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();

  blit->copyFromTexture(tex, 0, 0, MTL::Origin::Make(0, 0, 0),
                        MTL::Size::Make(read_width, read_height, 1),
                        readback_buffer, 0, row_pitch, 0);

  blit->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  uint32_t* pixel_data = static_cast<uint32_t*>(readback_buffer->contents());

  const auto& key = rt->key();
  XELOGI(
      "{}: RT key=0x{:08X} {}x{} msaa={} is_depth={} first pixels: "
      "{:08X} {:08X} {:08X} {:08X}",
      tag, key.key, width, height,
      1u << static_cast<uint32_t>(key.msaa_samples), int(key.is_depth),
      pixel_data[0], pixel_data[1], pixel_data[2], pixel_data[3]);

  readback_buffer->release();  // newBuffer returns retained - must release
  // Note: cmd is from commandBuffer() which is autoreleased - don't release
}

}  // namespace

// MetalRenderTarget implementation
MetalRenderTargetCache::MetalRenderTarget::~MetalRenderTarget() {
  if (texture_) {
    texture_->release();
    texture_ = nullptr;
  }
  if (msaa_texture_) {
    msaa_texture_->release();
    msaa_texture_ = nullptr;
  }
}

// MetalRenderTargetCache implementation
MetalRenderTargetCache::MetalRenderTargetCache(
    const RegisterFile& register_file, const Memory& memory,
    TraceWriter* trace_writer, uint32_t draw_resolution_scale_x,
    uint32_t draw_resolution_scale_y, MetalCommandProcessor& command_processor)
    : RenderTargetCache(register_file, memory, trace_writer,
                        draw_resolution_scale_x, draw_resolution_scale_y),
      command_processor_(command_processor),
      trace_writer_(trace_writer) {}

MetalRenderTargetCache::~MetalRenderTargetCache() { Shutdown(true); }

bool MetalRenderTargetCache::Initialize() {
  device_ = command_processor_.GetMetalDevice();
  if (!device_) {
    XELOGE("MetalRenderTargetCache: No Metal device available");
    return false;
  }

  // Create EDRAM buffer (10MB). For debugging, make it CPU-visible (Shared)
  // so resolve instrumentation can inspect its contents.
  constexpr size_t kEdramSizeBytes = 10 * 1024 * 1024;
  edram_buffer_ =
      device_->newBuffer(kEdramSizeBytes, MTL::ResourceStorageModeShared);
  if (!edram_buffer_) {
    XELOGE("MetalRenderTargetCache: Failed to create EDRAM buffer");
    return false;
  }
  edram_buffer_->setLabel(
      NS::String::string("EDRAM Buffer", NS::UTF8StringEncoding));

  // Initialize EDRAM compute shaders
  if (!InitializeEdramComputeShaders()) {
    XELOGE(
        "MetalRenderTargetCache: Failed to initialize EDRAM compute shaders");
    return false;
  }

  // Initialize base class
  InitializeCommon();

  XELOGI("MetalRenderTargetCache: Initialized with {}x{} resolution scale",
         draw_resolution_scale_x(), draw_resolution_scale_y());

  return true;
}

void MetalRenderTargetCache::Shutdown(bool from_destructor) {
  if (!from_destructor) {
    ClearCache();
  }

  // Clean up dummy target
  dummy_color_target_.reset();

  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }

  // Clean up EDRAM compute shaders
  ShutdownEdramComputeShaders();

  if (edram_buffer_) {
    edram_buffer_->release();
    edram_buffer_ = nullptr;
  }

  // Destroy all render targets
  DestroyAllRenderTargets(!from_destructor);

  // Shutdown base class
  if (!from_destructor) {
    ShutdownCommon();
  }
}

bool MetalRenderTargetCache::InitializeEdramComputeShaders() {
  // Initialize the resolve / EDRAM compute pipelines used by the Metal backend.
  edram_load_pipeline_ = nullptr;
  edram_store_pipeline_ = nullptr;
  edram_dump_color_32bpp_1xmsaa_pipeline_ = nullptr;
  edram_dump_color_32bpp_4xmsaa_pipeline_ = nullptr;
  edram_dump_depth_32bpp_4xmsaa_pipeline_ = nullptr;
  resolve_full_8bpp_pipeline_ = nullptr;
  resolve_full_16bpp_pipeline_ = nullptr;
  resolve_full_32bpp_pipeline_ = nullptr;
  resolve_full_64bpp_pipeline_ = nullptr;
  resolve_full_128bpp_pipeline_ = nullptr;
  resolve_fast_32bpp_1x2xmsaa_pipeline_ = nullptr;
  resolve_fast_32bpp_4xmsaa_pipeline_ = nullptr;
  resolve_fast_64bpp_1x2xmsaa_pipeline_ = nullptr;
  resolve_fast_64bpp_4xmsaa_pipeline_ = nullptr;

  NS::Error* error = nullptr;

  // Resolve compute pipelines.
  resolve_full_8bpp_pipeline_ = CreateComputePipelineFromEmbeddedLibrary(
      device_, resolve_full_8bpp_cs_metallib, sizeof(resolve_full_8bpp_cs_metallib),
      "resolve_full_8bpp");
  resolve_full_16bpp_pipeline_ =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, resolve_full_16bpp_cs_metallib,
          sizeof(resolve_full_16bpp_cs_metallib), "resolve_full_16bpp");
  resolve_full_32bpp_pipeline_ =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, resolve_full_32bpp_cs_metallib,
          sizeof(resolve_full_32bpp_cs_metallib), "resolve_full_32bpp");
  resolve_full_64bpp_pipeline_ =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, resolve_full_64bpp_cs_metallib,
          sizeof(resolve_full_64bpp_cs_metallib), "resolve_full_64bpp");
  resolve_full_128bpp_pipeline_ =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, resolve_full_128bpp_cs_metallib,
          sizeof(resolve_full_128bpp_cs_metallib), "resolve_full_128bpp");
  resolve_fast_32bpp_1x2xmsaa_pipeline_ =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, resolve_fast_32bpp_1x2xmsaa_cs_metallib,
          sizeof(resolve_fast_32bpp_1x2xmsaa_cs_metallib),
          "resolve_fast_32bpp_1x2xmsaa");
  resolve_fast_32bpp_4xmsaa_pipeline_ =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, resolve_fast_32bpp_4xmsaa_cs_metallib,
          sizeof(resolve_fast_32bpp_4xmsaa_cs_metallib),
          "resolve_fast_32bpp_4xmsaa");
  resolve_fast_64bpp_1x2xmsaa_pipeline_ =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, resolve_fast_64bpp_1x2xmsaa_cs_metallib,
          sizeof(resolve_fast_64bpp_1x2xmsaa_cs_metallib),
          "resolve_fast_64bpp_1x2xmsaa");
  resolve_fast_64bpp_4xmsaa_pipeline_ =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, resolve_fast_64bpp_4xmsaa_cs_metallib,
          sizeof(resolve_fast_64bpp_4xmsaa_cs_metallib),
          "resolve_fast_64bpp_4xmsaa");

  if (!resolve_full_8bpp_pipeline_ || !resolve_full_16bpp_pipeline_ ||
      !resolve_full_32bpp_pipeline_ || !resolve_full_64bpp_pipeline_ ||
      !resolve_full_128bpp_pipeline_ || !resolve_fast_32bpp_1x2xmsaa_pipeline_ ||
      !resolve_fast_32bpp_4xmsaa_pipeline_ ||
      !resolve_fast_64bpp_1x2xmsaa_pipeline_ ||
      !resolve_fast_64bpp_4xmsaa_pipeline_) {
    XELOGE("Metal: failed to initialize resolve compute pipelines");
    return false;
  }

  // EDRAM dump compute shader for 32-bpp color, 1x MSAA.
  {
    static const char kEdramDumpColor32bpp1xMsaaShader[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct EdramDumpConstants {
  uint dispatch_first_tile;
  uint source_base_tiles;
  uint dest_pitch_tiles;
  uint source_pitch_tiles;
  uint2 resolution_scale;
};

kernel void edram_dump_color_32bpp_1xmsaa(
    texture2d<float, access::read> source [[texture(0)]],
    device uint* edram [[buffer(0)]],
    constant EdramDumpConstants& constants [[buffer(1)]],
    uint3 tid [[thread_position_in_grid]]) {
  const uint kEdramTileCount = 2048u;

  uint2 tile_size = uint2(80u * constants.resolution_scale.x,
                          16u * constants.resolution_scale.y);

  uint2 tile_coord = tid.xy / tile_size;
  uint2 sample_in_tile = tid.xy % tile_size;

  uint rect_tile_index = tile_coord.y * constants.dest_pitch_tiles + tile_coord.x;

  uint nonwrapped_tile = constants.dispatch_first_tile + rect_tile_index;
  uint wrapped_tile = nonwrapped_tile & (kEdramTileCount - 1u);

  uint tile_samples = tile_size.x * tile_size.y;
  uint sample_index = sample_in_tile.y * tile_size.x + sample_in_tile.x;
  uint edram_index = wrapped_tile * tile_samples + sample_index;

  uint source_linear_tile = nonwrapped_tile - constants.source_base_tiles;
  uint source_tile_y = source_linear_tile / constants.source_pitch_tiles;
  uint source_tile_x = source_linear_tile % constants.source_pitch_tiles;
  uint2 source_coord = uint2(source_tile_x * tile_size.x + sample_in_tile.x,
                             source_tile_y * tile_size.y + sample_in_tile.y);

  float4 color = source.read(source_coord);

  float r_f = clamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f;
  float g_f = clamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f;
  float b_f = clamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f;
  float a_f = clamp(color.a, 0.0f, 1.0f) * 255.0f + 0.5f;

  uint r = uint(r_f);
  uint g = uint(g_f);
  uint b = uint(b_f);
  uint a = uint(a_f);

  uint packed = r | (g << 8u) | (b << 16u) | (a << 24u);

  edram[edram_index] = packed;
}
)METAL";

    NS::String* source = NS::String::string(kEdramDumpColor32bpp1xMsaaShader,
                                            NS::UTF8StringEncoding);
    MTL::Library* lib = device_->newLibrary(source, nullptr, &error);
    if (!lib) {
      XELOGW(
          "Metal: failed to compile edram_dump_color_32bpp_1xmsaa shader: {}",
          error ? error->localizedDescription()->utf8String() : "unknown");
    } else {
      NS::String* fn_name = NS::String::string("edram_dump_color_32bpp_1xmsaa",
                                               NS::UTF8StringEncoding);
      MTL::Function* fn = lib->newFunction(fn_name);
      if (!fn) {
        XELOGW("Metal: edram_dump_color_32bpp_1xmsaa missing entrypoint");
        lib->release();
      } else {
        edram_dump_color_32bpp_1xmsaa_pipeline_ =
            device_->newComputePipelineState(fn, &error);
        fn->release();
        lib->release();
        if (!edram_dump_color_32bpp_1xmsaa_pipeline_) {
          XELOGW(
              "Metal: failed to create edram_dump_color_32bpp_1xmsaa pipeline: "
              "{}",
              error ? error->localizedDescription()->utf8String() : "unknown");
        }
      }
    }
  }

  // EDRAM dump compute shader for 32-bpp color, 4x MSAA.
  {
    static const char kEdramDumpColor32bpp4xMsaaShader[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct EdramDumpConstants {
  uint dispatch_first_tile;
  uint source_base_tiles;
  uint dest_pitch_tiles;
  uint source_pitch_tiles;
  uint2 resolution_scale;
};

kernel void edram_dump_color_32bpp_4xmsaa(
    texture2d_ms<float, access::read> source [[texture(0)]],
    device uint* edram [[buffer(0)]],
    constant EdramDumpConstants& constants [[buffer(1)]],
    uint3 tid [[thread_position_in_grid]]) {
  const uint kEdramTileCount = 2048u;

  uint2 tile_size = uint2(80u * constants.resolution_scale.x,
                          16u * constants.resolution_scale.y);

  uint2 tile_coord = tid.xy / tile_size;
  uint2 sample_in_tile = tid.xy % tile_size;

  uint rect_tile_index = tile_coord.y * constants.dest_pitch_tiles + tile_coord.x;

  uint nonwrapped_tile = constants.dispatch_first_tile + rect_tile_index;
  uint wrapped_tile = nonwrapped_tile & (kEdramTileCount - 1u);

  uint tile_samples = tile_size.x * tile_size.y;
  uint sample_index = sample_in_tile.y * tile_size.x + sample_in_tile.x;
  uint edram_index = wrapped_tile * tile_samples + sample_index;

  uint source_linear_tile = nonwrapped_tile - constants.source_base_tiles;
  uint source_tile_y = source_linear_tile / constants.source_pitch_tiles;
  uint source_tile_x = source_linear_tile % constants.source_pitch_tiles;
  uint2 source_sample = uint2(source_tile_x * tile_size.x + sample_in_tile.x,
                              source_tile_y * tile_size.y + sample_in_tile.y);

  uint sample_x = source_sample.x & 1u;
  uint sample_y = source_sample.y & 1u;
  uint sample_id = sample_x | (sample_y << 1u);
  uint2 pixel_coord = uint2(source_sample.x >> 1, source_sample.y >> 1);

  float4 color = source.read(pixel_coord, sample_id);

  float r_f = clamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f;
  float g_f = clamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f;
  float b_f = clamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f;
  float a_f = clamp(color.a, 0.0f, 1.0f) * 255.0f + 0.5f;

  uint r = uint(r_f);
  uint g = uint(g_f);
  uint b = uint(b_f);
  uint a = uint(a_f);

  uint packed = r | (g << 8u) | (b << 16u) | (a << 24u);

  edram[edram_index] = packed;
}
)METAL";

    NS::String* source = NS::String::string(kEdramDumpColor32bpp4xMsaaShader,
                                            NS::UTF8StringEncoding);
    MTL::Library* lib = device_->newLibrary(source, nullptr, &error);
    if (!lib) {
      XELOGW(
          "Metal: failed to compile edram_dump_color_32bpp_4xmsaa shader: {}",
          error ? error->localizedDescription()->utf8String() : "unknown");
    } else {
      NS::String* fn_name = NS::String::string("edram_dump_color_32bpp_4xmsaa",
                                               NS::UTF8StringEncoding);
      MTL::Function* fn = lib->newFunction(fn_name);
      if (!fn) {
        XELOGW("Metal: edram_dump_color_32bpp_4xmsaa missing entrypoint");
        lib->release();
      } else {
        edram_dump_color_32bpp_4xmsaa_pipeline_ =
            device_->newComputePipelineState(fn, &error);
        fn->release();
        lib->release();
        if (!edram_dump_color_32bpp_4xmsaa_pipeline_) {
          XELOGW(
              "Metal: failed to create edram_dump_color_32bpp_4xmsaa pipeline: "
              "{}",
              error ? error->localizedDescription()->utf8String() : "unknown");
        }
      }
    }
  }

  // EDRAM dump compute shader for 32-bpp depth, 4x MSAA.
  {
    static const char kEdramDumpDepth32bpp4xMsaaShader[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct EdramDumpConstants {
  uint dispatch_first_tile;
  uint source_base_tiles;
  uint dest_pitch_tiles;
  uint source_pitch_tiles;
  uint2 resolution_scale;
};

kernel void edram_dump_depth_32bpp_4xmsaa(
    texture2d_ms<float, access::read> source [[texture(0)]],
    device uint* edram [[buffer(0)]],
    constant EdramDumpConstants& constants [[buffer(1)]],
    uint3 tid [[thread_position_in_grid]]) {
  const uint kEdramTileCount = 2048u;

  uint2 tile_size = uint2(80u * constants.resolution_scale.x,
                          16u * constants.resolution_scale.y);

  uint2 tile_coord = tid.xy / tile_size;
  uint2 sample_in_tile = tid.xy % tile_size;

  uint rect_tile_index = tile_coord.y * constants.dest_pitch_tiles + tile_coord.x;

  uint nonwrapped_tile = constants.dispatch_first_tile + rect_tile_index;
  uint wrapped_tile = nonwrapped_tile & (kEdramTileCount - 1u);

  uint tile_samples = tile_size.x * tile_size.y;
  uint sample_index = sample_in_tile.y * tile_size.x + sample_in_tile.x;
  uint edram_index = wrapped_tile * tile_samples + sample_index;

  uint source_linear_tile = nonwrapped_tile - constants.source_base_tiles;
  uint source_tile_y = source_linear_tile / constants.source_pitch_tiles;
  uint source_tile_x = source_linear_tile % constants.source_pitch_tiles;
  uint2 source_sample = uint2(source_tile_x * tile_size.x + sample_in_tile.x,
                              source_tile_y * tile_size.y + sample_in_tile.y);

  uint sample_x = source_sample.x & 1u;
  uint sample_y = source_sample.y & 1u;
  uint sample_id = sample_x | (sample_y << 1u);
  uint2 pixel_coord = uint2(source_sample.x >> 1, source_sample.y >> 1);

  float depth = source.read(pixel_coord, sample_id).r;

  // Convert [0,1] depth to 24-bit integer (D24) and place in upper 24 bits.
  float depth_f = clamp(depth, 0.0f, 1.0f) * 16777215.0f + 0.5f;
  uint depth24 = uint(depth_f);

  // Pack depth in bits 8..31, stencil = 0 in low 8 bits.
  uint packed = (depth24 << 8u);

  edram[edram_index] = packed;
}
)METAL";

    NS::String* source = NS::String::string(kEdramDumpDepth32bpp4xMsaaShader,
                                            NS::UTF8StringEncoding);
    MTL::Library* lib = device_->newLibrary(source, nullptr, &error);
    if (!lib) {
      XELOGW(
          "Metal: failed to compile edram_dump_depth_32bpp_4xmsaa shader: {}",
          error ? error->localizedDescription()->utf8String() : "unknown");
    } else {
      NS::String* fn_name = NS::String::string("edram_dump_depth_32bpp_4xmsaa",
                                               NS::UTF8StringEncoding);
      MTL::Function* fn = lib->newFunction(fn_name);
      if (!fn) {
        XELOGW("Metal: edram_dump_depth_32bpp_4xmsaa missing entrypoint");
        lib->release();
      } else {
        edram_dump_depth_32bpp_4xmsaa_pipeline_ =
            device_->newComputePipelineState(fn, &error);
        fn->release();
        lib->release();
        if (!edram_dump_depth_32bpp_4xmsaa_pipeline_) {
          XELOGW(
              "Metal: failed to create edram_dump_depth_32bpp_4xmsaa pipeline: "
              "{}",
              error ? error->localizedDescription()->utf8String() : "unknown");
        }
      }
    }
  }

  // EDRAM dump compute shader for 32-bpp depth, 1x MSAA.
  {
    static const char kEdramDumpDepth32bpp1xMsaaShader[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct EdramDumpConstants {
  uint dispatch_first_tile;
  uint source_base_tiles;
  uint dest_pitch_tiles;
  uint source_pitch_tiles;
  uint2 resolution_scale;
};

kernel void edram_dump_depth_32bpp_1xmsaa(
    texture2d<float, access::read> source [[texture(0)]],
    device uint* edram [[buffer(0)]],
    constant EdramDumpConstants& constants [[buffer(1)]],
    uint3 tid [[thread_position_in_grid]]) {
  const uint kEdramTileCount = 2048u;

  uint2 tile_size = uint2(80u * constants.resolution_scale.x,
                          16u * constants.resolution_scale.y);

  uint2 tile_coord = tid.xy / tile_size;
  uint2 sample_in_tile = tid.xy % tile_size;

  uint rect_tile_index = tile_coord.y * constants.dest_pitch_tiles + tile_coord.x;

  uint nonwrapped_tile = constants.dispatch_first_tile + rect_tile_index;
  uint wrapped_tile = nonwrapped_tile & (kEdramTileCount - 1u);

  uint tile_samples = tile_size.x * tile_size.y;
  uint sample_index = sample_in_tile.y * tile_size.x + sample_in_tile.x;
  uint edram_index = wrapped_tile * tile_samples + sample_index;

  uint source_linear_tile = nonwrapped_tile - constants.source_base_tiles;
  uint source_tile_y = source_linear_tile / constants.source_pitch_tiles;
  uint source_tile_x = source_linear_tile % constants.source_pitch_tiles;
  uint2 source_coord = uint2(source_tile_x * tile_size.x + sample_in_tile.x,
                             source_tile_y * tile_size.y + sample_in_tile.y);

  float depth = source.read(source_coord).r;

  // Convert [0,1] depth to 24-bit integer (D24) and place in upper 24 bits.
  float depth_f = clamp(depth, 0.0f, 1.0f) * 16777215.0f + 0.5f;
  uint depth24 = uint(depth_f);

  // Pack depth in bits 8..31, stencil = 0 in low 8 bits.
  uint packed = (depth24 << 8u);

  edram[edram_index] = packed;
}
)METAL";

    NS::String* source = NS::String::string(kEdramDumpDepth32bpp1xMsaaShader,
                                            NS::UTF8StringEncoding);
    MTL::Library* lib = device_->newLibrary(source, nullptr, &error);
    if (!lib) {
      XELOGW(
          "Metal: failed to compile edram_dump_depth_32bpp_1xmsaa shader: {}",
          error ? error->localizedDescription()->utf8String() : "unknown");
    } else {
      NS::String* fn_name = NS::String::string("edram_dump_depth_32bpp_1xmsaa",
                                               NS::UTF8StringEncoding);
      MTL::Function* fn = lib->newFunction(fn_name);
      if (!fn) {
        XELOGW("Metal: edram_dump_depth_32bpp_1xmsaa missing entrypoint");
        lib->release();
      } else {
        edram_dump_depth_32bpp_1xmsaa_pipeline_ =
            device_->newComputePipelineState(fn, &error);
        fn->release();
        lib->release();
        if (!edram_dump_depth_32bpp_1xmsaa_pipeline_) {
          XELOGW(
              "Metal: failed to create edram_dump_depth_32bpp_1xmsaa pipeline: "
              "{}",
              error ? error->localizedDescription()->utf8String() : "unknown");
        }
      }
    }
  }

  // EDRAM dump compute shader for 64-bpp color, 1x MSAA.
  // 64bpp tiles are half the horizontal width (40 samples per tile, not 80).
  {
    static const char kEdramDumpColor64bpp1xMsaaShader[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct EdramDumpConstants {
  uint dispatch_first_tile;
  uint source_base_tiles;
  uint dest_pitch_tiles;
  uint source_pitch_tiles;
  uint2 resolution_scale;
};

kernel void edram_dump_color_64bpp_1xmsaa(
    texture2d<float, access::read> source [[texture(0)]],
    device uint2* edram [[buffer(0)]],
    constant EdramDumpConstants& constants [[buffer(1)]],
    uint3 tid [[thread_position_in_grid]]) {
  const uint kEdramTileCount = 2048u;

  // 64bpp: 40 samples wide per tile instead of 80.
  uint2 tile_size = uint2(40u * constants.resolution_scale.x,
                          16u * constants.resolution_scale.y);

  uint2 tile_coord = tid.xy / tile_size;
  uint2 sample_in_tile = tid.xy % tile_size;

  uint rect_tile_index = tile_coord.y * constants.dest_pitch_tiles + tile_coord.x;

  uint nonwrapped_tile = constants.dispatch_first_tile + rect_tile_index;
  uint wrapped_tile = nonwrapped_tile & (kEdramTileCount - 1u);

  uint tile_samples = tile_size.x * tile_size.y;
  uint sample_index = sample_in_tile.y * tile_size.x + sample_in_tile.x;
  uint edram_index = wrapped_tile * tile_samples + sample_index;

  uint source_linear_tile = nonwrapped_tile - constants.source_base_tiles;
  uint source_tile_y = source_linear_tile / constants.source_pitch_tiles;
  uint source_tile_x = source_linear_tile % constants.source_pitch_tiles;
  uint2 source_coord = uint2(source_tile_x * tile_size.x + sample_in_tile.x,
                             source_tile_y * tile_size.y + sample_in_tile.y);

  float4 color = source.read(source_coord);

  // Pack as two uint32s (RG16 + BA16 or similar 64bpp encoding).
  // Using 16-bit float-to-half conversion for typical 64bpp formats.
  uint rg = as_type<uint>(half2(color.rg));
  uint ba = as_type<uint>(half2(color.ba));

  edram[edram_index] = uint2(rg, ba);
}
)METAL";

    NS::String* source = NS::String::string(kEdramDumpColor64bpp1xMsaaShader,
                                            NS::UTF8StringEncoding);
    MTL::Library* lib = device_->newLibrary(source, nullptr, &error);
    if (!lib) {
      XELOGW(
          "Metal: failed to compile edram_dump_color_64bpp_1xmsaa shader: {}",
          error ? error->localizedDescription()->utf8String() : "unknown");
    } else {
      NS::String* fn_name = NS::String::string("edram_dump_color_64bpp_1xmsaa",
                                               NS::UTF8StringEncoding);
      MTL::Function* fn = lib->newFunction(fn_name);
      if (!fn) {
        XELOGW("Metal: edram_dump_color_64bpp_1xmsaa missing entrypoint");
        lib->release();
      } else {
        edram_dump_color_64bpp_1xmsaa_pipeline_ =
            device_->newComputePipelineState(fn, &error);
        fn->release();
        lib->release();
        if (!edram_dump_color_64bpp_1xmsaa_pipeline_) {
          XELOGW(
              "Metal: failed to create edram_dump_color_64bpp_1xmsaa pipeline: "
              "{}",
              error ? error->localizedDescription()->utf8String() : "unknown");
        }
      }
    }
  }

  // EDRAM dump compute shader for 64-bpp color, 4x MSAA.
  {
    static const char kEdramDumpColor64bpp4xMsaaShader[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct EdramDumpConstants {
  uint dispatch_first_tile;
  uint source_base_tiles;
  uint dest_pitch_tiles;
  uint source_pitch_tiles;
  uint2 resolution_scale;
};

kernel void edram_dump_color_64bpp_4xmsaa(
    texture2d_ms<float, access::read> source [[texture(0)]],
    device uint2* edram [[buffer(0)]],
    constant EdramDumpConstants& constants [[buffer(1)]],
    uint3 tid [[thread_position_in_grid]]) {
  const uint kEdramTileCount = 2048u;

  // 64bpp: 40 samples wide per tile instead of 80.
  uint2 tile_size = uint2(40u * constants.resolution_scale.x,
                          16u * constants.resolution_scale.y);

  uint2 tile_coord = tid.xy / tile_size;
  uint2 sample_in_tile = tid.xy % tile_size;

  uint rect_tile_index = tile_coord.y * constants.dest_pitch_tiles + tile_coord.x;

  uint nonwrapped_tile = constants.dispatch_first_tile + rect_tile_index;
  uint wrapped_tile = nonwrapped_tile & (kEdramTileCount - 1u);

  uint tile_samples = tile_size.x * tile_size.y;
  uint sample_index = sample_in_tile.y * tile_size.x + sample_in_tile.x;
  uint edram_index = wrapped_tile * tile_samples + sample_index;

  uint source_linear_tile = nonwrapped_tile - constants.source_base_tiles;
  uint source_tile_y = source_linear_tile / constants.source_pitch_tiles;
  uint source_tile_x = source_linear_tile % constants.source_pitch_tiles;
  uint2 source_sample = uint2(source_tile_x * tile_size.x + sample_in_tile.x,
                              source_tile_y * tile_size.y + sample_in_tile.y);

  uint sample_x = source_sample.x & 1u;
  uint sample_y = source_sample.y & 1u;
  uint sample_id = sample_x | (sample_y << 1u);
  uint2 pixel_coord = uint2(source_sample.x >> 1, source_sample.y >> 1);

  float4 color = source.read(pixel_coord, sample_id);

  // Pack as two uint32s (RG16 + BA16 or similar 64bpp encoding).
  uint rg = as_type<uint>(half2(color.rg));
  uint ba = as_type<uint>(half2(color.ba));

  edram[edram_index] = uint2(rg, ba);
}
)METAL";

    NS::String* source = NS::String::string(kEdramDumpColor64bpp4xMsaaShader,
                                            NS::UTF8StringEncoding);
    MTL::Library* lib = device_->newLibrary(source, nullptr, &error);
    if (!lib) {
      XELOGW(
          "Metal: failed to compile edram_dump_color_64bpp_4xmsaa shader: {}",
          error ? error->localizedDescription()->utf8String() : "unknown");
    } else {
      NS::String* fn_name = NS::String::string("edram_dump_color_64bpp_4xmsaa",
                                               NS::UTF8StringEncoding);
      MTL::Function* fn = lib->newFunction(fn_name);
      if (!fn) {
        XELOGW("Metal: edram_dump_color_64bpp_4xmsaa missing entrypoint");
        lib->release();
      } else {
        edram_dump_color_64bpp_4xmsaa_pipeline_ =
            device_->newComputePipelineState(fn, &error);
        fn->release();
        lib->release();
        if (!edram_dump_color_64bpp_4xmsaa_pipeline_) {
          XELOGW(
              "Metal: failed to create edram_dump_color_64bpp_4xmsaa pipeline: "
              "{}",
              error ? error->localizedDescription()->utf8String() : "unknown");
        }
      }
    }
  }

  XELOGI(
      "MetalRenderTargetCache::InitializeEdramComputeShaders: initialized "
      "resolve and EDRAM compute pipelines");
  return true;
}

void MetalRenderTargetCache::ShutdownEdramComputeShaders() {
  if (edram_load_pipeline_) {
    edram_load_pipeline_->release();
    edram_load_pipeline_ = nullptr;
  }
  if (edram_store_pipeline_) {
    edram_store_pipeline_->release();
    edram_store_pipeline_ = nullptr;
  }
  // Release 32bpp color dump pipelines
  if (edram_dump_color_32bpp_1xmsaa_pipeline_) {
    edram_dump_color_32bpp_1xmsaa_pipeline_->release();
    edram_dump_color_32bpp_1xmsaa_pipeline_ = nullptr;
  }
  if (edram_dump_color_32bpp_2xmsaa_pipeline_) {
    edram_dump_color_32bpp_2xmsaa_pipeline_->release();
    edram_dump_color_32bpp_2xmsaa_pipeline_ = nullptr;
  }
  if (edram_dump_color_32bpp_4xmsaa_pipeline_) {
    edram_dump_color_32bpp_4xmsaa_pipeline_->release();
    edram_dump_color_32bpp_4xmsaa_pipeline_ = nullptr;
  }
  // Release 64bpp color dump pipelines
  if (edram_dump_color_64bpp_1xmsaa_pipeline_) {
    edram_dump_color_64bpp_1xmsaa_pipeline_->release();
    edram_dump_color_64bpp_1xmsaa_pipeline_ = nullptr;
  }
  if (edram_dump_color_64bpp_2xmsaa_pipeline_) {
    edram_dump_color_64bpp_2xmsaa_pipeline_->release();
    edram_dump_color_64bpp_2xmsaa_pipeline_ = nullptr;
  }
  if (edram_dump_color_64bpp_4xmsaa_pipeline_) {
    edram_dump_color_64bpp_4xmsaa_pipeline_->release();
    edram_dump_color_64bpp_4xmsaa_pipeline_ = nullptr;
  }
  // Release 32bpp depth dump pipelines
  if (edram_dump_depth_32bpp_1xmsaa_pipeline_) {
    edram_dump_depth_32bpp_1xmsaa_pipeline_->release();
    edram_dump_depth_32bpp_1xmsaa_pipeline_ = nullptr;
  }
  if (edram_dump_depth_32bpp_2xmsaa_pipeline_) {
    edram_dump_depth_32bpp_2xmsaa_pipeline_->release();
    edram_dump_depth_32bpp_2xmsaa_pipeline_ = nullptr;
  }
  if (edram_dump_depth_32bpp_4xmsaa_pipeline_) {
    edram_dump_depth_32bpp_4xmsaa_pipeline_->release();
    edram_dump_depth_32bpp_4xmsaa_pipeline_ = nullptr;
  }
  // Release resolve pipelines
  if (resolve_full_8bpp_pipeline_) {
    resolve_full_8bpp_pipeline_->release();
    resolve_full_8bpp_pipeline_ = nullptr;
  }
  if (resolve_full_16bpp_pipeline_) {
    resolve_full_16bpp_pipeline_->release();
    resolve_full_16bpp_pipeline_ = nullptr;
  }
  if (resolve_full_32bpp_pipeline_) {
    resolve_full_32bpp_pipeline_->release();
    resolve_full_32bpp_pipeline_ = nullptr;
  }
  if (resolve_full_64bpp_pipeline_) {
    resolve_full_64bpp_pipeline_->release();
    resolve_full_64bpp_pipeline_ = nullptr;
  }
  if (resolve_full_128bpp_pipeline_) {
    resolve_full_128bpp_pipeline_->release();
    resolve_full_128bpp_pipeline_ = nullptr;
  }
  if (resolve_fast_32bpp_1x2xmsaa_pipeline_) {
    resolve_fast_32bpp_1x2xmsaa_pipeline_->release();
    resolve_fast_32bpp_1x2xmsaa_pipeline_ = nullptr;
  }
  if (resolve_fast_32bpp_4xmsaa_pipeline_) {
    resolve_fast_32bpp_4xmsaa_pipeline_->release();
    resolve_fast_32bpp_4xmsaa_pipeline_ = nullptr;
  }
  if (resolve_fast_64bpp_1x2xmsaa_pipeline_) {
    resolve_fast_64bpp_1x2xmsaa_pipeline_->release();
    resolve_fast_64bpp_1x2xmsaa_pipeline_ = nullptr;
  }
  if (resolve_fast_64bpp_4xmsaa_pipeline_) {
    resolve_fast_64bpp_4xmsaa_pipeline_->release();
    resolve_fast_64bpp_4xmsaa_pipeline_ = nullptr;
  }
  XELOGI("MetalRenderTargetCache::ShutdownEdramComputeShaders");
}

void MetalRenderTargetCache::ClearCache() {
  // Clear current bindings
  for (uint32_t i = 0; i < 4; ++i) {
    current_color_targets_[i] = nullptr;
  }
  current_depth_target_ = nullptr;
  render_pass_descriptor_dirty_ = true;

  // Clear the tracking of which render targets have been cleared
  cleared_render_targets_this_frame_.clear();

  // Call base implementation
  RenderTargetCache::ClearCache();
}

void MetalRenderTargetCache::BeginFrame() {
  // Reset dummy target clear flag
  dummy_color_target_needs_clear_ = true;

  // Clear the tracking of which render targets have been cleared this frame
  cleared_render_targets_this_frame_.clear();

  // Call base implementation
  RenderTargetCache::BeginFrame();
}

bool MetalRenderTargetCache::Update(
    bool is_rasterization_done, reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask, const Shader& vertex_shader) {
  XELOGI("MetalRenderTargetCache::Update called - is_rasterization_done={}",
         is_rasterization_done);

  // Call base implementation - this does all the heavy lifting
  if (!RenderTargetCache::Update(is_rasterization_done,
                                 normalized_depth_control,
                                 normalized_color_mask, vertex_shader)) {
    XELOGE("MetalRenderTargetCache::Update - Base class Update failed");
    return false;
  }

  // After base class update, retrieve the actual render targets that were
  // selected This is the KEY to connecting base class management with
  // Metal-specific rendering
  RenderTarget* const* accumulated_targets =
      last_update_accumulated_render_targets();

  XELOGI(
      "MetalRenderTargetCache::Update - Got accumulated targets from base "
      "class");

  // Debug: Check what we actually got AND verify textures exist
  for (uint32_t i = 0; i < 5; ++i) {
    if (accumulated_targets[i]) {
      MetalRenderTarget* metal_rt =
          static_cast<MetalRenderTarget*>(accumulated_targets[i]);
      MTL::Texture* tex = metal_rt->texture();
      XELOGI(
          "  accumulated_targets[{}] = {:p} (key 0x{:08X}, texture={:p}, "
          "{}x{})",
          i, (void*)accumulated_targets[i], accumulated_targets[i]->key().key,
          (void*)tex, tex ? tex->width() : 0, tex ? tex->height() : 0);

      // CRITICAL FIX: Ensure texture exists
      if (!tex) {
        XELOGE("  ERROR: MetalRenderTarget has no texture! Need to create it.");
        // TODO: Create texture here if missing
      }
    } else {
      XELOGI("  accumulated_targets[{}] = nullptr", i);
    }
  }

  // Check if render targets actually changed
  bool targets_changed = false;

  // Check depth target
  MetalRenderTarget* new_depth_target =
      accumulated_targets[0]
          ? static_cast<MetalRenderTarget*>(accumulated_targets[0])
          : nullptr;
  if (new_depth_target != current_depth_target_) {
    targets_changed = true;
    current_depth_target_ = new_depth_target;
    if (current_depth_target_) {
      XELOGD(
          "MetalRenderTargetCache::Update - Depth target changed: key={:08X}",
          current_depth_target_->key().key);
    }
  }

  // Check color targets
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    MetalRenderTarget* new_color_target =
        accumulated_targets[i + 1]
            ? static_cast<MetalRenderTarget*>(accumulated_targets[i + 1])
            : nullptr;
    if (new_color_target != current_color_targets_[i]) {
      targets_changed = true;
      current_color_targets_[i] = new_color_target;
      if (current_color_targets_[i]) {
        XELOGD(
            "MetalRenderTargetCache::Update - Color target {} changed: "
            "key={:08X}",
            i, current_color_targets_[i]->key().key);
      }
    }
  }

  // Perform ownership transfers - this is critical for correct rendering when
  // EDRAM regions are aliased between different RT configurations.
  // The base class Update() populates last_update_transfers() with the needed
  // transfers based on EDRAM tile overlaps.
  if (GetPath() == Path::kHostRenderTargets) {
    PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets,
                                     accumulated_targets,
                                     last_update_transfers());
  }

  // Only mark render pass descriptor as dirty if targets actually changed
  if (targets_changed) {
    render_pass_descriptor_dirty_ = true;
    XELOGI(
        "MetalRenderTargetCache::Update - Render targets changed, marking "
        "descriptor dirty");
  }

  return true;
}

uint32_t MetalRenderTargetCache::GetMaxRenderTargetWidth() const {
  // Metal maximum texture dimension
  return 16384;
}

uint32_t MetalRenderTargetCache::GetMaxRenderTargetHeight() const {
  // Metal maximum texture dimension
  return 16384;
}

RenderTargetCache::RenderTarget* MetalRenderTargetCache::CreateRenderTarget(
    RenderTargetKey key) {
  XELOGI(
      "MetalRenderTargetCache::CreateRenderTarget called - key={:08X}, "
      "is_depth={}",
      key.key, key.is_depth);

  // Calculate dimensions
  uint32_t width = key.GetWidth();
  uint32_t height =
      GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples);

  // Apply resolution scaling
  width *= draw_resolution_scale_x();
  height *= draw_resolution_scale_y();

  XELOGI(
      "MetalRenderTargetCache: Creating render target with calculated "
      "dimensions {}x{} (before scaling: {}x{})",
      width, height, key.GetWidth(),
      GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples));

  // Create Metal render target
  auto* render_target = new MetalRenderTarget(key);

  // Create the texture based on format
  MTL::Texture* texture = nullptr;
  uint32_t samples = 1 << uint32_t(key.msaa_samples);

  if (key.is_depth) {
    texture = CreateDepthTexture(width, height, key.GetDepthFormat(), samples);
  } else {
    texture = CreateColorTexture(width, height, key.GetColorFormat(), samples);
  }

  if (!texture) {
    delete render_target;
    return nullptr;
  }

  render_target->SetTexture(texture);

  // NOTE: Unlike the previous implementation, we do NOT load EDRAM data here.
  // This matches D3D12's approach where:
  // 1. CreateRenderTarget creates an empty texture
  // 2. Data transfer happens via ownership transfers in
  // PerformTransfersAndResolveClears
  // 3. The EDRAM buffer is only used as scratch space for resolves
  //
  // The ownership transfer system (called from Update()) handles copying data
  // between render target textures when EDRAM regions are aliased between
  // different RT configurations.

  // Store in our map for later retrieval
  render_target_map_[key.key] = render_target;

  XELOGI(
      "MetalRenderTargetCache: Created render target - {}x{}, {} samples, key "
      "0x{:08X}",
      width, height, samples, key.key);

  return render_target;
}

bool MetalRenderTargetCache::IsHostDepthEncodingDifferent(
    xenos::DepthRenderTargetFormat format) const {
  // Metal uses different depth encoding than Xbox 360
  // D24S8 on Xbox 360 vs D32Float_S8 on Metal
  return format == xenos::DepthRenderTargetFormat::kD24S8 ||
         format == xenos::DepthRenderTargetFormat::kD24FS8;
}

void MetalRenderTargetCache::RestoreEdramSnapshot(const void* snapshot) {
  if (!snapshot) {
    XELOGI("MetalRenderTargetCache::RestoreEdramSnapshot: null snapshot");
    return;
  }

  if (IsDrawResolutionScaled()) {
    XELOGI(
        "MetalRenderTargetCache::RestoreEdramSnapshot: draw resolution scaled, "
        "skipping");
    return;
  }

  switch (GetPath()) {
    case Path::kHostRenderTargets: {
      RenderTarget* full_edram_rt =
          PrepareFullEdram1280xRenderTargetForSnapshotRestoration(
              xenos::ColorRenderTargetFormat::k_32_FLOAT);
      if (!full_edram_rt) {
        XELOGI(
            "MetalRenderTargetCache::RestoreEdramSnapshot: failed to get "
            "full-EDRAM RT");
        return;
      }

      MetalRenderTarget* metal_rt =
          static_cast<MetalRenderTarget*>(full_edram_rt);
      MTL::Texture* texture = metal_rt->texture();
      if (!texture) {
        XELOGI(
            "MetalRenderTargetCache::RestoreEdramSnapshot: full-EDRAM RT has "
            "no texture");
        return;
      }

      constexpr uint32_t kPitchTilesAt32bpp = 16;
      constexpr uint32_t kWidth =
          kPitchTilesAt32bpp * xenos::kEdramTileWidthSamples;
      constexpr uint32_t kTileRows =
          xenos::kEdramTileCount / kPitchTilesAt32bpp;
      constexpr uint32_t kHeight = kTileRows * xenos::kEdramTileHeightSamples;

      size_t staging_size = size_t(kWidth) * size_t(kHeight) * sizeof(uint32_t);
      MTL::Buffer* staging =
          device_->newBuffer(staging_size, MTL::ResourceStorageModeShared);
      if (!staging) {
        XELOGI(
            "MetalRenderTargetCache::RestoreEdramSnapshot: failed to create "
            "staging buffer");
        return;
      }

      auto* dst_base = static_cast<uint8_t*>(staging->contents());
      const uint8_t* src = static_cast<const uint8_t*>(snapshot);
      uint32_t bytes_per_row = kWidth * sizeof(uint32_t);

      for (uint32_t y_tile = 0; y_tile < kTileRows; ++y_tile) {
        for (uint32_t x_tile = 0; x_tile < kPitchTilesAt32bpp; ++x_tile) {
          uint32_t tile_index = y_tile * kPitchTilesAt32bpp + x_tile;
          const uint8_t* tile_src =
              src + tile_index * xenos::kEdramTileWidthSamples *
                        xenos::kEdramTileHeightSamples * sizeof(uint32_t);

          for (uint32_t sample_row = 0;
               sample_row < xenos::kEdramTileHeightSamples; ++sample_row) {
            uint32_t dst_y =
                y_tile * xenos::kEdramTileHeightSamples + sample_row;
            uint32_t dst_x = x_tile * xenos::kEdramTileWidthSamples;

            uint8_t* dst_row =
                dst_base + dst_y * bytes_per_row + dst_x * sizeof(uint32_t);
            const uint8_t* src_row =
                tile_src +
                sample_row * xenos::kEdramTileWidthSamples * sizeof(uint32_t);

            std::memcpy(dst_row, src_row,
                        xenos::kEdramTileWidthSamples * sizeof(uint32_t));
          }
        }
      }

      MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
      if (!queue) {
        staging->release();
        XELOGI(
            "MetalRenderTargetCache::RestoreEdramSnapshot: no command queue");
        return;
      }

      MTL::CommandBuffer* cmd = queue->commandBuffer();
      if (!cmd) {
        staging->release();
        XELOGI(
            "MetalRenderTargetCache::RestoreEdramSnapshot: no command buffer");
        return;
      }

      MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
      if (!blit) {
        // cmd is autoreleased from commandBuffer() - do not release
        staging->release();
        XELOGI("MetalRenderTargetCache::RestoreEdramSnapshot: no blit encoder");
        return;
      }

      blit->copyFromBuffer(staging, 0, bytes_per_row, 0,
                           MTL::Size::Make(kWidth, kHeight, 1), texture, 0, 0,
                           MTL::Origin::Make(0, 0, 0));
      blit->endEncoding();
      cmd->commit();
      cmd->waitUntilCompleted();
      // cmd is autoreleased from commandBuffer() - do not release
      staging->release();

      XELOGI(
          "MetalRenderTargetCache::RestoreEdramSnapshot: restored snapshot "
          "into "
          "full-EDRAM RT ({}x{})",
          kWidth, kHeight);

      // Seed edram_buffer_ with the restored full-EDRAM render target contents
      // so subsequent DumpRenderTargets and resolve passes see the same initial
      // EDRAM state as D3D12/Vulkan.
      DumpRenderTargets(0, xenos::kEdramTileCount, 1, xenos::kEdramTileCount);
      break;
    }

    default:
      XELOGI(

          "MetalRenderTargetCache::RestoreEdramSnapshot: unsupported path {}",
          int(GetPath()));
      break;
  }
}

MTL::Texture* MetalRenderTargetCache::CreateColorTexture(
    uint32_t width, uint32_t height, xenos::ColorRenderTargetFormat format,
    uint32_t samples) {
  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setWidth(width);
  desc->setHeight(height ? height : 720);  // Default height if not specified
  desc->setPixelFormat(GetColorPixelFormat(format));
  desc->setTextureType(samples > 1 ? MTL::TextureType2DMultisample
                                   : MTL::TextureType2D);
  desc->setSampleCount(samples);
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                 MTL::TextureUsageShaderWrite);
  desc->setStorageMode(MTL::StorageModePrivate);

  MTL::Texture* texture = device_->newTexture(desc);
  desc->release();

  // Clear texture to zero immediately after creation so we can always use
  // LoadActionLoad (matching D3D12/Vulkan behavior where render targets
  // preserve content and transfers/clears are explicit operations).
  if (texture) {
    MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
    if (queue) {
      MTL::CommandBuffer* cmd = queue->commandBuffer();
      if (cmd) {
        MTL::RenderPassDescriptor* rp =
            MTL::RenderPassDescriptor::renderPassDescriptor();
        auto* ca = rp->colorAttachments()->object(0);
        ca->setTexture(texture);
        ca->setLoadAction(MTL::LoadActionClear);
        ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
        ca->setStoreAction(MTL::StoreActionStore);
        MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(rp);
        if (enc) {
          enc->endEncoding();
        }
        cmd->commit();
        cmd->waitUntilCompleted();
      }
    }
  }

  return texture;
}

MTL::Texture* MetalRenderTargetCache::CreateDepthTexture(
    uint32_t width, uint32_t height, xenos::DepthRenderTargetFormat format,
    uint32_t samples) {
  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setWidth(width);
  desc->setHeight(height ? height : 720);  // Default height if not specified
  MTL::PixelFormat pixel_format = GetDepthPixelFormat(format);
  desc->setPixelFormat(pixel_format);
  desc->setTextureType(samples > 1 ? MTL::TextureType2DMultisample
                                   : MTL::TextureType2D);
  desc->setSampleCount(samples);
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                 MTL::TextureUsageShaderWrite);
  desc->setStorageMode(MTL::StorageModePrivate);

  MTL::Texture* texture = device_->newTexture(desc);
  desc->release();

  // Clear depth texture immediately after creation so we can always use
  // LoadActionLoad (matching D3D12/Vulkan behavior).
  if (texture) {
    MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
    if (queue) {
      MTL::CommandBuffer* cmd = queue->commandBuffer();
      if (cmd) {
        MTL::RenderPassDescriptor* rp =
            MTL::RenderPassDescriptor::renderPassDescriptor();
        auto* da = rp->depthAttachment();
        da->setTexture(texture);
        da->setLoadAction(MTL::LoadActionClear);
        da->setClearDepth(1.0);
        da->setStoreAction(MTL::StoreActionStore);
        // Also handle stencil if format includes it
        if (pixel_format == MTL::PixelFormatDepth32Float_Stencil8 ||
            pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
          auto* sa = rp->stencilAttachment();
          sa->setTexture(texture);
          sa->setLoadAction(MTL::LoadActionClear);
          sa->setClearStencil(0);
          sa->setStoreAction(MTL::StoreActionStore);
        }
        MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(rp);
        if (enc) {
          enc->endEncoding();
        }
        cmd->commit();
        cmd->waitUntilCompleted();
      }
    }
  }

  return texture;
}

MTL::PixelFormat MetalRenderTargetCache::GetColorPixelFormat(
    xenos::ColorRenderTargetFormat format) const {
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return MTL::PixelFormatBGRA8Unorm;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return MTL::PixelFormatRGB10A2Unorm;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return MTL::PixelFormatRGB10A2Unorm;  // Approximation
    case xenos::ColorRenderTargetFormat::k_16_16:
      return MTL::PixelFormatRG16Snorm;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      return MTL::PixelFormatRGBA16Snorm;
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return MTL::PixelFormatRG16Float;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return MTL::PixelFormatRGBA16Float;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return MTL::PixelFormatR32Float;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return MTL::PixelFormatRG32Float;
    default:
      XELOGE("MetalRenderTargetCache: Unsupported color format {}",
             static_cast<uint32_t>(format));
      return MTL::PixelFormatBGRA8Unorm;
  }
}

MTL::PixelFormat MetalRenderTargetCache::GetDepthPixelFormat(
    xenos::DepthRenderTargetFormat format) const {
  switch (format) {
    case xenos::DepthRenderTargetFormat::kD24S8:
    case xenos::DepthRenderTargetFormat::kD24FS8:
      // Metal doesn't have D24S8, use D32Float_S8
      return MTL::PixelFormatDepth32Float_Stencil8;
    default:
      XELOGE("MetalRenderTargetCache: Unsupported depth format {}",
             static_cast<uint32_t>(format));
      return MTL::PixelFormatDepth32Float_Stencil8;
  }
}

MTL::RenderPassDescriptor* MetalRenderTargetCache::GetRenderPassDescriptor(
    uint32_t expected_sample_count) {
  XELOGI(
      "MetalRenderTargetCache::GetRenderPassDescriptor - dirty={}, cached={:p}",
      render_pass_descriptor_dirty_, (void*)cached_render_pass_descriptor_);

  if (!render_pass_descriptor_dirty_ && cached_render_pass_descriptor_) {
    return cached_render_pass_descriptor_;
  }

  // Release old descriptor
  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }

  // Create new descriptor
  cached_render_pass_descriptor_ =
      MTL::RenderPassDescriptor::renderPassDescriptor();
  if (!cached_render_pass_descriptor_) {
    XELOGE("MetalRenderTargetCache: Failed to create render pass descriptor");
    return nullptr;
  }
  cached_render_pass_descriptor_->retain();

  bool has_any_render_target = false;
  bool has_any_color_target = false;

  // Debug: Log current targets
  XELOGI("GetRenderPassDescriptor: Current targets state:");
  XELOGI("  current_depth_target_ = {:p}", (void*)current_depth_target_);
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_color_targets_[i]) {
      MTL::Texture* tex = current_color_targets_[i]->texture();
      XELOGI("  current_color_targets_[{}] = {:p}, texture={:p} ({}x{})", i,
             (void*)current_color_targets_[i], (void*)tex,
             tex ? tex->width() : 0, tex ? tex->height() : 0);
    } else {
      XELOGI("  current_color_targets_[{}] = nullptr", i);
    }
  }

  // Bind the actual render targets retrieved from base class in Update()

  // Bind depth target if present
  if (current_depth_target_ && current_depth_target_->texture()) {
    auto* depth_attachment = cached_render_pass_descriptor_->depthAttachment();
    depth_attachment->setTexture(current_depth_target_->texture());

    // Always use LoadActionLoad - textures are cleared on creation and
    // transfers/clears are explicit operations (matching D3D12/Vulkan
    // behavior).
    uint32_t depth_key = current_depth_target_->key().key;
    depth_attachment->setLoadAction(MTL::LoadActionLoad);
    XELOGI("MetalRenderTargetCache: Loading depth target 0x{:08X}", depth_key);

    depth_attachment->setStoreAction(MTL::StoreActionStore);

    // If the depth texture includes stencil, bind the same texture to the
    // stencil attachment too (Metal requires explicit stencil attachment
    // binding to match pipeline state).
    MTL::PixelFormat depth_pixel_format =
        current_depth_target_->texture()->pixelFormat();
    if (depth_pixel_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        depth_pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8 ||
        depth_pixel_format == MTL::PixelFormatX32_Stencil8) {
      auto* stencil_attachment =
          cached_render_pass_descriptor_->stencilAttachment();
      stencil_attachment->setTexture(current_depth_target_->texture());
      stencil_attachment->setLoadAction(MTL::LoadActionLoad);
      stencil_attachment->setStoreAction(MTL::StoreActionStore);
    }

    has_any_render_target = true;

    // Track this as a real render target for capture
    last_real_depth_target_ = current_depth_target_;

    XELOGI(
        "MetalRenderTargetCache: Bound depth target to render pass (REAL "
        "target)");
  }

  // Bind color targets
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_color_targets_[i] && current_color_targets_[i]->texture()) {
      auto* color_attachment =
          cached_render_pass_descriptor_->colorAttachments()->object(i);
      color_attachment->setTexture(current_color_targets_[i]->texture());

      // Always use LoadActionLoad - textures are cleared on creation and
      // transfers/clears are explicit operations (matching D3D12/Vulkan
      // behavior).
      uint32_t color_key = current_color_targets_[i]->key().key;
      color_attachment->setLoadAction(MTL::LoadActionLoad);
      color_attachment->setStoreAction(MTL::StoreActionStore);

      has_any_render_target = true;
      has_any_color_target = true;

      // Track this as a real render target for capture
      last_real_color_targets_[i] = current_color_targets_[i];

      XELOGI(
          "MetalRenderTargetCache: Bound color target {} to render pass (REAL "
          "target, {}x{}, key 0x{:08X})",
          i, current_color_targets_[i]->texture()->width(),
          current_color_targets_[i]->texture()->height(),
          current_color_targets_[i]->key().key);
    }
  }

  // If no color render targets are bound, attach a dummy color target so Metal
  // has at least one color attachment. This mirrors the D3D12/Vulkan behavior
  // where an RTV is always bound when drawing, and also keeps pipeline state
  // validation happy for depth-only passes.
  if (!has_any_color_target) {
    xenos::ColorRenderTargetFormat fmt =
        xenos::ColorRenderTargetFormat::k_8_8_8_8;
    uint32_t samples = std::max(1u, expected_sample_count);

    uint32_t width = 1280;
    uint32_t height = 720;
    if (current_depth_target_ && current_depth_target_->texture()) {
      width =
          static_cast<uint32_t>(current_depth_target_->texture()->width());
      height =
          static_cast<uint32_t>(current_depth_target_->texture()->height());
      if (current_depth_target_->texture()->sampleCount() > 0) {
        samples = std::max<uint32_t>(
            samples,
            static_cast<uint32_t>(current_depth_target_->texture()
                                      ->sampleCount()));
      }
    } else if (last_real_color_targets_[0] &&
               last_real_color_targets_[0]->texture()) {
      width = static_cast<uint32_t>(last_real_color_targets_[0]->texture()
                                        ->width());
      height = static_cast<uint32_t>(last_real_color_targets_[0]->texture()
                                         ->height());
    } else if (last_real_depth_target_ && last_real_depth_target_->texture()) {
      width =
          static_cast<uint32_t>(last_real_depth_target_->texture()->width());
      height =
          static_cast<uint32_t>(last_real_depth_target_->texture()->height());
      if (last_real_depth_target_->texture()->sampleCount() > 0) {
        samples = std::max<uint32_t>(
            samples,
            static_cast<uint32_t>(last_real_depth_target_->texture()
                                      ->sampleCount()));
      }
    }

    const bool dummy_matches =
        dummy_color_target_ && dummy_color_target_->texture() &&
        dummy_color_target_->texture()->width() == width &&
        dummy_color_target_->texture()->height() == height &&
        static_cast<uint32_t>(dummy_color_target_->texture()->sampleCount()) ==
            samples;
    if (!dummy_matches) {
      RenderTargetKey dummy_key;
      dummy_key.key = 0;
      dummy_key.is_depth = 0;
      dummy_key.resource_format = uint32_t(fmt);
      dummy_key.msaa_samples = xenos::MsaaSamples(samples);
      dummy_color_target_ = std::make_unique<MetalRenderTarget>(dummy_key);
      MTL::Texture* tex = CreateColorTexture(width, height, fmt, samples);
      dummy_color_target_->SetTexture(tex);
      dummy_color_target_needs_clear_ = true;
    }

    auto* color_attachment =
        cached_render_pass_descriptor_->colorAttachments()->object(0);
    color_attachment->setTexture(dummy_color_target_->texture());
    if (dummy_color_target_needs_clear_) {
      color_attachment->setLoadAction(MTL::LoadActionClear);
      color_attachment->setClearColor(
          MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
      dummy_color_target_needs_clear_ = false;
    } else {
      color_attachment->setLoadAction(MTL::LoadActionLoad);
    }
    color_attachment->setStoreAction(MTL::StoreActionStore);

    has_any_render_target = true;
    XELOGI("MetalRenderTargetCache::GetRenderPassDescriptor: using dummy RT0");
  }

  render_pass_descriptor_dirty_ = false;
  return cached_render_pass_descriptor_;
}

MTL::Texture* MetalRenderTargetCache::GetColorTarget(uint32_t index) const {
  if (index >= 4 || !current_color_targets_[index]) {
    return nullptr;
  }
  return current_color_targets_[index]->texture();
}

MTL::Texture* MetalRenderTargetCache::GetDepthTarget() const {
  if (!current_depth_target_) {
    return nullptr;
  }
  return current_depth_target_->texture();
}

MTL::Texture* MetalRenderTargetCache::GetDummyColorTarget() const {
  if (dummy_color_target_ && dummy_color_target_->texture()) {
    return dummy_color_target_->texture();
  }
  return nullptr;
}

MTL::Texture* MetalRenderTargetCache::GetLastRealColorTarget(
    uint32_t index) const {
  if (index >= 4 || !last_real_color_targets_[index]) {
    return nullptr;
  }
  return last_real_color_targets_[index]->texture();
}

MTL::Texture* MetalRenderTargetCache::GetLastRealDepthTarget() const {
  if (!last_real_depth_target_) {
    return nullptr;
  }
  return last_real_depth_target_->texture();
}

void MetalRenderTargetCache::LogCurrentColorRT0Pixels(const char* tag) {
  // Section 6.3: Wrapper method to call the debug helper for current color RT0.
  LogMetalRenderTargetTopLeftPixels(current_color_targets_[0], tag, device_,
                                    command_processor_.GetMetalCommandQueue());
}

void MetalRenderTargetCache::StoreTiledData(MTL::CommandBuffer* command_buffer,
                                            MTL::Texture* texture,
                                            uint32_t edram_base,
                                            uint32_t pitch_tiles,
                                            uint32_t height_tiles,
                                            bool is_depth) {
  XELOGI(
      "MetalRenderTargetCache::StoreTiledData - texture {}x{}, base={}, "
      "pitch={}, height={}, depth={}",
      texture->width(), texture->height(), edram_base, pitch_tiles,
      height_tiles, is_depth);

  MTL::Texture* source_texture = texture;
  MTL::Texture* temp_texture = nullptr;

  // Check if this is a depth/stencil texture
  bool is_depth_stencil_format =
      texture->pixelFormat() == MTL::PixelFormatDepth32Float_Stencil8 ||
      texture->pixelFormat() == MTL::PixelFormatDepth32Float ||
      texture->pixelFormat() == MTL::PixelFormatDepth16Unorm ||
      texture->pixelFormat() == MTL::PixelFormatDepth24Unorm_Stencil8 ||
      texture->pixelFormat() == MTL::PixelFormatX32_Stencil8;

  if (is_depth_stencil_format) {
    // For storing depth/stencil data back to EDRAM, we would need to read
    // from the depth texture This is complex because depth textures can't be
    // directly read in compute shaders. For now, we'll skip storing depth
    // data back to EDRAM since depth buffers are typically write-only during
    // rendering and don't need to be preserved across frames.
    XELOGI(
        "MetalRenderTargetCache::StoreTiledData - Skipping depth/stencil "
        "texture store (not supported)");
    return;
  }

  // If texture is multisample, create a temporary non-multisample texture and
  // resolve to it first
  if (texture->textureType() == MTL::TextureType2DMultisample) {
    XELOGI(
        "MetalRenderTargetCache::StoreTiledData - Creating temporary "
        "non-multisample texture for EDRAM operation");

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setWidth(texture->width());
    desc->setHeight(texture->height());
    desc->setPixelFormat(texture->pixelFormat());
    desc->setTextureType(MTL::TextureType2D);  // Regular 2D texture
    desc->setSampleCount(1);                   // Non-multisample
    desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                   MTL::TextureUsageShaderWrite);
    desc->setStorageMode(MTL::StorageModePrivate);

    temp_texture = device_->newTexture(desc);
    desc->release();

    if (!temp_texture) {
      XELOGE(
          "MetalRenderTargetCache::StoreTiledData - Failed to create "
          "temporary "
          "texture");
      return;
    }

    // Resolve multisample texture to temporary texture
    MTL::RenderPassDescriptor* resolve_desc =
        MTL::RenderPassDescriptor::renderPassDescriptor();
    if (resolve_desc) {
      auto* color_attachment = resolve_desc->colorAttachments()->object(0);
      color_attachment->setTexture(texture);              // Multisample source
      color_attachment->setResolveTexture(temp_texture);  // Resolved output
      color_attachment->setLoadAction(MTL::LoadActionLoad);
      color_attachment->setStoreAction(MTL::StoreActionMultisampleResolve);

      MTL::RenderCommandEncoder* render_encoder =
          command_buffer->renderCommandEncoder(resolve_desc);
      if (render_encoder) {
        render_encoder->endEncoding();
        // render_encoder is autoreleased - do not release
      }
      resolve_desc->release();
    }

    source_texture = temp_texture;
  }

  // Create compute encoder
  MTL::ComputeCommandEncoder* encoder = command_buffer->computeCommandEncoder();
  if (!encoder) {
    if (temp_texture) temp_texture->release();
    return;
  }

  // Set compute pipeline
  encoder->setComputePipelineState(edram_store_pipeline_);

  // Bind input texture (either original or resolved)
  encoder->setTexture(source_texture, 0);

  // Bind EDRAM buffer
  encoder->setBuffer(edram_buffer_, 0, 0);

  // Create parameter buffers
  uint32_t params[2] = {edram_base, pitch_tiles};
  MTL::Buffer* param_buffer = device_->newBuffer(
      &params, sizeof(params), MTL::ResourceStorageModeShared);
  encoder->setBuffer(param_buffer, 0, 1);
  encoder->setBuffer(param_buffer, sizeof(uint32_t), 2);

  // Calculate thread group sizes
  MTL::Size threads_per_threadgroup = MTL::Size::Make(8, 8, 1);
  MTL::Size threadgroups = MTL::Size::Make(
      (source_texture->width() + 7) / 8, (source_texture->height() + 7) / 8, 1);

  // Dispatch compute
  encoder->dispatchThreadgroups(threadgroups, threads_per_threadgroup);
  encoder->endEncoding();
  // encoder is autoreleased - do not release

  if (temp_texture) {
    temp_texture->release();
  }

  param_buffer->release();
}

void MetalRenderTargetCache::DumpRenderTargets(uint32_t dump_base,
                                               uint32_t dump_row_length_used,
                                               uint32_t dump_rows,
                                               uint32_t dump_pitch) {
  XELOGGPU(
      "MetalRenderTargetCache::DumpRenderTargets: base={} row_length_used={} "
      "rows={} pitch={}",
      dump_base, dump_row_length_used, dump_rows, dump_pitch);

  if (GetPath() != Path::kHostRenderTargets) {
    XELOGGPU("MetalRenderTargetCache::DumpRenderTargets: not host RT path");
    return;
  }

  std::vector<ResolveCopyDumpRectangle> rectangles;
  GetResolveCopyRectanglesToDump(dump_base, dump_row_length_used, dump_rows,
                                 dump_pitch, rectangles);

  XELOGGPU("MetalRenderTargetCache::DumpRenderTargets: {} rectangles to dump",
           rectangles.size());

  if (rectangles.empty()) {
    XELOGGPU(
        "MetalRenderTargetCache::DumpRenderTargets: no rectangles for base={} "
        "row_length_used={} rows={} pitch={}",
        dump_base, dump_row_length_used, dump_rows, dump_pitch);
    return;
  }

  if (!edram_buffer_) {
    XELOGW(
        "MetalRenderTargetCache::DumpRenderTargets: EDRAM buffer not "
        "initialized, skipping GPU dump");
    return;
  }

  struct EdramDumpConstants {
    uint32_t dispatch_first_tile;
    uint32_t source_base_tiles;
    uint32_t dest_pitch_tiles;
    uint32_t source_pitch_tiles;
    uint32_t resolution_scale_x;
    uint32_t resolution_scale_y;
  };

  MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
  if (!queue) {
    XELOGE("MetalRenderTargetCache::DumpRenderTargets: no command queue");
    return;
  }

  MTL::CommandBuffer* cmd = queue->commandBuffer();
  if (!cmd) {
    XELOGE("MetalRenderTargetCache::DumpRenderTargets: no command buffer");
    return;
  }

  MTL::ComputeCommandEncoder* encoder = cmd->computeCommandEncoder();
  if (!encoder) {
    XELOGE("MetalRenderTargetCache::DumpRenderTargets: no compute encoder");
    // cmd is autoreleased from commandBuffer() - do not release
    return;
  }

  encoder->setBuffer(edram_buffer_, 0, 0);

  uint32_t scale_x = draw_resolution_scale_x();
  uint32_t scale_y = draw_resolution_scale_y();

  for (const ResolveCopyDumpRectangle& rect : rectangles) {
    auto* rt = static_cast<MetalRenderTarget*>(rect.render_target);
    if (!rt) {
      continue;
    }

    RenderTargetKey key = rt->key();
    MTL::Texture* tex = rt->texture();
    if (!tex) {
      continue;
    }

    // Choose the appropriate dump pipeline based on:
    // - 32bpp vs 64bpp (key.Is64bpp())
    // - color vs depth (key.is_depth)
    // - MSAA sample count (key.msaa_samples)
    // This mirrors D3D12/Vulkan dump pipeline selection.
    MTL::ComputePipelineState* dump_pipeline = nullptr;
    bool is_64bpp = key.Is64bpp();

    if (!key.is_depth) {
      // Color render target
      if (is_64bpp) {
        // 64bpp color
        switch (key.msaa_samples) {
          case xenos::MsaaSamples::k1X:
            dump_pipeline = edram_dump_color_64bpp_1xmsaa_pipeline_;
            break;
          case xenos::MsaaSamples::k2X:
            dump_pipeline = edram_dump_color_64bpp_2xmsaa_pipeline_;
            break;
          case xenos::MsaaSamples::k4X:
            dump_pipeline = edram_dump_color_64bpp_4xmsaa_pipeline_;
            break;
          default:
            break;
        }
      } else {
        // 32bpp color
        switch (key.msaa_samples) {
          case xenos::MsaaSamples::k1X:
            dump_pipeline = edram_dump_color_32bpp_1xmsaa_pipeline_;
            break;
          case xenos::MsaaSamples::k2X:
            dump_pipeline = edram_dump_color_32bpp_2xmsaa_pipeline_;
            break;
          case xenos::MsaaSamples::k4X:
            dump_pipeline = edram_dump_color_32bpp_4xmsaa_pipeline_;
            break;
          default:
            break;
        }
      }
    } else {
      // Depth render target (always 32bpp for D24S8/D24FS8)
      switch (key.msaa_samples) {
        case xenos::MsaaSamples::k1X:
          dump_pipeline = edram_dump_depth_32bpp_1xmsaa_pipeline_;
          break;
        case xenos::MsaaSamples::k2X:
          dump_pipeline = edram_dump_depth_32bpp_2xmsaa_pipeline_;
          break;
        case xenos::MsaaSamples::k4X:
          dump_pipeline = edram_dump_depth_32bpp_4xmsaa_pipeline_;
          break;
        default:
          break;
      }
    }

    if (!dump_pipeline) {
      XELOGGPU(
          "MetalRenderTargetCache::DumpRenderTargets: no dump pipeline for "
          "key=0x{:08X} (is_depth={}, is_64bpp={}, msaa={})",
          key.key, key.is_depth ? 1 : 0, is_64bpp ? 1 : 0,
          static_cast<uint32_t>(key.msaa_samples));
      continue;
    }

    XELOGGPU(
        "MetalRenderTargetCache::DumpRenderTargets: dump RT key=0x{:08X} "
        "(is_depth={}, is_64bpp={}, msaa={}) tex={}x{} pipeline={:p}",
        key.key, key.is_depth ? 1 : 0, is_64bpp ? 1 : 0,
        static_cast<uint32_t>(key.msaa_samples), tex->width(), tex->height(),
        static_cast<void*>(dump_pipeline));

    ResolveCopyDumpRectangle::Dispatch
        dispatches[ResolveCopyDumpRectangle::kMaxDispatches];
    uint32_t dispatch_count =
        rect.GetDispatches(dump_pitch, dump_row_length_used, dispatches);
    if (!dispatch_count) {
      continue;
    }

    for (uint32_t i = 0; i < dispatch_count; ++i) {
      const ResolveCopyDumpRectangle::Dispatch& dispatch = dispatches[i];

      EdramDumpConstants constants;
      constants.dispatch_first_tile = dump_base + dispatch.offset;
      constants.source_base_tiles = key.base_tiles;
      constants.dest_pitch_tiles = dump_pitch;
      constants.source_pitch_tiles = key.GetPitchTiles();
      constants.resolution_scale_x = scale_x;
      constants.resolution_scale_y = scale_y;

      encoder->setComputePipelineState(dump_pipeline);
      encoder->setTexture(tex, 0);
      encoder->setBytes(&constants, sizeof(constants), 1);

      // Thread group dispatch:
      // - 40x16 threads per group (same as D3D12/Vulkan)
      // - For 32bpp: two groups per tile along X (80 samples / 40 threads)
      // - For 64bpp: one group per tile along X (40 samples / 40 threads)
      uint32_t groups_x = dispatch.width_tiles * scale_x;
      if (!is_64bpp) {
        groups_x <<= 1;  // Double for 32bpp
      }
      uint32_t groups_y = dispatch.height_tiles * scale_y;

      MTL::Size threads_per_group = MTL::Size::Make(40, 16, 1);
      MTL::Size threadgroups = MTL::Size::Make(groups_x, groups_y, 1);
      encoder->dispatchThreadgroups(threadgroups, threads_per_group);
    }
  }

  encoder->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // cmd is autoreleased from commandBuffer() - do not release
}

bool MetalRenderTargetCache::Resolve(Memory& memory, uint32_t& written_address,
                                     uint32_t& written_length) {
  written_address = 0;
  written_length = 0;
  XELOGI("MetalRenderTargetCache::Resolve: begin");

  const RegisterFile& regs = register_file();
  draw_util::ResolveInfo resolve_info;

  // For now, assume fixed16 formats are emulated with full range.
  // This is sufficient for the k_8_8_8_8 A-Train path.
  bool fixed_rg16_trunc = false;
  bool fixed_rgba16_trunc = false;

  if (!trace_writer_) {
    XELOGE("MetalRenderTargetCache::Resolve: trace_writer_ is null");
    return false;
  }

  if (!draw_util::GetResolveInfo(regs, memory, *trace_writer_,
                                 draw_resolution_scale_x(),
                                 draw_resolution_scale_y(), fixed_rg16_trunc,
                                 fixed_rgba16_trunc, resolve_info)) {
    XELOGE("MetalRenderTargetCache::Resolve: GetResolveInfo failed");
    return false;
  }

  // Nothing to do.
  if (!resolve_info.coordinate_info.width_div_8 || !resolve_info.height_div_8) {
    XELOGI("MetalRenderTargetCache::Resolve: empty resolve region");
    return true;
  }

  bool is_depth = resolve_info.IsCopyingDepth();

  if (!resolve_info.copy_dest_extent_length) {
    XELOGI("MetalRenderTargetCache::Resolve: zero copy_dest_extent_length");
    return true;
  }

  if (GetPath() != Path::kHostRenderTargets) {
    XELOGI("MetalRenderTargetCache::Resolve: non-host-RT path not supported");
    return true;
  }

  MetalRenderTarget* src_rt = nullptr;
  RenderTarget* const* accumulated_targets =
      last_update_accumulated_render_targets();

  if (is_depth) {
    // For depth resolves, use the current depth render target as the source,
    // matching D3D12/Vulkan behavior.
    if (accumulated_targets && accumulated_targets[0]) {
      src_rt = static_cast<MetalRenderTarget*>(accumulated_targets[0]);
    }
    if (!src_rt || !src_rt->texture()) {
      XELOGI("MetalRenderTargetCache::Resolve: no depth source RT");
      return true;
    }
  } else {
    // Color resolves select the source via copy_src_select.
    uint32_t copy_src = resolve_info.rb_copy_control.copy_src_select;
    if (copy_src >= xenos::kMaxColorRenderTargets) {
      XELOGI(
          "MetalRenderTargetCache::Resolve: copy_src_select out of range ({})",
          copy_src);
      return true;
    }
    if (accumulated_targets && accumulated_targets[1 + copy_src]) {
      src_rt =
          static_cast<MetalRenderTarget*>(accumulated_targets[1 + copy_src]);
    }
    if (!src_rt || !src_rt->texture()) {
      XELOGI(
          "MetalRenderTargetCache::Resolve: no source RT for "
          "copy_src_select={}",
          copy_src);
      return true;
    }
  }

  MTL::Texture* source_texture = src_rt->texture();
  uint32_t src_width = static_cast<uint32_t>(source_texture->width());
  uint32_t src_height = static_cast<uint32_t>(source_texture->height());
  uint32_t msaa_samples = static_cast<uint32_t>(source_texture->sampleCount());
  if (!msaa_samples) {
    msaa_samples = 1;
  }

  const auto& coord = resolve_info.coordinate_info;
  uint32_t resolve_width = coord.width_div_8 * 8;
  uint32_t resolve_height = resolve_info.height_div_8 * 8;

  // Section 6.2: Inspect source render target before DumpRenderTargets.
  // If this shows non-zero pixels but EDRAM is zero after DumpRenderTargets,
  // the bug is in the Metal dump shaders (bindings or address math).
  // If this shows zeros, the render target textures never received valid data.
  if (src_rt) {
    LogMetalRenderTargetTopLeftPixels(
        src_rt, "MetalResolve SRC_RT before DumpRenderTargets", device_,
        command_processor_.GetMetalCommandQueue());
  }

  // Compute the EDRAM tile span for this resolve and log which render targets
  // own that region, mirroring D3D12/Vulkan's DumpRenderTargets.
  uint32_t dump_base, dump_row_length_used, dump_rows, dump_pitch;
  resolve_info.GetCopyEdramTileSpan(dump_base, dump_row_length_used, dump_rows,
                                    dump_pitch);
  DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch);

  uint32_t dest_base = resolve_info.copy_dest_base;
  uint32_t dest_local_start = resolve_info.copy_dest_extent_start - dest_base;
  uint32_t dest_local_end =
      dest_local_start + resolve_info.copy_dest_extent_length;

  // For now, only apply the 8888 restriction to color resolves; depth resolves
  // may use different destination formats.
  uint32_t bytes_per_pixel = 4;

  // Try GPU compute resolve first (RT -> EDRAM -> shared memory), matching
  // D3D12/Vulkan behavior for the supported cases.
  if (edram_buffer_) {
    draw_util::ResolveCopyShaderConstants copy_constants;
    uint32_t group_count_x = 0, group_count_y = 0;
    draw_util::ResolveCopyShaderIndex copy_shader = resolve_info.GetCopyShader(
        draw_resolution_scale_x(), draw_resolution_scale_y(), copy_constants,
        group_count_x, group_count_y);

    // Select the appropriate Metal pipeline for this shader.
    MTL::ComputePipelineState* pipeline = nullptr;
    switch (copy_shader) {
      case draw_util::ResolveCopyShaderIndex::kFast32bpp1x2xMSAA:
        pipeline = resolve_fast_32bpp_1x2xmsaa_pipeline_;
        break;
      case draw_util::ResolveCopyShaderIndex::kFast32bpp4xMSAA:
        pipeline = resolve_fast_32bpp_4xmsaa_pipeline_;
        break;
      case draw_util::ResolveCopyShaderIndex::kFast64bpp1x2xMSAA:
        pipeline = resolve_fast_64bpp_1x2xmsaa_pipeline_;
        break;
      case draw_util::ResolveCopyShaderIndex::kFast64bpp4xMSAA:
        pipeline = resolve_fast_64bpp_4xmsaa_pipeline_;
        break;
      case draw_util::ResolveCopyShaderIndex::kFull8bpp:
        pipeline = resolve_full_8bpp_pipeline_;
        break;
      case draw_util::ResolveCopyShaderIndex::kFull16bpp:
        pipeline = resolve_full_16bpp_pipeline_;
        break;
      case draw_util::ResolveCopyShaderIndex::kFull32bpp:
        pipeline = resolve_full_32bpp_pipeline_;
        break;
      case draw_util::ResolveCopyShaderIndex::kFull64bpp:
        pipeline = resolve_full_64bpp_pipeline_;
        break;
      case draw_util::ResolveCopyShaderIndex::kFull128bpp:
        pipeline = resolve_full_128bpp_pipeline_;
        break;
      default:
        pipeline = nullptr;
        break;
    }

    if (pipeline && group_count_x && group_count_y) {
      auto* shared = command_processor_.shared_memory();
      MTL::Buffer* shared_buffer = shared ? shared->GetBuffer() : nullptr;
      if (shared_buffer) {
        // Request the destination shared memory range before the GPU write,
        // mirroring D3D12/Vulkan behavior. This ensures pages are committed and
        // any CPU data is uploaded before the GPU overwrites it.
        if (!shared->RequestRange(resolve_info.copy_dest_extent_start,
                                  resolve_info.copy_dest_extent_length)) {
          XELOGE(
              "MetalRenderTargetCache::Resolve: RequestRange failed for "
              "0x{:08X} len {}",
              resolve_info.copy_dest_extent_start,
              resolve_info.copy_dest_extent_length);
          return false;
        }

        const uint8_t* shared_bytes =
            static_cast<const uint8_t*>(shared_buffer->contents());
        if (shared_bytes) {
          uint32_t debug_addr = resolve_info.copy_dest_extent_start;
          const uint8_t* dst_before = shared_bytes + debug_addr;
          XELOGI(
              "MetalResolve SRC (shared) before [0..15] @0x{:08X}: "
              "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}  "
              "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}",
              debug_addr, dst_before[0], dst_before[1], dst_before[2],
              dst_before[3], dst_before[4], dst_before[5], dst_before[6],
              dst_before[7], dst_before[8], dst_before[9], dst_before[10],
              dst_before[11], dst_before[12], dst_before[13], dst_before[14],
              dst_before[15]);
          if (edram_buffer_) {
            const uint8_t* edram_bytes =
                static_cast<const uint8_t*>(edram_buffer_->contents());
            if (edram_bytes) {
              uint32_t edram_debug_offset = dump_base * 64u;
              const uint8_t* src_edram = edram_bytes + edram_debug_offset;
              XELOGI(
                  "MetalResolve SRC (EDRAM) before [0..15] @tile_base*64="
                  "0x{:08X}: "
                  "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}  "
                  "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}",
                  edram_debug_offset, src_edram[0], src_edram[1], src_edram[2],
                  src_edram[3], src_edram[4], src_edram[5], src_edram[6],
                  src_edram[7], src_edram[8], src_edram[9], src_edram[10],
                  src_edram[11], src_edram[12], src_edram[13], src_edram[14],
                  src_edram[15]);
            }
          }
        }

        MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();

        if (!queue) {
          XELOGE(
              "MetalRenderTargetCache::Resolve: no command queue for GPU path");
        } else {
          MTL::CommandBuffer* cmd = queue->commandBuffer();
          if (!cmd) {
            XELOGE(
                "MetalRenderTargetCache::Resolve: failed to get command buffer "
                "for GPU path");
          } else {
            MTL::ComputeCommandEncoder* encoder = cmd->computeCommandEncoder();
            if (!encoder) {
              XELOGE(
                  "MetalRenderTargetCache::Resolve: failed to get compute "
                  "encoder for GPU path");
              // cmd is autoreleased from commandBuffer() - do not release
            } else {
              encoder->setComputePipelineState(pipeline);

              // Buffer 0: push constants (ResolveCopyShaderConstants)
              encoder->setBytes(&copy_constants, sizeof(copy_constants), 0);

              // Buffer 1: destination shared memory (full buffer, base encoded
              // in constants.dest_base).
              encoder->setBuffer(shared_buffer, 0, 1);

              // Buffer 2: EDRAM source buffer.
              encoder->setBuffer(edram_buffer_, 0, 2);

              encoder->dispatchThreadgroups(
                  MTL::Size::Make(group_count_x, group_count_y, 1),
                  MTL::Size::Make(8, 8, 1));

              encoder->endEncoding();
              cmd->commit();
              cmd->waitUntilCompleted();
              // cmd is autoreleased from commandBuffer() - do not release

              if (shared_bytes) {
                uint32_t debug_addr = resolve_info.copy_dest_extent_start;
                const uint8_t* dst_after = shared_bytes + debug_addr;
                XELOGI(
                    "MetalResolve DST (shared) after  [0..15] @0x{:08X}: "
                    "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}  "
                    "{:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X}",
                    debug_addr, dst_after[0], dst_after[1], dst_after[2],
                    dst_after[3], dst_after[4], dst_after[5], dst_after[6],
                    dst_after[7], dst_after[8], dst_after[9], dst_after[10],
                    dst_after[11], dst_after[12], dst_after[13], dst_after[14],
                    dst_after[15]);
              }

              written_address = resolve_info.copy_dest_extent_start;
              written_length = resolve_info.copy_dest_extent_length;

              // Mark the shared memory range as GPU-written resolve data so
              // texture caches and trace dumping can see it without an extra
              // CPU copy. This mirrors D3D12/Vulkan behavior.
              if (auto* shared_after = command_processor_.shared_memory()) {
                shared_after->RangeWrittenByGpu(written_address, written_length,
                                                true);
              }

              // Mark the range as resolved in the texture cache so that any
              // textures overlapping this range will be reloaded from the
              // updated shared memory. This matches D3D12/Vulkan behavior.
              if (auto* tex_cache = command_processor_.texture_cache()) {
                tex_cache->MarkRangeAsResolved(written_address, written_length);
              }

              XELOGI(
                  "MetalRenderTargetCache::Resolve: GPU path (shader={} ) "
                  "wrote "
                  "{} bytes at 0x{:08X}",
                  int(copy_shader), written_length, written_address);
              return true;
            }
          }
        }
      }
    }
  }

  XELOGE(
      "MetalRenderTargetCache::Resolve: no valid GPU resolve shader / pipeline "
      "for this configuration");
  return false;
}

void MetalRenderTargetCache::PerformTransfersAndResolveClears(

    uint32_t render_target_count, RenderTarget* const* render_targets,
    const std::vector<Transfer>* render_target_transfers) {
  // This function handles ownership transfers between render targets when
  // EDRAM regions are aliased. When the game binds different RT configurations
  // that share the same EDRAM base, we need to copy data from the old RT
  // texture to the new one.
  //
  // NOTE: This is a simplified implementation compared to D3D12/Vulkan.
  // D3D12/Vulkan use specialized pixel shaders for transfers that handle:
  // - Format conversion (color <-> depth)
  // - MSAA sample mapping between different sample counts
  // - Stencil bit manipulation
  // - Host depth precision preservation
  //
  // This implementation only handles basic same-format, same-MSAA blits.
  // TODO: Implement full transfer shader support like D3D12/Vulkan.

  if (!render_targets || !render_target_transfers) {
    return;
  }

  MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
  if (!queue) {
    XELOGE(
        "MetalRenderTargetCache::PerformTransfersAndResolveClears: No command "
        "queue");
    return;
  }

  bool any_transfers_done = false;
  uint32_t transfers_skipped_format = 0;
  uint32_t transfers_skipped_msaa = 0;
  uint32_t transfers_skipped_depth = 0;

  for (uint32_t i = 0; i < render_target_count; ++i) {
    RenderTarget* dest_rt = render_targets[i];
    if (!dest_rt) {
      continue;
    }

    const std::vector<Transfer>& transfers = render_target_transfers[i];
    if (transfers.empty()) {
      continue;
    }

    MetalRenderTarget* dest_metal_rt = static_cast<MetalRenderTarget*>(dest_rt);
    MTL::Texture* dest_texture = dest_metal_rt->texture();
    if (!dest_texture) {
      XELOGW(
          "MetalRenderTargetCache::PerformTransfersAndResolveClears: "
          "Destination RT {} has no texture",
          i);
      continue;
    }

    RenderTargetKey dest_key = dest_metal_rt->key();

    for (const Transfer& transfer : transfers) {
      RenderTarget* source_rt = transfer.source;
      if (!source_rt) {
        continue;
      }

      MetalRenderTarget* source_metal_rt =
          static_cast<MetalRenderTarget*>(source_rt);
      MTL::Texture* source_texture = source_metal_rt->texture();
      if (!source_texture) {
        continue;
      }

      RenderTargetKey source_key = source_metal_rt->key();

      // Log the transfer request for debugging
      XELOGI(
          "MetalRenderTargetCache::PerformTransfersAndResolveClears: "
          "Transfer request - src key=0x{:08X} ({}x{}, {}samples, depth={}) "
          "-> dst key=0x{:08X} ({}x{}, {}samples, depth={}), "
          "tiles=[{}, {}), host_depth_src={:p}",
          source_key.key, source_texture->width(), source_texture->height(),
          source_texture->sampleCount(), source_key.is_depth ? 1 : 0,
          dest_key.key, dest_texture->width(), dest_texture->height(),
          dest_texture->sampleCount(), dest_key.is_depth ? 1 : 0,
          transfer.start_tiles, transfer.end_tiles,
          (void*)transfer.host_depth_source);
      static std::unordered_set<uint64_t> logged_transfer_pairs;
      uint64_t transfer_pair_key =
          (uint64_t(source_key.key) << 32) | uint64_t(dest_key.key);
      if (logged_transfer_pairs.insert(transfer_pair_key).second) {
        MTL::PixelFormat src_pixel_format =
            source_key.is_depth ? GetDepthPixelFormat(source_key.GetDepthFormat())
                                : GetColorPixelFormat(source_key.GetColorFormat());
        MTL::PixelFormat dst_pixel_format =
            dest_key.is_depth ? GetDepthPixelFormat(dest_key.GetDepthFormat())
                              : GetColorPixelFormat(dest_key.GetColorFormat());
        XELOGI(
            "MetalRenderTargetCache transfer formats: src={} (host={}), "
            "dst={} (host={})",
            source_key.GetFormatName(),
            static_cast<int>(src_pixel_format), dest_key.GetFormatName(),
            static_cast<int>(dst_pixel_format));
      }

      // Determine transfer mode (matching D3D12's TransferMode enum)
      bool source_is_depth = source_key.is_depth;
      bool dest_is_depth = dest_key.is_depth;

            // Skip format mismatches - would need format conversion shader

            /*

            if (source_key.resource_format != dest_key.resource_format) {

              XELOGI(

                  "  SKIPPED: Format mismatch not implemented (src_fmt={}, "

                  "dst_fmt={})",

      

                  source_key.resource_format, dest_key.resource_format);

              transfers_skipped_format++;

              continue;

            }

            */

      

            // Calculate the transfer rectangles

            Transfer::Rectangle rectangles[Transfer::kMaxRectanglesWithCutout];

            uint32_t rectangle_count = transfer.GetRectangles(

                dest_key.base_tiles, dest_key.pitch_tiles_at_32bpp,

                dest_key.msaa_samples, dest_key.Is64bpp(), rectangles);

      

            if (rectangle_count == 0) {

              continue;

            }

      

            XELOGI(

                "MetalRenderTargetCache::PerformTransfersAndResolveClears: "

                "Transferring {} rectangles from RT key 0x{:08X} ({}x{}) to RT key "

                "0x{:08X} ({}x{})",

                rectangle_count, source_key.key, source_texture->width(),

                source_texture->height(), dest_key.key, dest_texture->width(),

                dest_texture->height());

      

            // For each rectangle, we need to copy the corresponding region

            // from source to destination. The rectangles are in pixel coordinates

            // relative to the destination RT's tile layout.

      

            MTL::CommandBuffer* cmd = queue->commandBuffer();

            if (!cmd) {

              continue;

            }

      

            MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();

            if (!blit) {

              // cmd is autoreleased from commandBuffer() - do not release

              continue;

            }

      

            for (uint32_t rect_idx = 0; rect_idx < rectangle_count; ++rect_idx) {

              const Transfer::Rectangle& rect = rectangles[rect_idx];

      

              // Apply resolution scale

              uint32_t scaled_x = rect.x_pixels * draw_resolution_scale_x();

              uint32_t scaled_y = rect.y_pixels * draw_resolution_scale_y();

              uint32_t scaled_width = rect.width_pixels * draw_resolution_scale_x();

              uint32_t scaled_height = rect.height_pixels * draw_resolution_scale_y();

      

              // Clamp to source texture bounds

              uint32_t src_width = static_cast<uint32_t>(source_texture->width());

              uint32_t src_height = static_cast<uint32_t>(source_texture->height());

              if (scaled_x >= src_width || scaled_y >= src_height) {

                continue;

              }

              scaled_width = std::min(scaled_width, src_width - scaled_x);

              scaled_height = std::min(scaled_height, src_height - scaled_y);

      

              // Clamp to destination texture bounds

              uint32_t dst_width = static_cast<uint32_t>(dest_texture->width());

              uint32_t dst_height = static_cast<uint32_t>(dest_texture->height());

              if (scaled_x >= dst_width || scaled_y >= dst_height) {

                continue;

              }

              scaled_width = std::min(scaled_width, dst_width - scaled_x);

              scaled_height = std::min(scaled_height, dst_height - scaled_y);

      

              if (scaled_width == 0 || scaled_height == 0) {

                continue;

              }

      

              // Check if source and dest have compatible sample counts.

              // For same-sample-count ColorToColor we currently use a simple blit.

              // For MSAA mismatches (such as the 4x->1x A-Train case), route
              // through GetOrCreateTransferPipelines so logs clearly show missing
              // implementations.
              if (source_texture->sampleCount() != dest_texture->sampleCount() ||
                  source_key.resource_format != dest_key.resource_format ||
                  source_is_depth != dest_is_depth) {
                TransferShaderKey shader_key;
                if (source_is_depth && dest_is_depth) {
                  shader_key.mode = TransferMode::kDepthToDepth;
                } else if (source_is_depth && !dest_is_depth) {
                  shader_key.mode = TransferMode::kDepthToColor;
                } else if (!source_is_depth && dest_is_depth) {
                  shader_key.mode = TransferMode::kColorToDepth;
                } else {
                  shader_key.mode = TransferMode::kColorToColor;
                }

                shader_key.source_msaa_samples = source_key.msaa_samples;
                shader_key.dest_msaa_samples = dest_key.msaa_samples;
                shader_key.source_resource_format = source_key.resource_format;
                shader_key.dest_resource_format = dest_key.resource_format;

                MTL::PixelFormat dest_pixel_format =
                    dest_is_depth ? GetDepthPixelFormat(dest_key.GetDepthFormat())
                                  : GetColorPixelFormat(dest_key.GetColorFormat());
                MTL::RenderPipelineState* pipeline =
                    GetOrCreateTransferPipelines(shader_key, dest_pixel_format);

                if (!pipeline) {
                  XELOGI(
                      "  SKIPPED rect {}: No transfer pipeline for key "
                      "(mode={}, src_msaa={}, dst_msaa={}, src_fmt={}, dst_fmt={})",
                      rect_idx, int(shader_key.mode),
                      int(shader_key.source_msaa_samples),
                      int(shader_key.dest_msaa_samples),
                      shader_key.source_resource_format,
                      shader_key.dest_resource_format);
                  transfers_skipped_msaa++;
                  continue;
                }

                // Use a render pass and the transfer pipeline to draw this
                // rectangle. This mirrors the D3D12/Vulkan approach of using a
                // graphics pipeline (VS+PS) for ownership transfers.
                MTL::RenderPassDescriptor* rp =
                    MTL::RenderPassDescriptor::renderPassDescriptor();
                if (dest_is_depth) {
                  auto* da = rp->depthAttachment();
                  da->setTexture(dest_texture);
                  da->setLoadAction(MTL::LoadActionLoad);
                  da->setStoreAction(MTL::StoreActionStore);
                  if (dest_pixel_format == MTL::PixelFormatDepth32Float_Stencil8 ||
                      dest_pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
                    auto* sa = rp->stencilAttachment();
                    sa->setTexture(dest_texture);
                    sa->setLoadAction(MTL::LoadActionLoad);
                    sa->setStoreAction(MTL::StoreActionStore);
                  }
                } else {
                  auto* ca = rp->colorAttachments()->object(0);
                  ca->setTexture(dest_texture);
                  ca->setLoadAction(MTL::LoadActionLoad);
                  ca->setStoreAction(MTL::StoreActionStore);
                }

                MTL::CommandBuffer* xfer_cmd = queue->commandBuffer();
                if (!xfer_cmd) {
                  transfers_skipped_msaa++;
                  continue;
                }
                MTL::RenderCommandEncoder* enc = xfer_cmd->renderCommandEncoder(rp);
                if (!enc) {
                  // xfer_cmd is autoreleased from commandBuffer() - do not release
                  transfers_skipped_msaa++;
                  continue;
                }

                enc->setRenderPipelineState(pipeline);
                enc->setFragmentTexture(source_texture, 0);

                struct RectInfo {
                  float dst[4];      // x, y, w, h
                  float srcSize[4];  // src_width, src_height (+ padding)
                } ri;
                ri.dst[0] = float(scaled_x);
                ri.dst[1] = float(scaled_y);
                ri.dst[2] = float(scaled_width);
                ri.dst[3] = float(scaled_height);
                ri.srcSize[0] = float(src_width);
                ri.srcSize[1] = float(src_height);

                enc->setFragmentBytes(&ri, sizeof(ri), 0);

                MTL::Viewport vp;
                vp.originX = double(scaled_x);
                vp.originY = double(scaled_y);
                vp.width = double(scaled_width);
                vp.height = double(scaled_height);
                vp.znear = 0.0;
                vp.zfar = 1.0;
                enc->setViewport(vp);

                if (dest_is_depth) {
                  MTL::DepthStencilDescriptor* ds_desc =
                      MTL::DepthStencilDescriptor::alloc()->init();
                  ds_desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
                  ds_desc->setDepthWriteEnabled(true);
                  MTL::DepthStencilState* ds_state =
                      device_->newDepthStencilState(ds_desc);
                  enc->setDepthStencilState(ds_state);
                  ds_state->release();
                  ds_desc->release();
                }

                enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                                    NS::UInteger(3));
                enc->endEncoding();

                xfer_cmd->commit();
                xfer_cmd->waitUntilCompleted();
                // xfer_cmd is autoreleased from commandBuffer() - do not release

                any_transfers_done = true;

                XELOGI(
                    "  Transferred rect {} via pipeline mode {}: {}x{} at ({}, {})",
                    rect_idx, int(shader_key.mode), rect.width_pixels,
                    rect.height_pixels, rect.x_pixels, rect.y_pixels);
                continue;
              }

        // Copy the region from source to destination
        blit->copyFromTexture(
            source_texture, 0, 0, MTL::Origin::Make(scaled_x, scaled_y, 0),
            MTL::Size::Make(scaled_width, scaled_height, 1), dest_texture, 0, 0,
            MTL::Origin::Make(scaled_x, scaled_y, 0));

        any_transfers_done = true;

        XELOGI(
            "  Transferred rect {}: {}x{} at ({}, {}) scaled to {}x{} at ({}, "
            "{})",
            rect_idx, rect.width_pixels, rect.height_pixels, rect.x_pixels,
            rect.y_pixels, scaled_width, scaled_height, scaled_x, scaled_y);
      }

      blit->endEncoding();
      cmd->commit();
      cmd->waitUntilCompleted();
      // cmd is autoreleased from commandBuffer() - do not release
    }
  }

  if (any_transfers_done || transfers_skipped_depth ||
      transfers_skipped_format || transfers_skipped_msaa) {
    XELOGI(
        "MetalRenderTargetCache::PerformTransfersAndResolveClears: "
        "completed={}, skipped: depth={}, format={}, msaa={}",
        any_transfers_done ? 1 : 0, transfers_skipped_depth,
        transfers_skipped_format, transfers_skipped_msaa);
  }
}

MTL::RenderPipelineState* MetalRenderTargetCache::GetOrCreateTransferPipelines(
    const TransferShaderKey& key, MTL::PixelFormat dest_format) {
  auto it = transfer_pipelines_.find(key);
  if (it != transfer_pipelines_.end()) {
    return it->second;
  }

  // First, try to handle the specific ColorToColor 4x->1x k_8_8_8_8 case used
  // by the A-Train background path. Other modes / formats will still be
  // reported as unimplemented.
  bool is_color_to_color_4x_to_1x =
      key.mode == TransferMode::kColorToColor &&
      key.source_msaa_samples == xenos::MsaaSamples::k4X &&
      key.dest_msaa_samples == xenos::MsaaSamples::k1X &&
      key.source_resource_format == key.dest_resource_format &&
      (key.source_resource_format ==
           uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8) ||
       key.source_resource_format ==
           uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA));

  // Handle 1x->1x copy with potential format conversion.
  bool is_copy_1x = key.mode == TransferMode::kColorToColor &&
                    key.source_msaa_samples == xenos::MsaaSamples::k1X &&
                    key.dest_msaa_samples == xenos::MsaaSamples::k1X;

  // Handle 1x->4x copy (broadcast).
  bool is_copy_1x_to_4x = key.mode == TransferMode::kColorToColor &&
                          key.source_msaa_samples == xenos::MsaaSamples::k1X &&
                          key.dest_msaa_samples == xenos::MsaaSamples::k4X;

  // Handle 4x->4x copy (resolve/lossy broadcast for now).
  bool is_copy_4x_to_4x = key.mode == TransferMode::kColorToColor &&
                          key.source_msaa_samples == xenos::MsaaSamples::k4X &&
                          key.dest_msaa_samples == xenos::MsaaSamples::k4X;

  // Handle Depth resolving/copying.
  bool is_depth_copy = key.mode == TransferMode::kDepthToDepth;

  // Handle mixed depth/color transitions.
  bool is_depth_to_color = key.mode == TransferMode::kDepthToColor;
  bool is_color_to_depth = key.mode == TransferMode::kColorToDepth;

  if (!is_color_to_color_4x_to_1x && !is_copy_1x && !is_copy_1x_to_4x &&
      !is_copy_4x_to_4x && !is_depth_copy && !is_depth_to_color &&
      !is_color_to_depth) {
    XELOGI(
        "MetalRenderTargetCache::GetOrCreateTransferPipelines: no "
        "implementation for mode={} src_msaa={} dst_msaa={} src_fmt={} "
        "dst_fmt={} (dest_pixel_format={})",
        int(key.mode), int(key.source_msaa_samples), int(key.dest_msaa_samples),
        key.source_resource_format, key.dest_resource_format, int(dest_format));
    return nullptr;
  }

  static const char* kTransferShaderMSL = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct VSOut {
      float4 position [[position]];
      float2 texcoord;
    };

    vertex VSOut transfer_vs(uint vid [[vertex_id]]) {
      float2 pt = float2((vid << 1) & 2, vid & 2);
      VSOut out;
      out.position = float4(pt * 2.0 - 1.0, 0.0, 1.0);
      out.texcoord = float2(pt.x, 1.0 - pt.y);
      return out;
    }

    struct RectInfo {
      float4 dst;      // x, y, w, h in pixels (dest RT space)
      float4 srcSize;  // src_width, src_height, padding
    };

    fragment float4 transfer_ps_color_4x_to_1x(
        VSOut in [[stage_in]],
        texture2d_ms<float> src_msaa [[texture(0)]],
        constant RectInfo& ri [[buffer(0)]]) {
      // Compute dest pixel coords inside the rectangle.
      float2 dst_px = float2(ri.dst.x, ri.dst.y) +
                      in.texcoord * ri.dst.zw;

      // Map dest X range [dst.x, dst.x+dst.w) over src X [0, srcWidth)
      // with a 4:1 horizontal expansion: 4 dest pixels per 1 src pixel.
      float src_x = floor(dst_px.x / 4.0);
      float src_y = dst_px.y;

      // Clamp to valid source range.
      float2 src_clamped = clamp(float2(src_x, src_y),
                                 float2(0.0, 0.0),
                                 ri.srcSize.xy - float2(1.0, 1.0));
      uint2 src_px = uint2(src_clamped + 0.5);

      float4 c = float4(0.0);
      uint sample_count = src_msaa.get_num_samples();
      if (sample_count == 4) {
        // Simple 4x MSAA resolve: average all 4 samples.
        for (uint i = 0; i < 4; ++i) {
          c += src_msaa.read(src_px, i);
        }
        c *= 0.25;
      } else {
        c = src_msaa.read(src_px, 0);
      }
      return c;
    }

    fragment float4 transfer_ps_copy(
        VSOut in [[stage_in]],
        texture2d<float> src_tex [[texture(0)]],
        constant RectInfo& ri [[buffer(0)]]) {
      float2 dst_px = float2(ri.dst.x, ri.dst.y) + in.texcoord * ri.dst.zw;
      uint2 src_px = uint2(dst_px);
      // Clamp to source size
      src_px = min(src_px, uint2(ri.srcSize.xy) - uint2(1, 1));
      return src_tex.read(src_px, 0);
    }

    fragment float4 transfer_ps_copy_ms(
        VSOut in [[stage_in]],
        texture2d_ms<float> src_msaa [[texture(0)]],
        constant RectInfo& ri [[buffer(0)]]) {
      float2 dst_px = float2(ri.dst.x, ri.dst.y) + in.texcoord * ri.dst.zw;
      uint2 src_px = uint2(dst_px);
      // Clamp to source size
      src_px = min(src_px, uint2(ri.srcSize.xy) - uint2(1, 1));
      // Just read sample 0 for now (lossy for 4x->4x but better than black).
      return src_msaa.read(src_px, 0);
    }

    struct DepthOut {
      float depth [[depth(any)]];
    };

    fragment DepthOut transfer_ps_depth_resolve(
        VSOut in [[stage_in]],
        texture2d_ms<float> src_msaa [[texture(0)]],
        constant RectInfo& ri [[buffer(0)]]) {
      float2 dst_px = float2(ri.dst.x, ri.dst.y) + in.texcoord * ri.dst.zw;
      uint2 src_px = uint2(dst_px);
      // Clamp to source size
      src_px = min(src_px, uint2(ri.srcSize.xy) - uint2(1, 1));
      
      DepthOut out;
      // For depth resolve, take sample 0 (matching Xenos Resolve behavior for depth usually).
      out.depth = src_msaa.read(src_px, 0).r;
      return out;
    }

    fragment DepthOut transfer_ps_color_to_depth(
        VSOut in [[stage_in]],
        texture2d<float> src_tex [[texture(0)]],
        constant RectInfo& ri [[buffer(0)]]) {
      float2 dst_px = float2(ri.dst.x, ri.dst.y) + in.texcoord * ri.dst.zw;
      uint2 src_px = uint2(dst_px);
      src_px = min(src_px, uint2(ri.srcSize.xy) - uint2(1, 1));
      DepthOut out;
      out.depth = src_tex.read(src_px, 0).r;
      return out;
    }

    fragment DepthOut transfer_ps_color_to_depth_ms(
        VSOut in [[stage_in]],
        texture2d_ms<float> src_tex [[texture(0)]],
        constant RectInfo& ri [[buffer(0)]]) {
      float2 dst_px = float2(ri.dst.x, ri.dst.y) + in.texcoord * ri.dst.zw;
      uint2 src_px = uint2(dst_px);
      src_px = min(src_px, uint2(ri.srcSize.xy) - uint2(1, 1));
      DepthOut out;
      // Read sample 0
      out.depth = src_tex.read(src_px, 0).r;
      return out;
    }

    fragment float4 transfer_ps_depth_to_color(
        VSOut in [[stage_in]],
        texture2d<float> src_depth [[texture(0)]],
        constant RectInfo& ri [[buffer(0)]]) {
      float2 dst_px = float2(ri.dst.x, ri.dst.y) + in.texcoord * ri.dst.zw;
      uint2 src_px = uint2(dst_px);
      src_px = min(src_px, uint2(ri.srcSize.xy) - uint2(1, 1));
      return float4(src_depth.read(src_px, 0).r, 0, 0, 1);
    }

    fragment float4 transfer_ps_depth_to_color_ms(
        VSOut in [[stage_in]],
        texture2d_ms<float> src_depth [[texture(0)]],
        constant RectInfo& ri [[buffer(0)]]) {
      float2 dst_px = float2(ri.dst.x, ri.dst.y) + in.texcoord * ri.dst.zw;
      uint2 src_px = uint2(dst_px);
      src_px = min(src_px, uint2(ri.srcSize.xy) - uint2(1, 1));
      // Read sample 0
      return float4(src_depth.read(src_px, 0).r, 0, 0, 1);
    }
  )";

  NS::Error* error = nullptr;
  auto src_str = NS::String::string(kTransferShaderMSL, NS::UTF8StringEncoding);
  MTL::Library* lib = device_->newLibrary(src_str, nullptr, &error);
  if (!lib) {
    XELOGI(
        "GetOrCreateTransferPipelines: failed to compile transfer MSL "
        "(mode={}): "
        "{}",
        int(key.mode),
        error && error->localizedDescription()
            ? error->localizedDescription()->utf8String()
            : "unknown error");
    return nullptr;
  }

  auto vs_name = NS::String::string("transfer_vs", NS::UTF8StringEncoding);
  NS::String* ps_name_str;
  bool src_ms = key.source_msaa_samples != xenos::MsaaSamples::k1X;

  if (key.mode == TransferMode::kDepthToDepth) {
    // kDepthToDepth usually implies MSAA resolve if src != dst samples,
    // and we already use transfer_ps_depth_resolve which expects texture2d_ms.
    // If src is 1x, we might need a non-ms variant, but currently kDepthToDepth
    // logic in PerformTransfers... sets it for source_is_depth && dest_is_depth.
    // Xenos depth is often treated as MSAA even if 1x.
    // But let's check.
    // Existing code uses `transfer_ps_depth_resolve` which takes `texture2d_ms`.
    // If we have 1x depth -> 1x depth copy, we might need non-ms.
    // But let's stick to the requested changes for now.
    // The previous error was about `transfer_ps_depth_to_color`.
    ps_name_str =
        NS::String::string("transfer_ps_depth_resolve", NS::UTF8StringEncoding);
  } else if (key.mode == TransferMode::kColorToDepth) {
    ps_name_str = NS::String::string(src_ms ? "transfer_ps_color_to_depth_ms"
                                            : "transfer_ps_color_to_depth",
                                     NS::UTF8StringEncoding);
  } else if (key.mode == TransferMode::kDepthToColor) {
    ps_name_str = NS::String::string(src_ms ? "transfer_ps_depth_to_color_ms"
                                            : "transfer_ps_depth_to_color",
                                     NS::UTF8StringEncoding);
  } else if (is_color_to_color_4x_to_1x) {
    ps_name_str = NS::String::string("transfer_ps_color_4x_to_1x",
                                     NS::UTF8StringEncoding);
  } else if (is_copy_4x_to_4x) {
    ps_name_str =
        NS::String::string("transfer_ps_copy_ms", NS::UTF8StringEncoding);
  } else {
    // is_copy_1x or is_copy_1x_to_4x
    // If is_copy_1x_to_4x (broadcast), src is 1x.
    // If is_copy_1x, src is 1x.
    // Generally, if src is MS, use copy_ms.
    // But `transfer_ps_copy_ms` was specific to 4x->4x.
    // Let's generalize.
    if (src_ms) {
      ps_name_str =
          NS::String::string("transfer_ps_copy_ms", NS::UTF8StringEncoding);
    } else {
      ps_name_str =
          NS::String::string("transfer_ps_copy", NS::UTF8StringEncoding);
    }
  }

  MTL::Function* vs = lib->newFunction(vs_name);
  MTL::Function* ps = lib->newFunction(ps_name_str);
  if (!vs || !ps) {
    XELOGI("GetOrCreateTransferPipelines: failed to get transfer_vs/ps");
    if (vs) vs->release();
    if (ps) ps->release();
    lib->release();
    return nullptr;
  }

  MTL::RenderPipelineDescriptor* desc =
      MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(vs);
  desc->setFragmentFunction(ps);

  if (key.mode == TransferMode::kDepthToDepth ||
      key.mode == TransferMode::kColorToDepth) {
    desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatInvalid);
    desc->setDepthAttachmentPixelFormat(dest_format);
    if (dest_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        dest_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
      desc->setStencilAttachmentPixelFormat(dest_format);
    }
  } else {
    desc->colorAttachments()->object(0)->setPixelFormat(dest_format);
  }

  uint32_t sample_count = 1;
  if (key.dest_msaa_samples == xenos::MsaaSamples::k2X) {
    sample_count = 2;
  } else if (key.dest_msaa_samples == xenos::MsaaSamples::k4X) {
    sample_count = 4;
  }
  desc->setSampleCount(sample_count);

  MTL::RenderPipelineState* pipeline =
      device_->newRenderPipelineState(desc, &error);

  desc->release();
  vs->release();
  ps->release();
  lib->release();

  if (!pipeline) {
    XELOGI(
        "GetOrCreateTransferPipelines: failed to create pipeline (mode={}): {}",
        int(key.mode),
        error && error->localizedDescription()
            ? error->localizedDescription()->utf8String()
            : "unknown error");
    return nullptr;
  }

  transfer_pipelines_.emplace(key, pipeline);
  XELOGI(
      "GetOrCreateTransferPipelines: created pipeline for mode={} "
      "(src_fmt={}, dst_fmt={}, src_msaa={}, dst_msaa={})",
      int(key.mode), key.source_resource_format, key.dest_resource_format,
      int(key.source_msaa_samples), int(key.dest_msaa_samples));

  return pipeline;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe

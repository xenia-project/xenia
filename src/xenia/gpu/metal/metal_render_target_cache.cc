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
#include "xenia/gpu/shaders/bytecode/metal/host_depth_store_1xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/host_depth_store_2xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/host_depth_store_4xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/edram_blend_32bpp_1xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/edram_blend_32bpp_2xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/edram_blend_32bpp_4xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/edram_blend_64bpp_1xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/edram_blend_64bpp_2xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/metal/edram_blend_64bpp_4xmsaa_cs.h"

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

// Packing formats for transferring host RT contents to the EDRAM buffer.
// Keep numeric values in sync with Metal dump shaders in
// InitializeEdramComputeShaders.
enum class MetalEdramDumpFormat : uint32_t {
  kColorRGBA8 = 0,
  kColorRGB10A2Unorm = 1,
  kColorRGB10A2Float = 2,
  kColorRG16Snorm = 3,
  kColorRG16Float = 4,
  kColorR32Float = 5,
  kColorRGBA16Snorm = 6,
  kColorRGBA16Float = 7,
  kColorRG32Float = 8,
  kDepthD24S8 = 16,
  kDepthD24FS8 = 17,
};

constexpr uint32_t kMetalEdramDumpFlagHasStencil = 1u << 0;
constexpr uint32_t kMetalEdramDumpFlagDepthRound = 1u << 1;

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

  // Check if this is a depth/stencil texture and read back via a tiny compute
  // shader to avoid render-pass conversion.
  MTL::PixelFormat format = tex->pixelFormat();
  bool is_depth_stencil = format == MTL::PixelFormatDepth32Float_Stencil8 ||
                          format == MTL::PixelFormatDepth32Float ||
                          format == MTL::PixelFormatDepth16Unorm ||
                          format == MTL::PixelFormatDepth24Unorm_Stencil8 ||
                          format == MTL::PixelFormatX32_Stencil8;
  if (is_depth_stencil) {
    static MTL::Device* cached_device = nullptr;
    static MTL::ComputePipelineState* depth_pipeline = nullptr;
    static MTL::ComputePipelineState* depth_msaa_pipeline = nullptr;

    if (cached_device != device) {
      if (depth_pipeline) {
        depth_pipeline->release();
        depth_pipeline = nullptr;
      }
      if (depth_msaa_pipeline) {
        depth_msaa_pipeline->release();
        depth_msaa_pipeline = nullptr;
      }
      cached_device = device;
    }

    if (!depth_pipeline || !depth_msaa_pipeline) {
      static const char* kDepthReadbackMSL = R"(
        #include <metal_stdlib>
        using namespace metal;

        struct ReadbackParams {
          uint width;
          uint height;
          uint sample;
          uint pad;
        };

        kernel void readback_depth(
            depth2d<float, access::read> src [[texture(0)]],
            device float* out [[buffer(0)]],
            constant ReadbackParams& params [[buffer(1)]],
            uint2 gid [[thread_position_in_grid]]) {
          if (gid.x >= params.width || gid.y >= params.height) {
            return;
          }
          float d = src.read(gid);
          out[gid.y * params.width + gid.x] = d;
        }

        kernel void readback_depth_ms(
            depth2d_ms<float, access::read> src [[texture(0)]],
            device float* out [[buffer(0)]],
            constant ReadbackParams& params [[buffer(1)]],
            uint2 gid [[thread_position_in_grid]]) {
          if (gid.x >= params.width || gid.y >= params.height) {
            return;
          }
          float d = src.read(gid, params.sample);
          out[gid.y * params.width + gid.x] = d;
        }
      )";

      NS::Error* error = nullptr;
      auto src_str =
          NS::String::string(kDepthReadbackMSL, NS::UTF8StringEncoding);
      MTL::Library* lib = device->newLibrary(src_str, nullptr, &error);
      if (!lib) {
        XELOGE(
            "{}: failed to compile depth readback MSL: {}", tag,
            error && error->localizedDescription()
                ? error->localizedDescription()->utf8String()
                : "unknown error");
        return;
      }

      NS::String* fn_depth =
          NS::String::string("readback_depth", NS::UTF8StringEncoding);
      NS::String* fn_depth_ms =
          NS::String::string("readback_depth_ms", NS::UTF8StringEncoding);
      MTL::Function* depth_fn = lib->newFunction(fn_depth);
      MTL::Function* depth_ms_fn = lib->newFunction(fn_depth_ms);
      if (depth_fn) {
        depth_pipeline = device->newComputePipelineState(depth_fn, &error);
        depth_fn->release();
      }
      if (depth_ms_fn) {
        depth_msaa_pipeline = device->newComputePipelineState(depth_ms_fn,
                                                             &error);
        depth_ms_fn->release();
      }
      lib->release();
      if (!depth_pipeline || !depth_msaa_pipeline) {
        XELOGE("{}: failed to create depth readback pipelines", tag);
        if (depth_pipeline) {
          depth_pipeline->release();
          depth_pipeline = nullptr;
        }
        if (depth_msaa_pipeline) {
          depth_msaa_pipeline->release();
          depth_msaa_pipeline = nullptr;
        }
        return;
      }
    }

    uint32_t read_width = std::min<uint32_t>(width, 4u);
    uint32_t read_height = std::min<uint32_t>(height, 4u);
    uint32_t pixel_count = read_width * read_height;
    uint32_t buffer_size = pixel_count * sizeof(float);

    MTL::Buffer* readback_buffer =
        device->newBuffer(buffer_size, MTL::ResourceStorageModeShared);
    if (!readback_buffer) {
      return;
    }

    bool is_msaa = tex->textureType() == MTL::TextureType2DMultisample;

    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* encoder = cmd->computeCommandEncoder();
    encoder->setComputePipelineState(
        is_msaa ? depth_msaa_pipeline : depth_pipeline);
    encoder->setTexture(tex, 0);
    encoder->setBuffer(readback_buffer, 0, 0);

    struct ReadbackParams {
      uint32_t width;
      uint32_t height;
      uint32_t sample;
      uint32_t pad;
    } params = {read_width, read_height, 0u, 0u};

    encoder->setBytes(&params, sizeof(params), 1);
    MTL::Size threads_per_group = MTL::Size::Make(4, 4, 1);
    MTL::Size threadgroups =
        MTL::Size::Make((read_width + 3) / 4, (read_height + 3) / 4, 1);
    encoder->dispatchThreadgroups(threadgroups, threads_per_group);
    encoder->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();

    float* depth_data = static_cast<float*>(readback_buffer->contents());

    const auto& key = rt->key();
    XELOGI(
        "{}: RT key=0x{:08X} {}x{} msaa={} is_depth={} depth samples: "
        "{:.6f} {:.6f} {:.6f} {:.6f}",
        tag, key.key, width, height,
        1u << static_cast<uint32_t>(key.msaa_samples), int(key.is_depth),
        depth_data[0], depth_data[1], depth_data[2], depth_data[3]);

    readback_buffer->release();
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

struct DebugColor {
  float r;
  float g;
  float b;
  float a;
};

uint32_t FloatToBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float BitsToFloat(uint32_t value) {
  float out = 0.0f;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

float HalfToFloat(uint16_t value) {
  uint32_t sign = (value >> 15) & 1u;
  uint32_t exponent = (value >> 10) & 0x1Fu;
  uint32_t mantissa = value & 0x3FFu;
  if (exponent == 0u) {
    if (mantissa == 0u) {
      return sign ? -0.0f : 0.0f;
    }
    float base = float(mantissa) * (1.0f / 1024.0f);
    float result = std::ldexp(base, -14);
    return sign ? -result : result;
  }
  if (exponent == 31u) {
    float inf = std::numeric_limits<float>::infinity();
    return sign ? -inf : inf;
  }
  float base = 1.0f + float(mantissa) * (1.0f / 1024.0f);
  float result = std::ldexp(base, int(exponent) - 15);
  return sign ? -result : result;
}

uint16_t FloatToHalf(float value) {
  uint32_t bits = FloatToBits(value);
  uint32_t sign = (bits >> 16) & 0x8000u;
  int exponent = int((bits >> 23) & 0xFFu) - 127 + 15;
  uint32_t mantissa = bits & 0x7FFFFFu;
  if (exponent <= 0) {
    if (exponent < -10) {
      return uint16_t(sign);
    }
    mantissa |= 0x800000u;
    uint32_t shift = uint32_t(14 - exponent);
    uint32_t half = mantissa >> shift;
    if ((mantissa >> (shift - 1u)) & 1u) {
      ++half;
    }
    return uint16_t(sign | half);
  }
  if (exponent >= 31) {
    return uint16_t(sign | 0x7C00u);
  }
  uint32_t half = (uint32_t(exponent) << 10) | (mantissa >> 13);
  if (mantissa & 0x1000u) {
    ++half;
  }
  return uint16_t(sign | half);
}

uint32_t PackUnorm(float value, float scale) {
  float clamped = std::min(std::max(value, 0.0f), 1.0f);
  return uint32_t(clamped * scale + 0.5f);
}

uint32_t PackSnorm16(float value) {
  float clamped = std::min(std::max(value, -1.0f), 1.0f);
  float bias = clamped >= 0.0f ? 0.5f : -0.5f;
  int packed = int(clamped * 32767.0f + bias);
  return uint32_t(packed) & 0xFFFFu;
}

uint32_t XePreClampedFloat32To7e3(float value) {
  uint32_t f32 = FloatToBits(value);
  uint32_t biased_f32;
  if (f32 < 0x3E800000u) {
    uint32_t f32_exp = f32 >> 23u;
    uint32_t shift = 125u - f32_exp;
    shift = std::min(shift, 24u);
    uint32_t mantissa = (f32 & 0x7FFFFFu) | 0x800000u;
    biased_f32 = mantissa >> shift;
  } else {
    biased_f32 = f32 + 0xC2000000u;
  }
  uint32_t round_bit = (biased_f32 >> 16u) & 1u;
  uint32_t f10 = biased_f32 + 0x7FFFu + round_bit;
  return (f10 >> 16u) & 0x3FFu;
}

uint32_t XeUnclampedFloat32To7e3(float value) {
  if (!std::isfinite(value)) {
    value = 0.0f;
  }
  float clamped = std::min(std::max(value, 0.0f), 31.875f);
  return XePreClampedFloat32To7e3(clamped);
}

float XeFloat7e3To32(uint32_t f10) {
  f10 &= 0x3FFu;
  if (!f10) {
    return 0.0f;
  }
  uint32_t mantissa = f10 & 0x7Fu;
  uint32_t exponent = f10 >> 7u;
  if (exponent == 0u) {
    uint32_t lzcnt = 0;
    if (mantissa != 0u) {
      lzcnt = uint32_t(__builtin_clz(mantissa)) - 24u;
    }
    exponent = uint32_t(int32_t(1) - int32_t(lzcnt));
    mantissa = (mantissa << lzcnt) & 0x7Fu;
  }
  uint32_t f32 = ((exponent + 124u) << 23u) | (mantissa << 16u);
  return BitsToFloat(f32);
}

uint32_t PackR8G8B8A8Unorm(const DebugColor& color) {
  uint32_t r = PackUnorm(color.r, 255.0f);
  uint32_t g = PackUnorm(color.g, 255.0f);
  uint32_t b = PackUnorm(color.b, 255.0f);
  uint32_t a = PackUnorm(color.a, 255.0f);
  return r | (g << 8u) | (b << 16u) | (a << 24u);
}

bool PackColor32bpp(uint32_t format, const DebugColor& color,
                    uint32_t* packed_out) {
  switch (format) {
    case uint32_t(MetalEdramDumpFormat::kColorRGBA8): {
      *packed_out = PackR8G8B8A8Unorm(color);
      return true;
    }
    case uint32_t(MetalEdramDumpFormat::kColorRGB10A2Unorm): {
      uint32_t r = PackUnorm(color.r, 1023.0f);
      uint32_t g = PackUnorm(color.g, 1023.0f);
      uint32_t b = PackUnorm(color.b, 1023.0f);
      uint32_t a = PackUnorm(color.a, 3.0f);
      *packed_out = r | (g << 10u) | (b << 20u) | (a << 30u);
      return true;
    }
    case uint32_t(MetalEdramDumpFormat::kColorRGB10A2Float): {
      uint32_t r = XeUnclampedFloat32To7e3(color.r);
      uint32_t g = XeUnclampedFloat32To7e3(color.g);
      uint32_t b = XeUnclampedFloat32To7e3(color.b);
      uint32_t a = PackUnorm(color.a, 3.0f);
      *packed_out =
          (r & 0x3FFu) | ((g & 0x3FFu) << 10u) |
          ((b & 0x3FFu) << 20u) | ((a & 0x3u) << 30u);
      return true;
    }
    case uint32_t(MetalEdramDumpFormat::kColorRG16Snorm): {
      uint32_t r = PackSnorm16(color.r);
      uint32_t g = PackSnorm16(color.g);
      *packed_out = r | (g << 16u);
      return true;
    }
    case uint32_t(MetalEdramDumpFormat::kColorRG16Float): {
      uint16_t r = FloatToHalf(color.r);
      uint16_t g = FloatToHalf(color.g);
      *packed_out = uint32_t(r) | (uint32_t(g) << 16u);
      return true;
    }
    case uint32_t(MetalEdramDumpFormat::kColorR32Float): {
      *packed_out = FloatToBits(color.r);
      return true;
    }
    default:
      break;
  }
  return false;
}

bool UnpackColor32bpp(uint32_t format, uint32_t packed,
                      DebugColor* color_out) {
  if (!color_out) {
    return false;
  }
  switch (format) {
    case uint32_t(MetalEdramDumpFormat::kColorRGBA8): {
      color_out->r = float(packed & 0xFFu) * (1.0f / 255.0f);
      color_out->g = float((packed >> 8u) & 0xFFu) * (1.0f / 255.0f);
      color_out->b = float((packed >> 16u) & 0xFFu) * (1.0f / 255.0f);
      color_out->a = float(packed >> 24u) * (1.0f / 255.0f);
      return true;
    }
    case uint32_t(MetalEdramDumpFormat::kColorRGB10A2Unorm): {
      color_out->r = float(packed & 0x3FFu) * (1.0f / 1023.0f);
      color_out->g = float((packed >> 10u) & 0x3FFu) * (1.0f / 1023.0f);
      color_out->b = float((packed >> 20u) & 0x3FFu) * (1.0f / 1023.0f);
      color_out->a = float((packed >> 30u) & 0x3u) * (1.0f / 3.0f);
      return true;
    }
    case uint32_t(MetalEdramDumpFormat::kColorRGB10A2Float): {
      color_out->r = XeFloat7e3To32(packed & 0x3FFu);
      color_out->g = XeFloat7e3To32((packed >> 10u) & 0x3FFu);
      color_out->b = XeFloat7e3To32((packed >> 20u) & 0x3FFu);
      color_out->a = float((packed >> 30u) & 0x3u) * (1.0f / 3.0f);
      return true;
    }
    case uint32_t(MetalEdramDumpFormat::kColorRG16Snorm): {
      int16_t r = int16_t(packed & 0xFFFFu);
      int16_t g = int16_t(packed >> 16u);
      color_out->r = std::max(float(r) * (1.0f / 32767.0f), -1.0f);
      color_out->g = std::max(float(g) * (1.0f / 32767.0f), -1.0f);
      color_out->b = 0.0f;
      color_out->a = 1.0f;
      return true;
    }
    case uint32_t(MetalEdramDumpFormat::kColorRG16Float): {
      uint16_t r = uint16_t(packed & 0xFFFFu);
      uint16_t g = uint16_t(packed >> 16u);
      color_out->r = HalfToFloat(r);
      color_out->g = HalfToFloat(g);
      color_out->b = 0.0f;
      color_out->a = 1.0f;
      return true;
    }
    case uint32_t(MetalEdramDumpFormat::kColorR32Float): {
      color_out->r = BitsToFloat(packed);
      color_out->g = 0.0f;
      color_out->b = 0.0f;
      color_out->a = 1.0f;
      return true;
    }
    default:
      break;
  }
  return false;
}

bool DecodeColorTexel(MTL::PixelFormat format, const uint8_t* bytes,
                      DebugColor* color_out) {
  if (!color_out) {
    return false;
  }
  switch (format) {
    case MTL::PixelFormatRGBA16Float: {
      uint16_t components[4];
      std::memcpy(components, bytes, sizeof(components));
      color_out->r = HalfToFloat(components[0]);
      color_out->g = HalfToFloat(components[1]);
      color_out->b = HalfToFloat(components[2]);
      color_out->a = HalfToFloat(components[3]);
      return true;
    }
    case MTL::PixelFormatRG16Float: {
      uint16_t components[2];
      std::memcpy(components, bytes, sizeof(components));
      color_out->r = HalfToFloat(components[0]);
      color_out->g = HalfToFloat(components[1]);
      color_out->b = 0.0f;
      color_out->a = 1.0f;
      return true;
    }
    case MTL::PixelFormatRGBA8Unorm: {
      color_out->r = float(bytes[0]) * (1.0f / 255.0f);
      color_out->g = float(bytes[1]) * (1.0f / 255.0f);
      color_out->b = float(bytes[2]) * (1.0f / 255.0f);
      color_out->a = float(bytes[3]) * (1.0f / 255.0f);
      return true;
    }
    case MTL::PixelFormatBGRA8Unorm: {
      color_out->b = float(bytes[0]) * (1.0f / 255.0f);
      color_out->g = float(bytes[1]) * (1.0f / 255.0f);
      color_out->r = float(bytes[2]) * (1.0f / 255.0f);
      color_out->a = float(bytes[3]) * (1.0f / 255.0f);
      return true;
    }
    case MTL::PixelFormatRGB10A2Unorm:
    case MTL::PixelFormatBGR10A2Unorm: {
      uint32_t packed = 0;
      std::memcpy(&packed, bytes, sizeof(packed));
      DebugColor unpacked;
      unpacked.r = float(packed & 0x3FFu) * (1.0f / 1023.0f);
      unpacked.g = float((packed >> 10u) & 0x3FFu) * (1.0f / 1023.0f);
      unpacked.b = float((packed >> 20u) & 0x3FFu) * (1.0f / 1023.0f);
      unpacked.a = float((packed >> 30u) & 0x3u) * (1.0f / 3.0f);
      if (format == MTL::PixelFormatBGR10A2Unorm) {
        std::swap(unpacked.r, unpacked.b);
      }
      *color_out = unpacked;
      return true;
    }
    case MTL::PixelFormatR32Float: {
      uint32_t packed = 0;
      std::memcpy(&packed, bytes, sizeof(packed));
      color_out->r = BitsToFloat(packed);
      color_out->g = 0.0f;
      color_out->b = 0.0f;
      color_out->a = 1.0f;
      return true;
    }
    case MTL::PixelFormatRG32Float: {
      uint32_t packed[2] = {};
      std::memcpy(packed, bytes, sizeof(packed));
      color_out->r = BitsToFloat(packed[0]);
      color_out->g = BitsToFloat(packed[1]);
      color_out->b = 0.0f;
      color_out->a = 1.0f;
      return true;
    }
    default:
      break;
  }
  return false;
}

void ScheduleEdramDumpColorSamples(MTL::CommandBuffer* cmd, MTL::Texture* tex,
                                   uint32_t rt_key_value,
                                   uint32_t dump_format) {
  if (!::cvars::metal_log_edram_dump_color_samples || !cmd || !tex) {
    return;
  }
  static uint32_t log_count = 0;
  if (log_count >= 8) {
    return;
  }
  ++log_count;

  uint32_t bytes_per_pixel = 0;
  switch (tex->pixelFormat()) {
    case MTL::PixelFormatRGBA16Float:
    case MTL::PixelFormatRG32Float:
      bytes_per_pixel = 8;
      break;
    case MTL::PixelFormatRG16Float:
    case MTL::PixelFormatR32Float:
    case MTL::PixelFormatRGBA8Unorm:
    case MTL::PixelFormatBGRA8Unorm:
    case MTL::PixelFormatRGB10A2Unorm:
    case MTL::PixelFormatBGR10A2Unorm:
      bytes_per_pixel = 4;
      break;
    default:
      XELOGI(
          "MetalEdramDump32bpp: unsupported source pixel format {}",
          int(tex->pixelFormat()));
      return;
  }

  uint32_t width = uint32_t(tex->width());
  uint32_t height = uint32_t(tex->height());
  std::array<std::pair<uint32_t, uint32_t>, 2> sample_coords = {
      std::make_pair(538u, 205u),
      std::make_pair(623u, 68u),
  };
  const char* sample_labels[2] = {"tl", "mid"};

  uint32_t max_x = width ? width - 1u : 0u;
  uint32_t max_y = height ? height - 1u : 0u;
  for (auto& coord : sample_coords) {
    coord.first = std::min(coord.first, max_x);
    coord.second = std::min(coord.second, max_y);
  }

  MTL::Device* device = cmd->device();
  if (!device) {
    return;
  }
  MTL::Buffer* buffer = device->newBuffer(
      bytes_per_pixel * uint32_t(sample_coords.size()),
      MTL::ResourceStorageModeShared);
  if (!buffer) {
    return;
  }
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  if (!blit) {
    buffer->release();
    return;
  }
  for (size_t i = 0; i < sample_coords.size(); ++i) {
    blit->copyFromTexture(
        tex, 0, 0,
        MTL::Origin::Make(sample_coords[i].first, sample_coords[i].second, 0),
        MTL::Size::Make(1, 1, 1), buffer,
        uint32_t(i) * bytes_per_pixel, bytes_per_pixel, 0);
  }
  blit->endEncoding();

  buffer->retain();
  MTL::PixelFormat format = tex->pixelFormat();
  cmd->addCompletedHandler([buffer, bytes_per_pixel, format, dump_format,
                            rt_key_value, sample_coords, sample_labels](
                               MTL::CommandBuffer*) {
    const uint8_t* bytes =
        static_cast<const uint8_t*>(buffer->contents());
    if (!bytes) {
      buffer->release();
      return;
    }
    for (size_t i = 0; i < sample_coords.size(); ++i) {
      const uint8_t* sample_bytes = bytes + i * bytes_per_pixel;
      DebugColor source = {};
      uint32_t packed = 0;
      if (!DecodeColorTexel(format, sample_bytes, &source)) {
        continue;
      }
      if (!PackColor32bpp(dump_format, source, &packed)) {
        XELOGI(
            "MetalEdramDump32bpp {}: unsupported dump_format {} for key=0x{:08X}",
            sample_labels[i], dump_format, rt_key_value);
        continue;
      }
      DebugColor unpacked = {};
      if (!UnpackColor32bpp(dump_format, packed, &unpacked)) {
        continue;
      }
      uint32_t rgba8 = PackR8G8B8A8Unorm(unpacked);
      XELOGI(
          "MetalEdramDump32bpp {}: key=0x{:08X} src_fmt={} dump_fmt={} "
          "coord=({}, {}) src=({:.6f} {:.6f} {:.6f} {:.6f}) packed=0x{:08X} "
          "unpacked_rgba8={:02X} {:02X} {:02X} {:02X}",
          sample_labels[i], rt_key_value, int(format), dump_format,
          sample_coords[i].first, sample_coords[i].second, source.r, source.g,
          source.b, source.a, packed, rgba8 & 0xFFu, (rgba8 >> 8u) & 0xFFu,
          (rgba8 >> 16u) & 0xFFu, (rgba8 >> 24u) & 0xFFu);
    }
    buffer->release();
  });
  buffer->release();
}

size_t MsaaSamplesToIndex(xenos::MsaaSamples samples) {
  switch (samples) {
    case xenos::MsaaSamples::k1X:
      return 0;
    case xenos::MsaaSamples::k2X:
      return 1;
    case xenos::MsaaSamples::k4X:
      return 2;
    default:
      return 0;
  }
}

uint32_t MsaaSamplesToCount(xenos::MsaaSamples samples) {
  switch (samples) {
    case xenos::MsaaSamples::k1X:
      return 1;
    case xenos::MsaaSamples::k2X:
      return 2;
    case xenos::MsaaSamples::k4X:
      return 4;
    default:
      return 1;
  }
}

struct TransferAddressConstants {
  uint32_t dest_pitch;
  uint32_t source_pitch;
  int32_t source_to_dest;
};

struct TransferShaderConstants {
  TransferAddressConstants address;
  TransferAddressConstants host_depth_address;
  uint32_t source_format;
  uint32_t dest_format;
  uint32_t source_is_depth;
  uint32_t dest_is_depth;
  uint32_t source_is_uint;
  uint32_t dest_is_uint;
  uint32_t source_is_64bpp;
  uint32_t dest_is_64bpp;
  uint32_t source_msaa_samples;
  uint32_t dest_msaa_samples;
  uint32_t host_depth_source_msaa_samples;
  uint32_t host_depth_source_is_copy;
  uint32_t depth_round;
  uint32_t msaa_2x_supported;
  uint32_t tile_width_samples;
  uint32_t tile_height_samples;
  uint32_t dest_sample_id;
  uint32_t stencil_mask;
  uint32_t stencil_clear;
};

struct TransferClearColorFloatConstants {
  float color[4];
};

struct TransferClearColorUintConstants {
  uint32_t color[4];
};

struct TransferClearDepthConstants {
  float depth;
  float padding[3];
};

enum class TransferOutput {
  kColor,
  kDepth,
  kStencilBit,
};

struct TransferModeInfo {
  TransferOutput output;
  bool source_is_color;
  bool uses_host_depth;
};

constexpr TransferModeInfo kTransferModeInfos[] = {
    {TransferOutput::kColor, true, false},   // kColorToColor
    {TransferOutput::kDepth, true, false},   // kColorToDepth
    {TransferOutput::kColor, false, false},  // kDepthToColor
    {TransferOutput::kDepth, false, false},  // kDepthToDepth
    {TransferOutput::kStencilBit, true, false},   // kColorToStencilBit
    {TransferOutput::kStencilBit, false, false},  // kDepthToStencilBit
    {TransferOutput::kDepth, true, true},   // kColorAndHostDepthToDepth
    {TransferOutput::kDepth, false, true},  // kDepthAndHostDepthToDepth
};

}  // namespace

uint32_t MetalRenderTargetCache::GetMetalEdramDumpFormat(RenderTargetKey key) {
  if (key.is_depth) {
    switch (key.GetDepthFormat()) {
      case xenos::DepthRenderTargetFormat::kD24FS8:
        return static_cast<uint32_t>(MetalEdramDumpFormat::kDepthD24FS8);
      case xenos::DepthRenderTargetFormat::kD24S8:
      default:
        return static_cast<uint32_t>(MetalEdramDumpFormat::kDepthD24S8);
    }
  }
  switch (key.GetColorFormat()) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return static_cast<uint32_t>(MetalEdramDumpFormat::kColorRGBA8);
    case xenos::ColorRenderTargetFormat::k_2_10_10_10:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return static_cast<uint32_t>(
          MetalEdramDumpFormat::kColorRGB10A2Unorm);
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return static_cast<uint32_t>(
          MetalEdramDumpFormat::kColorRGB10A2Float);
    case xenos::ColorRenderTargetFormat::k_16_16:
      return static_cast<uint32_t>(MetalEdramDumpFormat::kColorRG16Snorm);
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return static_cast<uint32_t>(MetalEdramDumpFormat::kColorRG16Float);
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return static_cast<uint32_t>(MetalEdramDumpFormat::kColorR32Float);
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      return static_cast<uint32_t>(
          MetalEdramDumpFormat::kColorRGBA16Snorm);
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return static_cast<uint32_t>(
          MetalEdramDumpFormat::kColorRGBA16Float);
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return static_cast<uint32_t>(MetalEdramDumpFormat::kColorRG32Float);
    default:
      return static_cast<uint32_t>(MetalEdramDumpFormat::kColorRGBA8);
  }
}

// MetalRenderTarget implementation
MetalRenderTargetCache::MetalRenderTarget::~MetalRenderTarget() {
  if (draw_texture_ && draw_texture_ != texture_) {
    draw_texture_->release();
    draw_texture_ = nullptr;
  }
  if (transfer_texture_ && transfer_texture_ != texture_) {
    transfer_texture_->release();
    transfer_texture_ = nullptr;
  }
  if (msaa_draw_texture_ && msaa_draw_texture_ != msaa_texture_) {
    msaa_draw_texture_->release();
    msaa_draw_texture_ = nullptr;
  }
  if (msaa_transfer_texture_ && msaa_transfer_texture_ != msaa_texture_) {
    msaa_transfer_texture_->release();
    msaa_transfer_texture_ = nullptr;
  }
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

RenderTargetCache::Path MetalRenderTargetCache::GetPath() const {
  if (cvars::metal_edram_rov && raster_order_groups_supported_) {
    return Path::kPixelShaderInterlock;
  }
  return Path::kHostRenderTargets;
}

bool MetalRenderTargetCache::Initialize() {
  device_ = command_processor_.GetMetalDevice();
  if (!device_) {
    XELOGE("MetalRenderTargetCache: No Metal device available");
    return false;
  }

  raster_order_groups_supported_ = device_->areRasterOrderGroupsSupported();
  if (cvars::metal_edram_rov && !raster_order_groups_supported_) {
    XELOGW("MetalRenderTargetCache: raster order groups unsupported; forcing "
           "host render target path");
  }

  msaa_2x_supported_ = device_->supportsTextureSampleCount(2);

  gamma_render_target_as_srgb_ = cvars::gamma_render_target_as_srgb;

  // Create the EDRAM buffer.
  //
  // The guest has 10 MiB of EDRAM for samples, but with host resolution
  // scaling enabled the compute path addresses a scaled EDRAM layout (the
  // shaders multiply the tile dimensions by resolution_scale_x/y). The buffer
  // therefore must be scaled by the same factor to avoid out-of-bounds writes.
  const uint32_t scale_x = std::max<uint32_t>(1u, draw_resolution_scale_x());
  const uint32_t scale_y = std::max<uint32_t>(1u, draw_resolution_scale_y());
  const size_t edram_dwords =
      size_t(xenos::kEdramTileCount) * size_t(xenos::kEdramTileWidthSamples) *
      size_t(xenos::kEdramTileHeightSamples) * size_t(scale_x) *
      size_t(scale_y);
  const size_t edram_size_bytes = edram_dwords * sizeof(uint32_t);
  edram_buffer_ =
      device_->newBuffer(edram_size_bytes, MTL::ResourceStorageModeShared);
  if (!edram_buffer_) {
    XELOGE("MetalRenderTargetCache: Failed to create EDRAM buffer");
    return false;
  }
  edram_buffer_->setLabel(
      NS::String::string("EDRAM Buffer", NS::UTF8StringEncoding));
  void* edram_contents = edram_buffer_->contents();
  if (edram_contents) {
    std::memset(edram_contents, 0, edram_size_bytes);
  }
  XELOGI("MetalRenderTargetCache: EDRAM buffer size {} bytes (scale {}x{})",
         edram_size_bytes, scale_x, scale_y);

  // Initialize EDRAM compute shaders
  if (!InitializeEdramComputeShaders()) {
    XELOGE(
        "MetalRenderTargetCache: Failed to initialize EDRAM compute shaders");
    return false;
  }

  // Initialize base class
  InitializeCommon();

  XELOGI("MetalRenderTargetCache: path={}",
         GetPath() == Path::kHostRenderTargets ? "host" : "rov");
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
  ordered_blend_coverage_active_ = false;
  if (ordered_blend_coverage_texture_) {
    ordered_blend_coverage_texture_->release();
    ordered_blend_coverage_texture_ = nullptr;
  }
  ordered_blend_coverage_width_ = 0;
  ordered_blend_coverage_height_ = 0;
  ordered_blend_coverage_samples_ = 0;

  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }

  for (auto& it : transfer_pipelines_) {
    if (it.second) {
      it.second->release();
    }
  }
  transfer_pipelines_.clear();
  for (auto& it : edram_load_pipelines_) {
    if (it.second) {
      it.second->release();
    }
  }
  edram_load_pipelines_.clear();
  for (auto& it : transfer_clear_pipelines_) {
    if (it.second) {
      it.second->release();
    }
  }
  transfer_clear_pipelines_.clear();
  if (transfer_library_) {
    transfer_library_->release();
    transfer_library_ = nullptr;
  }
  if (edram_load_library_) {
    edram_load_library_->release();
    edram_load_library_ = nullptr;
  }
  if (edram_load_library_msaa_) {
    edram_load_library_msaa_->release();
    edram_load_library_msaa_ = nullptr;
  }
  if (transfer_depth_state_) {
    transfer_depth_state_->release();
    transfer_depth_state_ = nullptr;
  }
  if (transfer_depth_state_none_) {
    transfer_depth_state_none_->release();
    transfer_depth_state_none_ = nullptr;
  }
  if (transfer_depth_clear_state_) {
    transfer_depth_clear_state_->release();
    transfer_depth_clear_state_ = nullptr;
  }
  if (transfer_stencil_clear_state_) {
    transfer_stencil_clear_state_->release();
    transfer_stencil_clear_state_ = nullptr;
  }
  for (auto& state : transfer_stencil_bit_states_) {
    if (state) {
      state->release();
      state = nullptr;
    }
  }
  if (transfer_dummy_buffer_) {
    transfer_dummy_buffer_->release();
    transfer_dummy_buffer_ = nullptr;
  }
  for (size_t i = 0; i < xe::countof(transfer_dummy_color_float_); ++i) {
    if (transfer_dummy_color_float_[i]) {
      transfer_dummy_color_float_[i]->release();
      transfer_dummy_color_float_[i] = nullptr;
    }
    if (transfer_dummy_color_uint_[i]) {
      transfer_dummy_color_uint_[i]->release();
      transfer_dummy_color_uint_[i] = nullptr;
    }
    if (transfer_dummy_depth_[i]) {
      transfer_dummy_depth_[i]->release();
      transfer_dummy_depth_[i] = nullptr;
    }
    if (transfer_dummy_stencil_[i]) {
      transfer_dummy_stencil_[i]->release();
      transfer_dummy_stencil_[i] = nullptr;
    }
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
  edram_dump_color_32bpp_2xmsaa_pipeline_ = nullptr;
  edram_dump_color_32bpp_4xmsaa_pipeline_ = nullptr;
  edram_dump_color_64bpp_1xmsaa_pipeline_ = nullptr;
  edram_dump_color_64bpp_2xmsaa_pipeline_ = nullptr;
  edram_dump_color_64bpp_4xmsaa_pipeline_ = nullptr;
  edram_dump_depth_32bpp_1xmsaa_pipeline_ = nullptr;
  edram_dump_depth_32bpp_2xmsaa_pipeline_ = nullptr;
  edram_dump_depth_32bpp_4xmsaa_pipeline_ = nullptr;
  edram_blend_32bpp_1xmsaa_pipeline_ = nullptr;
  edram_blend_32bpp_2xmsaa_pipeline_ = nullptr;
  edram_blend_32bpp_4xmsaa_pipeline_ = nullptr;
  edram_blend_64bpp_1xmsaa_pipeline_ = nullptr;
  edram_blend_64bpp_2xmsaa_pipeline_ = nullptr;
  edram_blend_64bpp_4xmsaa_pipeline_ = nullptr;
  resolve_full_8bpp_pipeline_ = nullptr;
  resolve_full_16bpp_pipeline_ = nullptr;
  resolve_full_32bpp_pipeline_ = nullptr;
  resolve_full_64bpp_pipeline_ = nullptr;
  resolve_full_128bpp_pipeline_ = nullptr;
  resolve_fast_32bpp_1x2xmsaa_pipeline_ = nullptr;
  resolve_fast_32bpp_4xmsaa_pipeline_ = nullptr;
  resolve_fast_64bpp_1x2xmsaa_pipeline_ = nullptr;
  resolve_fast_64bpp_4xmsaa_pipeline_ = nullptr;
  for (size_t i = 0; i < xe::countof(host_depth_store_pipelines_); ++i) {
    host_depth_store_pipelines_[i] = nullptr;
  }

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

  host_depth_store_pipelines_[size_t(xenos::MsaaSamples::k1X)] =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, host_depth_store_1xmsaa_cs_metallib,
          sizeof(host_depth_store_1xmsaa_cs_metallib),
          "host_depth_store_1xmsaa");
  host_depth_store_pipelines_[size_t(xenos::MsaaSamples::k2X)] =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, host_depth_store_2xmsaa_cs_metallib,
          sizeof(host_depth_store_2xmsaa_cs_metallib),
          "host_depth_store_2xmsaa");
  host_depth_store_pipelines_[size_t(xenos::MsaaSamples::k4X)] =
      CreateComputePipelineFromEmbeddedLibrary(
          device_, host_depth_store_4xmsaa_cs_metallib,
          sizeof(host_depth_store_4xmsaa_cs_metallib),
          "host_depth_store_4xmsaa");

  for (size_t i = 0; i < xe::countof(host_depth_store_pipelines_); ++i) {
    if (!host_depth_store_pipelines_[i]) {
      XELOGE("Metal: failed to initialize host depth store pipelines");
      return false;
    }
  }

  edram_blend_32bpp_1xmsaa_pipeline_ = CreateComputePipelineFromEmbeddedLibrary(
      device_, edram_blend_32bpp_1xmsaa_cs_metallib,
      sizeof(edram_blend_32bpp_1xmsaa_cs_metallib),
      "edram_blend_32bpp_1xmsaa");
  edram_blend_32bpp_2xmsaa_pipeline_ = CreateComputePipelineFromEmbeddedLibrary(
      device_, edram_blend_32bpp_2xmsaa_cs_metallib,
      sizeof(edram_blend_32bpp_2xmsaa_cs_metallib),
      "edram_blend_32bpp_2xmsaa");
  edram_blend_32bpp_4xmsaa_pipeline_ = CreateComputePipelineFromEmbeddedLibrary(
      device_, edram_blend_32bpp_4xmsaa_cs_metallib,
      sizeof(edram_blend_32bpp_4xmsaa_cs_metallib),
      "edram_blend_32bpp_4xmsaa");
  edram_blend_64bpp_1xmsaa_pipeline_ = CreateComputePipelineFromEmbeddedLibrary(
      device_, edram_blend_64bpp_1xmsaa_cs_metallib,
      sizeof(edram_blend_64bpp_1xmsaa_cs_metallib),
      "edram_blend_64bpp_1xmsaa");
  edram_blend_64bpp_2xmsaa_pipeline_ = CreateComputePipelineFromEmbeddedLibrary(
      device_, edram_blend_64bpp_2xmsaa_cs_metallib,
      sizeof(edram_blend_64bpp_2xmsaa_cs_metallib),
      "edram_blend_64bpp_2xmsaa");
  edram_blend_64bpp_4xmsaa_pipeline_ = CreateComputePipelineFromEmbeddedLibrary(
      device_, edram_blend_64bpp_4xmsaa_cs_metallib,
      sizeof(edram_blend_64bpp_4xmsaa_cs_metallib),
      "edram_blend_64bpp_4xmsaa");
  if (!edram_blend_32bpp_1xmsaa_pipeline_) {
    XELOGW("Metal: failed to initialize edram_blend_32bpp_1xmsaa pipeline");
  }
  if (!edram_blend_32bpp_2xmsaa_pipeline_) {
    XELOGW("Metal: failed to initialize edram_blend_32bpp_2xmsaa pipeline");
  }
  if (!edram_blend_32bpp_4xmsaa_pipeline_) {
    XELOGW("Metal: failed to initialize edram_blend_32bpp_4xmsaa pipeline");
  }
  if (!edram_blend_64bpp_1xmsaa_pipeline_) {
    XELOGW("Metal: failed to initialize edram_blend_64bpp_1xmsaa pipeline");
  }
  if (!edram_blend_64bpp_2xmsaa_pipeline_) {
    XELOGW("Metal: failed to initialize edram_blend_64bpp_2xmsaa pipeline");
  }
  if (!edram_blend_64bpp_4xmsaa_pipeline_) {
    XELOGW("Metal: failed to initialize edram_blend_64bpp_4xmsaa pipeline");
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
  uint format;
  uint flags;
};

constant uint kDumpFormatColorRGBA8 = 0;
constant uint kDumpFormatColorRGB10A2Unorm = 1;
constant uint kDumpFormatColorRGB10A2Float = 2;
constant uint kDumpFormatColorRG16Snorm = 3;
constant uint kDumpFormatColorRG16Float = 4;
constant uint kDumpFormatColorR32Float = 5;
constant uint kDumpFormatColorRGBA16Snorm = 6;
constant uint kDumpFormatColorRGBA16Float = 7;
constant uint kDumpFormatColorRG32Float = 8;
constant uint kDumpFormatDepthD24S8 = 16;
constant uint kDumpFormatDepthD24FS8 = 17;
constant uint kDumpFlagHasStencil = 1;
constant uint kDumpFlagDepthRound = 2;

inline uint XePackUnorm(float value, float scale) {
  return uint(clamp(value, 0.0f, 1.0f) * scale + 0.5f);
}

inline uint XePackSnorm16(float value) {
  float clamped = clamp(value, -1.0f, 1.0f);
  float bias = clamped >= 0.0f ? 0.5f : -0.5f;
  int packed = int(clamped * 32767.0f + bias);
  return uint(packed) & 0xFFFFu;
}

uint XePreClampedFloat32To7e3(float value) {
  uint f32 = as_type<uint>(value);
  uint biased_f32;
  if (f32 < 0x3E800000u) {
    uint f32_exp = f32 >> 23u;
    uint shift = 125u - f32_exp;
    shift = min(shift, 24u);
    uint mantissa = (f32 & 0x7FFFFFu) | 0x800000u;
    biased_f32 = mantissa >> shift;
  } else {
    biased_f32 = f32 + 0xC2000000u;
  }
  uint round_bit = (biased_f32 >> 16u) & 1u;
  uint f10 = biased_f32 + 0x7FFFu + round_bit;
  return (f10 >> 16u) & 0x3FFu;
}

uint XeUnclampedFloat32To7e3(float value) {
  float clamped = min(max(value, 0.0f), 31.875f);
  return XePreClampedFloat32To7e3(clamped);
}

uint XePackColor32bpp(uint format, float4 color) {
  switch (format) {
    case kDumpFormatColorRGBA8: {
      uint r = XePackUnorm(color.r, 255.0f);
      uint g = XePackUnorm(color.g, 255.0f);
      uint b = XePackUnorm(color.b, 255.0f);
      uint a = XePackUnorm(color.a, 255.0f);
      return r | (g << 8u) | (b << 16u) | (a << 24u);
    }
    case kDumpFormatColorRGB10A2Unorm: {
      uint r = XePackUnorm(color.r, 1023.0f);
      uint g = XePackUnorm(color.g, 1023.0f);
      uint b = XePackUnorm(color.b, 1023.0f);
      uint a = XePackUnorm(color.a, 3.0f);
      return r | (g << 10u) | (b << 20u) | (a << 30u);
    }
    case kDumpFormatColorRGB10A2Float: {
      uint r = XeUnclampedFloat32To7e3(color.r);
      uint g = XeUnclampedFloat32To7e3(color.g);
      uint b = XeUnclampedFloat32To7e3(color.b);
      uint a = XePackUnorm(color.a, 3.0f);
      return (r & 0x3FFu) | ((g & 0x3FFu) << 10u) |
             ((b & 0x3FFu) << 20u) | ((a & 0x3u) << 30u);
    }
    case kDumpFormatColorRG16Snorm: {
      uint r = XePackSnorm16(color.r);
      uint g = XePackSnorm16(color.g);
      return r | (g << 16u);
    }
    case kDumpFormatColorRG16Float:
      return as_type<uint>(half2(color.rg));
    case kDumpFormatColorR32Float:
      return as_type<uint>(color.r);
    default: {
      uint r = XePackUnorm(color.r, 255.0f);
      uint g = XePackUnorm(color.g, 255.0f);
      uint b = XePackUnorm(color.b, 255.0f);
      uint a = XePackUnorm(color.a, 255.0f);
      return r | (g << 8u) | (b << 16u) | (a << 24u);
    }
  }
}

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

  uint packed = XePackColor32bpp(constants.format, color);

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

  // EDRAM dump compute shader for 32-bpp color, 2x MSAA.
  {
    static const char kEdramDumpColor32bpp2xMsaaShader[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct EdramDumpConstants {
  uint dispatch_first_tile;
  uint source_base_tiles;
  uint dest_pitch_tiles;
  uint source_pitch_tiles;
  uint2 resolution_scale;
  uint format;
  uint flags;
};

constant uint kDumpFormatColorRGBA8 = 0;
constant uint kDumpFormatColorRGB10A2Unorm = 1;
constant uint kDumpFormatColorRGB10A2Float = 2;
constant uint kDumpFormatColorRG16Snorm = 3;
constant uint kDumpFormatColorRG16Float = 4;
constant uint kDumpFormatColorR32Float = 5;
constant uint kDumpFormatColorRGBA16Snorm = 6;
constant uint kDumpFormatColorRGBA16Float = 7;
constant uint kDumpFormatColorRG32Float = 8;
constant uint kDumpFormatDepthD24S8 = 16;
constant uint kDumpFormatDepthD24FS8 = 17;
constant uint kDumpFlagHasStencil = 1;
constant uint kDumpFlagDepthRound = 2;

inline uint XePackUnorm(float value, float scale) {
  return uint(clamp(value, 0.0f, 1.0f) * scale + 0.5f);
}

inline uint XePackSnorm16(float value) {
  float clamped = clamp(value, -1.0f, 1.0f);
  float bias = clamped >= 0.0f ? 0.5f : -0.5f;
  int packed = int(clamped * 32767.0f + bias);
  return uint(packed) & 0xFFFFu;
}

uint XePreClampedFloat32To7e3(float value) {
  uint f32 = as_type<uint>(value);
  uint biased_f32;
  if (f32 < 0x3E800000u) {
    uint f32_exp = f32 >> 23u;
    uint shift = 125u - f32_exp;
    shift = min(shift, 24u);
    uint mantissa = (f32 & 0x7FFFFFu) | 0x800000u;
    biased_f32 = mantissa >> shift;
  } else {
    biased_f32 = f32 + 0xC2000000u;
  }
  uint round_bit = (biased_f32 >> 16u) & 1u;
  uint f10 = biased_f32 + 0x7FFFu + round_bit;
  return (f10 >> 16u) & 0x3FFu;
}

uint XeUnclampedFloat32To7e3(float value) {
  float clamped = min(max(value, 0.0f), 31.875f);
  return XePreClampedFloat32To7e3(clamped);
}

uint XePackColor32bpp(uint format, float4 color) {
  switch (format) {
    case kDumpFormatColorRGBA8: {
      uint r = XePackUnorm(color.r, 255.0f);
      uint g = XePackUnorm(color.g, 255.0f);
      uint b = XePackUnorm(color.b, 255.0f);
      uint a = XePackUnorm(color.a, 255.0f);
      return r | (g << 8u) | (b << 16u) | (a << 24u);
    }
    case kDumpFormatColorRGB10A2Unorm: {
      uint r = XePackUnorm(color.r, 1023.0f);
      uint g = XePackUnorm(color.g, 1023.0f);
      uint b = XePackUnorm(color.b, 1023.0f);
      uint a = XePackUnorm(color.a, 3.0f);
      return r | (g << 10u) | (b << 20u) | (a << 30u);
    }
    case kDumpFormatColorRGB10A2Float: {
      uint r = XeUnclampedFloat32To7e3(color.r);
      uint g = XeUnclampedFloat32To7e3(color.g);
      uint b = XeUnclampedFloat32To7e3(color.b);
      uint a = XePackUnorm(color.a, 3.0f);
      return (r & 0x3FFu) | ((g & 0x3FFu) << 10u) |
             ((b & 0x3FFu) << 20u) | ((a & 0x3u) << 30u);
    }
    case kDumpFormatColorRG16Snorm: {
      uint r = XePackSnorm16(color.r);
      uint g = XePackSnorm16(color.g);
      return r | (g << 16u);
    }
    case kDumpFormatColorRG16Float:
      return as_type<uint>(half2(color.rg));
    case kDumpFormatColorR32Float:
      return as_type<uint>(color.r);
    default: {
      uint r = XePackUnorm(color.r, 255.0f);
      uint g = XePackUnorm(color.g, 255.0f);
      uint b = XePackUnorm(color.b, 255.0f);
      uint a = XePackUnorm(color.a, 255.0f);
      return r | (g << 8u) | (b << 16u) | (a << 24u);
    }
  }
}

kernel void edram_dump_color_32bpp_2xmsaa(
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

  uint sample_id = source_sample.y & 1u;
  uint2 pixel_coord = uint2(source_sample.x, source_sample.y >> 1);

  float4 color = source.read(pixel_coord, sample_id);

  uint packed = XePackColor32bpp(constants.format, color);

  edram[edram_index] = packed;
}
)METAL";

    NS::String* source = NS::String::string(kEdramDumpColor32bpp2xMsaaShader,
                                            NS::UTF8StringEncoding);
    MTL::Library* lib = device_->newLibrary(source, nullptr, &error);
    if (!lib) {
      XELOGW(
          "Metal: failed to compile edram_dump_color_32bpp_2xmsaa shader: {}",
          error ? error->localizedDescription()->utf8String() : "unknown");
    } else {
      NS::String* fn_name = NS::String::string("edram_dump_color_32bpp_2xmsaa",
                                               NS::UTF8StringEncoding);
      MTL::Function* fn = lib->newFunction(fn_name);
      if (!fn) {
        XELOGW("Metal: edram_dump_color_32bpp_2xmsaa missing entrypoint");
        lib->release();
      } else {
        edram_dump_color_32bpp_2xmsaa_pipeline_ =
            device_->newComputePipelineState(fn, &error);
        fn->release();
        lib->release();
        if (!edram_dump_color_32bpp_2xmsaa_pipeline_) {
          XELOGW(
              "Metal: failed to create edram_dump_color_32bpp_2xmsaa pipeline: "
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
  uint format;
  uint flags;
};

constant uint kDumpFormatColorRGBA8 = 0;
constant uint kDumpFormatColorRGB10A2Unorm = 1;
constant uint kDumpFormatColorRGB10A2Float = 2;
constant uint kDumpFormatColorRG16Snorm = 3;
constant uint kDumpFormatColorRG16Float = 4;
constant uint kDumpFormatColorR32Float = 5;
constant uint kDumpFormatColorRGBA16Snorm = 6;
constant uint kDumpFormatColorRGBA16Float = 7;
constant uint kDumpFormatColorRG32Float = 8;
constant uint kDumpFormatDepthD24S8 = 16;
constant uint kDumpFormatDepthD24FS8 = 17;
constant uint kDumpFlagHasStencil = 1;
constant uint kDumpFlagDepthRound = 2;

inline uint XePackUnorm(float value, float scale) {
  return uint(clamp(value, 0.0f, 1.0f) * scale + 0.5f);
}

inline uint XePackSnorm16(float value) {
  float clamped = clamp(value, -1.0f, 1.0f);
  float bias = clamped >= 0.0f ? 0.5f : -0.5f;
  int packed = int(clamped * 32767.0f + bias);
  return uint(packed) & 0xFFFFu;
}

uint XePreClampedFloat32To7e3(float value) {
  uint f32 = as_type<uint>(value);
  uint biased_f32;
  if (f32 < 0x3E800000u) {
    uint f32_exp = f32 >> 23u;
    uint shift = 125u - f32_exp;
    shift = min(shift, 24u);
    uint mantissa = (f32 & 0x7FFFFFu) | 0x800000u;
    biased_f32 = mantissa >> shift;
  } else {
    biased_f32 = f32 + 0xC2000000u;
  }
  uint round_bit = (biased_f32 >> 16u) & 1u;
  uint f10 = biased_f32 + 0x7FFFu + round_bit;
  return (f10 >> 16u) & 0x3FFu;
}

uint XeUnclampedFloat32To7e3(float value) {
  float clamped = min(max(value, 0.0f), 31.875f);
  return XePreClampedFloat32To7e3(clamped);
}

uint XePackColor32bpp(uint format, float4 color) {
  switch (format) {
    case kDumpFormatColorRGBA8: {
      uint r = XePackUnorm(color.r, 255.0f);
      uint g = XePackUnorm(color.g, 255.0f);
      uint b = XePackUnorm(color.b, 255.0f);
      uint a = XePackUnorm(color.a, 255.0f);
      return r | (g << 8u) | (b << 16u) | (a << 24u);
    }
    case kDumpFormatColorRGB10A2Unorm: {
      uint r = XePackUnorm(color.r, 1023.0f);
      uint g = XePackUnorm(color.g, 1023.0f);
      uint b = XePackUnorm(color.b, 1023.0f);
      uint a = XePackUnorm(color.a, 3.0f);
      return r | (g << 10u) | (b << 20u) | (a << 30u);
    }
    case kDumpFormatColorRGB10A2Float: {
      uint r = XeUnclampedFloat32To7e3(color.r);
      uint g = XeUnclampedFloat32To7e3(color.g);
      uint b = XeUnclampedFloat32To7e3(color.b);
      uint a = XePackUnorm(color.a, 3.0f);
      return (r & 0x3FFu) | ((g & 0x3FFu) << 10u) |
             ((b & 0x3FFu) << 20u) | ((a & 0x3u) << 30u);
    }
    case kDumpFormatColorRG16Snorm: {
      uint r = XePackSnorm16(color.r);
      uint g = XePackSnorm16(color.g);
      return r | (g << 16u);
    }
    case kDumpFormatColorRG16Float:
      return as_type<uint>(half2(color.rg));
    case kDumpFormatColorR32Float:
      return as_type<uint>(color.r);
    default: {
      uint r = XePackUnorm(color.r, 255.0f);
      uint g = XePackUnorm(color.g, 255.0f);
      uint b = XePackUnorm(color.b, 255.0f);
      uint a = XePackUnorm(color.a, 255.0f);
      return r | (g << 8u) | (b << 16u) | (a << 24u);
    }
  }
}

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

  uint packed = XePackColor32bpp(constants.format, color);

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
  uint format;
  uint flags;
};

constant uint kDumpFormatColorRGBA8 = 0;
constant uint kDumpFormatColorRGB10A2Unorm = 1;
constant uint kDumpFormatColorRGB10A2Float = 2;
constant uint kDumpFormatColorRG16Snorm = 3;
constant uint kDumpFormatColorRG16Float = 4;
constant uint kDumpFormatColorR32Float = 5;
constant uint kDumpFormatColorRGBA16Snorm = 6;
constant uint kDumpFormatColorRGBA16Float = 7;
constant uint kDumpFormatColorRG32Float = 8;
constant uint kDumpFormatDepthD24S8 = 16;
constant uint kDumpFormatDepthD24FS8 = 17;
constant uint kDumpFlagHasStencil = 1;
constant uint kDumpFlagDepthRound = 2;

inline uint XeRoundToNearestEven(float value) {
  float floor_value = floor(value);
  float frac = value - floor_value;
  uint result = uint(floor_value);
  if (frac > 0.5f || (frac == 0.5f && (result & 1u))) {
    result += 1u;
  }
  return result;
}

uint XeFloat32To20e4(float value, bool round_to_nearest_even) {
  uint f32 = as_type<uint>(value);
  f32 = min((f32 <= 0x7FFFFFFFu) ? f32 : 0u, 0x3FFFFFF8u);
  uint denormalized =
      ((f32 & 0x7FFFFFu) | 0x800000u) >> min(113u - (f32 >> 23u), 24u);
  uint f24 = (f32 < 0x38800000u) ? denormalized : (f32 + 0xC8000000u);
  if (round_to_nearest_even) {
    f24 += 3u + ((f24 >> 3u) & 1u);
  }
  return (f24 >> 3u) & 0xFFFFFFu;
}

kernel void edram_dump_depth_32bpp_4xmsaa(
    texture2d_ms<float, access::read> source [[texture(0)]],
    texture2d_ms<uint, access::read> stencil [[texture(1)]],
    device uint* edram [[buffer(0)]],
    constant EdramDumpConstants& constants [[buffer(1)]],
    uint3 tid [[thread_position_in_grid]]) {
  const uint kEdramTileCount = 2048u;

  uint2 tile_size = uint2(80u * constants.resolution_scale.x,
                          16u * constants.resolution_scale.y);

  uint2 tile_coord = tid.xy / tile_size;
  uint2 sample_in_tile = tid.xy % tile_size;
  uint2 edram_sample_in_tile = sample_in_tile;
  uint tile_width_half = tile_size.x >> 1u;
  edram_sample_in_tile.x =
      (edram_sample_in_tile.x < tile_width_half)
          ? (edram_sample_in_tile.x + tile_width_half)
          : (edram_sample_in_tile.x - tile_width_half);

  uint rect_tile_index = tile_coord.y * constants.dest_pitch_tiles + tile_coord.x;

  uint nonwrapped_tile = constants.dispatch_first_tile + rect_tile_index;
  uint wrapped_tile = nonwrapped_tile & (kEdramTileCount - 1u);

  uint tile_samples = tile_size.x * tile_size.y;
  uint sample_index =
      edram_sample_in_tile.y * tile_size.x + edram_sample_in_tile.x;
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

  uint depth24;
  if (constants.format == kDumpFormatDepthD24FS8) {
    bool round_depth = (constants.flags & kDumpFlagDepthRound) != 0u;
    depth24 = XeFloat32To20e4(depth * 2.0f, round_depth);
  } else {
    float depth_f = clamp(depth, 0.0f, 1.0f) * 16777215.0f;
    depth24 = XeRoundToNearestEven(depth_f);
  }

  uint stencil_value = 0u;
  if ((constants.flags & kDumpFlagHasStencil) != 0u) {
    stencil_value = stencil.read(pixel_coord, sample_id).x & 0xFFu;
  }

  uint packed = (depth24 << 8u) | stencil_value;

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

  // EDRAM dump compute shader for 32-bpp depth, 2x MSAA.
  {
    static const char kEdramDumpDepth32bpp2xMsaaShader[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct EdramDumpConstants {
  uint dispatch_first_tile;
  uint source_base_tiles;
  uint dest_pitch_tiles;
  uint source_pitch_tiles;
  uint2 resolution_scale;
  uint format;
  uint flags;
};

constant uint kDumpFormatColorRGBA8 = 0;
constant uint kDumpFormatColorRGB10A2Unorm = 1;
constant uint kDumpFormatColorRGB10A2Float = 2;
constant uint kDumpFormatColorRG16Snorm = 3;
constant uint kDumpFormatColorRG16Float = 4;
constant uint kDumpFormatColorR32Float = 5;
constant uint kDumpFormatColorRGBA16Snorm = 6;
constant uint kDumpFormatColorRGBA16Float = 7;
constant uint kDumpFormatColorRG32Float = 8;
constant uint kDumpFormatDepthD24S8 = 16;
constant uint kDumpFormatDepthD24FS8 = 17;
constant uint kDumpFlagHasStencil = 1;
constant uint kDumpFlagDepthRound = 2;

inline uint XeRoundToNearestEven(float value) {
  float floor_value = floor(value);
  float frac = value - floor_value;
  uint result = uint(floor_value);
  if (frac > 0.5f || (frac == 0.5f && (result & 1u))) {
    result += 1u;
  }
  return result;
}

uint XeFloat32To20e4(float value, bool round_to_nearest_even) {
  uint f32 = as_type<uint>(value);
  f32 = min((f32 <= 0x7FFFFFFFu) ? f32 : 0u, 0x3FFFFFF8u);
  uint denormalized =
      ((f32 & 0x7FFFFFu) | 0x800000u) >> min(113u - (f32 >> 23u), 24u);
  uint f24 = (f32 < 0x38800000u) ? denormalized : (f32 + 0xC8000000u);
  if (round_to_nearest_even) {
    f24 += 3u + ((f24 >> 3u) & 1u);
  }
  return (f24 >> 3u) & 0xFFFFFFu;
}

kernel void edram_dump_depth_32bpp_2xmsaa(
    texture2d_ms<float, access::read> source [[texture(0)]],
    texture2d_ms<uint, access::read> stencil [[texture(1)]],
    device uint* edram [[buffer(0)]],
    constant EdramDumpConstants& constants [[buffer(1)]],
    uint3 tid [[thread_position_in_grid]]) {
  const uint kEdramTileCount = 2048u;

  uint2 tile_size = uint2(80u * constants.resolution_scale.x,
                          16u * constants.resolution_scale.y);

  uint2 tile_coord = tid.xy / tile_size;
  uint2 sample_in_tile = tid.xy % tile_size;
  uint2 edram_sample_in_tile = sample_in_tile;
  uint tile_width_half = tile_size.x >> 1u;
  edram_sample_in_tile.x =
      (edram_sample_in_tile.x < tile_width_half)
          ? (edram_sample_in_tile.x + tile_width_half)
          : (edram_sample_in_tile.x - tile_width_half);

  uint rect_tile_index = tile_coord.y * constants.dest_pitch_tiles + tile_coord.x;

  uint nonwrapped_tile = constants.dispatch_first_tile + rect_tile_index;
  uint wrapped_tile = nonwrapped_tile & (kEdramTileCount - 1u);

  uint tile_samples = tile_size.x * tile_size.y;
  uint sample_index =
      edram_sample_in_tile.y * tile_size.x + edram_sample_in_tile.x;
  uint edram_index = wrapped_tile * tile_samples + sample_index;

  uint source_linear_tile = nonwrapped_tile - constants.source_base_tiles;
  uint source_tile_y = source_linear_tile / constants.source_pitch_tiles;
  uint source_tile_x = source_linear_tile % constants.source_pitch_tiles;
  uint2 source_sample = uint2(source_tile_x * tile_size.x + sample_in_tile.x,
                              source_tile_y * tile_size.y + sample_in_tile.y);

  uint sample_id = source_sample.y & 1u;
  uint2 pixel_coord = uint2(source_sample.x, source_sample.y >> 1);

  float depth = source.read(pixel_coord, sample_id).r;

  uint depth24;
  if (constants.format == kDumpFormatDepthD24FS8) {
    bool round_depth = (constants.flags & kDumpFlagDepthRound) != 0u;
    depth24 = XeFloat32To20e4(depth * 2.0f, round_depth);
  } else {
    float depth_f = clamp(depth, 0.0f, 1.0f) * 16777215.0f;
    depth24 = XeRoundToNearestEven(depth_f);
  }

  uint stencil_value = 0u;
  if ((constants.flags & kDumpFlagHasStencil) != 0u) {
    stencil_value = stencil.read(pixel_coord, sample_id).x & 0xFFu;
  }

  uint packed = (depth24 << 8u) | stencil_value;

  edram[edram_index] = packed;
}
)METAL";

    NS::String* source = NS::String::string(kEdramDumpDepth32bpp2xMsaaShader,
                                            NS::UTF8StringEncoding);
    MTL::Library* lib = device_->newLibrary(source, nullptr, &error);
    if (!lib) {
      XELOGW(
          "Metal: failed to compile edram_dump_depth_32bpp_2xmsaa shader: {}",
          error ? error->localizedDescription()->utf8String() : "unknown");
    } else {
      NS::String* fn_name = NS::String::string("edram_dump_depth_32bpp_2xmsaa",
                                               NS::UTF8StringEncoding);
      MTL::Function* fn = lib->newFunction(fn_name);
      if (!fn) {
        XELOGW("Metal: edram_dump_depth_32bpp_2xmsaa missing entrypoint");
        lib->release();
      } else {
        edram_dump_depth_32bpp_2xmsaa_pipeline_ =
            device_->newComputePipelineState(fn, &error);
        fn->release();
        lib->release();
        if (!edram_dump_depth_32bpp_2xmsaa_pipeline_) {
          XELOGW(
              "Metal: failed to create edram_dump_depth_32bpp_2xmsaa pipeline: "
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
  uint format;
  uint flags;
};

constant uint kDumpFormatColorRGBA8 = 0;
constant uint kDumpFormatColorRGB10A2Unorm = 1;
constant uint kDumpFormatColorRGB10A2Float = 2;
constant uint kDumpFormatColorRG16Snorm = 3;
constant uint kDumpFormatColorRG16Float = 4;
constant uint kDumpFormatColorR32Float = 5;
constant uint kDumpFormatColorRGBA16Snorm = 6;
constant uint kDumpFormatColorRGBA16Float = 7;
constant uint kDumpFormatColorRG32Float = 8;
constant uint kDumpFormatDepthD24S8 = 16;
constant uint kDumpFormatDepthD24FS8 = 17;
constant uint kDumpFlagHasStencil = 1;
constant uint kDumpFlagDepthRound = 2;

inline uint XeRoundToNearestEven(float value) {
  float floor_value = floor(value);
  float frac = value - floor_value;
  uint result = uint(floor_value);
  if (frac > 0.5f || (frac == 0.5f && (result & 1u))) {
    result += 1u;
  }
  return result;
}

uint XeFloat32To20e4(float value, bool round_to_nearest_even) {
  uint f32 = as_type<uint>(value);
  f32 = min((f32 <= 0x7FFFFFFFu) ? f32 : 0u, 0x3FFFFFF8u);
  uint denormalized =
      ((f32 & 0x7FFFFFu) | 0x800000u) >> min(113u - (f32 >> 23u), 24u);
  uint f24 = (f32 < 0x38800000u) ? denormalized : (f32 + 0xC8000000u);
  if (round_to_nearest_even) {
    f24 += 3u + ((f24 >> 3u) & 1u);
  }
  return (f24 >> 3u) & 0xFFFFFFu;
}

kernel void edram_dump_depth_32bpp_1xmsaa(
    texture2d<float, access::read> source [[texture(0)]],
    texture2d<uint, access::read> stencil [[texture(1)]],
    device uint* edram [[buffer(0)]],
    constant EdramDumpConstants& constants [[buffer(1)]],
    uint3 tid [[thread_position_in_grid]]) {
  const uint kEdramTileCount = 2048u;

  uint2 tile_size = uint2(80u * constants.resolution_scale.x,
                          16u * constants.resolution_scale.y);

  uint2 tile_coord = tid.xy / tile_size;
  uint2 sample_in_tile = tid.xy % tile_size;
  uint2 edram_sample_in_tile = sample_in_tile;
  uint tile_width_half = tile_size.x >> 1u;
  edram_sample_in_tile.x =
      (edram_sample_in_tile.x < tile_width_half)
          ? (edram_sample_in_tile.x + tile_width_half)
          : (edram_sample_in_tile.x - tile_width_half);

  uint rect_tile_index = tile_coord.y * constants.dest_pitch_tiles + tile_coord.x;

  uint nonwrapped_tile = constants.dispatch_first_tile + rect_tile_index;
  uint wrapped_tile = nonwrapped_tile & (kEdramTileCount - 1u);

  uint tile_samples = tile_size.x * tile_size.y;
  uint sample_index =
      edram_sample_in_tile.y * tile_size.x + edram_sample_in_tile.x;
  uint edram_index = wrapped_tile * tile_samples + sample_index;

  uint source_linear_tile = nonwrapped_tile - constants.source_base_tiles;
  uint source_tile_y = source_linear_tile / constants.source_pitch_tiles;
  uint source_tile_x = source_linear_tile % constants.source_pitch_tiles;
  uint2 source_coord = uint2(source_tile_x * tile_size.x + sample_in_tile.x,
                             source_tile_y * tile_size.y + sample_in_tile.y);

  float depth = source.read(source_coord).r;

  uint depth24;
  if (constants.format == kDumpFormatDepthD24FS8) {
    bool round_depth = (constants.flags & kDumpFlagDepthRound) != 0u;
    depth24 = XeFloat32To20e4(depth * 2.0f, round_depth);
  } else {
    float depth_f = clamp(depth, 0.0f, 1.0f) * 16777215.0f;
    depth24 = XeRoundToNearestEven(depth_f);
  }

  uint stencil_value = 0u;
  if ((constants.flags & kDumpFlagHasStencil) != 0u) {
    stencil_value = stencil.read(source_coord).x & 0xFFu;
  }

  uint packed = (depth24 << 8u) | stencil_value;

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
  uint format;
  uint flags;
};

constant uint kDumpFormatColorRGBA8 = 0;
constant uint kDumpFormatColorRGB10A2Unorm = 1;
constant uint kDumpFormatColorRGB10A2Float = 2;
constant uint kDumpFormatColorRG16Snorm = 3;
constant uint kDumpFormatColorRG16Float = 4;
constant uint kDumpFormatColorR32Float = 5;
constant uint kDumpFormatColorRGBA16Snorm = 6;
constant uint kDumpFormatColorRGBA16Float = 7;
constant uint kDumpFormatColorRG32Float = 8;
constant uint kDumpFormatDepthD24S8 = 16;
constant uint kDumpFormatDepthD24FS8 = 17;
constant uint kDumpFlagHasStencil = 1;
constant uint kDumpFlagDepthRound = 2;

inline uint XePackSnorm16(float value) {
  float clamped = clamp(value, -1.0f, 1.0f);
  float bias = clamped >= 0.0f ? 0.5f : -0.5f;
  int packed = int(clamped * 32767.0f + bias);
  return uint(packed) & 0xFFFFu;
}

uint2 XePackColor64bpp(uint format, float4 color) {
  switch (format) {
    case kDumpFormatColorRGBA16Snorm: {
      uint r = XePackSnorm16(color.r);
      uint g = XePackSnorm16(color.g);
      uint b = XePackSnorm16(color.b);
      uint a = XePackSnorm16(color.a);
      uint rg = r | (g << 16u);
      uint ba = b | (a << 16u);
      return uint2(rg, ba);
    }
    case kDumpFormatColorRGBA16Float: {
      uint rg = as_type<uint>(half2(color.rg));
      uint ba = as_type<uint>(half2(color.ba));
      return uint2(rg, ba);
    }
    case kDumpFormatColorRG32Float: {
      uint r = as_type<uint>(color.r);
      uint g = as_type<uint>(color.g);
      return uint2(r, g);
    }
    default: {
      uint rg = as_type<uint>(half2(color.rg));
      uint ba = as_type<uint>(half2(color.ba));
      return uint2(rg, ba);
    }
  }
}

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

  edram[edram_index] = XePackColor64bpp(constants.format, color);
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

  // EDRAM dump compute shader for 64-bpp color, 2x MSAA.
  {
    static const char kEdramDumpColor64bpp2xMsaaShader[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct EdramDumpConstants {
  uint dispatch_first_tile;
  uint source_base_tiles;
  uint dest_pitch_tiles;
  uint source_pitch_tiles;
  uint2 resolution_scale;
  uint format;
  uint flags;
};

constant uint kDumpFormatColorRGBA8 = 0;
constant uint kDumpFormatColorRGB10A2Unorm = 1;
constant uint kDumpFormatColorRGB10A2Float = 2;
constant uint kDumpFormatColorRG16Snorm = 3;
constant uint kDumpFormatColorRG16Float = 4;
constant uint kDumpFormatColorR32Float = 5;
constant uint kDumpFormatColorRGBA16Snorm = 6;
constant uint kDumpFormatColorRGBA16Float = 7;
constant uint kDumpFormatColorRG32Float = 8;
constant uint kDumpFormatDepthD24S8 = 16;
constant uint kDumpFormatDepthD24FS8 = 17;
constant uint kDumpFlagHasStencil = 1;
constant uint kDumpFlagDepthRound = 2;

inline uint XePackSnorm16(float value) {
  float clamped = clamp(value, -1.0f, 1.0f);
  float bias = clamped >= 0.0f ? 0.5f : -0.5f;
  int packed = int(clamped * 32767.0f + bias);
  return uint(packed) & 0xFFFFu;
}

uint2 XePackColor64bpp(uint format, float4 color) {
  switch (format) {
    case kDumpFormatColorRGBA16Snorm: {
      uint r = XePackSnorm16(color.r);
      uint g = XePackSnorm16(color.g);
      uint b = XePackSnorm16(color.b);
      uint a = XePackSnorm16(color.a);
      uint rg = r | (g << 16u);
      uint ba = b | (a << 16u);
      return uint2(rg, ba);
    }
    case kDumpFormatColorRGBA16Float: {
      uint rg = as_type<uint>(half2(color.rg));
      uint ba = as_type<uint>(half2(color.ba));
      return uint2(rg, ba);
    }
    case kDumpFormatColorRG32Float: {
      uint r = as_type<uint>(color.r);
      uint g = as_type<uint>(color.g);
      return uint2(r, g);
    }
    default: {
      uint rg = as_type<uint>(half2(color.rg));
      uint ba = as_type<uint>(half2(color.ba));
      return uint2(rg, ba);
    }
  }
}

kernel void edram_dump_color_64bpp_2xmsaa(
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

  uint sample_id = source_sample.y & 1u;
  uint2 pixel_coord = uint2(source_sample.x, source_sample.y >> 1);

  float4 color = source.read(pixel_coord, sample_id);

  edram[edram_index] = XePackColor64bpp(constants.format, color);
}
)METAL";

    NS::String* source = NS::String::string(kEdramDumpColor64bpp2xMsaaShader,
                                            NS::UTF8StringEncoding);
    MTL::Library* lib = device_->newLibrary(source, nullptr, &error);
    if (!lib) {
      XELOGW(
          "Metal: failed to compile edram_dump_color_64bpp_2xmsaa shader: {}",
          error ? error->localizedDescription()->utf8String() : "unknown");
    } else {
      NS::String* fn_name = NS::String::string("edram_dump_color_64bpp_2xmsaa",
                                               NS::UTF8StringEncoding);
      MTL::Function* fn = lib->newFunction(fn_name);
      if (!fn) {
        XELOGW("Metal: edram_dump_color_64bpp_2xmsaa missing entrypoint");
        lib->release();
      } else {
        edram_dump_color_64bpp_2xmsaa_pipeline_ =
            device_->newComputePipelineState(fn, &error);
        fn->release();
        lib->release();
        if (!edram_dump_color_64bpp_2xmsaa_pipeline_) {
          XELOGW(
              "Metal: failed to create edram_dump_color_64bpp_2xmsaa pipeline: "
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
  uint format;
  uint flags;
};

constant uint kDumpFormatColorRGBA8 = 0;
constant uint kDumpFormatColorRGB10A2Unorm = 1;
constant uint kDumpFormatColorRGB10A2Float = 2;
constant uint kDumpFormatColorRG16Snorm = 3;
constant uint kDumpFormatColorRG16Float = 4;
constant uint kDumpFormatColorR32Float = 5;
constant uint kDumpFormatColorRGBA16Snorm = 6;
constant uint kDumpFormatColorRGBA16Float = 7;
constant uint kDumpFormatColorRG32Float = 8;
constant uint kDumpFormatDepthD24S8 = 16;
constant uint kDumpFormatDepthD24FS8 = 17;
constant uint kDumpFlagHasStencil = 1;
constant uint kDumpFlagDepthRound = 2;

inline uint XePackSnorm16(float value) {
  float clamped = clamp(value, -1.0f, 1.0f);
  float bias = clamped >= 0.0f ? 0.5f : -0.5f;
  int packed = int(clamped * 32767.0f + bias);
  return uint(packed) & 0xFFFFu;
}

uint2 XePackColor64bpp(uint format, float4 color) {
  switch (format) {
    case kDumpFormatColorRGBA16Snorm: {
      uint r = XePackSnorm16(color.r);
      uint g = XePackSnorm16(color.g);
      uint b = XePackSnorm16(color.b);
      uint a = XePackSnorm16(color.a);
      uint rg = r | (g << 16u);
      uint ba = b | (a << 16u);
      return uint2(rg, ba);
    }
    case kDumpFormatColorRGBA16Float: {
      uint rg = as_type<uint>(half2(color.rg));
      uint ba = as_type<uint>(half2(color.ba));
      return uint2(rg, ba);
    }
    case kDumpFormatColorRG32Float: {
      uint r = as_type<uint>(color.r);
      uint g = as_type<uint>(color.g);
      return uint2(r, g);
    }
    default: {
      uint rg = as_type<uint>(half2(color.rg));
      uint ba = as_type<uint>(half2(color.ba));
      return uint2(rg, ba);
    }
  }
}

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

  edram[edram_index] = XePackColor64bpp(constants.format, color);
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
  if (edram_blend_32bpp_1xmsaa_pipeline_) {
    edram_blend_32bpp_1xmsaa_pipeline_->release();
    edram_blend_32bpp_1xmsaa_pipeline_ = nullptr;
  }
  if (edram_blend_32bpp_2xmsaa_pipeline_) {
    edram_blend_32bpp_2xmsaa_pipeline_->release();
    edram_blend_32bpp_2xmsaa_pipeline_ = nullptr;
  }
  if (edram_blend_32bpp_4xmsaa_pipeline_) {
    edram_blend_32bpp_4xmsaa_pipeline_->release();
    edram_blend_32bpp_4xmsaa_pipeline_ = nullptr;
  }
  if (edram_blend_64bpp_1xmsaa_pipeline_) {
    edram_blend_64bpp_1xmsaa_pipeline_->release();
    edram_blend_64bpp_1xmsaa_pipeline_ = nullptr;
  }
  if (edram_blend_64bpp_2xmsaa_pipeline_) {
    edram_blend_64bpp_2xmsaa_pipeline_->release();
    edram_blend_64bpp_2xmsaa_pipeline_ = nullptr;
  }
  if (edram_blend_64bpp_4xmsaa_pipeline_) {
    edram_blend_64bpp_4xmsaa_pipeline_->release();
    edram_blend_64bpp_4xmsaa_pipeline_ = nullptr;
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
  for (size_t i = 0; i < xe::countof(host_depth_store_pipelines_); ++i) {
    if (host_depth_store_pipelines_[i]) {
      host_depth_store_pipelines_[i]->release();
      host_depth_store_pipelines_[i] = nullptr;
    }
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

  if (GetPath() != Path::kHostRenderTargets) {
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      current_color_targets_[i] = nullptr;
    }
    current_depth_target_ = nullptr;
    render_pass_descriptor_dirty_ = true;
    return true;
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
                                     last_update_transfers(), nullptr, nullptr,
                                     nullptr);
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
  if (!key.is_depth) {
    MTL::PixelFormat resource_format =
        GetColorResourcePixelFormat(key.GetColorFormat());
    MTL::PixelFormat draw_format =
        GetColorDrawPixelFormat(key.GetColorFormat());
    MTL::PixelFormat transfer_format =
        GetColorOwnershipTransferPixelFormat(key.GetColorFormat(), nullptr);
    if (draw_format != resource_format) {
      MTL::Texture* draw_view = texture->newTextureView(draw_format);
      render_target->SetDrawTexture(draw_view);
    }
    if (transfer_format != resource_format) {
      MTL::Texture* transfer_view =
          texture->newTextureView(transfer_format);
      render_target->SetTransferTexture(transfer_view);
    }
    if (render_target->msaa_texture()) {
      if (draw_format != render_target->msaa_texture()->pixelFormat()) {
        MTL::Texture* msaa_draw_view =
            render_target->msaa_texture()->newTextureView(draw_format);
        render_target->SetMsaaDrawTexture(msaa_draw_view);
      }
      if (transfer_format != render_target->msaa_texture()->pixelFormat()) {
        MTL::Texture* msaa_transfer_view =
            render_target->msaa_texture()->newTextureView(transfer_format);
        render_target->SetMsaaTransferTexture(msaa_transfer_view);
      }
    }
  }

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
      DumpRenderTargets(0, kPitchTilesAt32bpp, kTileRows, kPitchTilesAt32bpp);
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
  desc->setPixelFormat(GetColorResourcePixelFormat(format));
  desc->setTextureType(samples > 1 ? MTL::TextureType2DMultisample
                                   : MTL::TextureType2D);
  desc->setSampleCount(samples);
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                 MTL::TextureUsageShaderWrite |
                 MTL::TextureUsagePixelFormatView);
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
                 MTL::TextureUsageShaderWrite |
                 MTL::TextureUsagePixelFormatView);
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

MTL::PixelFormat MetalRenderTargetCache::GetColorResourcePixelFormat(
    xenos::ColorRenderTargetFormat format) const {
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return MTL::PixelFormatRGBA8Unorm;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return MTL::PixelFormatRGB10A2Unorm;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      // Match D3D12 behavior: store as RGBA16F and pack to float10 on dump.
      return MTL::PixelFormatRGBA16Float;
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

MTL::PixelFormat MetalRenderTargetCache::GetColorDrawPixelFormat(
    xenos::ColorRenderTargetFormat format) const {
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
      return MTL::PixelFormatRGBA8Unorm;
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return gamma_render_target_as_srgb_ ? MTL::PixelFormatRGBA8Unorm_sRGB
                                          : MTL::PixelFormatRGBA8Unorm;
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
      return GetColorResourcePixelFormat(format);
  }
}

MTL::PixelFormat MetalRenderTargetCache::GetColorOwnershipTransferPixelFormat(
    xenos::ColorRenderTargetFormat format, bool* is_integer_out) const {
  if (is_integer_out) {
    *is_integer_out = true;
  }
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_16_16:
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return MTL::PixelFormatRG16Uint;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return MTL::PixelFormatRGBA16Uint;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return MTL::PixelFormatR32Uint;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return MTL::PixelFormatRG32Uint;
    default:
      if (is_integer_out) {
        *is_integer_out = false;
      }
      // Ownership transfers must use a linear resource view to avoid
      // implicit sRGB conversion for gamma render targets.
      return GetColorResourcePixelFormat(format);
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

void MetalRenderTargetCache::SetOrderedBlendCoverageActive(bool active) {
  if (ordered_blend_coverage_active_ == active) {
    return;
  }
  ordered_blend_coverage_active_ = active;
  render_pass_descriptor_dirty_ = true;
}

bool MetalRenderTargetCache::EnsureOrderedBlendCoverageTexture(
    uint32_t width, uint32_t height, uint32_t sample_count) {
  if (!width || !height) {
    return false;
  }
  if (ordered_blend_coverage_texture_ &&
      ordered_blend_coverage_width_ == width &&
      ordered_blend_coverage_height_ == height &&
      ordered_blend_coverage_samples_ == sample_count) {
    return true;
  }

  if (ordered_blend_coverage_texture_) {
    ordered_blend_coverage_texture_->release();
    ordered_blend_coverage_texture_ = nullptr;
  }

  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setPixelFormat(MTL::PixelFormatR8Unorm);
  desc->setWidth(width);
  desc->setHeight(height);
  if (sample_count > 1) {
    desc->setTextureType(MTL::TextureType2DMultisample);
    desc->setSampleCount(sample_count);
  } else {
    desc->setTextureType(MTL::TextureType2D);
    desc->setSampleCount(1);
  }
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  desc->setStorageMode(MTL::StorageModePrivate);
  ordered_blend_coverage_texture_ = device_->newTexture(desc);
  desc->release();

  if (!ordered_blend_coverage_texture_) {
    ordered_blend_coverage_width_ = 0;
    ordered_blend_coverage_height_ = 0;
    ordered_blend_coverage_samples_ = 0;
    return false;
  }

  ordered_blend_coverage_width_ = width;
  ordered_blend_coverage_height_ = height;
  ordered_blend_coverage_samples_ = sample_count;
  ordered_blend_coverage_texture_->setLabel(NS::String::string(
      "OrderedBlendCoverage", NS::UTF8StringEncoding));
  return true;
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
  uint32_t coverage_width = 0;
  uint32_t coverage_height = 0;
  uint32_t coverage_samples = std::max(1u, expected_sample_count);

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
    depth_attachment->setTexture(current_depth_target_->draw_texture());

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
        current_depth_target_->draw_texture()->pixelFormat();
    if (depth_pixel_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        depth_pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8 ||
        depth_pixel_format == MTL::PixelFormatX32_Stencil8) {
      auto* stencil_attachment =
          cached_render_pass_descriptor_->stencilAttachment();
      stencil_attachment->setTexture(current_depth_target_->draw_texture());
      stencil_attachment->setLoadAction(MTL::LoadActionLoad);
      stencil_attachment->setStoreAction(MTL::StoreActionStore);
    }

    has_any_render_target = true;

    // Track this as a real render target for capture
    last_real_depth_target_ = current_depth_target_;

    XELOGI(
        "MetalRenderTargetCache: Bound depth target to render pass (REAL "
        "target)");
    if (!coverage_width && current_depth_target_->draw_texture()) {
      coverage_width =
          static_cast<uint32_t>(current_depth_target_->draw_texture()->width());
      coverage_height =
          static_cast<uint32_t>(current_depth_target_->draw_texture()->height());
      if (current_depth_target_->draw_texture()->sampleCount() > 0) {
        coverage_samples = std::max<uint32_t>(
            coverage_samples,
            static_cast<uint32_t>(
                current_depth_target_->draw_texture()->sampleCount()));
      }
    }
  }

  // Bind color targets
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_color_targets_[i] && current_color_targets_[i]->texture()) {
      auto* color_attachment =
          cached_render_pass_descriptor_->colorAttachments()->object(i);
      color_attachment->setTexture(current_color_targets_[i]->draw_texture());

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
          i, current_color_targets_[i]->draw_texture()->width(),
          current_color_targets_[i]->draw_texture()->height(),
          current_color_targets_[i]->key().key);
      if (!coverage_width) {
        coverage_width = static_cast<uint32_t>(
            current_color_targets_[i]->draw_texture()->width());
        coverage_height = static_cast<uint32_t>(
            current_color_targets_[i]->draw_texture()->height());
        if (current_color_targets_[i]->draw_texture()->sampleCount() > 0) {
          coverage_samples = std::max<uint32_t>(
              coverage_samples,
              static_cast<uint32_t>(
                  current_color_targets_[i]->draw_texture()->sampleCount()));
        }
      }
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
      if (tex) {
        MTL::PixelFormat resource_format = GetColorResourcePixelFormat(fmt);
        MTL::PixelFormat draw_format = GetColorDrawPixelFormat(fmt);
        MTL::PixelFormat transfer_format =
            GetColorOwnershipTransferPixelFormat(fmt, nullptr);
        if (draw_format != resource_format) {
          dummy_color_target_->SetDrawTexture(
              tex->newTextureView(draw_format));
        }
        if (transfer_format != resource_format) {
          dummy_color_target_->SetTransferTexture(
              tex->newTextureView(transfer_format));
        }
      }
      dummy_color_target_needs_clear_ = true;
    }

    auto* color_attachment =
        cached_render_pass_descriptor_->colorAttachments()->object(0);
    color_attachment->setTexture(dummy_color_target_->draw_texture());
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
    if (!coverage_width && dummy_color_target_->draw_texture()) {
      coverage_width =
          static_cast<uint32_t>(dummy_color_target_->draw_texture()->width());
      coverage_height =
          static_cast<uint32_t>(dummy_color_target_->draw_texture()->height());
      if (dummy_color_target_->draw_texture()->sampleCount() > 0) {
        coverage_samples = std::max<uint32_t>(
            coverage_samples,
            static_cast<uint32_t>(
                dummy_color_target_->draw_texture()->sampleCount()));
      }
    }
  }

  if (ordered_blend_coverage_active_) {
    if (EnsureOrderedBlendCoverageTexture(coverage_width, coverage_height,
                                          coverage_samples)) {
      auto* coverage_attachment =
          cached_render_pass_descriptor_->colorAttachments()->object(
              kOrderedBlendCoverageAttachmentIndex);
      coverage_attachment->setTexture(ordered_blend_coverage_texture_);
      coverage_attachment->setLoadAction(MTL::LoadActionClear);
      coverage_attachment->setClearColor(
          MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
      coverage_attachment->setStoreAction(MTL::StoreActionStore);
    } else {
      XELOGW(
          "MetalRenderTargetCache::GetRenderPassDescriptor: failed to create "
          "ordered-blend coverage texture");
    }
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

MetalRenderTargetCache::MetalRenderTarget*
MetalRenderTargetCache::GetColorRenderTarget(uint32_t index) const {
  if (index >= 4) {
    return nullptr;
  }
  return current_color_targets_[index];
}

MTL::Texture* MetalRenderTargetCache::GetColorTargetForDraw(
    uint32_t index) const {
  if (index >= 4 || !current_color_targets_[index]) {
    return nullptr;
  }
  return current_color_targets_[index]->draw_texture();
}

MTL::Texture* MetalRenderTargetCache::GetDepthTargetForDraw() const {
  if (!current_depth_target_) {
    return nullptr;
  }
  return current_depth_target_->draw_texture();
}

MTL::Texture* MetalRenderTargetCache::GetDummyColorTargetForDraw() const {
  if (dummy_color_target_ && dummy_color_target_->draw_texture()) {
    return dummy_color_target_->draw_texture();
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

MTL::Texture* MetalRenderTargetCache::GetRenderTargetTexture(
    RenderTargetKey key) const {
  auto it = render_target_map_.find(key.key);
  if (it == render_target_map_.end()) {
    return nullptr;
  }
  MetalRenderTarget* target = it->second;
  return target ? target->texture() : nullptr;
}

MTL::Texture* MetalRenderTargetCache::GetColorRenderTargetTexture(
    uint32_t pitch, xenos::MsaaSamples samples, uint32_t base,
    xenos::ColorRenderTargetFormat format) const {
  if (!pitch) {
    return nullptr;
  }
  RenderTargetKey key;
  key.base_tiles = base;
  uint32_t msaa_samples_x_log2 =
      uint32_t(samples >= xenos::MsaaSamples::k4X);
  key.pitch_tiles_at_32bpp =
      ((pitch << msaa_samples_x_log2) +
       (xenos::kEdramTileWidthSamples - 1)) /
      xenos::kEdramTileWidthSamples;
  key.msaa_samples = samples;
  key.is_depth = 0;
  xenos::ColorRenderTargetFormat resource_format =
      format == xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA
          ? xenos::ColorRenderTargetFormat::k_8_8_8_8
          : xenos::GetStorageColorFormat(format);
  key.resource_format = uint32_t(resource_format);
  return GetRenderTargetTexture(key);
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

void MetalRenderTargetCache::DumpRenderTargets(
    uint32_t dump_base, uint32_t dump_row_length_used, uint32_t dump_rows,
    uint32_t dump_pitch, MTL::CommandBuffer* command_buffer) {
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
    XELOGW(
        "MetalRenderTargetCache::DumpRenderTargets: no rectangles for base={} "
        "row_length_used={} rows={} pitch={}",
        dump_base, dump_row_length_used, dump_rows, dump_pitch);
    LogOwnershipRangesAround(
        dump_base, "MetalRenderTargetCache::DumpRenderTargets");
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
    uint32_t format;
    uint32_t flags;
  };

  MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
  if (!queue) {
    XELOGE("MetalRenderTargetCache::DumpRenderTargets: no command queue");
    return;
  }

  bool owns_command_buffer = false;
  MTL::CommandBuffer* cmd = command_buffer;
  if (!cmd) {
    cmd = queue->commandBuffer();
    if (!cmd) {
      XELOGE("MetalRenderTargetCache::DumpRenderTargets: no command buffer");
      return;
    }
    owns_command_buffer = true;
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

    uint32_t dump_format = GetMetalEdramDumpFormat(key);
    uint32_t dump_flags = 0;
    MTL::Texture* stencil_tex = nullptr;
    if (key.is_depth) {
      if (!cvars::depth_float24_convert_in_pixel_shader &&
          cvars::depth_float24_round) {
        dump_flags |= kMetalEdramDumpFlagDepthRound;
      }
      stencil_tex = tex->newTextureView(MTL::PixelFormatX32_Stencil8);
      if (stencil_tex) {
        dump_flags |= kMetalEdramDumpFlagHasStencil;
      }
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
      if (stencil_tex) {
        stencil_tex->release();
      }
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
      if (stencil_tex) {
        stencil_tex->release();
      }
      continue;
    }

    if (!log_texture && ::cvars::metal_log_edram_dump_color_samples &&
        !key.is_depth && !is_64bpp &&
        key.msaa_samples == xenos::MsaaSamples::k1X) {
      log_texture = tex;
      log_key_value = key.key;
      log_dump_format = dump_format;
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
      constants.format = dump_format;
      constants.flags = dump_flags;

      encoder->setComputePipelineState(dump_pipeline);
      encoder->setTexture(tex, 0);
      if (stencil_tex) {
        encoder->setTexture(stencil_tex, 1);
      }
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

    if (stencil_tex) {
      stencil_tex->release();
    }
  }

  encoder->endEncoding();
  if (log_texture) {
    ScheduleEdramDumpColorSamples(cmd, log_texture, log_key_value,
                                  log_dump_format);
  }
  if (owns_command_buffer) {
    cmd->commit();
    cmd->waitUntilCompleted();
  }
  // cmd is autoreleased from commandBuffer() - do not release
}

void MetalRenderTargetCache::BlendRenderTargetsToEdram(
    uint32_t dump_base, uint32_t dump_row_length_used, uint32_t dump_rows,
    uint32_t dump_pitch, MTL::CommandBuffer* command_buffer) {
  XELOGGPU(
      "MetalRenderTargetCache::BlendRenderTargetsToEdram: base={} "
      "row_length_used={} rows={} pitch={}",
      dump_base, dump_row_length_used, dump_rows, dump_pitch);

  if (GetPath() != Path::kHostRenderTargets) {
    XELOGGPU(
        "MetalRenderTargetCache::BlendRenderTargetsToEdram: not host RT path");
    return;
  }

  if (!edram_blend_32bpp_1xmsaa_pipeline_) {
    XELOGW(
        "MetalRenderTargetCache::BlendRenderTargetsToEdram: blend pipeline "
        "missing");
    return;
  }

  std::vector<ResolveCopyDumpRectangle> rectangles;
  GetResolveCopyRectanglesToDump(dump_base, dump_row_length_used, dump_rows,
                                 dump_pitch, rectangles);

  if (rectangles.empty()) {
    XELOGGPU(
        "MetalRenderTargetCache::BlendRenderTargetsToEdram: no rectangles "
        "for base={} row_length_used={} rows={} pitch={}",
        dump_base, dump_row_length_used, dump_rows, dump_pitch);
    return;
  }

  if (!edram_buffer_) {
    XELOGW(
        "MetalRenderTargetCache::BlendRenderTargetsToEdram: EDRAM buffer not "
        "initialized");
    return;
  }

  const RegisterFile& regs = register_file();
  uint32_t normalized_color_mask = draw_util::GetNormalizedColorMask(regs, 1);
  uint32_t rt_write_mask = normalized_color_mask & 0xF;

  float clamp_rgb_low = 0.0f;
  float clamp_alpha_low = 0.0f;
  float clamp_rgb_high = 0.0f;
  float clamp_alpha_high = 0.0f;
  uint32_t keep_mask_low = 0;
  uint32_t keep_mask_high = 0;

  MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
  if (!queue) {
    XELOGE(
        "MetalRenderTargetCache::BlendRenderTargetsToEdram: no command queue");
    return;
  }

  bool owns_command_buffer = false;
  MTL::CommandBuffer* cmd = command_buffer;
  if (!cmd) {
    cmd = queue->commandBuffer();
    if (!cmd) {
      XELOGE(
          "MetalRenderTargetCache::BlendRenderTargetsToEdram: no command buffer");
      return;
    }
    owns_command_buffer = true;
  }

  MTL::ComputeCommandEncoder* encoder = cmd->computeCommandEncoder();
  if (!encoder) {
    XELOGE(
        "MetalRenderTargetCache::BlendRenderTargetsToEdram: no compute encoder");
    // cmd is autoreleased from commandBuffer() - do not release
    return;
  }

  uint32_t scale_x = draw_resolution_scale_x();
  uint32_t scale_y = draw_resolution_scale_y();

  struct EdramDumpConstants {
    uint32_t dispatch_first_tile;
    uint32_t source_base_tiles;
    uint32_t dest_pitch_tiles;
    uint32_t source_pitch_tiles;
    uint32_t resolution_scale_x;
    uint32_t resolution_scale_y;
    uint32_t format;
    uint32_t flags;
  };

  struct EdramBlendConstants {
    uint32_t tile_info[4];
    uint32_t misc_info[4];
    uint32_t mask_info[4];
    float clamp[4];
    float blend_constant[4];
  };

  auto blend_control =
      regs.Get<reg::RB_BLENDCONTROL>(reg::RB_BLENDCONTROL::rt_register_indices[0])
          .value;
  uint32_t blend_factors_ops = blend_control & 0x1FFF1FFF;
  float blend_constant[4] = {
      regs.Get<float>(XE_GPU_REG_RB_BLEND_RED),
      regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN),
      regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE),
      regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA),
  };

  for (const ResolveCopyDumpRectangle& rect : rectangles) {
    auto* rt = static_cast<MetalRenderTarget*>(rect.render_target);
    if (!rt) {
      continue;
    }

    RenderTargetKey key = rt->key();
    bool is_64bpp = key.Is64bpp();

    if (!key.is_depth) {
      xenos::ColorRenderTargetFormat format = key.GetColorFormat();
      if (!is_64bpp) {
        switch (format) {
          case xenos::ColorRenderTargetFormat::k_8_8_8_8:
          case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
          case xenos::ColorRenderTargetFormat::k_2_10_10_10:
          case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
          case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
          case xenos::ColorRenderTargetFormat::
              k_2_10_10_10_FLOAT_AS_16_16_16_16:
          case xenos::ColorRenderTargetFormat::k_16_16:
          case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
          case xenos::ColorRenderTargetFormat::k_32_FLOAT:
            break;
          default: {
            static std::unordered_set<uint32_t> logged_formats;
            uint32_t format_key = (key.key & 0xFFFF0000u) |
                                  uint32_t(format);
            if (logged_formats.insert(format_key).second) {
              XELOGW(
                  "MetalRenderTargetCache::BlendRenderTargetsToEdram: "
                  "unsupported format {} for RT key=0x{:08X}",
                  key.GetFormatName(), key.key);
            }
            continue;
          }
        }
      } else {
        switch (format) {
          case xenos::ColorRenderTargetFormat::k_16_16_16_16:
          case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
          case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
            break;
          default: {
            static std::unordered_set<uint32_t> logged_formats;
            uint32_t format_key = (key.key & 0xFFFF0000u) |
                                  uint32_t(format);
            if (logged_formats.insert(format_key).second) {
              XELOGW(
                  "MetalRenderTargetCache::BlendRenderTargetsToEdram: "
                  "unsupported 64bpp format {} for RT key=0x{:08X}",
                  key.GetFormatName(), key.key);
            }
            continue;
          }
        }
      }
    }

    MTL::Texture* tex = rt->texture();
    if (!tex) {
      continue;
    }

    uint32_t dump_format = GetMetalEdramDumpFormat(key);
    uint32_t dump_flags = 0;
    MTL::Texture* stencil_tex = nullptr;
    if (key.is_depth) {
      if (!cvars::depth_float24_convert_in_pixel_shader &&
          cvars::depth_float24_round) {
        dump_flags |= kMetalEdramDumpFlagDepthRound;
      }
      stencil_tex = tex->newTextureView(MTL::PixelFormatX32_Stencil8);
      if (stencil_tex) {
        dump_flags |= kMetalEdramDumpFlagHasStencil;
      }
    }

    MTL::ComputePipelineState* blend_pipeline = nullptr;
    if (!key.is_depth) {
      if (!is_64bpp) {
        switch (key.msaa_samples) {
          case xenos::MsaaSamples::k1X:
            blend_pipeline = edram_blend_32bpp_1xmsaa_pipeline_;
            break;
          case xenos::MsaaSamples::k2X:
            blend_pipeline = edram_blend_32bpp_2xmsaa_pipeline_;
            break;
          case xenos::MsaaSamples::k4X:
            blend_pipeline = edram_blend_32bpp_4xmsaa_pipeline_;
            break;
          default:
            break;
        }
      } else {
        switch (key.msaa_samples) {
          case xenos::MsaaSamples::k1X:
            blend_pipeline = edram_blend_64bpp_1xmsaa_pipeline_;
            break;
          case xenos::MsaaSamples::k2X:
            blend_pipeline = edram_blend_64bpp_2xmsaa_pipeline_;
            break;
          case xenos::MsaaSamples::k4X:
            blend_pipeline = edram_blend_64bpp_4xmsaa_pipeline_;
            break;
          default:
            break;
        }
      }
      if (!blend_pipeline) {
        static std::unordered_set<uint32_t> logged_pipelines;
        if (logged_pipelines.insert(key.key).second) {
          XELOGW(
              "MetalRenderTargetCache::BlendRenderTargetsToEdram: missing "
              "blend pipeline for RT key=0x{:08X} (msaa={}, 64bpp={})",
              key.key, uint32_t(key.msaa_samples), is_64bpp ? 1 : 0);
        }
        if (stencil_tex) {
          stencil_tex->release();
        }
        continue;
      }
    } else {
      switch (key.msaa_samples) {
        case xenos::MsaaSamples::k1X:
          blend_pipeline = edram_dump_depth_32bpp_1xmsaa_pipeline_;
          break;
        case xenos::MsaaSamples::k2X:
          blend_pipeline = edram_dump_depth_32bpp_2xmsaa_pipeline_;
          break;
        case xenos::MsaaSamples::k4X:
          blend_pipeline = edram_dump_depth_32bpp_4xmsaa_pipeline_;
          break;
        default:
          break;
      }
      if (!blend_pipeline) {
        static std::unordered_set<uint32_t> logged_pipelines;
        if (logged_pipelines.insert(key.key).second) {
          XELOGW(
              "MetalRenderTargetCache::BlendRenderTargetsToEdram: missing "
              "depth dump pipeline for RT key=0x{:08X} (msaa={})",
              key.key, uint32_t(key.msaa_samples));
        }
        if (stencil_tex) {
          stencil_tex->release();
        }
        continue;
      }
    }

    if (!key.is_depth) {
      xenos::ColorRenderTargetFormat format = key.GetColorFormat();
      RenderTargetCache::GetPSIColorFormatInfo(
          format, rt_write_mask, clamp_rgb_low, clamp_alpha_low, clamp_rgb_high,
          clamp_alpha_high, keep_mask_low, keep_mask_high);
    }

    uint32_t format_flags = 0;
    if (!key.is_depth) {
      xenos::ColorRenderTargetFormat format = key.GetColorFormat();
      format_flags = RenderTargetCache::AddPSIColorFormatFlags(format);
    }

    ResolveCopyDumpRectangle::Dispatch
        dispatches[ResolveCopyDumpRectangle::kMaxDispatches];
    uint32_t dispatch_count =
        rect.GetDispatches(dump_pitch, dump_row_length_used, dispatches);
    if (!dispatch_count) {
      if (stencil_tex) {
        stencil_tex->release();
      }
      continue;
    }

    for (uint32_t i = 0; i < dispatch_count; ++i) {
      const ResolveCopyDumpRectangle::Dispatch& dispatch = dispatches[i];

      if (key.is_depth) {
        EdramDumpConstants constants = {};
        constants.dispatch_first_tile = dump_base + dispatch.offset;
        constants.source_base_tiles = key.base_tiles;
        constants.dest_pitch_tiles = dump_pitch;
        constants.source_pitch_tiles = key.GetPitchTiles();
        constants.resolution_scale_x = scale_x;
        constants.resolution_scale_y = scale_y;
        constants.format = dump_format;
        constants.flags = dump_flags;

        encoder->setComputePipelineState(blend_pipeline);
        encoder->setTexture(tex, 0);
        if (stencil_tex) {
          encoder->setTexture(stencil_tex, 1);
        }
        encoder->setBuffer(edram_buffer_, 0, 0);
        encoder->setBytes(&constants, sizeof(constants), 1);

        uint32_t groups_x = dispatch.width_tiles * scale_x * 2;
        uint32_t groups_y = dispatch.height_tiles * scale_y;

        MTL::Size threads_per_group = MTL::Size::Make(40, 16, 1);
        MTL::Size threadgroups = MTL::Size::Make(groups_x, groups_y, 1);
        encoder->dispatchThreadgroups(threadgroups, threads_per_group);
      } else {
        EdramBlendConstants constants = {};
        constants.tile_info[0] = dump_base + dispatch.offset;
        constants.tile_info[1] = key.base_tiles;
        constants.tile_info[2] = dump_pitch;
        constants.tile_info[3] = key.GetPitchTiles();
        constants.misc_info[0] = scale_x;
        constants.misc_info[1] = scale_y;
        constants.misc_info[2] = format_flags;
        constants.misc_info[3] = keep_mask_low;
        constants.mask_info[0] = keep_mask_high;
        constants.mask_info[1] = blend_factors_ops;
        if (cvars::metal_edram_blend_bounds_check) {
          constants.mask_info[2] = dispatch.width_tiles;
          constants.mask_info[3] = dispatch.height_tiles;
        } else {
          constants.mask_info[2] = 0;
          constants.mask_info[3] = 0;
        }
        constants.clamp[0] = clamp_rgb_low;
        constants.clamp[1] = clamp_alpha_low;
        constants.clamp[2] = clamp_rgb_high;
        constants.clamp[3] = clamp_alpha_high;
        constants.blend_constant[0] = blend_constant[0];
        constants.blend_constant[1] = blend_constant[1];
        constants.blend_constant[2] = blend_constant[2];
        constants.blend_constant[3] = blend_constant[3];

        if (!ordered_blend_coverage_texture_) {
          XELOGW(
              "MetalRenderTargetCache::BlendRenderTargetsToEdram: missing "
              "coverage texture");
          continue;
        }

        encoder->setComputePipelineState(blend_pipeline);
        encoder->setTexture(tex, 0);
        encoder->setTexture(ordered_blend_coverage_texture_, 1);
        encoder->setBuffer(edram_buffer_, 0, 1);
        encoder->setBuffer(edram_buffer_, 0, 2);
        encoder->setBytes(&constants, sizeof(constants), 0);

        uint32_t groups_x = dispatch.width_tiles * scale_x;
        if (!is_64bpp) {
          groups_x <<= 1;
        }
        uint32_t groups_y = dispatch.height_tiles * scale_y;

        static int blend_log_count = 0;
        if (blend_log_count < 12) {
          ++blend_log_count;
          XELOGI(
              "MetalEdramBlend: base={} offset={} src_base={} dest_pitch={} "
              "src_pitch={} rect={}x{} scale={}x{} groups={}x{}",
              dump_base, dispatch.offset, key.base_tiles, dump_pitch,
              key.GetPitchTiles(), dispatch.width_tiles, dispatch.height_tiles,
              scale_x, scale_y, groups_x, groups_y);
        }

        MTL::Size threads_per_group = MTL::Size::Make(40, 16, 1);
        MTL::Size threadgroups = MTL::Size::Make(groups_x, groups_y, 1);
        encoder->dispatchThreadgroups(threadgroups, threads_per_group);
      }
    }

    if (stencil_tex) {
      stencil_tex->release();
    }
  }

  encoder->endEncoding();
  if (owns_command_buffer) {
    cmd->commit();
    cmd->waitUntilCompleted();
  }
  // cmd is autoreleased from commandBuffer() - do not release
}

void MetalRenderTargetCache::BlendRenderTargetToEdramRect(
    MetalRenderTarget* render_target, uint32_t rt_index,
    uint32_t rt_write_mask, uint32_t scissor_x, uint32_t scissor_y,
    uint32_t scissor_width, uint32_t scissor_height,
    MTL::CommandBuffer* command_buffer) {
  if (!render_target || !rt_write_mask) {
    return;
  }
  if (GetPath() != Path::kHostRenderTargets) {
    return;
  }
  if (!edram_buffer_ || !edram_blend_32bpp_1xmsaa_pipeline_) {
    return;
  }

  RenderTargetKey key = render_target->key();
  if (key.is_depth) {
    return;
  }
  xenos::ColorRenderTargetFormat format = key.GetColorFormat();
  bool is_64bpp = key.Is64bpp();
  if (!is_64bpp) {
    switch (format) {
      case xenos::ColorRenderTargetFormat::k_8_8_8_8:
      case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      case xenos::ColorRenderTargetFormat::k_2_10_10_10:
      case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
      case xenos::ColorRenderTargetFormat::
          k_2_10_10_10_FLOAT_AS_16_16_16_16:
      case xenos::ColorRenderTargetFormat::k_16_16:
      case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      case xenos::ColorRenderTargetFormat::k_32_FLOAT:
        break;
      default: {
        static std::unordered_set<uint32_t> logged_formats;
        uint32_t format_key = (key.key & 0xFFFF0000u) | uint32_t(format);
        if (logged_formats.insert(format_key).second) {
          XELOGW(
              "MetalRenderTargetCache::BlendRenderTargetToEdramRect: "
              "unsupported format {} for RT key=0x{:08X}",
              key.GetFormatName(), key.key);
        }
        return;
      }
    }
  } else {
    switch (format) {
      case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
        break;
      default: {
        static std::unordered_set<uint32_t> logged_formats;
        uint32_t format_key = (key.key & 0xFFFF0000u) | uint32_t(format);
        if (logged_formats.insert(format_key).second) {
          XELOGW(
              "MetalRenderTargetCache::BlendRenderTargetToEdramRect: "
              "unsupported 64bpp format {} for RT key=0x{:08X}",
              key.GetFormatName(), key.key);
        }
        return;
      }
    }
  }

  uint32_t rt_width_pixels = key.GetWidth();
  uint32_t rt_height_pixels =
      GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples);
  if (!rt_width_pixels || !rt_height_pixels) {
    return;
  }
  if (scissor_x >= rt_width_pixels || scissor_y >= rt_height_pixels) {
    return;
  }
  scissor_width = std::min(scissor_width, rt_width_pixels - scissor_x);
  scissor_height = std::min(scissor_height, rt_height_pixels - scissor_y);
  if (!scissor_width || !scissor_height) {
    return;
  }

  uint32_t msaa_samples_x_log2 =
      uint32_t(key.msaa_samples >= xenos::MsaaSamples::k4X);
  uint32_t msaa_samples_y_log2 =
      uint32_t(key.msaa_samples >= xenos::MsaaSamples::k2X);
  uint32_t tile_width_samples = xenos::kEdramTileWidthSamples;
  if (is_64bpp) {
    tile_width_samples >>= 1;
  }
  uint32_t tile_width_pixels = tile_width_samples >> msaa_samples_x_log2;
  uint32_t tile_height_pixels =
      xenos::kEdramTileHeightSamples >> msaa_samples_y_log2;

  uint32_t tile_x0 = scissor_x / tile_width_pixels;
  uint32_t tile_y0 = scissor_y / tile_height_pixels;
  uint32_t tile_x1 =
      (scissor_x + scissor_width + tile_width_pixels - 1) /
      tile_width_pixels;
  uint32_t tile_y1 =
      (scissor_y + scissor_height + tile_height_pixels - 1) /
      tile_height_pixels;

  uint32_t pitch_tiles = key.GetPitchTiles();
  if (!pitch_tiles) {
    return;
  }
  tile_x1 = std::min(tile_x1, pitch_tiles);
  uint32_t tile_rows =
      (rt_height_pixels + tile_height_pixels - 1) / tile_height_pixels;
  tile_y1 = std::min(tile_y1, tile_rows);
  if (tile_x0 >= tile_x1 || tile_y0 >= tile_y1) {
    return;
  }

  static uint32_t blend_rect_log_count = 0;
  if (blend_rect_log_count < 12) {
    ++blend_rect_log_count;
    XELOGI(
        "MetalEdramBlendRect: rt={} key=0x{:08X} scissor=({},{} {}x{}) "
        "tiles=({},{} {}x{}) pitch_tiles={} msaa={}",
        rt_index, key.key, scissor_x, scissor_y, scissor_width,
        scissor_height, tile_x0, tile_y0, (tile_x1 - tile_x0),
        (tile_y1 - tile_y0), pitch_tiles,
        static_cast<uint32_t>(key.msaa_samples));
  }

  uint32_t dump_row_length_used = tile_x1 - tile_x0;
  uint32_t dump_rows = tile_y1 - tile_y0;
  uint32_t dump_pitch = pitch_tiles;
  uint32_t dump_base =
      key.base_tiles + tile_y0 * pitch_tiles + tile_x0;

  ResolveCopyDumpRectangle rect(render_target, 0, dump_rows, 0,
                                dump_row_length_used);
  ResolveCopyDumpRectangle::Dispatch
      dispatches[ResolveCopyDumpRectangle::kMaxDispatches];
  uint32_t dispatch_count =
      rect.GetDispatches(dump_pitch, dump_row_length_used, dispatches);
  if (!dispatch_count) {
    return;
  }

  const RegisterFile& regs = register_file();
  float clamp_rgb_low = 0.0f;
  float clamp_alpha_low = 0.0f;
  float clamp_rgb_high = 0.0f;
  float clamp_alpha_high = 0.0f;
  uint32_t keep_mask_low = 0;
  uint32_t keep_mask_high = 0;

  RenderTargetCache::GetPSIColorFormatInfo(
      format, rt_write_mask, clamp_rgb_low, clamp_alpha_low, clamp_rgb_high,
      clamp_alpha_high, keep_mask_low, keep_mask_high);

  uint32_t format_flags = RenderTargetCache::AddPSIColorFormatFlags(format);

  auto blend_control =
      regs.Get<reg::RB_BLENDCONTROL>(
              reg::RB_BLENDCONTROL::rt_register_indices[rt_index])
          .value;
  uint32_t blend_factors_ops = blend_control & 0x1FFF1FFF;
  float blend_constant[4] = {
      regs.Get<float>(XE_GPU_REG_RB_BLEND_RED),
      regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN),
      regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE),
      regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA),
  };

  MTL::Texture* tex = render_target->texture();
  if (!tex) {
    return;
  }

  MTL::ComputePipelineState* blend_pipeline = nullptr;
  if (!is_64bpp) {
    switch (key.msaa_samples) {
      case xenos::MsaaSamples::k1X:
        blend_pipeline = edram_blend_32bpp_1xmsaa_pipeline_;
        break;
      case xenos::MsaaSamples::k2X:
        blend_pipeline = edram_blend_32bpp_2xmsaa_pipeline_;
        break;
      case xenos::MsaaSamples::k4X:
        blend_pipeline = edram_blend_32bpp_4xmsaa_pipeline_;
        break;
      default:
        break;
    }
  } else {
    switch (key.msaa_samples) {
      case xenos::MsaaSamples::k1X:
        blend_pipeline = edram_blend_64bpp_1xmsaa_pipeline_;
        break;
      case xenos::MsaaSamples::k2X:
        blend_pipeline = edram_blend_64bpp_2xmsaa_pipeline_;
        break;
      case xenos::MsaaSamples::k4X:
        blend_pipeline = edram_blend_64bpp_4xmsaa_pipeline_;
        break;
      default:
        break;
    }
  }
  if (!blend_pipeline) {
    return;
  }

  MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
  if (!queue) {
    return;
  }

  bool owns_command_buffer = false;
  MTL::CommandBuffer* cmd = command_buffer;
  if (!cmd) {
    cmd = queue->commandBuffer();
    if (!cmd) {
      return;
    }
    owns_command_buffer = true;
  }

  MTL::ComputeCommandEncoder* encoder = cmd->computeCommandEncoder();
  if (!encoder) {
    return;
  }

  uint32_t scale_x = draw_resolution_scale_x();
  uint32_t scale_y = draw_resolution_scale_y();

  struct EdramBlendConstants {
    uint32_t tile_info[4];
    uint32_t misc_info[4];
    uint32_t mask_info[4];
    float clamp[4];
    float blend_constant[4];
  };

  for (uint32_t i = 0; i < dispatch_count; ++i) {
    const ResolveCopyDumpRectangle::Dispatch& dispatch = dispatches[i];

    EdramBlendConstants constants = {};
    constants.tile_info[0] = dump_base + dispatch.offset;
    constants.tile_info[1] = key.base_tiles;
    constants.tile_info[2] = dump_pitch;
    constants.tile_info[3] = key.GetPitchTiles();
    constants.misc_info[0] = scale_x;
    constants.misc_info[1] = scale_y;
    constants.misc_info[2] = format_flags;
    constants.misc_info[3] = keep_mask_low;
    constants.mask_info[0] = keep_mask_high;
    constants.mask_info[1] = blend_factors_ops;
    if (cvars::metal_edram_blend_bounds_check) {
      constants.mask_info[2] = dispatch.width_tiles;
      constants.mask_info[3] = dispatch.height_tiles;
    } else {
      constants.mask_info[2] = 0;
      constants.mask_info[3] = 0;
    }
    constants.clamp[0] = clamp_rgb_low;
    constants.clamp[1] = clamp_alpha_low;
    constants.clamp[2] = clamp_rgb_high;
    constants.clamp[3] = clamp_alpha_high;
    constants.blend_constant[0] = blend_constant[0];
    constants.blend_constant[1] = blend_constant[1];
    constants.blend_constant[2] = blend_constant[2];
    constants.blend_constant[3] = blend_constant[3];

    if (!ordered_blend_coverage_texture_) {
      XELOGW(
          "MetalRenderTargetCache::BlendRenderTargetToEdramRect: missing "
          "coverage texture");
      return;
    }

    encoder->setComputePipelineState(blend_pipeline);
    encoder->setTexture(tex, 0);
    encoder->setTexture(ordered_blend_coverage_texture_, 1);
    encoder->setBuffer(edram_buffer_, 0, 1);
    encoder->setBuffer(edram_buffer_, 0, 2);
    encoder->setBytes(&constants, sizeof(constants), 0);

    uint32_t groups_x = dispatch.width_tiles * scale_x;
    if (!is_64bpp) {
      groups_x <<= 1;
    }
    uint32_t groups_y = dispatch.height_tiles * scale_y;

    MTL::Size threads_per_group = MTL::Size::Make(40, 16, 1);
    MTL::Size threadgroups = MTL::Size::Make(groups_x, groups_y, 1);
    encoder->dispatchThreadgroups(threadgroups, threads_per_group);
  }

  encoder->endEncoding();
  ReloadRenderTargetFromEdramTiles(render_target, tile_x0, tile_y0, tile_x1,
                                   tile_y1, cmd);
  if (owns_command_buffer) {
    cmd->commit();
    cmd->waitUntilCompleted();
  }
}

MTL::Library* MetalRenderTargetCache::GetOrCreateEdramLoadLibrary(bool msaa) {
  MTL::Library*& library =
      msaa ? edram_load_library_msaa_ : edram_load_library_;
  if (library) {
    return library;
  }

  static const char kEdramLoadShaderSource[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct EdramLoadConstants {
  uint base_tiles;
  uint pitch_tiles;
  uint format;
  uint format_is_64bpp;
  uint msaa_samples;
  uint sample_id;
  uint resolution_scale_x;
  uint resolution_scale_y;
};

struct VSOut {
  float4 position [[position]];
};

vertex VSOut edram_load_vs(uint vid [[vertex_id]]) {
  float2 pt = float2((vid << 1) & 2, vid & 2);
  VSOut out;
  out.position = float4(pt * 2.0f - 1.0f, 0.0f, 1.0f);
  return out;
}

constant uint kXenosMsaaSamples1X = 0u;
constant uint kXenosMsaaSamples2X = 1u;
constant uint kXenosMsaaSamples4X = 2u;
constant uint kEdramTileCount = 2048u;

uint XeEdramOffsetInts(uint2 pixel_index, uint base_tiles, bool wrap,
                       uint pitch_tiles, uint msaa_samples, bool is_depth,
                       uint format_ints_log2, uint pixel_sample_index,
                       uint2 resolution_scale) {
  uint msaa_samples_x_log2 = (msaa_samples >= kXenosMsaaSamples4X) ? 1u : 0u;
  uint msaa_samples_y_log2 = (msaa_samples >= kXenosMsaaSamples2X) ? 1u : 0u;
  uint2 rt_sample_index =
      pixel_index << uint2(msaa_samples_x_log2, msaa_samples_y_log2);
  rt_sample_index +=
      (uint2(pixel_sample_index) >> uint2(1u, 0u)) & 1u;
  uint2 tile_size_at_32bpp = uint2(80u, 16u) * resolution_scale;
  uint2 tile_size_samples =
      tile_size_at_32bpp >> uint2(format_ints_log2, 0u);
  uint2 tile_offset_xy = rt_sample_index / tile_size_samples;
  base_tiles += tile_offset_xy.y * pitch_tiles + tile_offset_xy.x;
  rt_sample_index -= tile_offset_xy * tile_size_samples;
  if (is_depth) {
    uint tile_width_half = tile_size_samples.x >> 1u;
    rt_sample_index.x =
        uint(int(rt_sample_index.x) +
             ((rt_sample_index.x >= tile_width_half)
                  ? -int(tile_width_half)
                  : int(tile_width_half)));
  }
  uint address =
      base_tiles * (tile_size_at_32bpp.x * tile_size_at_32bpp.y) +
      ((rt_sample_index.y * tile_size_samples.x + rt_sample_index.x) <<
       format_ints_log2);
  if (wrap) {
    address %= tile_size_at_32bpp.x * tile_size_at_32bpp.y * kEdramTileCount;
  }
  return address;
}

float XeFloat7e3To32(uint f10) {
  f10 &= 0x3FFu;
  if (f10 == 0u) {
    return 0.0f;
  }
  uint mantissa = f10 & 0x7Fu;
  uint exponent = f10 >> 7u;
  if (exponent == 0u) {
    uint mantissa_lzcnt = clz(mantissa) - 24u;
    exponent = uint(int(1) - int(mantissa_lzcnt));
    mantissa = (mantissa << mantissa_lzcnt) & 0x7Fu;
  }
  uint f32 = ((exponent + 124u) << 23u) | (mantissa << 16u);
  return as_type<float>(f32);
}

float4 XeUnpackR8G8B8A8UNorm(uint packed) {
  float4 value = float4(packed & 0xFFu, (packed >> 8u) & 0xFFu,
                        (packed >> 16u) & 0xFFu, packed >> 24u);
  return value * (1.0f / 255.0f);
}

float4 XeUnpackR10G10B10A2UNorm(uint packed) {
  float4 value = float4(packed & 0x3FFu, (packed >> 10u) & 0x3FFu,
                        (packed >> 20u) & 0x3FFu, (packed >> 30u) & 0x3u);
  return value * float4(1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f,
                        1.0f / 3.0f);
}

float4 XeUnpackR10G10B10A2Float(uint packed) {
  float r = XeFloat7e3To32(packed & 0x3FFu);
  float g = XeFloat7e3To32((packed >> 10u) & 0x3FFu);
  float b = XeFloat7e3To32((packed >> 20u) & 0x3FFu);
  float a = float((packed >> 30u) & 0x3u) * (1.0f / 3.0f);
  return float4(r, g, b, a);
}

float2 XeUnpackR16G16Edram(uint packed) {
  int r = int(packed << 16u) >> 16u;
  int g = int(packed) >> 16u;
  float2 value = float2(float(r), float(g)) * (32.0f / 32767.0f);
  return max(value, float2(-1.0f));
}

float4 XeUnpackR16G16B16A16Edram(uint2 packed) {
  int r = int(packed.x << 16u) >> 16u;
  int g = int(packed.x) >> 16u;
  int b = int(packed.y << 16u) >> 16u;
  int a = int(packed.y) >> 16u;
  float4 value = float4(float(r), float(g), float(b), float(a)) *
                 (32.0f / 32767.0f);
  return max(value, float4(-1.0f));
}

float2 XeUnpackHalf2(uint packed) {
  return float2(as_type<half2>(packed));
}

float4 XeUnpackColor32bpp(uint format, uint packed) {
  switch (format) {
    case 0u:  // kXenosColorRenderTargetFormat_8_8_8_8
    case 1u:  // kXenosColorRenderTargetFormat_8_8_8_8_GAMMA
      return XeUnpackR8G8B8A8UNorm(packed);
    case 2u:  // kXenosColorRenderTargetFormat_2_10_10_10
    case 10u: // kXenosColorRenderTargetFormat_2_10_10_10_AS_10_10_10_10
      return XeUnpackR10G10B10A2UNorm(packed);
    case 3u:  // kXenosColorRenderTargetFormat_2_10_10_10_FLOAT
    case 12u: // kXenosColorRenderTargetFormat_2_10_10_10_FLOAT_AS_16_16_16_16
      return XeUnpackR10G10B10A2Float(packed);
    case 4u: {  // kXenosColorRenderTargetFormat_16_16
      float2 rg = XeUnpackR16G16Edram(packed);
      return float4(rg, 0.0f, 1.0f);
    }
    case 6u: {  // kXenosColorRenderTargetFormat_16_16_FLOAT
      float2 rg = XeUnpackHalf2(packed);
      return float4(rg, 0.0f, 1.0f);
    }
    case 14u:  // kXenosColorRenderTargetFormat_32_FLOAT
      return float4(as_type<float>(packed), 0.0f, 0.0f, 1.0f);
    default:
      return float4(0.0f);
  }
}

float4 XeUnpackColor64bpp(uint format, uint2 packed) {
  switch (format) {
    case 5u:  // kXenosColorRenderTargetFormat_16_16_16_16
      return XeUnpackR16G16B16A16Edram(packed);
    case 7u: {  // kXenosColorRenderTargetFormat_16_16_16_16_FLOAT
      float2 rg = XeUnpackHalf2(packed.x);
      float2 ba = XeUnpackHalf2(packed.y);
      return float4(rg, ba);
    }
    case 15u:  // kXenosColorRenderTargetFormat_32_32_FLOAT
      return float4(as_type<float>(packed.x), as_type<float>(packed.y),
                    0.0f, 0.0f);
    default:
      return float4(0.0f);
  }
}

struct EdramLoadOut {
  float4 color [[color(0)]];
#if XE_EDRAM_LOAD_MSAA
  uint sample_mask [[sample_mask]];
#endif
};

fragment EdramLoadOut edram_load_ps(
    VSOut in [[stage_in]],
    constant EdramLoadConstants& constants [[buffer(0)]],
    device const uint* edram [[buffer(1)]]) {
  uint2 pixel = uint2(in.position.xy);
  uint format_ints_log2 = constants.format_is_64bpp;
  uint address = XeEdramOffsetInts(
      pixel, constants.base_tiles, true, constants.pitch_tiles,
      constants.msaa_samples, false, format_ints_log2, constants.sample_id,
      uint2(constants.resolution_scale_x, constants.resolution_scale_y));
  float4 color;
  if (constants.format_is_64bpp != 0u) {
    uint2 packed = uint2(edram[address], edram[address + 1u]);
    color = XeUnpackColor64bpp(constants.format, packed);
  } else {
    color = XeUnpackColor32bpp(constants.format, edram[address]);
  }
  EdramLoadOut out;
  out.color = color;
#if XE_EDRAM_LOAD_MSAA
  out.sample_mask = 1u << (constants.sample_id & 0x1Fu);
#endif
  return out;
}
)METAL";

  std::string source;
  source.reserve(sizeof(kEdramLoadShaderSource) + 32);
  source.append(msaa ? "#define XE_EDRAM_LOAD_MSAA 1\n"
                     : "#define XE_EDRAM_LOAD_MSAA 0\n");
  source.append(kEdramLoadShaderSource);

  NS::Error* error = nullptr;
  auto source_str =
      NS::String::string(source.c_str(), NS::UTF8StringEncoding);
  library = device_->newLibrary(source_str, nullptr, &error);
  if (!library) {
    XELOGE("Metal: failed to compile edram load shader: {}",
           error && error->localizedDescription()
               ? error->localizedDescription()->utf8String()
               : "unknown error");
  }
  return library;
}

MTL::RenderPipelineState* MetalRenderTargetCache::GetOrCreateEdramLoadPipeline(
    MTL::PixelFormat dest_format, uint32_t sample_count) {
  uint64_t key = uint64_t(dest_format) | (uint64_t(sample_count) << 32);
  auto it = edram_load_pipelines_.find(key);
  if (it != edram_load_pipelines_.end()) {
    return it->second;
  }

  bool msaa = sample_count > 1;
  MTL::Library* lib = GetOrCreateEdramLoadLibrary(msaa);
  if (!lib) {
    return nullptr;
  }

  NS::String* vs_name =
      NS::String::string("edram_load_vs", NS::UTF8StringEncoding);
  NS::String* ps_name =
      NS::String::string("edram_load_ps", NS::UTF8StringEncoding);
  MTL::Function* vs = lib->newFunction(vs_name);
  MTL::Function* ps = lib->newFunction(ps_name);
  if (!vs || !ps) {
    if (vs) {
      vs->release();
    }
    if (ps) {
      ps->release();
    }
    XELOGE("Metal: edram load missing shader entrypoints");
    return nullptr;
  }

  MTL::RenderPipelineDescriptor* desc =
      MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(vs);
  desc->setFragmentFunction(ps);
  desc->colorAttachments()->object(0)->setPixelFormat(dest_format);
  desc->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);
  desc->setSampleCount(sample_count);

  NS::Error* error = nullptr;
  MTL::RenderPipelineState* pipeline =
      device_->newRenderPipelineState(desc, &error);
  desc->release();
  vs->release();
  ps->release();

  if (!pipeline) {
    XELOGE("Metal: failed to create edram load pipeline: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    return nullptr;
  }

  edram_load_pipelines_.emplace(key, pipeline);
  return pipeline;
}

void MetalRenderTargetCache::ReloadRenderTargetFromEdramTiles(
    MetalRenderTarget* render_target, uint32_t tile_x0, uint32_t tile_y0,
    uint32_t tile_x1, uint32_t tile_y1,
    MTL::CommandBuffer* command_buffer) {
  if (!render_target || !command_buffer || !edram_buffer_) {
    return;
  }

  MTL::Texture* dest_texture = render_target->texture();
  if (!dest_texture) {
    return;
  }

  uint32_t sample_count =
      std::max<uint32_t>(1u, dest_texture->sampleCount());
  MTL::RenderPipelineState* pipeline =
      GetOrCreateEdramLoadPipeline(dest_texture->pixelFormat(), sample_count);
  if (!pipeline) {
    return;
  }

  RenderTargetKey key = render_target->key();
  uint32_t msaa_samples_x_log2 =
      uint32_t(key.msaa_samples >= xenos::MsaaSamples::k4X);
  uint32_t msaa_samples_y_log2 =
      uint32_t(key.msaa_samples >= xenos::MsaaSamples::k2X);
  uint32_t tile_width_samples = xenos::kEdramTileWidthSamples;
  if (key.Is64bpp()) {
    tile_width_samples >>= 1;
  }
  uint32_t tile_width_pixels = tile_width_samples >> msaa_samples_x_log2;
  uint32_t tile_height_pixels =
      xenos::kEdramTileHeightSamples >> msaa_samples_y_log2;
  uint32_t scale_x = std::max<uint32_t>(1u, draw_resolution_scale_x());
  uint32_t scale_y = std::max<uint32_t>(1u, draw_resolution_scale_y());

  uint32_t reload_x = tile_x0 * tile_width_pixels * scale_x;
  uint32_t reload_y = tile_y0 * tile_height_pixels * scale_y;
  uint32_t reload_width = (tile_x1 - tile_x0) * tile_width_pixels * scale_x;
  uint32_t reload_height = (tile_y1 - tile_y0) * tile_height_pixels * scale_y;

  uint32_t texture_width = static_cast<uint32_t>(dest_texture->width());
  uint32_t texture_height = static_cast<uint32_t>(dest_texture->height());
  if (reload_x >= texture_width || reload_y >= texture_height) {
    return;
  }
  reload_width = std::min(reload_width, texture_width - reload_x);
  reload_height = std::min(reload_height, texture_height - reload_y);
  if (!reload_width || !reload_height) {
    return;
  }

  static uint32_t reload_log_count = 0;
  if (reload_log_count < 8) {
    ++reload_log_count;
    uint32_t sample_count = 1u << uint32_t(key.msaa_samples);
    XELOGI(
        "MetalEdramReload: rt_key=0x{:08X} base_tiles={} pitch_tiles={} "
        "tiles=({},{} {}x{}) pixels=({},{} {}x{}) samples={} format={} "
        "format64={} msaa={} scale={}x{}",
        key.key, key.base_tiles, key.GetPitchTiles(), tile_x0, tile_y0,
        (tile_x1 - tile_x0), (tile_y1 - tile_y0), reload_x, reload_y,
        reload_width, reload_height, sample_count, key.GetFormatName(),
        key.Is64bpp() ? 1 : 0, static_cast<uint32_t>(key.msaa_samples),
        draw_resolution_scale_x(), draw_resolution_scale_y());
  }

  MTL::RenderPassDescriptor* rp =
      MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = rp->colorAttachments()->object(0);
  ca->setTexture(dest_texture);
  ca->setLoadAction(MTL::LoadActionLoad);
  ca->setStoreAction(MTL::StoreActionStore);

  MTL::RenderCommandEncoder* encoder =
      command_buffer->renderCommandEncoder(rp);
  if (!encoder) {
    return;
  }

  encoder->setRenderPipelineState(pipeline);
  MTL::Viewport viewport;
  viewport.originX = 0.0;
  viewport.originY = 0.0;
  viewport.width = double(texture_width);
  viewport.height = double(texture_height);
  viewport.znear = 0.0;
  viewport.zfar = 1.0;
  encoder->setViewport(viewport);

  MTL::ScissorRect scissor;
  scissor.x = reload_x;
  scissor.y = reload_y;
  scissor.width = reload_width;
  scissor.height = reload_height;
  encoder->setScissorRect(scissor);

  struct EdramLoadConstants {
    uint32_t base_tiles;
    uint32_t pitch_tiles;
    uint32_t format;
    uint32_t format_is_64bpp;
    uint32_t msaa_samples;
    uint32_t sample_id;
    uint32_t resolution_scale_x;
    uint32_t resolution_scale_y;
  };

  EdramLoadConstants constants = {};
  constants.base_tiles = key.base_tiles;
  constants.pitch_tiles = key.GetPitchTiles();
  constants.format = uint32_t(key.GetColorFormat());
  constants.format_is_64bpp = key.Is64bpp() ? 1u : 0u;
  constants.msaa_samples = uint32_t(key.msaa_samples);
  constants.resolution_scale_x = draw_resolution_scale_x();
  constants.resolution_scale_y = draw_resolution_scale_y();

  encoder->setFragmentBuffer(edram_buffer_, 0, 1);

  uint32_t sample_count_mask = 1u << uint32_t(key.msaa_samples);
  if (!sample_count_mask) {
    sample_count_mask = 1u;
  }
  for (uint32_t sample_id = 0; sample_id < sample_count_mask; ++sample_id) {
    constants.sample_id = sample_id;
    encoder->setFragmentBytes(&constants, sizeof(constants), 0);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                            NS::UInteger(3));
  }

  encoder->endEncoding();
  rp->release();
}

void MetalRenderTargetCache::FlushEdramFromHostRenderTargets() {
  if (GetPath() != Path::kHostRenderTargets) {
    XELOGI(
        "MetalRenderTargetCache::FlushEdramFromHostRenderTargets: skipped "
        "(path={})",
        GetPath() == Path::kPixelShaderInterlock ? "rov" : "host");
    return;
  }
  if (!edram_buffer_) {
    XELOGW(
        "MetalRenderTargetCache::FlushEdramFromHostRenderTargets: missing "
        "EDRAM buffer");
    return;
  }

  constexpr uint32_t kPitchTilesAt32bpp = 16;
  constexpr uint32_t kHeightTileRows =
      xenos::kEdramTileCount / kPitchTilesAt32bpp;
  static_assert(
      kPitchTilesAt32bpp * kHeightTileRows == xenos::kEdramTileCount,
      "EDRAM tile count must match pitch * rows for full flush.");

  XELOGI(
      "MetalRenderTargetCache::FlushEdramFromHostRenderTargets: dumping full "
      "EDRAM from host RTs");
  DumpRenderTargets(0, kPitchTilesAt32bpp, kHeightTileRows,
                    kPitchTilesAt32bpp);
}

bool MetalRenderTargetCache::Resolve(Memory& memory, uint32_t& written_address,
                                     uint32_t& written_length,
                                     MTL::CommandBuffer* command_buffer) {
  written_address = 0;
  written_length = 0;
  XELOGI("MetalRenderTargetCache::Resolve: begin");

  const RegisterFile& regs = register_file();
  draw_util::ResolveInfo resolve_info;

  // Fixed16 formats may be truncated to -1..1 when backed by SNORM.
  bool fixed_rg16_trunc = IsFixedRG16TruncatedToMinus1To1();
  bool fixed_rgba16_trunc = IsFixedRGBA16TruncatedToMinus1To1();

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

  if (cvars::metal_log_resolve_copy_dest_info) {
    static uint32_t copy_dest_log_count = 0;
    if (copy_dest_log_count < 16) {
      ++copy_dest_log_count;
      auto copy_dest_info = regs.Get<reg::RB_COPY_DEST_INFO>();
      XELOGI(
          "MetalResolve RB_COPY_DEST_INFO=0x{:08X} endian={} array={} slice={} "
          "format={} number={} exp_bias={} swap={} (resolved_format={})",
          copy_dest_info.value,
          uint32_t(copy_dest_info.copy_dest_endian),
          copy_dest_info.copy_dest_array ? 1 : 0,
          uint32_t(copy_dest_info.copy_dest_slice),
          uint32_t(copy_dest_info.copy_dest_format),
          uint32_t(copy_dest_info.copy_dest_number),
          int(copy_dest_info.copy_dest_exp_bias),
          copy_dest_info.copy_dest_swap ? 1 : 0,
          uint32_t(resolve_info.copy_dest_info.copy_dest_format));
    }
  }

  bool host_rt_path = GetPath() == Path::kHostRenderTargets;
  if (!host_rt_path) {
    XELOGI("MetalRenderTargetCache::Resolve: non-host-RT path using EDRAM");
  }

  MetalRenderTarget* src_rt = nullptr;
  RenderTarget* const* accumulated_targets =
      last_update_accumulated_render_targets();

  if (host_rt_path) {
    if (is_depth) {
      // For depth resolves, use the current depth render target as the source,
      // matching D3D12/Vulkan behavior.
      if (accumulated_targets && accumulated_targets[0]) {
        src_rt = static_cast<MetalRenderTarget*>(accumulated_targets[0]);
      }
      if (!src_rt || !src_rt->texture()) {
        XELOGI("MetalRenderTargetCache::Resolve: no depth source RT");
      }
    } else {
      // Color resolves select the source via copy_src_select.
      uint32_t copy_src = resolve_info.rb_copy_control.copy_src_select;
      if (copy_src < xenos::kMaxColorRenderTargets) {
        if (accumulated_targets && accumulated_targets[1 + copy_src]) {
          src_rt = static_cast<MetalRenderTarget*>(
              accumulated_targets[1 + copy_src]);
        }
        if (!src_rt || !src_rt->texture()) {
          XELOGI(
              "MetalRenderTargetCache::Resolve: no source RT for "
              "copy_src_select={}",
              copy_src);
        }
      } else {
        XELOGI(
            "MetalRenderTargetCache::Resolve: copy_src_select out of range "
            "({})",
            copy_src);
      }
    }
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
  static uint32_t resolve_source_log_count = 0;
  if (resolve_source_log_count < 8 && src_rt) {
    ++resolve_source_log_count;
    const RenderTargetKey& src_key = src_rt->key();
    XELOGI(
        "MetalResolve source: rt_key=0x{:08X} {}x{} pitch_tiles={} msaa={} "
        "is_depth={} copy_src_select={}",
        src_key.key, src_rt->texture()->width(), src_rt->texture()->height(),
        src_key.GetPitchTiles(), static_cast<uint32_t>(src_key.msaa_samples),
        src_key.is_depth ? 1 : 0,
        is_depth ? -1
                 : static_cast<int>(
                       resolve_info.rb_copy_control.copy_src_select));
  }
  if (src_rt) {
    const RenderTargetKey& src_key = src_rt->key();
    if (dump_pitch != src_key.GetPitchTiles()) {
      XELOGW(
          "MetalResolve: dump_pitch {} does not match src pitch_tiles {} "
          "(rt_key=0x{:08X})",
          dump_pitch, src_key.GetPitchTiles(), src_key.key);
    }
  }
  static uint32_t resolve_log_count = 0;
  if (resolve_log_count < 8) {
    ++resolve_log_count;
    XELOGI(
        "MetalResolve constants: base={} row_length_used={} rows={} pitch={} "
        "scale={}x{} is_depth={} host_rt_path={}",
        dump_base, dump_row_length_used, dump_rows, dump_pitch,
        draw_resolution_scale_x(), draw_resolution_scale_y(), is_depth ? 1 : 0,
        host_rt_path ? 1 : 0);
  }
  if (host_rt_path) {
    // Match D3D12/Vulkan: dump host RT ownership into EDRAM, then resolve
    // from EDRAM to shared memory. Resolve-time blend fallback is not correct
    // because blending state is per-draw, not per-resolve.
    if (cvars::metal_disable_resolve_edram_dump) {
      static uint32_t disable_dump_log_count = 0;
      if (disable_dump_log_count < 8) {
        ++disable_dump_log_count;
        XELOGW(
            "MetalResolve: EDRAM dump disabled via "
            "metal_disable_resolve_edram_dump");
      }
    } else {
      DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch,
                        command_buffer);
      static uint32_t edram_after_dump_log_count = 0;
      const bool edram_cpu_visible =
          edram_buffer_ &&
          edram_buffer_->storageMode() == MTL::StorageModeShared;
      if (edram_after_dump_log_count < 8 && edram_cpu_visible) {
        ++edram_after_dump_log_count;
        const uint8_t* edram_bytes =
            static_cast<const uint8_t*>(edram_buffer_->contents());
        if (edram_bytes) {
          uint32_t edram_debug_offset = dump_base * 64u;
          const uint8_t* src_edram = edram_bytes + edram_debug_offset;
          XELOGI(
              "MetalResolve SRC (EDRAM) after dump [0..15] @tile_base*64="
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
  }

  uint32_t dest_base = resolve_info.copy_dest_base;
  uint32_t dest_local_start = resolve_info.copy_dest_extent_start - dest_base;
  uint32_t dest_local_end =
      dest_local_start + resolve_info.copy_dest_extent_length;

  static uint32_t swap_resolve_log_count = 0;
  command_processor_.SetSwapDestSwap(
      dest_base, resolve_info.copy_dest_info.copy_dest_swap);
  if (swap_resolve_log_count < 8) {
    ++swap_resolve_log_count;
    draw_util::Scissor scissor;
    draw_util::GetScissor(register_file(), scissor);
    XELOGI(
        "MetalResolve dest=0x{:08X} extent=0x{:08X} scissor=({},{} {}x{}) "
        "swap={}",
        dest_base, resolve_info.copy_dest_extent_length, scissor.offset[0],
        scissor.offset[1], scissor.extent[0], scissor.extent[1],
        resolve_info.copy_dest_info.copy_dest_swap ? 1 : 0);
  }

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

    if (swap_resolve_log_count <= 8) {
      uint32_t dest_pitch_pixels =
          copy_constants.dest_relative.dest_coordinate_info
              .pitch_aligned_div_32
          << 5;
      uint32_t dest_height_pixels =
          copy_constants.dest_relative.dest_coordinate_info
              .height_aligned_div_32
          << 5;
      uint32_t dest_offset_x =
          copy_constants.dest_relative.dest_coordinate_info.offset_x_div_8 << 3;
      uint32_t dest_offset_y =
          copy_constants.dest_relative.dest_coordinate_info.offset_y_div_8 << 3;
      XELOGI(
          "MetalResolve dest info: base=0x{:08X} extent=0x{:08X} "
          "pitch={} height={} offset=({}, {}) sample_select={} format={} "
          "endian={} swap={} exp_bias={}",
          copy_constants.dest_base, resolve_info.copy_dest_extent_length,
          dest_pitch_pixels, dest_height_pixels, dest_offset_x, dest_offset_y,
          uint32_t(copy_constants.dest_relative.dest_coordinate_info
                       .copy_sample_select),
          uint32_t(resolve_info.copy_dest_info.copy_dest_format),
          uint32_t(resolve_info.copy_dest_info.copy_dest_endian),
          resolve_info.copy_dest_info.copy_dest_swap ? 1 : 0,
          int(resolve_info.copy_dest_info.copy_dest_exp_bias));
    }

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
      if (is_swap_resolve) {
        uint32_t dest_pitch_pixels =
            copy_constants.dest_relative.dest_coordinate_info
                .pitch_aligned_div_32
            << 5;
        if (dest_pitch_pixels < resolve_width) {
          uint32_t new_pitch_pixels = (resolve_width + 31) & ~31u;
          XELOGW(
              "MetalResolve swap: overriding dest pitch {} -> {} "
              "(resolve_width={})",
              dest_pitch_pixels, new_pitch_pixels, resolve_width);
          copy_constants.dest_relative.dest_coordinate_info
              .pitch_aligned_div_32 = new_pitch_pixels >> 5;
        }
      }
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
          bool owns_command_buffer = false;
          MTL::CommandBuffer* cmd = command_buffer;
          if (!cmd) {
            cmd = queue->commandBuffer();
            if (!cmd) {
              XELOGE(
                  "MetalRenderTargetCache::Resolve: failed to get command "
                  "buffer for GPU path");
              cmd = nullptr;
            }
            owns_command_buffer = true;
          }
          if (cmd) {
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
              if (owns_command_buffer) {
                cmd->commit();
                cmd->waitUntilCompleted();
              }
              // cmd is autoreleased from commandBuffer() - do not release

              if (shared_bytes) {
                uint32_t debug_addr = resolve_info.copy_dest_extent_start;
                const uint8_t* dst_after = shared_bytes + debug_addr;
                XELOGI(
                    "MetalResolve DST after [0..15] @0x{:08X}: "
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

              bool clear_depth = resolve_info.IsClearingDepth();
              bool clear_color = resolve_info.IsClearingColor();
              if (clear_depth || clear_color) {
                switch (GetPath()) {
                  case Path::kHostRenderTargets: {
                    Transfer::Rectangle clear_rectangle;
                    RenderTarget* clear_targets[2] = {};
                    std::vector<Transfer> clear_transfers[2];
                    if (PrepareHostRenderTargetsResolveClear(
                            resolve_info, clear_rectangle, clear_targets[0],
                            clear_transfers[0], clear_targets[1],
                            clear_transfers[1])) {
                      uint64_t clear_values[2];
                      clear_values[0] = resolve_info.rb_depth_clear;
                      clear_values[1] =
                          resolve_info.rb_color_clear |
                          (uint64_t(resolve_info.rb_color_clear_lo) << 32);
                      PerformTransfersAndResolveClears(
                          2, clear_targets, clear_transfers, clear_values,
                          &clear_rectangle, command_buffer);
                    }
                  } break;
                  case Path::kPixelShaderInterlock:
                    // TODO(wmarti): implement ROV/EDRAM clear path for Metal.
                    break;
                  default:
                    break;
                }
              }
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
    const std::vector<Transfer>* render_target_transfers,
    const uint64_t* render_target_resolve_clear_values,
    const Transfer::Rectangle* resolve_clear_rectangle,
    MTL::CommandBuffer* command_buffer) {
  if (!render_targets || !render_target_transfers) {
    return;
  }

  MTL::CommandBuffer* cmd = command_buffer;
  if (!cmd) {
    cmd = command_processor_.EnsureCommandBuffer();
  }
  if (!cmd) {
    XELOGE(
        "MetalRenderTargetCache::PerformTransfersAndResolveClears: no command "
        "buffer");
    return;
  }

  command_processor_.EndRenderEncoder();

  bool resolve_clear_needed =
      render_target_resolve_clear_values && resolve_clear_rectangle;

  uint32_t scale_x = draw_resolution_scale_x();
  uint32_t scale_y = draw_resolution_scale_y();
  uint32_t tile_width_samples =
      xenos::kEdramTileWidthSamples * draw_resolution_scale_x();
  uint32_t tile_height_samples =
      xenos::kEdramTileHeightSamples * draw_resolution_scale_y();
  uint32_t depth_round =
      (!cvars::depth_float24_convert_in_pixel_shader &&
       cvars::depth_float24_round)
          ? 1u
          : 0u;

  // Host depth store pass (dest depth where host depth source == dest).
  bool host_depth_store_dispatched = false;
  for (uint32_t i = 0; i < render_target_count; ++i) {
    RenderTarget* dest_rt = render_targets[i];
    if (!dest_rt) {
      continue;
    }
    RenderTargetKey dest_key = dest_rt->key();
    if (!dest_key.is_depth) {
      continue;
    }
    const std::vector<Transfer>& depth_transfers = render_target_transfers[i];
    for (const Transfer& transfer : depth_transfers) {
      if (transfer.host_depth_source != dest_rt) {
        continue;
      }
      auto* dest_metal_rt = static_cast<MetalRenderTarget*>(dest_rt);
      MTL::Texture* depth_texture = dest_metal_rt->texture();
      if (!depth_texture || !edram_buffer_) {
        continue;
      }
      size_t pipeline_index = size_t(dest_key.msaa_samples);
      if (pipeline_index >= xe::countof(host_depth_store_pipelines_) ||
          !host_depth_store_pipelines_[pipeline_index]) {
        XELOGE(
            "MetalRenderTargetCache::PerformTransfersAndResolveClears: missing "
            "host depth store pipeline for msaa={}",
            uint32_t(dest_key.msaa_samples));
        continue;
      }
      Transfer::Rectangle rectangles[Transfer::kMaxRectanglesWithCutout];
      uint32_t rectangle_count = transfer.GetRectangles(
          dest_key.base_tiles, dest_key.pitch_tiles_at_32bpp,
          dest_key.msaa_samples, false, rectangles, resolve_clear_rectangle);
      if (!rectangle_count) {
        continue;
      }
      HostDepthStoreRenderTargetConstant render_target_constant =
          GetHostDepthStoreRenderTargetConstant(dest_key.pitch_tiles_at_32bpp,
                                                msaa_2x_supported_);
      MTL::ComputeCommandEncoder* encoder = cmd->computeCommandEncoder();
      if (!encoder) {
        XELOGE(
            "MetalRenderTargetCache::PerformTransfersAndResolveClears: "
            "failed to create host depth store encoder");
        continue;
      }
      encoder->setComputePipelineState(
          host_depth_store_pipelines_[pipeline_index]);
      encoder->setBuffer(edram_buffer_, 0, 1);
      encoder->setTexture(depth_texture, 0);
      for (uint32_t rect_index = 0; rect_index < rectangle_count; ++rect_index) {
        uint32_t group_count_x = 0;
        uint32_t group_count_y = 0;
        HostDepthStoreRectangleConstant rectangle_constant;
        GetHostDepthStoreRectangleInfo(
            rectangles[rect_index], dest_key.msaa_samples, rectangle_constant,
            group_count_x, group_count_y);
        if (!group_count_x || !group_count_y) {
          continue;
        }
        HostDepthStoreConstants constants = {};
        constants.rectangle = rectangle_constant;
        constants.render_target = render_target_constant;
        encoder->setBytes(&constants, sizeof(constants), 0);
        encoder->dispatchThreadgroups(
            MTL::Size::Make(group_count_x, group_count_y, 1),
            MTL::Size::Make(8, 8, 1));
        host_depth_store_dispatched = true;
      }
      encoder->endEncoding();
    }
    break;
  }

  bool any_transfers_done = false;

  for (uint32_t i = 0; i < render_target_count; ++i) {
    RenderTarget* dest_rt = render_targets[i];
    if (!dest_rt) {
      continue;
    }

    const std::vector<Transfer>& transfers = render_target_transfers[i];
    if (transfers.empty() && !resolve_clear_needed) {
      continue;
    }

    auto* dest_metal_rt = static_cast<MetalRenderTarget*>(dest_rt);
    RenderTargetKey dest_key = dest_metal_rt->key();
    bool dest_is_depth = dest_key.is_depth;

    bool dest_is_uint = false;
    MTL::PixelFormat dest_pixel_format =
        dest_is_depth ? GetDepthPixelFormat(dest_key.GetDepthFormat())
                      : GetColorOwnershipTransferPixelFormat(
                            dest_key.GetColorFormat(), &dest_is_uint);

    MTL::Texture* dest_texture = dest_is_depth
                                     ? dest_metal_rt->texture()
                                     : dest_metal_rt->transfer_texture();
    if (!dest_texture) {
      XELOGW(
          "MetalRenderTargetCache::PerformTransfersAndResolveClears: "
          "Destination RT {} has no texture",
          i);
      continue;
    }

    uint32_t dest_sample_count = MsaaSamplesToCount(dest_key.msaa_samples);
    uint32_t dest_width = uint32_t(dest_texture->width());
    uint32_t dest_height = uint32_t(dest_texture->height());

    auto set_rect_viewport = [&](MTL::RenderCommandEncoder* encoder,
                                 const Transfer::Rectangle& rect) -> bool {
      uint32_t scaled_x = rect.x_pixels * scale_x;
      uint32_t scaled_y = rect.y_pixels * scale_y;
      uint32_t scaled_width = rect.width_pixels * scale_x;
      uint32_t scaled_height = rect.height_pixels * scale_y;
      if (scaled_x >= dest_width || scaled_y >= dest_height) {
        return false;
      }
      scaled_width = std::min(scaled_width, dest_width - scaled_x);
      scaled_height = std::min(scaled_height, dest_height - scaled_y);
      if (!scaled_width || !scaled_height) {
        return false;
      }
      MTL::Viewport vp;
      vp.originX = double(scaled_x);
      vp.originY = double(scaled_y);
      vp.width = double(scaled_width);
      vp.height = double(scaled_height);
      vp.znear = 0.0;
      vp.zfar = 1.0;
      encoder->setViewport(vp);
      MTL::ScissorRect scissor;
      scissor.x = scaled_x;
      scissor.y = scaled_y;
      scissor.width = scaled_width;
      scissor.height = scaled_height;
      encoder->setScissorRect(scissor);
      return true;
    };

    if (!transfers.empty()) {
      bool need_stencil_bit_draws = dest_is_depth;
      bool stencil_clear_needed = need_stencil_bit_draws;

      transfer_invocations_.clear();
      transfer_invocations_.reserve(
          transfers.size() * (need_stencil_bit_draws ? 2 : 1));

      for (const Transfer& transfer : transfers) {
        if (transfer.source) {
          auto* source = static_cast<MetalRenderTarget*>(transfer.source);
          source->SetTemporarySortIndex(UINT32_MAX);
        }
        if (transfer.host_depth_source) {
          auto* host_depth =
              static_cast<MetalRenderTarget*>(transfer.host_depth_source);
          host_depth->SetTemporarySortIndex(UINT32_MAX);
        }
      }

      uint32_t rt_sort_index = 0;
      auto ensure_sort_index = [&](MetalRenderTarget* rt) {
        if (rt && rt->temporary_sort_index() == UINT32_MAX) {
          rt->SetTemporarySortIndex(rt_sort_index++);
        }
      };

      for (uint32_t pass = 0; pass <= uint32_t(need_stencil_bit_draws); ++pass) {
        for (const Transfer& transfer : transfers) {
          if (!transfer.source) {
            continue;
          }
          auto* source_rt =
              static_cast<MetalRenderTarget*>(transfer.source);
          auto* host_depth_rt =
              pass ? nullptr
                   : static_cast<MetalRenderTarget*>(transfer.host_depth_source);
          ensure_sort_index(source_rt);
          ensure_sort_index(host_depth_rt);

          RenderTargetKey source_key = source_rt->key();
          TransferShaderKey shader_key = {};
          shader_key.source_msaa_samples = source_key.msaa_samples;
          shader_key.dest_msaa_samples = dest_key.msaa_samples;
          shader_key.source_resource_format = source_key.resource_format;
          shader_key.dest_resource_format = dest_key.resource_format;

          if (pass) {
            shader_key.mode = source_key.is_depth
                                  ? TransferMode::kDepthToStencilBit
                                  : TransferMode::kColorToStencilBit;
            shader_key.host_depth_source_msaa_samples = xenos::MsaaSamples::k1X;
            shader_key.host_depth_source_is_copy = 0;
          } else {
            if (dest_is_depth) {
              if (host_depth_rt) {
                bool host_depth_is_copy = host_depth_rt == dest_metal_rt;
                shader_key.mode = source_key.is_depth
                                      ? TransferMode::kDepthAndHostDepthToDepth
                                      : TransferMode::kColorAndHostDepthToDepth;
                shader_key.host_depth_source_is_copy =
                    host_depth_is_copy ? 1 : 0;
                shader_key.host_depth_source_msaa_samples =
                    host_depth_is_copy ? xenos::MsaaSamples::k1X
                                       : host_depth_rt->key().msaa_samples;
              } else {
                shader_key.mode = source_key.is_depth
                                      ? TransferMode::kDepthToDepth
                                      : TransferMode::kColorToDepth;
                shader_key.host_depth_source_msaa_samples =
                    xenos::MsaaSamples::k1X;
                shader_key.host_depth_source_is_copy = 0;
              }
            } else {
              shader_key.mode = source_key.is_depth
                                    ? TransferMode::kDepthToColor
                                    : TransferMode::kColorToColor;
              shader_key.host_depth_source_msaa_samples =
                  xenos::MsaaSamples::k1X;
              shader_key.host_depth_source_is_copy = 0;
            }
          }

          transfer_invocations_.emplace_back(transfer, shader_key);
          if (pass) {
            transfer_invocations_.back().transfer.host_depth_source = nullptr;
          }
        }
      }

      std::sort(transfer_invocations_.begin(), transfer_invocations_.end());

      if (stencil_clear_needed) {
        MTL::RenderPipelineState* clear_pipeline =
            GetOrCreateTransferClearPipeline(dest_pixel_format, false, true,
                                             dest_sample_count);
        MTL::DepthStencilState* stencil_clear_state =
            GetTransferStencilClearState();
        if (clear_pipeline && stencil_clear_state) {
          MTL::RenderPassDescriptor* rp =
              MTL::RenderPassDescriptor::renderPassDescriptor();
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
          MTL::RenderCommandEncoder* encoder = cmd->renderCommandEncoder(rp);
          if (encoder) {
            TransferClearDepthConstants constants = {};
            constants.depth = 0.0f;
            encoder->setRenderPipelineState(clear_pipeline);
            encoder->setDepthStencilState(stencil_clear_state);
            encoder->setStencilReferenceValue(0);
            encoder->setFragmentBytes(&constants, sizeof(constants), 0);
            for (const Transfer& transfer : transfers) {
              Transfer::Rectangle rectangles[Transfer::kMaxRectanglesWithCutout];
              uint32_t rectangle_count = transfer.GetRectangles(
                  dest_key.base_tiles, dest_key.GetPitchTiles(),
                  dest_key.msaa_samples, dest_key.Is64bpp(), rectangles,
                  resolve_clear_rectangle);
              for (uint32_t rect_index = 0; rect_index < rectangle_count;
                   ++rect_index) {
                if (!set_rect_viewport(encoder, rectangles[rect_index])) {
                  continue;
                }
                encoder->drawPrimitives(MTL::PrimitiveTypeTriangle,
                                        NS::UInteger(0), NS::UInteger(3));
              }
            }
            encoder->endEncoding();
          }
        }
      }

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

      MTL::RenderCommandEncoder* encoder = cmd->renderCommandEncoder(rp);
      if (encoder) {
        for (const auto& invocation : transfer_invocations_) {
          const Transfer& transfer = invocation.transfer;
          const TransferShaderKey& shader_key = invocation.shader_key;
          const TransferModeInfo& mode_info =
              kTransferModeInfos[size_t(shader_key.mode)];
          bool is_stencil_bit =
              mode_info.output == TransferOutput::kStencilBit;

          auto* source_rt = static_cast<MetalRenderTarget*>(transfer.source);
          if (!source_rt) {
            continue;
          }

          RenderTargetKey source_key = source_rt->key();
          bool source_is_uint = false;
          if (mode_info.source_is_color) {
            GetColorOwnershipTransferPixelFormat(
                source_key.GetColorFormat(), &source_is_uint);
          }

          MTL::RenderPipelineState* pipeline =
              GetOrCreateTransferPipelines(shader_key, dest_pixel_format,
                                           dest_is_uint);
          if (!pipeline) {
            continue;
          }

          encoder->setRenderPipelineState(pipeline);

          if (is_stencil_bit) {
            // Depth/stencil state set per-bit below.
          } else if (dest_is_depth) {
            encoder->setDepthStencilState(
                GetTransferDepthStencilState(true));
          } else {
            MTL::DepthStencilState* no_depth_state =
                GetTransferNoDepthStencilState();
            if (!no_depth_state) {
              continue;
            }
            encoder->setDepthStencilState(no_depth_state);
          }

          // Bind source textures.
          if (mode_info.source_is_color) {
            MTL::Texture* source_texture = source_rt->transfer_texture();
            if (!source_texture) {
              continue;
            }
            encoder->setFragmentTexture(source_texture, 0);
          } else {
            MTL::Texture* depth_texture = source_rt->texture();
            if (!depth_texture) {
              continue;
            }
            encoder->setFragmentTexture(depth_texture, 0);
            MTL::Texture* stencil_texture =
                depth_texture->newTextureView(MTL::PixelFormatX32_Stencil8);
            if (!stencil_texture) {
              continue;
            }
            encoder->setFragmentTexture(stencil_texture, 1);
            stencil_texture->release();
          }

          // Bind host depth source if needed.
          if (mode_info.uses_host_depth) {
            if (shader_key.host_depth_source_is_copy) {
              if (edram_buffer_) {
                encoder->setFragmentBuffer(edram_buffer_, 0, 1);
              } else {
                MTL::Buffer* dummy = GetTransferDummyBuffer();
                if (dummy) {
                  encoder->setFragmentBuffer(dummy, 0, 1);
                }
              }
            } else {
              auto* host_depth_rt =
                  static_cast<MetalRenderTarget*>(transfer.host_depth_source);
              MTL::Texture* host_depth_texture =
                  host_depth_rt ? host_depth_rt->texture() : nullptr;
              if (!host_depth_texture) {
                continue;
              }
              uint32_t host_depth_index = mode_info.source_is_color ? 1 : 2;
              encoder->setFragmentTexture(host_depth_texture, host_depth_index);
            }
          }

          TransferShaderConstants constants = {};
          constants.address.dest_pitch = dest_key.GetPitchTiles();
          constants.address.source_pitch = source_key.GetPitchTiles();
          constants.address.source_to_dest =
              int32_t(dest_key.base_tiles) - int32_t(source_key.base_tiles);
          constants.host_depth_address = {};
          if (mode_info.uses_host_depth &&
              !shader_key.host_depth_source_is_copy) {
            auto* host_depth_rt =
                static_cast<MetalRenderTarget*>(transfer.host_depth_source);
            if (host_depth_rt) {
              RenderTargetKey host_depth_key = host_depth_rt->key();
              constants.host_depth_address.dest_pitch = dest_key.GetPitchTiles();
              constants.host_depth_address.source_pitch =
                  host_depth_key.GetPitchTiles();
              constants.host_depth_address.source_to_dest =
                  int32_t(dest_key.base_tiles) -
                  int32_t(host_depth_key.base_tiles);
            }
          }
          constants.source_format = source_key.resource_format;
          constants.dest_format = dest_key.resource_format;
          constants.source_is_depth = source_key.is_depth ? 1 : 0;
          constants.dest_is_depth = dest_key.is_depth ? 1 : 0;
          constants.source_is_uint = source_is_uint ? 1 : 0;
          constants.dest_is_uint = dest_is_uint ? 1 : 0;
          constants.source_is_64bpp = source_key.Is64bpp() ? 1 : 0;
          constants.dest_is_64bpp = dest_key.Is64bpp() ? 1 : 0;
          constants.source_msaa_samples =
              MsaaSamplesToCount(source_key.msaa_samples);
          constants.dest_msaa_samples =
              MsaaSamplesToCount(dest_key.msaa_samples);
          constants.host_depth_source_msaa_samples =
              MsaaSamplesToCount(shader_key.host_depth_source_msaa_samples);
          constants.host_depth_source_is_copy =
              shader_key.host_depth_source_is_copy ? 1 : 0;
          constants.depth_round = depth_round;
          constants.msaa_2x_supported = msaa_2x_supported_ ? 1 : 0;
          constants.tile_width_samples = tile_width_samples;
          constants.tile_height_samples = tile_height_samples;
          constants.dest_sample_id = 0;

          Transfer::Rectangle rectangles[Transfer::kMaxRectanglesWithCutout];
          uint32_t rectangle_count = transfer.GetRectangles(
              dest_key.base_tiles, dest_key.GetPitchTiles(),
              dest_key.msaa_samples, dest_key.Is64bpp(), rectangles,
              resolve_clear_rectangle);
          if (!rectangle_count) {
            continue;
          }

          if (is_stencil_bit) {
            for (uint32_t bit = 0; bit < 8; ++bit) {
              MTL::DepthStencilState* stencil_state =
                  GetTransferStencilBitState(bit);
              if (!stencil_state) {
                continue;
              }
              constants.stencil_mask = uint32_t(1) << bit;
              constants.stencil_clear = 0;
              encoder->setDepthStencilState(stencil_state);
              encoder->setStencilReferenceValue(uint32_t(1) << bit);
              for (uint32_t sample_id = 0; sample_id < dest_sample_count;
                   ++sample_id) {
                constants.dest_sample_id = sample_id;
                encoder->setFragmentBytes(&constants, sizeof(constants), 0);
                for (uint32_t rect_index = 0; rect_index < rectangle_count;
                     ++rect_index) {
                  if (!set_rect_viewport(encoder, rectangles[rect_index])) {
                    continue;
                  }
                  encoder->drawPrimitives(MTL::PrimitiveTypeTriangle,
                                          NS::UInteger(0), NS::UInteger(3));
                }
              }
            }
          } else {
            constants.stencil_mask = 0;
            constants.stencil_clear = 0;
            for (uint32_t sample_id = 0; sample_id < dest_sample_count;
                 ++sample_id) {
              constants.dest_sample_id = sample_id;
              encoder->setFragmentBytes(&constants, sizeof(constants), 0);
              for (uint32_t rect_index = 0; rect_index < rectangle_count;
                   ++rect_index) {
                if (!set_rect_viewport(encoder, rectangles[rect_index])) {
                  continue;
                }
                encoder->drawPrimitives(MTL::PrimitiveTypeTriangle,
                                        NS::UInteger(0), NS::UInteger(3));
              }
            }
          }
          any_transfers_done = true;
        }
        encoder->endEncoding();
      }
    }

    if (resolve_clear_needed) {
      uint64_t clear_value = render_target_resolve_clear_values[i];
      if (dest_is_depth) {
        uint32_t depth_guest_clear_value =
            (uint32_t(clear_value) >> 8) & 0xFFFFFF;
        float depth_host_clear_value = 0.0f;
        switch (dest_key.GetDepthFormat()) {
          case xenos::DepthRenderTargetFormat::kD24S8:
            depth_host_clear_value =
                xenos::UNorm24To32(depth_guest_clear_value);
            break;
          case xenos::DepthRenderTargetFormat::kD24FS8:
            depth_host_clear_value =
                xenos::Float20e4To32(depth_guest_clear_value) * 0.5f;
            break;
        }
        MTL::RenderPipelineState* clear_pipeline =
            GetOrCreateTransferClearPipeline(dest_pixel_format, false, true,
                                             dest_sample_count);
        MTL::DepthStencilState* clear_state = GetTransferDepthClearState();
        if (clear_pipeline && clear_state) {
          MTL::RenderPassDescriptor* clear_rp =
              MTL::RenderPassDescriptor::renderPassDescriptor();
          auto* da = clear_rp->depthAttachment();
          da->setTexture(dest_texture);
          da->setLoadAction(MTL::LoadActionLoad);
          da->setStoreAction(MTL::StoreActionStore);
          if (dest_pixel_format == MTL::PixelFormatDepth32Float_Stencil8 ||
              dest_pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
            auto* sa = clear_rp->stencilAttachment();
            sa->setTexture(dest_texture);
            sa->setLoadAction(MTL::LoadActionLoad);
            sa->setStoreAction(MTL::StoreActionStore);
          }
          MTL::RenderCommandEncoder* clear_encoder =
              cmd->renderCommandEncoder(clear_rp);
          if (clear_encoder) {
            TransferClearDepthConstants constants = {};
            constants.depth = depth_host_clear_value;
            clear_encoder->setRenderPipelineState(clear_pipeline);
            clear_encoder->setDepthStencilState(clear_state);
            clear_encoder->setStencilReferenceValue(
                uint32_t(clear_value) & 0xFF);
            clear_encoder->setFragmentBytes(&constants, sizeof(constants), 0);
            Transfer::Rectangle clear_rect = *resolve_clear_rectangle;
            if (set_rect_viewport(clear_encoder, clear_rect)) {
              clear_encoder->drawPrimitives(MTL::PrimitiveTypeTriangle,
                                            NS::UInteger(0),
                                            NS::UInteger(3));
            }
            clear_encoder->endEncoding();
          }
        }
      } else {
        TransferClearColorFloatConstants float_constants = {};
        TransferClearColorUintConstants uint_constants = {};
        bool clear_via_drawing = false;
        bool clear_use_uint = false;
        switch (dest_key.GetColorFormat()) {
          case xenos::ColorRenderTargetFormat::k_8_8_8_8:
          case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA: {
            for (uint32_t j = 0; j < 4; ++j) {
              float_constants.color[j] =
                  ((clear_value >> (j * 8)) & 0xFF) * (1.0f / 0xFF);
            }
          } break;
          case xenos::ColorRenderTargetFormat::k_2_10_10_10:
          case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10: {
            for (uint32_t j = 0; j < 3; ++j) {
              float_constants.color[j] =
                  ((clear_value >> (j * 10)) & 0x3FF) * (1.0f / 0x3FF);
            }
            float_constants.color[3] =
                ((clear_value >> 30) & 0x3) * (1.0f / 0x3);
          } break;
          case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
          case xenos::ColorRenderTargetFormat::
              k_2_10_10_10_FLOAT_AS_16_16_16_16: {
            for (uint32_t j = 0; j < 3; ++j) {
              float_constants.color[j] =
                  xenos::Float7e3To32((clear_value >> (j * 10)) & 0x3FF);
            }
            float_constants.color[3] =
                ((clear_value >> 30) & 0x3) * (1.0f / 0x3);
          } break;
          case xenos::ColorRenderTargetFormat::k_16_16:
          case xenos::ColorRenderTargetFormat::k_16_16_FLOAT: {
            for (uint32_t j = 0; j < 2; ++j) {
              float_constants.color[j] =
                  float((clear_value >> (j * 16)) & 0xFFFF);
            }
          } break;
          case xenos::ColorRenderTargetFormat::k_16_16_16_16:
          case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT: {
            for (uint32_t j = 0; j < 4; ++j) {
              float_constants.color[j] =
                  float((clear_value >> (j * 16)) & 0xFFFF);
            }
          } break;
          case xenos::ColorRenderTargetFormat::k_32_FLOAT: {
            float_constants.color[0] = float(uint32_t(clear_value));
            if (uint64_t(float_constants.color[0]) != uint32_t(clear_value)) {
              clear_via_drawing = true;
            }
          } break;
          case xenos::ColorRenderTargetFormat::k_32_32_FLOAT: {
            float_constants.color[0] = float(uint32_t(clear_value));
            float_constants.color[1] = float(uint32_t(clear_value >> 32));
            if (uint64_t(float_constants.color[0]) !=
                    uint32_t(clear_value) ||
                uint64_t(float_constants.color[1]) !=
                    uint32_t(clear_value >> 32)) {
              clear_via_drawing = true;
            }
          } break;
        }

        MTL::Texture* clear_texture = dest_metal_rt->draw_texture();
        MTL::PixelFormat clear_format =
            GetColorDrawPixelFormat(dest_key.GetColorFormat());

        if (clear_via_drawing) {
          bool clear_is_uint = false;
          MTL::PixelFormat transfer_format =
              GetColorOwnershipTransferPixelFormat(
                  dest_key.GetColorFormat(), &clear_is_uint);
          if (clear_is_uint) {
            clear_use_uint = true;
            clear_texture = dest_metal_rt->transfer_texture();
            clear_format = transfer_format;
            uint_constants.color[0] = uint32_t(clear_value);
            uint_constants.color[1] = uint32_t(clear_value >> 32);
            uint_constants.color[2] = 0;
            uint_constants.color[3] = 0;
          }
        }

        if (clear_texture) {
          MTL::RenderPipelineState* clear_pipeline =
              GetOrCreateTransferClearPipeline(clear_format, clear_use_uint,
                                               false, dest_sample_count);
          if (clear_pipeline) {
            MTL::RenderPassDescriptor* clear_rp =
                MTL::RenderPassDescriptor::renderPassDescriptor();
            auto* ca = clear_rp->colorAttachments()->object(0);
            ca->setTexture(clear_texture);
            ca->setLoadAction(MTL::LoadActionLoad);
            ca->setStoreAction(MTL::StoreActionStore);
            MTL::RenderCommandEncoder* clear_encoder =
                cmd->renderCommandEncoder(clear_rp);
            if (clear_encoder) {
              MTL::DepthStencilState* no_depth_state =
                  GetTransferNoDepthStencilState();
              if (!no_depth_state) {
                clear_encoder->endEncoding();
                continue;
              }
              clear_encoder->setRenderPipelineState(clear_pipeline);
              clear_encoder->setDepthStencilState(no_depth_state);
              if (clear_use_uint) {
                clear_encoder->setFragmentBytes(&uint_constants,
                                                sizeof(uint_constants), 0);
              } else {
                clear_encoder->setFragmentBytes(&float_constants,
                                                sizeof(float_constants), 0);
              }
              Transfer::Rectangle clear_rect = *resolve_clear_rectangle;
              if (set_rect_viewport(clear_encoder, clear_rect)) {
                clear_encoder->drawPrimitives(MTL::PrimitiveTypeTriangle,
                                              NS::UInteger(0),
                                              NS::UInteger(3));
              }
              clear_encoder->endEncoding();
            }
          }
        }
      }
    }
  }

  if (host_depth_store_dispatched) {
    XELOGI(
        "MetalRenderTargetCache::PerformTransfersAndResolveClears: host depth "
        "store dispatched");
  }
  if (any_transfers_done) {
    XELOGI(
        "MetalRenderTargetCache::PerformTransfersAndResolveClears: transfers "
        "encoded");
  }
}

MTL::RenderPipelineState* MetalRenderTargetCache::GetOrCreateTransferPipelines(
    const TransferShaderKey& key, MTL::PixelFormat dest_format,
    bool dest_is_uint) {
  auto it = transfer_pipelines_.find(key);
  if (it != transfer_pipelines_.end()) {
    return it->second;
  }

  const TransferModeInfo& mode_info =
      kTransferModeInfos[size_t(key.mode)];
  TransferOutput output = mode_info.output;
  bool source_is_color = mode_info.source_is_color;
  bool has_host_depth = mode_info.uses_host_depth;

  xenos::ColorRenderTargetFormat source_color_format =
      xenos::ColorRenderTargetFormat(key.source_resource_format);
  xenos::ColorRenderTargetFormat dest_color_format =
      xenos::ColorRenderTargetFormat(key.dest_resource_format);
  xenos::DepthRenderTargetFormat source_depth_format =
      xenos::DepthRenderTargetFormat(key.source_resource_format);
  xenos::DepthRenderTargetFormat dest_depth_format =
      xenos::DepthRenderTargetFormat(key.dest_resource_format);

  bool source_is_uint = false;
  if (source_is_color) {
    GetColorOwnershipTransferPixelFormat(source_color_format, &source_is_uint);
  }

  uint32_t dest_component_count = 1;
  if (output == TransferOutput::kColor) {
    dest_component_count =
        xenos::GetColorRenderTargetFormatComponentCount(dest_color_format);
  }

  bool source_is_multisample =
      key.source_msaa_samples != xenos::MsaaSamples::k1X;
  bool dest_is_multisample =
      key.dest_msaa_samples != xenos::MsaaSamples::k1X;
  bool host_depth_is_copy = key.host_depth_source_is_copy != 0;
  bool host_depth_is_multisample =
      key.host_depth_source_msaa_samples != xenos::MsaaSamples::k1X;
  uint32_t host_depth_texture_index = source_is_color ? 1 : 2;

  auto append_define = [](std::string& source, const char* name,
                          uint32_t value) {
    source.append("#define ");
    source.append(name);
    source.push_back(' ');
    source.append(std::to_string(value));
    source.push_back('\n');
  };

  std::string source;
  source.reserve(16384);
  append_define(source, "XE_TRANSFER_SOURCE_IS_COLOR",
                source_is_color ? 1 : 0);
  append_define(source, "XE_TRANSFER_SOURCE_IS_UINT", source_is_uint ? 1 : 0);
  append_define(source, "XE_TRANSFER_SOURCE_IS_MULTISAMPLE",
                source_is_multisample ? 1 : 0);
  append_define(source, "XE_TRANSFER_DEST_IS_UINT", dest_is_uint ? 1 : 0);
  append_define(source, "XE_TRANSFER_DEST_COMPONENTS", dest_component_count);
  append_define(source, "XE_TRANSFER_DEST_IS_MULTISAMPLE",
                dest_is_multisample ? 1 : 0);
  append_define(source, "XE_TRANSFER_HAS_HOST_DEPTH", has_host_depth ? 1 : 0);
  append_define(source, "XE_TRANSFER_HOST_DEPTH_IS_COPY",
                host_depth_is_copy ? 1 : 0);
  append_define(source, "XE_TRANSFER_HOST_DEPTH_IS_MULTISAMPLE",
                host_depth_is_multisample ? 1 : 0);
  append_define(source, "XE_TRANSFER_SOURCE_TEXTURE_INDEX", 0);
  append_define(source, "XE_TRANSFER_STENCIL_TEXTURE_INDEX", 1);
  append_define(source, "XE_TRANSFER_HOST_DEPTH_TEXTURE_INDEX",
                host_depth_texture_index);
  append_define(source, "XE_TRANSFER_OUTPUT_COLOR",
                output == TransferOutput::kColor ? 1 : 0);
  append_define(source, "XE_TRANSFER_OUTPUT_DEPTH",
                output == TransferOutput::kDepth ? 1 : 0);
  append_define(source, "XE_TRANSFER_OUTPUT_STENCIL_BIT",
                output == TransferOutput::kStencilBit ? 1 : 0);
  append_define(source, "XE_FMT_8_8_8_8",
                uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8));
  append_define(source, "XE_FMT_8_8_8_8_GAMMA",
                uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA));
  append_define(source, "XE_FMT_2_10_10_10",
                uint32_t(xenos::ColorRenderTargetFormat::k_2_10_10_10));
  append_define(
      source, "XE_FMT_2_10_10_10_AS_10_10_10_10",
      uint32_t(xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10));
  append_define(source, "XE_FMT_2_10_10_10_FLOAT",
                uint32_t(xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT));
  append_define(
      source, "XE_FMT_2_10_10_10_FLOAT_AS_16_16_16_16",
      uint32_t(xenos::ColorRenderTargetFormat::
                   k_2_10_10_10_FLOAT_AS_16_16_16_16));
  append_define(source, "XE_FMT_16_16",
                uint32_t(xenos::ColorRenderTargetFormat::k_16_16));
  append_define(source, "XE_FMT_16_16_FLOAT",
                uint32_t(xenos::ColorRenderTargetFormat::k_16_16_FLOAT));
  append_define(source, "XE_FMT_16_16_16_16",
                uint32_t(xenos::ColorRenderTargetFormat::k_16_16_16_16));
  append_define(source, "XE_FMT_16_16_16_16_FLOAT",
                uint32_t(xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT));
  append_define(source, "XE_FMT_32_FLOAT",
                uint32_t(xenos::ColorRenderTargetFormat::k_32_FLOAT));
  append_define(source, "XE_FMT_32_32_FLOAT",
                uint32_t(xenos::ColorRenderTargetFormat::k_32_32_FLOAT));
  append_define(source, "XE_FMT_D24S8",
                uint32_t(xenos::DepthRenderTargetFormat::kD24S8));
  append_define(source, "XE_FMT_D24FS8",
                uint32_t(xenos::DepthRenderTargetFormat::kD24FS8));

  static const char kTransferShaderSource[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct TransferAddressConstants {
  uint dest_pitch;
  uint source_pitch;
  int source_to_dest;
};

struct TransferShaderConstants {
  TransferAddressConstants address;
  TransferAddressConstants host_depth_address;
  uint source_format;
  uint dest_format;
  uint source_is_depth;
  uint dest_is_depth;
  uint source_is_uint;
  uint dest_is_uint;
  uint source_is_64bpp;
  uint dest_is_64bpp;
  uint source_msaa_samples;
  uint dest_msaa_samples;
  uint host_depth_source_msaa_samples;
  uint host_depth_source_is_copy;
  uint depth_round;
  uint msaa_2x_supported;
  uint tile_width_samples;
  uint tile_height_samples;
  uint dest_sample_id;
  uint stencil_mask;
  uint stencil_clear;
};

constant uint kEdramTileCount = 2048u;

inline uint XeBitFieldMask(uint count) {
  if (count >= 32u) {
    return 0xFFFFFFFFu;
  }
  return (1u << count) - 1u;
}

inline uint XeBitFieldInsert(uint base, uint insert, uint offset, uint count) {
  uint mask = XeBitFieldMask(count) << offset;
  return (base & ~mask) | ((insert << offset) & mask);
}

inline uint XeBitFieldExtract(uint value, uint offset, uint count) {
  return (value >> offset) & XeBitFieldMask(count);
}

inline uint XeRoundToNearestEven(float value) {
  float floor_value = floor(value);
  float frac = value - floor_value;
  uint result = uint(floor_value);
  if (frac > 0.5f || (frac == 0.5f && (result & 1u))) {
    result += 1u;
  }
  return result;
}

inline uint XePackUnorm(float value, float scale) {
  return uint(clamp(value, 0.0f, 1.0f) * scale + 0.5f);
}

uint XePreClampedFloat32To7e3(float value) {
  uint f32 = as_type<uint>(value);
  uint biased_f32;
  if (f32 < 0x3E800000u) {
    uint f32_exp = f32 >> 23u;
    uint shift = 125u - f32_exp;
    shift = min(shift, 24u);
    uint mantissa = (f32 & 0x7FFFFFu) | 0x800000u;
    biased_f32 = mantissa >> shift;
  } else {
    biased_f32 = f32 + 0xC2000000u;
  }
  uint round_bit = (biased_f32 >> 16u) & 1u;
  uint f10 = biased_f32 + 0x7FFFu + round_bit;
  return (f10 >> 16u) & 0x3FFu;
}

uint XeUnclampedFloat32To7e3(float value) {
  float clamped = min(max(value, 0.0f), 31.875f);
  return XePreClampedFloat32To7e3(clamped);
}

float XeFloat7e3To32(uint f10) {
  f10 &= 0x3FFu;
  if (f10 == 0u) {
    return 0.0f;
  }
  uint mantissa = f10 & 0x7Fu;
  uint exponent = f10 >> 7u;
  if (exponent == 0u) {
    uint mantissa_lzcnt = clz(mantissa) - 24u;
    exponent = uint(int(1) - int(mantissa_lzcnt));
    mantissa = (mantissa << mantissa_lzcnt) & 0x7Fu;
  }
  uint f32 = ((exponent + 124u) << 23u) | (mantissa << 3u);
  return as_type<float>(f32);
}

uint XeFloat32To20e4(float value, bool round_to_nearest_even) {
  uint f32 = as_type<uint>(value);
  f32 = min((f32 <= 0x7FFFFFFFu) ? f32 : 0u, 0x3FFFFFF8u);
  uint denormalized =
      ((f32 & 0x7FFFFFu) | 0x800000u) >> min(113u - (f32 >> 23u), 24u);
  uint f24 = (f32 < 0x38800000u) ? denormalized : (f32 + 0xC8000000u);
  if (round_to_nearest_even) {
    f24 += 3u + ((f24 >> 3u) & 1u);
  }
  return (f24 >> 3u) & 0xFFFFFFu;
}

float XeFloat20e4To32(uint f24, bool remap_to_0_to_0_5) {
  if (f24 == 0u) {
    return 0.0f;
  }
  uint mantissa = f24 & 0xFFFFFu;
  uint exponent = f24 >> 20u;
  if (exponent == 0u) {
    uint msb = 31u - clz(mantissa);
    uint mantissa_lzcnt = 20u - msb;
    exponent = 1u - mantissa_lzcnt;
    mantissa = (mantissa << mantissa_lzcnt) & 0xFFFFFu;
  }
  uint bias = remap_to_0_to_0_5 ? 111u : 112u;
  uint f32 = ((exponent + bias) << 23u) | (mantissa << 3u);
  return as_type<float>(f32);
}

float XeUnorm24To32(uint n24) {
  return float(n24 + (n24 >> 23u)) * (1.0f / 16777216.0f);
}

uint XePackColorRGBA8(float4 color) {
  uint r = XePackUnorm(color.r, 255.0f);
  uint g = XePackUnorm(color.g, 255.0f);
  uint b = XePackUnorm(color.b, 255.0f);
  uint a = XePackUnorm(color.a, 255.0f);
  return r | (g << 8u) | (b << 16u) | (a << 24u);
}

uint XePackColorRGB10A2(float4 color) {
  uint r = XePackUnorm(color.r, 1023.0f);
  uint g = XePackUnorm(color.g, 1023.0f);
  uint b = XePackUnorm(color.b, 1023.0f);
  uint a = XePackUnorm(color.a, 3.0f);
  return r | (g << 10u) | (b << 20u) | (a << 30u);
}

uint XePackColorRGB10A2Float(float4 color) {
  uint r = XeUnclampedFloat32To7e3(color.r);
  uint g = XeUnclampedFloat32To7e3(color.g);
  uint b = XeUnclampedFloat32To7e3(color.b);
  uint a = XePackUnorm(color.a, 3.0f);
  return (r & 0x3FFu) | ((g & 0x3FFu) << 10u) |
         ((b & 0x3FFu) << 20u) | ((a & 0x3u) << 30u);
}

struct VSOut {
  float4 position [[position]];
};

vertex VSOut transfer_vs(uint vid [[vertex_id]]) {
  float2 pt = float2((vid << 1) & 2, vid & 2);
  VSOut out;
  out.position = float4(pt * 2.0f - 1.0f, 0.0f, 1.0f);
  return out;
}

#if XE_TRANSFER_SOURCE_IS_COLOR
  #if XE_TRANSFER_SOURCE_IS_UINT
    #if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
      #define XE_TRANSFER_SOURCE_PARAMS \
          , texture2d_ms<uint, access::read> xe_transfer_source \
              [[texture(XE_TRANSFER_SOURCE_TEXTURE_INDEX)]]
    #else
      #define XE_TRANSFER_SOURCE_PARAMS \
          , texture2d<uint, access::read> xe_transfer_source \
              [[texture(XE_TRANSFER_SOURCE_TEXTURE_INDEX)]]
    #endif
  #else
    #if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
      #define XE_TRANSFER_SOURCE_PARAMS \
          , texture2d_ms<float, access::read> xe_transfer_source \
              [[texture(XE_TRANSFER_SOURCE_TEXTURE_INDEX)]]
    #else
      #define XE_TRANSFER_SOURCE_PARAMS \
          , texture2d<float, access::read> xe_transfer_source \
              [[texture(XE_TRANSFER_SOURCE_TEXTURE_INDEX)]]
    #endif
  #endif
#else
  #if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
    #define XE_TRANSFER_SOURCE_PARAMS \
        , texture2d_ms<float, access::read> xe_transfer_source_depth \
            [[texture(XE_TRANSFER_SOURCE_TEXTURE_INDEX)]], \
          texture2d_ms<uint, access::read> xe_transfer_source_stencil \
            [[texture(XE_TRANSFER_STENCIL_TEXTURE_INDEX)]]
  #else
    #define XE_TRANSFER_SOURCE_PARAMS \
        , texture2d<float, access::read> xe_transfer_source_depth \
            [[texture(XE_TRANSFER_SOURCE_TEXTURE_INDEX)]], \
          texture2d<uint, access::read> xe_transfer_source_stencil \
            [[texture(XE_TRANSFER_STENCIL_TEXTURE_INDEX)]]
  #endif
#endif

#if XE_TRANSFER_HAS_HOST_DEPTH && XE_TRANSFER_HOST_DEPTH_IS_COPY
  #define XE_TRANSFER_HOST_DEPTH_BUFFER_PARAM \
      , device const uint* xe_transfer_host_depth_buffer [[buffer(1)]]
#else
  #define XE_TRANSFER_HOST_DEPTH_BUFFER_PARAM
#endif

#if XE_TRANSFER_HAS_HOST_DEPTH && !XE_TRANSFER_HOST_DEPTH_IS_COPY
  #if XE_TRANSFER_HOST_DEPTH_IS_MULTISAMPLE
    #define XE_TRANSFER_HOST_DEPTH_TEXTURE_PARAM \
        , texture2d_ms<float, access::read> xe_transfer_host_depth \
            [[texture(XE_TRANSFER_HOST_DEPTH_TEXTURE_INDEX)]]
  #else
    #define XE_TRANSFER_HOST_DEPTH_TEXTURE_PARAM \
        , texture2d<float, access::read> xe_transfer_host_depth \
            [[texture(XE_TRANSFER_HOST_DEPTH_TEXTURE_INDEX)]]
  #endif
#else
  #define XE_TRANSFER_HOST_DEPTH_TEXTURE_PARAM
#endif

#define XE_TRANSFER_SAMPLE_ID_PARAM

#if XE_TRANSFER_OUTPUT_COLOR
  #if XE_TRANSFER_DEST_IS_UINT
    #if XE_TRANSFER_DEST_COMPONENTS == 1
      typedef uint XeTransferColorOutType;
    #elif XE_TRANSFER_DEST_COMPONENTS == 2
      typedef uint2 XeTransferColorOutType;
    #else
      typedef uint4 XeTransferColorOutType;
    #endif
  #else
    #if XE_TRANSFER_DEST_COMPONENTS == 1
      typedef float XeTransferColorOutType;
    #elif XE_TRANSFER_DEST_COMPONENTS == 2
      typedef float2 XeTransferColorOutType;
    #else
      typedef float4 XeTransferColorOutType;
    #endif
  #endif

struct TransferColorOut {
  XeTransferColorOutType color [[color(0)]];
#if XE_TRANSFER_DEST_IS_MULTISAMPLE
  uint sample_mask [[sample_mask]];
#endif
};

fragment TransferColorOut transfer_ps(
    VSOut in [[stage_in]],
    constant TransferShaderConstants& constants [[buffer(0)]]
    XE_TRANSFER_HOST_DEPTH_BUFFER_PARAM
    XE_TRANSFER_SOURCE_PARAMS
    XE_TRANSFER_HOST_DEPTH_TEXTURE_PARAM
    XE_TRANSFER_SAMPLE_ID_PARAM) {
  uint2 dest_pixel = uint2(in.position.xy);
  uint dest_sample_id = 0u;
#if XE_TRANSFER_DEST_IS_MULTISAMPLE
  dest_sample_id = constants.dest_sample_id;
#endif

  uint tile_width_samples = constants.tile_width_samples;
  uint tile_height_samples = constants.tile_height_samples;
  uint dest_tile_width_pixels =
      tile_width_samples >>
      ((constants.dest_is_64bpp != 0u) +
       (constants.dest_msaa_samples >= 4u ? 1u : 0u));
  uint dest_tile_height_pixels =
      tile_height_samples >> (constants.dest_msaa_samples >= 2u ? 1u : 0u);

  uint dest_tile_index_x = dest_pixel.x / dest_tile_width_pixels;
  uint dest_tile_pixel_x = dest_pixel.x % dest_tile_width_pixels;
  uint dest_tile_index_y = dest_pixel.y / dest_tile_height_pixels;
  uint dest_tile_pixel_y = dest_pixel.y % dest_tile_height_pixels;

  uint dest_tile_index =
      dest_tile_index_x +
      dest_tile_index_y * constants.address.dest_pitch;

  uint source_sample_id = dest_sample_id;
  uint source_tile_pixel_x = dest_tile_pixel_x;
  uint source_tile_pixel_y = dest_tile_pixel_y;
  uint source_color_half = 0u;
  bool source_color_half_valid = false;

  bool source_is_64bpp = constants.source_is_64bpp != 0u;
  bool dest_is_64bpp = constants.dest_is_64bpp != 0u;
  uint source_msaa = constants.source_msaa_samples;
  uint dest_msaa = constants.dest_msaa_samples;
  bool msaa_2x_supported = constants.msaa_2x_supported != 0u;

  if (!source_is_64bpp && dest_is_64bpp) {
    if (source_msaa >= 4u) {
      if (dest_msaa >= 4u) {
        source_sample_id = dest_sample_id & 2u;
        source_tile_pixel_x =
            XeBitFieldInsert(dest_sample_id, dest_tile_pixel_x, 1u, 31u);
      } else if (dest_msaa == 2u) {
        if (msaa_2x_supported) {
          source_sample_id = (dest_sample_id ^ 1u) << 1u;
        } else {
          source_sample_id = dest_sample_id & 2u;
        }
      } else {
        source_sample_id =
            XeBitFieldInsert(0u, dest_tile_pixel_y, 1u, 1u);
        source_tile_pixel_y = dest_tile_pixel_y >> 1u;
      }
    } else {
      if (dest_msaa >= 4u) {
        source_tile_pixel_x = XeBitFieldInsert(
            dest_tile_pixel_x << 2u, dest_sample_id, 1u, 1u);
      } else {
        source_tile_pixel_x = dest_tile_pixel_x << 1u;
      }
    }
  } else if (source_is_64bpp && !dest_is_64bpp) {
    if (dest_msaa >= 4u) {
      if (source_msaa >= 4u) {
        source_sample_id =
            XeBitFieldInsert(dest_sample_id, dest_tile_pixel_x, 0u, 1u);
        source_tile_pixel_x = dest_tile_pixel_x >> 1u;
      }
      source_color_half = dest_sample_id & 1u;
      source_color_half_valid = true;
    } else {
      if (source_msaa >= 4u) {
        source_sample_id = XeBitFieldExtract(dest_tile_pixel_x, 1u, 1u);
        if (dest_msaa == 2u) {
          if (msaa_2x_supported) {
            source_sample_id = XeBitFieldInsert(
                source_sample_id, dest_sample_id ^ 1u, 1u, 1u);
          } else {
            source_sample_id = XeBitFieldInsert(
                dest_sample_id, source_sample_id, 0u, 1u);
          }
        } else {
          source_sample_id = XeBitFieldInsert(
              source_sample_id, dest_tile_pixel_y, 1u, 1u);
          source_tile_pixel_y = dest_tile_pixel_y >> 1u;
        }
        source_tile_pixel_x = dest_tile_pixel_x >> 2u;
      } else {
        source_tile_pixel_x = dest_tile_pixel_x >> 1u;
      }
      source_color_half = dest_tile_pixel_x & 1u;
      source_color_half_valid = true;
    }
  } else {
    if (source_msaa != dest_msaa) {
      if (source_msaa >= 4u) {
        if (dest_msaa == 2u) {
          if (msaa_2x_supported) {
            source_sample_id = XeBitFieldInsert(
                dest_tile_pixel_x, dest_sample_id ^ 1u, 1u, 31u);
          } else {
            source_sample_id = XeBitFieldInsert(
                dest_sample_id, dest_tile_pixel_x, 0u, 1u);
          }
          source_tile_pixel_x = dest_tile_pixel_x >> 1u;
        } else {
          source_sample_id = XeBitFieldInsert(
              dest_tile_pixel_x & 1u, dest_tile_pixel_y, 1u, 1u);
          source_tile_pixel_x = dest_tile_pixel_x >> 1u;
          source_tile_pixel_y = dest_tile_pixel_y >> 1u;
        }
      } else if (dest_msaa >= 4u) {
        source_tile_pixel_x = XeBitFieldInsert(
            dest_sample_id, dest_tile_pixel_x, 1u, 31u);
      }
    }
  }

  if (source_msaa < 4u && source_msaa != dest_msaa) {
    if (dest_msaa >= 4u) {
      if (source_msaa == 2u) {
        source_sample_id = dest_sample_id >> 1u;
        if (msaa_2x_supported) {
          source_sample_id ^= 1u;
        } else {
          source_sample_id = XeBitFieldInsert(
              source_sample_id, source_sample_id, 1u, 1u);
        }
      } else {
        source_tile_pixel_y = XeBitFieldInsert(
            dest_sample_id >> 1u, dest_tile_pixel_y, 1u, 31u);
      }
    } else {
      if (source_msaa == 2u) {
        source_sample_id = dest_tile_pixel_y & 1u;
        if (msaa_2x_supported) {
          source_sample_id ^= 1u;
        } else {
          source_sample_id = XeBitFieldInsert(
              source_sample_id, source_sample_id, 1u, 1u);
        }
        source_tile_pixel_y = dest_tile_pixel_y >> 1u;
      } else {
        if (msaa_2x_supported) {
          source_tile_pixel_y = XeBitFieldInsert(
              dest_sample_id ^ 1u, dest_tile_pixel_y, 1u, 31u);
        } else {
          source_tile_pixel_y = XeBitFieldInsert(
              dest_sample_id >> 1u, dest_tile_pixel_y, 1u, 31u);
        }
      }
    }
  }

  uint source_pixel_width_dwords_log2 =
      (source_msaa >= 4u ? 1u : 0u) + (source_is_64bpp ? 1u : 0u);

  if ((constants.source_is_depth != 0u) != (constants.dest_is_depth != 0u)) {
    uint source_32bpp_tile_half_pixels =
        tile_width_samples >> (1u + source_pixel_width_dwords_log2);
    if (source_tile_pixel_x < source_32bpp_tile_half_pixels) {
      source_tile_pixel_x += source_32bpp_tile_half_pixels;
    } else {
      source_tile_pixel_x -= source_32bpp_tile_half_pixels;
    }
  }

  uint source_tile_index =
      uint(int(dest_tile_index) + constants.address.source_to_dest) &
      (kEdramTileCount - 1u);
  uint source_pitch_tiles = constants.address.source_pitch;
  uint source_tile_index_y = source_tile_index / source_pitch_tiles;
  uint source_tile_index_x = source_tile_index % source_pitch_tiles;

  uint source_pixel_x =
      source_tile_index_x *
          (tile_width_samples >> source_pixel_width_dwords_log2) +
      source_tile_pixel_x;
  uint source_pixel_y =
      source_tile_index_y *
          (tile_height_samples >> (source_msaa >= 2u ? 1u : 0u)) +
      source_tile_pixel_y;

  bool load_two = !source_is_64bpp && dest_is_64bpp;
  uint source_pixel_x1 = source_pixel_x;
  uint source_sample_id1 = source_sample_id;
  if (load_two) {
    if (source_msaa >= 4u) {
      source_sample_id1 = source_sample_id | 1u;
    } else {
      source_pixel_x1 = source_pixel_x | 1u;
    }
  }

#if XE_TRANSFER_SOURCE_IS_COLOR
  #if XE_TRANSFER_SOURCE_IS_UINT
  uint4 source_color0 =
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
      xe_transfer_source.read(uint2(source_pixel_x, source_pixel_y),
                              source_sample_id);
#else
      xe_transfer_source.read(uint2(source_pixel_x, source_pixel_y));
#endif
  uint4 source_color1 = source_color0;
  if (load_two) {
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
    source_color1 = xe_transfer_source.read(
        uint2(source_pixel_x1, source_pixel_y), source_sample_id1);
#else
    source_color1 = xe_transfer_source.read(
        uint2(source_pixel_x1, source_pixel_y));
#endif
  }
  #else
  float4 source_color0 =
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
      xe_transfer_source.read(uint2(source_pixel_x, source_pixel_y),
                              source_sample_id);
#else
      xe_transfer_source.read(uint2(source_pixel_x, source_pixel_y));
#endif
  float4 source_color1 = source_color0;
  if (load_two) {
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
    source_color1 = xe_transfer_source.read(
        uint2(source_pixel_x1, source_pixel_y), source_sample_id1);
#else
    source_color1 = xe_transfer_source.read(
        uint2(source_pixel_x1, source_pixel_y));
#endif
  }
  #endif
#else
  float source_depth0 =
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
      xe_transfer_source_depth.read(uint2(source_pixel_x, source_pixel_y),
                                    source_sample_id).r;
#else
      xe_transfer_source_depth.read(uint2(source_pixel_x, source_pixel_y)).r;
#endif
  float source_depth1 = source_depth0;
  if (load_two) {
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
    source_depth1 = xe_transfer_source_depth.read(
        uint2(source_pixel_x1, source_pixel_y), source_sample_id1).r;
#else
    source_depth1 = xe_transfer_source_depth.read(
        uint2(source_pixel_x1, source_pixel_y)).r;
#endif
  }
  uint source_stencil0 =
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
      xe_transfer_source_stencil.read(uint2(source_pixel_x, source_pixel_y),
                                      source_sample_id).r;
#else
      xe_transfer_source_stencil.read(uint2(source_pixel_x, source_pixel_y)).r;
#endif
  uint source_stencil1 = source_stencil0;
  if (load_two) {
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
    source_stencil1 = xe_transfer_source_stencil.read(
        uint2(source_pixel_x1, source_pixel_y), source_sample_id1).r;
#else
    source_stencil1 = xe_transfer_source_stencil.read(
        uint2(source_pixel_x1, source_pixel_y)).r;
#endif
  }
#endif

#if XE_TRANSFER_SOURCE_IS_COLOR
  if (source_is_64bpp && !dest_is_64bpp && source_color_half_valid) {
    uint source_component_count = 0u;
    switch (constants.source_format) {
      case XE_FMT_32_FLOAT:
        source_component_count = 1u;
        break;
      case XE_FMT_16_16:
      case XE_FMT_16_16_FLOAT:
      case XE_FMT_32_32_FLOAT:
        source_component_count = 2u;
        break;
      default:
        source_component_count = 4u;
        break;
    }
    if (source_component_count == 2u) {
  #if XE_TRANSFER_SOURCE_IS_UINT
      source_color0[0] = source_color_half != 0u ? source_color0[1]
                                                 : source_color0[0];
  #else
      source_color0[0] = source_color_half != 0u ? source_color0[1]
                                                 : source_color0[0];
  #endif
    } else if (source_color_half != 0u) {
  #if XE_TRANSFER_SOURCE_IS_UINT
      source_color0[0] = source_color0[2];
      source_color0[1] = source_color0[3];
  #else
      source_color0[0] = source_color0[2];
      source_color0[1] = source_color0[3];
  #endif
    }
  }
#endif

#if XE_TRANSFER_DEST_IS_UINT
  uint4 out_color = uint4(0u);
#else
  float4 out_color = float4(0.0f);
#endif

  if (dest_is_64bpp) {
    uint2 packed64 = uint2(0u);
#if XE_TRANSFER_SOURCE_IS_COLOR
  #if XE_TRANSFER_SOURCE_IS_UINT
    switch (constants.source_format) {
      case XE_FMT_16_16:
      case XE_FMT_16_16_FLOAT:
        packed64.x = source_color0[0] | (source_color0[1] << 16u);
        packed64.y = source_color1[0] | (source_color1[1] << 16u);
        break;
      case XE_FMT_16_16_16_16:
      case XE_FMT_16_16_16_16_FLOAT:
        packed64.x = source_color0[0] | (source_color0[1] << 16u);
        packed64.y = source_color0[2] | (source_color0[3] << 16u);
        break;
      case XE_FMT_32_FLOAT:
        packed64.x = source_color0[0];
        packed64.y = source_color1[0];
        break;
      case XE_FMT_32_32_FLOAT:
        packed64.x = source_color0[0];
        packed64.y = source_color0[1];
        break;
      default:
        packed64.x = source_color0[0];
        packed64.y = source_color1[0];
        break;
    }
  #else
    switch (constants.source_format) {
      case XE_FMT_8_8_8_8:
      case XE_FMT_8_8_8_8_GAMMA:
        packed64.x = XePackColorRGBA8(source_color0);
        packed64.y = XePackColorRGBA8(source_color1);
        break;
      case XE_FMT_2_10_10_10:
      case XE_FMT_2_10_10_10_AS_10_10_10_10:
        packed64.x = XePackColorRGB10A2(source_color0);
        packed64.y = XePackColorRGB10A2(source_color1);
        break;
      case XE_FMT_2_10_10_10_FLOAT:
      case XE_FMT_2_10_10_10_FLOAT_AS_16_16_16_16:
        packed64.x = XePackColorRGB10A2Float(source_color0);
        packed64.y = XePackColorRGB10A2Float(source_color1);
        break;
      case XE_FMT_32_FLOAT:
        packed64.x = as_type<uint>(source_color0[0]);
        packed64.y = as_type<uint>(source_color1[0]);
        break;
      case XE_FMT_32_32_FLOAT:
        packed64.x = as_type<uint>(source_color0[0]);
        packed64.y = as_type<uint>(source_color0[1]);
        break;
      default:
        packed64.x = as_type<uint>(source_color0[0]);
        packed64.y = as_type<uint>(source_color1[0]);
        break;
    }
  #endif
#else
    uint depth24_0 = 0u;
    uint depth24_1 = 0u;
    if (constants.source_format == XE_FMT_D24FS8) {
      bool round_depth = constants.depth_round != 0u;
      depth24_0 = XeFloat32To20e4(source_depth0 * 2.0f, round_depth);
      depth24_1 = XeFloat32To20e4(source_depth1 * 2.0f, round_depth);
    } else {
      depth24_0 = XeRoundToNearestEven(
          clamp(source_depth0, 0.0f, 1.0f) * 16777215.0f);
      depth24_1 = XeRoundToNearestEven(
          clamp(source_depth1, 0.0f, 1.0f) * 16777215.0f);
    }
    packed64.x = (depth24_0 << 8u) | (source_stencil0 & 0xFFu);
    packed64.y = (depth24_1 << 8u) | (source_stencil1 & 0xFFu);
#endif

    if (constants.dest_format == XE_FMT_32_32_FLOAT) {
#if XE_TRANSFER_DEST_IS_UINT
      out_color = uint4(packed64.x, packed64.y, 0u, 0u);
#else
      out_color = float4(as_type<float>(packed64.x),
                         as_type<float>(packed64.y), 0.0f, 0.0f);
#endif
    } else {
      uint4 components = uint4(packed64.x & 0xFFFFu, packed64.x >> 16u,
                               packed64.y & 0xFFFFu, packed64.y >> 16u);
#if XE_TRANSFER_DEST_IS_UINT
      out_color = components;
#else
      out_color = float4(components);
#endif
    }
  } else {
    bool wrote_direct = false;
    uint packed32 = 0u;
#if XE_TRANSFER_SOURCE_IS_COLOR
  #if XE_TRANSFER_SOURCE_IS_UINT
    switch (constants.source_format) {
      case XE_FMT_16_16:
      case XE_FMT_16_16_FLOAT:
        if (constants.dest_format == XE_FMT_16_16 ||
            constants.dest_format == XE_FMT_16_16_FLOAT) {
#if XE_TRANSFER_DEST_IS_UINT
          out_color = uint4(source_color0[0], source_color0[1], 0u, 0u);
#else
          out_color = float4(float(source_color0[0]),
                             float(source_color0[1]), 0.0f, 0.0f);
#endif
          wrote_direct = true;
        } else {
          packed32 = source_color0[0] | (source_color0[1] << 16u);
        }
        break;
      case XE_FMT_16_16_16_16:
      case XE_FMT_16_16_16_16_FLOAT:
        if (constants.dest_format == XE_FMT_16_16 ||
            constants.dest_format == XE_FMT_16_16_FLOAT) {
#if XE_TRANSFER_DEST_IS_UINT
          out_color = uint4(source_color0[0], source_color0[1], 0u, 0u);
#else
          out_color = float4(float(source_color0[0]),
                             float(source_color0[1]), 0.0f, 0.0f);
#endif
          wrote_direct = true;
        } else {
          packed32 = source_color0[0] | (source_color0[1] << 16u);
        }
        break;
      case XE_FMT_32_FLOAT:
      case XE_FMT_32_32_FLOAT:
        packed32 = source_color0[0];
        break;
      default:
        packed32 = source_color0[0];
        break;
    }
  #else
    switch (constants.source_format) {
      case XE_FMT_8_8_8_8:
      case XE_FMT_8_8_8_8_GAMMA:
#if !XE_TRANSFER_DEST_IS_UINT
        if (constants.dest_format == XE_FMT_8_8_8_8 ||
            constants.dest_format == XE_FMT_8_8_8_8_GAMMA) {
          out_color = source_color0;
          wrote_direct = true;
        } else
#endif
        {
          uint packed_component_offset = 0u;
          if (constants.dest_is_depth != 0u) {
            packed_component_offset = 1u;
          }
          packed32 = XePackUnorm(
              source_color0[packed_component_offset], 255.0f);
          if (constants.dest_is_depth == 0u) {
            packed32 |= XePackUnorm(source_color0[packed_component_offset + 1],
                                    255.0f) << 8u;
            packed32 |= XePackUnorm(source_color0[packed_component_offset + 2],
                                    255.0f) << 16u;
            packed32 |= XePackUnorm(source_color0[packed_component_offset + 3],
                                    255.0f) << 24u;
          }
        }
        break;
      case XE_FMT_2_10_10_10:
      case XE_FMT_2_10_10_10_AS_10_10_10_10:
#if !XE_TRANSFER_DEST_IS_UINT
        if (constants.dest_format == XE_FMT_2_10_10_10 ||
            constants.dest_format == XE_FMT_2_10_10_10_AS_10_10_10_10) {
          out_color = source_color0;
          wrote_direct = true;
        } else
#endif
        {
          packed32 = XePackUnorm(source_color0[0], 1023.0f);
          if (constants.dest_is_depth == 0u) {
            packed32 |= XePackUnorm(source_color0[1], 1023.0f) << 10u;
            packed32 |= XePackUnorm(source_color0[2], 1023.0f) << 20u;
            packed32 |= XePackUnorm(source_color0[3], 3.0f) << 30u;
          }
        }
        break;
      case XE_FMT_2_10_10_10_FLOAT:
      case XE_FMT_2_10_10_10_FLOAT_AS_16_16_16_16:
#if !XE_TRANSFER_DEST_IS_UINT
        if (constants.dest_format == XE_FMT_2_10_10_10_FLOAT ||
            constants.dest_format == XE_FMT_2_10_10_10_FLOAT_AS_16_16_16_16) {
          out_color = source_color0;
          wrote_direct = true;
        } else
#endif
        {
          packed32 = XeUnclampedFloat32To7e3(source_color0[0]);
          if (constants.dest_is_depth == 0u) {
            packed32 |= XeUnclampedFloat32To7e3(source_color0[1]) << 10u;
            packed32 |= XeUnclampedFloat32To7e3(source_color0[2]) << 20u;
            packed32 |= XePackUnorm(source_color0[3], 3.0f) << 30u;
          }
        }
        break;
      case XE_FMT_32_FLOAT:
      case XE_FMT_32_32_FLOAT:
        packed32 = as_type<uint>(source_color0[0]);
        break;
      default:
        packed32 = as_type<uint>(source_color0[0]);
        break;
    }
  #endif
#else
    if (constants.dest_is_depth != 0u &&
        constants.dest_format == constants.source_format) {
      TransferColorOut out;
      out.color = XeTransferColorOutType(source_depth0);
#if XE_TRANSFER_DEST_IS_MULTISAMPLE
      out.sample_mask = 1u << dest_sample_id;
#endif
      return out;
    }
    if (constants.source_format == XE_FMT_D24FS8) {
      bool round_depth = constants.depth_round != 0u;
      packed32 = XeFloat32To20e4(source_depth0 * 2.0f, round_depth);
    } else {
      packed32 = XeRoundToNearestEven(
          clamp(source_depth0, 0.0f, 1.0f) * 16777215.0f);
    }
    if (constants.dest_is_depth == 0u) {
      packed32 = (packed32 << 8u) | (source_stencil0 & 0xFFu);
    }
#endif

    if (!wrote_direct) {
      switch (constants.dest_format) {
        case XE_FMT_8_8_8_8:
        case XE_FMT_8_8_8_8_GAMMA: {
#if XE_TRANSFER_DEST_IS_UINT
          out_color = uint4(packed32, 0u, 0u, 0u);
#else
          out_color = float4(
              float((packed32 >> 0u) & 0xFFu) * (1.0f / 255.0f),
              float((packed32 >> 8u) & 0xFFu) * (1.0f / 255.0f),
              float((packed32 >> 16u) & 0xFFu) * (1.0f / 255.0f),
              float((packed32 >> 24u) & 0xFFu) * (1.0f / 255.0f));
#endif
        } break;
        case XE_FMT_2_10_10_10:
        case XE_FMT_2_10_10_10_AS_10_10_10_10: {
#if XE_TRANSFER_DEST_IS_UINT
          out_color = uint4(packed32, 0u, 0u, 0u);
#else
          out_color = float4(
              float((packed32 >> 0u) & 0x3FFu) * (1.0f / 1023.0f),
              float((packed32 >> 10u) & 0x3FFu) * (1.0f / 1023.0f),
              float((packed32 >> 20u) & 0x3FFu) * (1.0f / 1023.0f),
              float((packed32 >> 30u) & 0x3u) * (1.0f / 3.0f));
#endif
        } break;
        case XE_FMT_2_10_10_10_FLOAT:
        case XE_FMT_2_10_10_10_FLOAT_AS_16_16_16_16: {
#if XE_TRANSFER_DEST_IS_UINT
          out_color = uint4(packed32, 0u, 0u, 0u);
#else
          out_color = float4(
              XeFloat7e3To32((packed32 >> 0u) & 0x3FFu),
              XeFloat7e3To32((packed32 >> 10u) & 0x3FFu),
              XeFloat7e3To32((packed32 >> 20u) & 0x3FFu),
              float((packed32 >> 30u) & 0x3u) * (1.0f / 3.0f));
#endif
        } break;
        case XE_FMT_16_16:
        case XE_FMT_16_16_FLOAT: {
#if XE_TRANSFER_DEST_IS_UINT
          out_color = uint4(packed32 & 0xFFFFu, packed32 >> 16u, 0u, 0u);
#else
          out_color = float4(float(packed32 & 0xFFFFu),
                             float(packed32 >> 16u), 0.0f, 0.0f);
#endif
        } break;
        case XE_FMT_32_FLOAT: {
#if XE_TRANSFER_DEST_IS_UINT
          out_color = uint4(packed32, 0u, 0u, 0u);
#else
          out_color = float4(as_type<float>(packed32), 0.0f, 0.0f, 0.0f);
#endif
        } break;
        default:
          break;
      }
    }
  }

  TransferColorOut out;
#if XE_TRANSFER_DEST_COMPONENTS == 1
  out.color = out_color.x;
#elif XE_TRANSFER_DEST_COMPONENTS == 2
  out.color = out_color.xy;
#else
  out.color = out_color;
#endif
#if XE_TRANSFER_DEST_IS_MULTISAMPLE
  out.sample_mask = 1u << dest_sample_id;
#endif
  return out;
}
#elif XE_TRANSFER_OUTPUT_DEPTH || XE_TRANSFER_OUTPUT_STENCIL_BIT
struct TransferDepthOut {
  float depth [[depth(any)]];
#if XE_TRANSFER_DEST_IS_MULTISAMPLE
  uint sample_mask [[sample_mask]];
#endif
};

fragment TransferDepthOut transfer_ps(
    VSOut in [[stage_in]],
    constant TransferShaderConstants& constants [[buffer(0)]]
    XE_TRANSFER_HOST_DEPTH_BUFFER_PARAM
    XE_TRANSFER_SOURCE_PARAMS
    XE_TRANSFER_HOST_DEPTH_TEXTURE_PARAM
    XE_TRANSFER_SAMPLE_ID_PARAM) {
  uint2 dest_pixel = uint2(in.position.xy);
  uint dest_sample_id = 0u;
#if XE_TRANSFER_DEST_IS_MULTISAMPLE
  dest_sample_id = constants.dest_sample_id;
#endif

  uint tile_width_samples = constants.tile_width_samples;
  uint tile_height_samples = constants.tile_height_samples;
  uint dest_tile_width_pixels =
      tile_width_samples >>
      ((constants.dest_is_64bpp != 0u) +
       (constants.dest_msaa_samples >= 4u ? 1u : 0u));
  uint dest_tile_height_pixels =
      tile_height_samples >> (constants.dest_msaa_samples >= 2u ? 1u : 0u);

  uint dest_tile_index_x = dest_pixel.x / dest_tile_width_pixels;
  uint dest_tile_pixel_x = dest_pixel.x % dest_tile_width_pixels;
  uint dest_tile_index_y = dest_pixel.y / dest_tile_height_pixels;
  uint dest_tile_pixel_y = dest_pixel.y % dest_tile_height_pixels;

  uint dest_tile_index =
      dest_tile_index_x +
      dest_tile_index_y * constants.address.dest_pitch;

  uint source_sample_id = dest_sample_id;
  uint source_tile_pixel_x = dest_tile_pixel_x;
  uint source_tile_pixel_y = dest_tile_pixel_y;

  bool source_is_64bpp = constants.source_is_64bpp != 0u;
  bool dest_is_64bpp = constants.dest_is_64bpp != 0u;
  uint source_msaa = constants.source_msaa_samples;
  uint dest_msaa = constants.dest_msaa_samples;
  bool msaa_2x_supported = constants.msaa_2x_supported != 0u;

  if (!source_is_64bpp && dest_is_64bpp) {
    if (source_msaa >= 4u) {
      if (dest_msaa >= 4u) {
        source_sample_id = dest_sample_id & 2u;
        source_tile_pixel_x =
            XeBitFieldInsert(dest_sample_id, dest_tile_pixel_x, 1u, 31u);
      } else if (dest_msaa == 2u) {
        if (msaa_2x_supported) {
          source_sample_id = (dest_sample_id ^ 1u) << 1u;
        } else {
          source_sample_id = dest_sample_id & 2u;
        }
      } else {
        source_sample_id =
            XeBitFieldInsert(0u, dest_tile_pixel_y, 1u, 1u);
        source_tile_pixel_y = dest_tile_pixel_y >> 1u;
      }
    } else {
      if (dest_msaa >= 4u) {
        source_tile_pixel_x = XeBitFieldInsert(
            dest_tile_pixel_x << 2u, dest_sample_id, 1u, 1u);
      } else {
        source_tile_pixel_x = dest_tile_pixel_x << 1u;
      }
    }
  } else if (source_is_64bpp && !dest_is_64bpp) {
    if (dest_msaa >= 4u) {
      if (source_msaa >= 4u) {
        source_sample_id =
            XeBitFieldInsert(dest_sample_id, dest_tile_pixel_x, 0u, 1u);
        source_tile_pixel_x = dest_tile_pixel_x >> 1u;
      }
    } else {
      if (source_msaa >= 4u) {
        source_sample_id = XeBitFieldExtract(dest_tile_pixel_x, 1u, 1u);
        if (dest_msaa == 2u) {
          if (msaa_2x_supported) {
            source_sample_id = XeBitFieldInsert(
                source_sample_id, dest_sample_id ^ 1u, 1u, 1u);
          } else {
            source_sample_id = XeBitFieldInsert(
                dest_sample_id, source_sample_id, 0u, 1u);
          }
        } else {
          source_sample_id = XeBitFieldInsert(
              source_sample_id, dest_tile_pixel_y, 1u, 1u);
          source_tile_pixel_y = dest_tile_pixel_y >> 1u;
        }
        source_tile_pixel_x = dest_tile_pixel_x >> 2u;
      } else {
        source_tile_pixel_x = dest_tile_pixel_x >> 1u;
      }
    }
  } else {
    if (source_msaa != dest_msaa) {
      if (source_msaa >= 4u) {
        if (dest_msaa == 2u) {
          if (msaa_2x_supported) {
            source_sample_id = XeBitFieldInsert(
                dest_tile_pixel_x, dest_sample_id ^ 1u, 1u, 31u);
          } else {
            source_sample_id = XeBitFieldInsert(
                dest_sample_id, dest_tile_pixel_x, 0u, 1u);
          }
          source_tile_pixel_x = dest_tile_pixel_x >> 1u;
        } else {
          source_sample_id = XeBitFieldInsert(
              dest_tile_pixel_x & 1u, dest_tile_pixel_y, 1u, 1u);
          source_tile_pixel_x = dest_tile_pixel_x >> 1u;
          source_tile_pixel_y = dest_tile_pixel_y >> 1u;
        }
      } else if (dest_msaa >= 4u) {
        source_tile_pixel_x = XeBitFieldInsert(
            dest_sample_id, dest_tile_pixel_x, 1u, 31u);
      }
    }
  }

  if (source_msaa < 4u && source_msaa != dest_msaa) {
    if (dest_msaa >= 4u) {
      if (source_msaa == 2u) {
        source_sample_id = dest_sample_id >> 1u;
        if (msaa_2x_supported) {
          source_sample_id ^= 1u;
        } else {
          source_sample_id = XeBitFieldInsert(
              source_sample_id, source_sample_id, 1u, 1u);
        }
      } else {
        source_tile_pixel_y = XeBitFieldInsert(
            dest_sample_id >> 1u, dest_tile_pixel_y, 1u, 31u);
      }
    } else {
      if (source_msaa == 2u) {
        source_sample_id = dest_tile_pixel_y & 1u;
        if (msaa_2x_supported) {
          source_sample_id ^= 1u;
        } else {
          source_sample_id = XeBitFieldInsert(
              source_sample_id, source_sample_id, 1u, 1u);
        }
        source_tile_pixel_y = dest_tile_pixel_y >> 1u;
      } else {
        if (msaa_2x_supported) {
          source_tile_pixel_y = XeBitFieldInsert(
              dest_sample_id ^ 1u, dest_tile_pixel_y, 1u, 31u);
        } else {
          source_tile_pixel_y = XeBitFieldInsert(
              dest_sample_id >> 1u, dest_tile_pixel_y, 1u, 31u);
        }
      }
    }
  }

  uint source_pixel_width_dwords_log2 =
      (source_msaa >= 4u ? 1u : 0u) + (source_is_64bpp ? 1u : 0u);

  if ((constants.source_is_depth != 0u) != (constants.dest_is_depth != 0u)) {
    uint source_32bpp_tile_half_pixels =
        tile_width_samples >> (1u + source_pixel_width_dwords_log2);
    if (source_tile_pixel_x < source_32bpp_tile_half_pixels) {
      source_tile_pixel_x += source_32bpp_tile_half_pixels;
    } else {
      source_tile_pixel_x -= source_32bpp_tile_half_pixels;
    }
  }

  uint source_tile_index =
      uint(int(dest_tile_index) + constants.address.source_to_dest) &
      (kEdramTileCount - 1u);
  uint source_pitch_tiles = constants.address.source_pitch;
  uint source_tile_index_y = source_tile_index / source_pitch_tiles;
  uint source_tile_index_x = source_tile_index % source_pitch_tiles;

  uint source_pixel_x =
      source_tile_index_x *
          (tile_width_samples >> source_pixel_width_dwords_log2) +
      source_tile_pixel_x;
  uint source_pixel_y =
      source_tile_index_y *
          (tile_height_samples >> (source_msaa >= 2u ? 1u : 0u)) +
      source_tile_pixel_y;

  bool load_two = !source_is_64bpp && dest_is_64bpp;
  uint source_pixel_x1 = source_pixel_x;
  uint source_sample_id1 = source_sample_id;
  if (load_two) {
    if (source_msaa >= 4u) {
      source_sample_id1 = source_sample_id | 1u;
    } else {
      source_pixel_x1 = source_pixel_x | 1u;
    }
  }

#if XE_TRANSFER_SOURCE_IS_COLOR
  #if XE_TRANSFER_SOURCE_IS_UINT
  uint4 source_color0 =
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
      xe_transfer_source.read(uint2(source_pixel_x, source_pixel_y),
                              source_sample_id);
#else
      xe_transfer_source.read(uint2(source_pixel_x, source_pixel_y));
#endif
  uint4 source_color1 = source_color0;
  if (load_two) {
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
    source_color1 = xe_transfer_source.read(
        uint2(source_pixel_x1, source_pixel_y), source_sample_id1);
#else
    source_color1 = xe_transfer_source.read(
        uint2(source_pixel_x1, source_pixel_y));
#endif
  }
  #else
  float4 source_color0 =
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
      xe_transfer_source.read(uint2(source_pixel_x, source_pixel_y),
                              source_sample_id);
#else
      xe_transfer_source.read(uint2(source_pixel_x, source_pixel_y));
#endif
  float4 source_color1 = source_color0;
  if (load_two) {
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
    source_color1 = xe_transfer_source.read(
        uint2(source_pixel_x1, source_pixel_y), source_sample_id1);
#else
    source_color1 = xe_transfer_source.read(
        uint2(source_pixel_x1, source_pixel_y));
#endif
  }
  #endif
#else
  float source_depth0 =
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
      xe_transfer_source_depth.read(uint2(source_pixel_x, source_pixel_y),
                                    source_sample_id).r;
#else
      xe_transfer_source_depth.read(uint2(source_pixel_x, source_pixel_y)).r;
#endif
  float source_depth1 = source_depth0;
  if (load_two) {
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
    source_depth1 = xe_transfer_source_depth.read(
        uint2(source_pixel_x1, source_pixel_y), source_sample_id1).r;
#else
    source_depth1 = xe_transfer_source_depth.read(
        uint2(source_pixel_x1, source_pixel_y)).r;
#endif
  }
  uint source_stencil0 =
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
      xe_transfer_source_stencil.read(uint2(source_pixel_x, source_pixel_y),
                                      source_sample_id).r;
#else
      xe_transfer_source_stencil.read(uint2(source_pixel_x, source_pixel_y)).r;
#endif
  uint source_stencil1 = source_stencil0;
  if (load_two) {
#if XE_TRANSFER_SOURCE_IS_MULTISAMPLE
    source_stencil1 = xe_transfer_source_stencil.read(
        uint2(source_pixel_x1, source_pixel_y), source_sample_id1).r;
#else
    source_stencil1 = xe_transfer_source_stencil.read(
        uint2(source_pixel_x1, source_pixel_y)).r;
#endif
  }
#endif

  uint packed = 0u;
#if !XE_TRANSFER_OUTPUT_STENCIL_BIT
  bool packed_only_depth = false;
#endif
#if XE_TRANSFER_SOURCE_IS_COLOR
  #if XE_TRANSFER_SOURCE_IS_UINT
  switch (constants.source_format) {
    case XE_FMT_16_16:
    case XE_FMT_16_16_FLOAT:
    case XE_FMT_16_16_16_16:
    case XE_FMT_16_16_16_16_FLOAT:
      packed = source_color0[0] | (source_color0[1] << 16u);
      break;
    case XE_FMT_32_FLOAT:
    case XE_FMT_32_32_FLOAT:
      packed = source_color0[0];
      break;
    default:
      packed = source_color0[0];
      break;
  }
  #else
  switch (constants.source_format) {
    case XE_FMT_8_8_8_8:
    case XE_FMT_8_8_8_8_GAMMA: {
      uint packed_component_offset = 0u;
      if (constants.dest_is_depth != 0u) {
        packed_component_offset = 1u;
#if !XE_TRANSFER_OUTPUT_STENCIL_BIT
        packed_only_depth = true;
#endif
      }
      packed = XePackUnorm(source_color0[packed_component_offset], 255.0f);
      if (constants.dest_is_depth == 0u) {
        packed |= XePackUnorm(source_color0[packed_component_offset + 1],
                              255.0f) << 8u;
        packed |= XePackUnorm(source_color0[packed_component_offset + 2],
                              255.0f) << 16u;
        packed |= XePackUnorm(source_color0[packed_component_offset + 3],
                              255.0f) << 24u;
      }
    } break;
    case XE_FMT_2_10_10_10:
    case XE_FMT_2_10_10_10_AS_10_10_10_10: {
      packed = XePackUnorm(source_color0[0], 1023.0f);
      if (constants.dest_is_depth == 0u) {
        packed |= XePackUnorm(source_color0[1], 1023.0f) << 10u;
        packed |= XePackUnorm(source_color0[2], 1023.0f) << 20u;
        packed |= XePackUnorm(source_color0[3], 3.0f) << 30u;
      }
    } break;
    case XE_FMT_2_10_10_10_FLOAT:
    case XE_FMT_2_10_10_10_FLOAT_AS_16_16_16_16: {
      packed = XeUnclampedFloat32To7e3(source_color0[0]);
      if (constants.dest_is_depth == 0u) {
        packed |= XeUnclampedFloat32To7e3(source_color0[1]) << 10u;
        packed |= XeUnclampedFloat32To7e3(source_color0[2]) << 20u;
        packed |= XePackUnorm(source_color0[3], 3.0f) << 30u;
      }
    } break;
    case XE_FMT_32_FLOAT:
    case XE_FMT_32_32_FLOAT:
      packed = as_type<uint>(source_color0[0]);
      break;
    default:
      packed = as_type<uint>(source_color0[0]);
      break;
  }
  #endif
#else
  if (constants.source_format == XE_FMT_D24FS8) {
    bool round_depth = constants.depth_round != 0u;
    packed = XeFloat32To20e4(source_depth0 * 2.0f, round_depth);
  } else {
    packed = XeRoundToNearestEven(
        clamp(source_depth0, 0.0f, 1.0f) * 16777215.0f);
  }
  if (constants.dest_is_depth != 0u) {
#if !XE_TRANSFER_OUTPUT_STENCIL_BIT
    packed_only_depth = true;
#endif
  } else {
    packed = (packed << 8u) | (source_stencil0 & 0xFFu);
  }
#endif

#if XE_TRANSFER_OUTPUT_STENCIL_BIT
  if (constants.stencil_clear == 0u) {
    if ((packed & constants.stencil_mask) == 0u) {
      discard_fragment();
    }
  }
  TransferDepthOut out;
  out.depth = 0.0f;
#if XE_TRANSFER_DEST_IS_MULTISAMPLE
  out.sample_mask = 1u << dest_sample_id;
#endif
  return out;
#else
  uint guest_depth24 = packed;
  if (!packed_only_depth) {
    guest_depth24 = packed >> 8u;
  }

  float host_depth32 = 0.0f;
  bool has_host_depth = false;

#if XE_TRANSFER_HAS_HOST_DEPTH && !XE_TRANSFER_HOST_DEPTH_IS_COPY
  uint host_tile_pixel_x = dest_tile_pixel_x;
  uint host_tile_pixel_y = dest_tile_pixel_y;
  uint host_sample_id = dest_sample_id;
  uint host_msaa = constants.host_depth_source_msaa_samples;

  if (host_msaa != dest_msaa) {
    if (host_msaa >= 4u) {
      if (dest_msaa == 2u) {
        if (msaa_2x_supported) {
          host_sample_id = XeBitFieldInsert(
              dest_tile_pixel_x, dest_sample_id ^ 1u, 1u, 31u);
        } else {
          host_sample_id = XeBitFieldInsert(
              dest_sample_id, dest_tile_pixel_x, 0u, 1u);
        }
        host_tile_pixel_x = dest_tile_pixel_x >> 1u;
      } else {
        host_sample_id = XeBitFieldInsert(
            dest_tile_pixel_x & 1u, dest_tile_pixel_y, 1u, 1u);
        host_tile_pixel_x = dest_tile_pixel_x >> 1u;
        host_tile_pixel_y = dest_tile_pixel_y >> 1u;
      }
    } else if (dest_msaa >= 4u) {
      host_tile_pixel_x = XeBitFieldInsert(
          dest_sample_id, dest_tile_pixel_x, 1u, 31u);
    }

    if (host_msaa < 4u) {
      if (dest_msaa >= 4u) {
        if (host_msaa == 2u) {
          host_sample_id = dest_sample_id >> 1u;
          if (msaa_2x_supported) {
            host_sample_id ^= 1u;
          } else {
            host_sample_id = XeBitFieldInsert(
                host_sample_id, host_sample_id, 1u, 1u);
          }
        } else {
          host_tile_pixel_y = XeBitFieldInsert(
              dest_sample_id >> 1u, dest_tile_pixel_y, 1u, 31u);
        }
      } else {
        if (host_msaa == 2u) {
          host_sample_id = dest_tile_pixel_y & 1u;
          if (msaa_2x_supported) {
            host_sample_id ^= 1u;
          } else {
            host_sample_id = XeBitFieldInsert(
                host_sample_id, host_sample_id, 1u, 1u);
          }
          host_tile_pixel_y = dest_tile_pixel_y >> 1u;
        } else {
          if (msaa_2x_supported) {
            host_tile_pixel_y = XeBitFieldInsert(
                dest_sample_id ^ 1u, dest_tile_pixel_y, 1u, 31u);
          } else {
            host_tile_pixel_y = XeBitFieldInsert(
                dest_sample_id >> 1u, dest_tile_pixel_y, 1u, 31u);
          }
        }
      }
    }
  }

  uint host_tile_index =
      uint(int(dest_tile_index) + constants.host_depth_address.source_to_dest) &
      (kEdramTileCount - 1u);
  uint host_pitch_tiles = constants.host_depth_address.source_pitch;
  uint host_tile_index_y = host_tile_index / host_pitch_tiles;
  uint host_tile_index_x = host_tile_index % host_pitch_tiles;
  uint host_pixel_x =
      host_tile_index_x *
          (tile_width_samples >> (host_msaa >= 4u ? 1u : 0u)) +
      host_tile_pixel_x;
  uint host_pixel_y =
      host_tile_index_y *
          (tile_height_samples >> (host_msaa >= 2u ? 1u : 0u)) +
      host_tile_pixel_y;

#if XE_TRANSFER_HOST_DEPTH_IS_MULTISAMPLE
  host_depth32 = xe_transfer_host_depth.read(
      uint2(host_pixel_x, host_pixel_y), host_sample_id).r;
#else
  host_depth32 =
      xe_transfer_host_depth.read(uint2(host_pixel_x, host_pixel_y)).r;
#endif
  has_host_depth = true;
#endif

#if XE_TRANSFER_HAS_HOST_DEPTH && XE_TRANSFER_HOST_DEPTH_IS_COPY
  uint dest_tile_sample_x = dest_tile_pixel_x;
  uint dest_tile_sample_y = dest_tile_pixel_y;
  if (dest_msaa >= 2u) {
    if (dest_msaa >= 4u) {
      dest_tile_sample_x = XeBitFieldInsert(
          dest_sample_id, dest_tile_pixel_x, 1u, 31u);
    }
    uint vert_sample = 0u;
    if (dest_msaa == 2u && msaa_2x_supported) {
      vert_sample = dest_sample_id ^ 1u;
    } else {
      vert_sample = dest_sample_id >> 1u;
    }
    dest_tile_sample_y = XeBitFieldInsert(
        vert_sample, dest_tile_pixel_y, 1u, 31u);
  }
  uint host_depth_offset =
      (tile_width_samples * tile_height_samples) * dest_tile_index +
      tile_width_samples * dest_tile_sample_y + dest_tile_sample_x;
  host_depth32 = as_type<float>(xe_transfer_host_depth_buffer[host_depth_offset]);
  has_host_depth = true;
#endif

  float fragment_depth = 0.0f;
  if (constants.dest_format == XE_FMT_D24FS8) {
    float guest_depth32 = XeFloat20e4To32(guest_depth24, true);
    fragment_depth = guest_depth32;
  } else {
    fragment_depth = XeUnorm24To32(guest_depth24);
  }

  if (has_host_depth) {
    uint host_depth24 = 0u;
    if (constants.dest_format == XE_FMT_D24FS8) {
      bool round_depth = constants.depth_round != 0u;
      host_depth24 = XeFloat32To20e4(host_depth32, round_depth);
    } else {
      host_depth24 = XeRoundToNearestEven(
          clamp(host_depth32, 0.0f, 1.0f) * 16777215.0f);
    }
    if (host_depth24 == guest_depth24) {
      fragment_depth = host_depth32;
    }
  }

  TransferDepthOut out;
  out.depth = fragment_depth;
#if XE_TRANSFER_DEST_IS_MULTISAMPLE
  out.sample_mask = 1u << dest_sample_id;
#endif
  return out;
#endif
}
#endif
)METAL";

  source.append(kTransferShaderSource);

  NS::Error* error = nullptr;
  auto src_str = NS::String::string(source.c_str(), NS::UTF8StringEncoding);
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
  auto ps_name = NS::String::string("transfer_ps", NS::UTF8StringEncoding);
  MTL::Function* vs = lib->newFunction(vs_name);
  MTL::Function* ps = lib->newFunction(ps_name);
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

  if (output == TransferOutput::kColor) {
    desc->colorAttachments()->object(0)->setPixelFormat(dest_format);
  } else {
    desc->colorAttachments()->object(0)->setPixelFormat(
        MTL::PixelFormatInvalid);
    desc->setDepthAttachmentPixelFormat(dest_format);
    if (dest_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        dest_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
      desc->setStencilAttachmentPixelFormat(dest_format);
    }
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
  XELOGI("GetOrCreateTransferPipelines: created pipeline mode={} src_fmt={} "
         "dst_fmt={} src_msaa={} dst_msaa={} output={}",
         int(key.mode), key.source_resource_format, key.dest_resource_format,
         int(key.source_msaa_samples), int(key.dest_msaa_samples),
         int(output));

  return pipeline;
}

MTL::Library* MetalRenderTargetCache::GetOrCreateTransferLibrary() {
  if (transfer_library_) {
    return transfer_library_;
  }
  static const char kTransferLibrarySource[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct VSOut {
  float4 position [[position]];
};

struct TransferClearColorFloatConstants {
  float4 color;
};

struct TransferClearColorUintConstants {
  uint4 color;
};

struct TransferClearDepthConstants {
  float4 depth;
};

vertex VSOut transfer_clear_vs(uint vid [[vertex_id]]) {
  float2 pt = float2((vid << 1) & 2, vid & 2);
  VSOut out;
  out.position = float4(pt * 2.0f - 1.0f, 0.0f, 1.0f);
  return out;
}

fragment float4 transfer_clear_color_float_ps(
    VSOut in [[stage_in]],
    constant TransferClearColorFloatConstants& constants [[buffer(0)]]) {
  return constants.color;
}

fragment uint4 transfer_clear_color_uint_ps(
    VSOut in [[stage_in]],
    constant TransferClearColorUintConstants& constants [[buffer(0)]]) {
  return constants.color;
}

struct TransferDepthOut {
  float depth [[depth(any)]];
};

fragment TransferDepthOut transfer_clear_depth_ps(
    VSOut in [[stage_in]],
    constant TransferClearDepthConstants& constants [[buffer(0)]]) {
  TransferDepthOut out;
  out.depth = constants.depth.x;
  return out;
}
)METAL";

  NS::Error* error = nullptr;
  auto source_str =
      NS::String::string(kTransferLibrarySource, NS::UTF8StringEncoding);
  transfer_library_ = device_->newLibrary(source_str, nullptr, &error);
  if (!transfer_library_) {
    XELOGE("GetOrCreateTransferLibrary: failed to compile transfer library: {}",
           error && error->localizedDescription()
               ? error->localizedDescription()->utf8String()
               : "unknown error");
  }
  return transfer_library_;
}

MTL::RenderPipelineState* MetalRenderTargetCache::GetOrCreateTransferClearPipeline(
    MTL::PixelFormat dest_format, bool dest_is_uint, bool is_depth,
    uint32_t sample_count) {
  uint32_t key = uint32_t(dest_format);
  key ^= (sample_count & 0x7u) << 24;
  if (dest_is_uint) {
    key ^= 1u << 30;
  }
  if (is_depth) {
    key ^= 1u << 31;
  }
  auto it = transfer_clear_pipelines_.find(key);
  if (it != transfer_clear_pipelines_.end()) {
    return it->second;
  }

  MTL::Library* lib = GetOrCreateTransferLibrary();
  if (!lib) {
    return nullptr;
  }

  auto vs_name = NS::String::string("transfer_clear_vs",
                                    NS::UTF8StringEncoding);
  const char* ps_name_cstr = nullptr;
  if (is_depth) {
    ps_name_cstr = "transfer_clear_depth_ps";
  } else {
    ps_name_cstr = dest_is_uint ? "transfer_clear_color_uint_ps"
                                : "transfer_clear_color_float_ps";
  }
  auto ps_name = NS::String::string(ps_name_cstr, NS::UTF8StringEncoding);

  MTL::Function* vs = lib->newFunction(vs_name);
  MTL::Function* ps = lib->newFunction(ps_name);
  if (!vs || !ps) {
    XELOGE(
        "GetOrCreateTransferClearPipeline: missing transfer clear functions");
    if (vs) {
      vs->release();
    }
    if (ps) {
      ps->release();
    }
    return nullptr;
  }

  MTL::RenderPipelineDescriptor* desc =
      MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(vs);
  desc->setFragmentFunction(ps);
  desc->setSampleCount(sample_count ? sample_count : 1);

  if (is_depth) {
    desc->colorAttachments()->object(0)->setPixelFormat(
        MTL::PixelFormatInvalid);
    desc->setDepthAttachmentPixelFormat(dest_format);
    if (dest_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        dest_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
      desc->setStencilAttachmentPixelFormat(dest_format);
    }
  } else {
    desc->colorAttachments()->object(0)->setPixelFormat(dest_format);
  }

  NS::Error* error = nullptr;
  MTL::RenderPipelineState* pipeline =
      device_->newRenderPipelineState(desc, &error);

  desc->release();
  vs->release();
  ps->release();

  if (!pipeline) {
    XELOGE(
        "GetOrCreateTransferClearPipeline: failed to create pipeline: {}",
        error && error->localizedDescription()
            ? error->localizedDescription()->utf8String()
            : "unknown error");
    return nullptr;
  }

  transfer_clear_pipelines_.emplace(key, pipeline);
  return pipeline;
}

MTL::Texture* MetalRenderTargetCache::GetTransferDummyTexture(
    MTL::PixelFormat format, uint32_t sample_count) {
  if (!device_) {
    return nullptr;
  }
  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setWidth(1);
  desc->setHeight(1);
  desc->setPixelFormat(format);
  desc->setTextureType(sample_count > 1 ? MTL::TextureType2DMultisample
                                        : MTL::TextureType2D);
  desc->setSampleCount(sample_count ? sample_count : 1);
  desc->setUsage(MTL::TextureUsageShaderRead |
                 MTL::TextureUsagePixelFormatView);
  desc->setStorageMode(MTL::StorageModePrivate);
  MTL::Texture* tex = device_->newTexture(desc);
  desc->release();
  return tex;
}

MTL::Texture* MetalRenderTargetCache::GetTransferDummyColorFloatTexture(
    uint32_t sample_count) {
  size_t index = sample_count >= 4 ? 2 : (sample_count == 2 ? 1 : 0);
  if (!transfer_dummy_color_float_[index]) {
    transfer_dummy_color_float_[index] =
        GetTransferDummyTexture(MTL::PixelFormatRGBA8Unorm, sample_count);
  }
  return transfer_dummy_color_float_[index];
}

MTL::Texture* MetalRenderTargetCache::GetTransferDummyColorUintTexture(
    uint32_t sample_count) {
  size_t index = sample_count >= 4 ? 2 : (sample_count == 2 ? 1 : 0);
  if (!transfer_dummy_color_uint_[index]) {
    transfer_dummy_color_uint_[index] =
        GetTransferDummyTexture(MTL::PixelFormatRGBA8Uint, sample_count);
  }
  return transfer_dummy_color_uint_[index];
}

MTL::Texture* MetalRenderTargetCache::GetTransferDummyDepthTexture(
    uint32_t sample_count) {
  size_t index = sample_count >= 4 ? 2 : (sample_count == 2 ? 1 : 0);
  if (!transfer_dummy_depth_[index]) {
    transfer_dummy_depth_[index] = GetTransferDummyTexture(
        MTL::PixelFormatDepth32Float_Stencil8, sample_count);
  }
  return transfer_dummy_depth_[index];
}

MTL::Texture* MetalRenderTargetCache::GetTransferDummyStencilTexture(
    uint32_t sample_count) {
  size_t index = sample_count >= 4 ? 2 : (sample_count == 2 ? 1 : 0);
  if (!transfer_dummy_stencil_[index]) {
    MTL::Texture* depth_tex = GetTransferDummyDepthTexture(sample_count);
    if (!depth_tex) {
      return nullptr;
    }
    transfer_dummy_stencil_[index] =
        depth_tex->newTextureView(MTL::PixelFormatX32_Stencil8);
  }
  return transfer_dummy_stencil_[index];
}

MTL::Buffer* MetalRenderTargetCache::GetTransferDummyBuffer() {
  if (!transfer_dummy_buffer_ && device_) {
    transfer_dummy_buffer_ =
        device_->newBuffer(sizeof(uint32_t), MTL::ResourceStorageModeShared);
    if (transfer_dummy_buffer_) {
      std::memset(transfer_dummy_buffer_->contents(), 0,
                  sizeof(uint32_t));
    }
  }
  return transfer_dummy_buffer_;
}

MTL::DepthStencilState* MetalRenderTargetCache::GetTransferDepthStencilState(
    bool depth_write) {
  if (transfer_depth_state_) {
    return transfer_depth_state_;
  }
  MTL::DepthStencilDescriptor* desc =
      MTL::DepthStencilDescriptor::alloc()->init();
  desc->setDepthCompareFunction(
      cvars::depth_transfer_not_equal_test ? MTL::CompareFunctionNotEqual
                                           : MTL::CompareFunctionAlways);
  desc->setDepthWriteEnabled(depth_write);
  transfer_depth_state_ = device_->newDepthStencilState(desc);
  desc->release();
  return transfer_depth_state_;
}

MTL::DepthStencilState*
MetalRenderTargetCache::GetTransferNoDepthStencilState() {
  if (transfer_depth_state_none_) {
    return transfer_depth_state_none_;
  }
  MTL::DepthStencilDescriptor* desc =
      MTL::DepthStencilDescriptor::alloc()->init();
  desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
  desc->setDepthWriteEnabled(false);
  transfer_depth_state_none_ = device_->newDepthStencilState(desc);
  desc->release();
  return transfer_depth_state_none_;
}

MTL::DepthStencilState* MetalRenderTargetCache::GetTransferDepthClearState() {
  if (transfer_depth_clear_state_) {
    return transfer_depth_clear_state_;
  }
  MTL::DepthStencilDescriptor* desc =
      MTL::DepthStencilDescriptor::alloc()->init();
  desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
  desc->setDepthWriteEnabled(true);
  MTL::StencilDescriptor* stencil =
      MTL::StencilDescriptor::alloc()->init();
  stencil->setStencilCompareFunction(MTL::CompareFunctionAlways);
  stencil->setStencilFailureOperation(MTL::StencilOperationKeep);
  stencil->setDepthFailureOperation(MTL::StencilOperationKeep);
  stencil->setDepthStencilPassOperation(MTL::StencilOperationReplace);
  stencil->setReadMask(0xFF);
  stencil->setWriteMask(0xFF);
  desc->setFrontFaceStencil(stencil);
  desc->setBackFaceStencil(stencil);
  transfer_depth_clear_state_ = device_->newDepthStencilState(desc);
  stencil->release();
  desc->release();
  return transfer_depth_clear_state_;
}

MTL::DepthStencilState* MetalRenderTargetCache::GetTransferStencilClearState() {
  if (transfer_stencil_clear_state_) {
    return transfer_stencil_clear_state_;
  }
  MTL::DepthStencilDescriptor* desc =
      MTL::DepthStencilDescriptor::alloc()->init();
  desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
  desc->setDepthWriteEnabled(false);
  MTL::StencilDescriptor* stencil =
      MTL::StencilDescriptor::alloc()->init();
  stencil->setStencilCompareFunction(MTL::CompareFunctionAlways);
  stencil->setStencilFailureOperation(MTL::StencilOperationKeep);
  stencil->setDepthFailureOperation(MTL::StencilOperationKeep);
  stencil->setDepthStencilPassOperation(MTL::StencilOperationReplace);
  stencil->setReadMask(0xFF);
  stencil->setWriteMask(0xFF);
  desc->setFrontFaceStencil(stencil);
  desc->setBackFaceStencil(stencil);
  transfer_stencil_clear_state_ = device_->newDepthStencilState(desc);
  stencil->release();
  desc->release();
  return transfer_stencil_clear_state_;
}

MTL::DepthStencilState* MetalRenderTargetCache::GetTransferStencilBitState(
    uint32_t bit) {
  if (bit >= 8) {
    return nullptr;
  }
  if (transfer_stencil_bit_states_[bit]) {
    return transfer_stencil_bit_states_[bit];
  }
  uint32_t mask = uint32_t(1) << bit;
  MTL::DepthStencilDescriptor* desc =
      MTL::DepthStencilDescriptor::alloc()->init();
  desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
  desc->setDepthWriteEnabled(false);
  MTL::StencilDescriptor* stencil =
      MTL::StencilDescriptor::alloc()->init();
  stencil->setStencilCompareFunction(MTL::CompareFunctionAlways);
  stencil->setStencilFailureOperation(MTL::StencilOperationKeep);
  stencil->setDepthFailureOperation(MTL::StencilOperationKeep);
  stencil->setDepthStencilPassOperation(MTL::StencilOperationReplace);
  stencil->setReadMask(0xFF);
  stencil->setWriteMask(uint32_t(mask));
  desc->setFrontFaceStencil(stencil);
  desc->setBackFaceStencil(stencil);
  transfer_stencil_bit_states_[bit] = device_->newDepthStencilState(desc);
  stencil->release();
  desc->release();
  return transfer_stencil_bit_states_[bit];
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_texture_cache.h"

#include <algorithm>
#include <array>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_mem_alloc.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_128bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_128bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_16bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_16bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_32bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_32bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_64bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_64bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_8bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_8bpb_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_bgrg8_rgb8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_ctx1_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_depth_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_depth_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_depth_unorm_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_depth_unorm_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxn_rg8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt1_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt3_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt3a_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt3aas1111_argb4_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt5_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_dxt5a_r8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_gbgr8_rgb8_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r10g11b11_rgba16_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r10g11b11_rgba16_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r10g11b11_rgba16_snorm_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r10g11b11_rgba16_snorm_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r11g11b10_rgba16_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r11g11b10_rgba16_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r11g11b10_rgba16_snorm_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r11g11b10_rgba16_snorm_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r16_snorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r16_snorm_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r16_unorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r16_unorm_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r4g4b4a4_a4r4g4b4_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r4g4b4a4_a4r4g4b4_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g5b5a1_b5g5r5a1_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g5b5a1_b5g5r5a1_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g5b6_b5g6r5_swizzle_rbga_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g6b5_b5g6r5_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_r5g6b5_b5g6r5_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rg16_snorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rg16_snorm_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rg16_unorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rg16_unorm_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rgba16_snorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rgba16_snorm_float_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rgba16_unorm_float_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_rgba16_unorm_float_scaled_cs.h"
}  // namespace shaders

static_assert(VK_FORMAT_UNDEFINED == VkFormat(0),
              "Assuming that skipping a VkFormat in an initializer results in "
              "VK_FORMAT_UNDEFINED");
const VulkanTextureCache::HostFormatPair
    VulkanTextureCache::kBestHostFormats[64] = {
        // k_1_REVERSE
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_1
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_8
        {{kLoadShaderIndex8bpb, VK_FORMAT_R8_UNORM},
         {kLoadShaderIndex8bpb, VK_FORMAT_R8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_1_5_5_5
        // Red and blue swapped in the load shader for simplicity.
        {{kLoadShaderIndexR5G5B5A1ToB5G5R5A1, VK_FORMAT_A1R5G5B5_UNORM_PACK16},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_5_6_5
        // Red and blue swapped in the load shader for simplicity.
        {{kLoadShaderIndexR5G6B5ToB5G6R5, VK_FORMAT_R5G6B5_UNORM_PACK16},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_6_5_5
        // On the host, green bits in blue, blue bits in green.
        {{kLoadShaderIndexR5G5B6ToB5G6R5WithRBGASwizzle,
          VK_FORMAT_R5G6B5_UNORM_PACK16},
         {kLoadShaderIndexUnknown},
         XE_GPU_MAKE_TEXTURE_SWIZZLE(R, B, G, G)},
        // k_8_8_8_8
        {{kLoadShaderIndex32bpb, VK_FORMAT_R8G8B8A8_UNORM},
         {kLoadShaderIndex32bpb, VK_FORMAT_R8G8B8A8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_2_10_10_10
        // VK_FORMAT_A2B10G10R10_SNORM_PACK32 is optional.
        {{kLoadShaderIndex32bpb, VK_FORMAT_A2B10G10R10_UNORM_PACK32},
         {kLoadShaderIndex32bpb, VK_FORMAT_A2B10G10R10_SNORM_PACK32},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_8_A
        {{kLoadShaderIndex8bpb, VK_FORMAT_R8_UNORM},
         {kLoadShaderIndex8bpb, VK_FORMAT_R8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_8_B
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_8_8
        {{kLoadShaderIndex16bpb, VK_FORMAT_R8G8_UNORM},
         {kLoadShaderIndex16bpb, VK_FORMAT_R8G8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG,
         true},
        // k_Cr_Y1_Cb_Y0_REP
        // VK_FORMAT_G8B8G8R8_422_UNORM_KHR (added in
        // VK_KHR_sampler_ycbcr_conversion and promoted to Vulkan 1.1) is
        // optional.
        {{kLoadShaderIndex32bpb, VK_FORMAT_G8B8G8R8_422_UNORM_KHR, true},
         {kLoadShaderIndexGBGR8ToRGB8, VK_FORMAT_R8G8B8A8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_Y1_Cr_Y0_Cb_REP
        // VK_FORMAT_B8G8R8G8_422_UNORM_KHR (added in
        // VK_KHR_sampler_ycbcr_conversion and promoted to Vulkan 1.1) is
        // optional.
        {{kLoadShaderIndex32bpb, VK_FORMAT_B8G8R8G8_422_UNORM_KHR, true},
         {kLoadShaderIndexBGRG8ToRGB8, VK_FORMAT_R8G8B8A8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_16_16_EDRAM
        // Not usable as a texture, also has -32...32 range.
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_8_8_8_8_A
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_4_4_4_4
        // Components swapped in the load shader for simplicity.
        {{kLoadShaderIndexRGBA4ToARGB4, VK_FORMAT_B4G4R4A4_UNORM_PACK16},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_10_11_11
        // TODO(Triang3l): 16_UNORM/SNORM are optional, convert to float16
        // instead.
        {{kLoadShaderIndexR11G11B10ToRGBA16, VK_FORMAT_R16G16B16A16_UNORM},
         {kLoadShaderIndexR11G11B10ToRGBA16SNorm, VK_FORMAT_R16G16B16A16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_11_11_10
        // TODO(Triang3l): 16_UNORM/SNORM are optional, convert to float16
        // instead.
        {{kLoadShaderIndexR10G11B11ToRGBA16, VK_FORMAT_R16G16B16A16_UNORM},
         {kLoadShaderIndexR10G11B11ToRGBA16SNorm, VK_FORMAT_R16G16B16A16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_DXT1
        // VK_FORMAT_BC1_RGBA_UNORM_BLOCK is optional.
        {{kLoadShaderIndex64bpb, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, true},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_DXT2_3
        // VK_FORMAT_BC2_UNORM_BLOCK is optional.
        {{kLoadShaderIndex128bpb, VK_FORMAT_BC2_UNORM_BLOCK, true},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_DXT4_5
        // VK_FORMAT_BC3_UNORM_BLOCK is optional.
        {{kLoadShaderIndex128bpb, VK_FORMAT_BC3_UNORM_BLOCK, true},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_16_16_16_16_EDRAM
        // Not usable as a texture, also has -32...32 range.
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_24_8
        {{kLoadShaderIndexDepthUnorm, VK_FORMAT_R32_SFLOAT},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_24_8_FLOAT
        {{kLoadShaderIndexDepthFloat, VK_FORMAT_R32_SFLOAT},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_16
        // VK_FORMAT_R16_UNORM and VK_FORMAT_R16_SNORM are optional.
        {{kLoadShaderIndex16bpb, VK_FORMAT_R16_UNORM},
         {kLoadShaderIndex16bpb, VK_FORMAT_R16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_16_16
        // VK_FORMAT_R16G16_UNORM and VK_FORMAT_R16G16_SNORM are optional.
        {{kLoadShaderIndex32bpb, VK_FORMAT_R16G16_UNORM},
         {kLoadShaderIndex32bpb, VK_FORMAT_R16G16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG,
         true},
        // k_16_16_16_16
        // VK_FORMAT_R16G16B16A16_UNORM and VK_FORMAT_R16G16B16A16_SNORM are
        // optional.
        {{kLoadShaderIndex64bpb, VK_FORMAT_R16G16B16A16_UNORM},
         {kLoadShaderIndex64bpb, VK_FORMAT_R16G16B16A16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_16_EXPAND
        {{kLoadShaderIndex16bpb, VK_FORMAT_R16_SFLOAT},
         {kLoadShaderIndex16bpb, VK_FORMAT_R16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_16_16_EXPAND
        {{kLoadShaderIndex32bpb, VK_FORMAT_R16G16_SFLOAT},
         {kLoadShaderIndex32bpb, VK_FORMAT_R16G16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG,
         true},
        // k_16_16_16_16_EXPAND
        {{kLoadShaderIndex64bpb, VK_FORMAT_R16G16B16A16_SFLOAT},
         {kLoadShaderIndex64bpb, VK_FORMAT_R16G16B16A16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_16_FLOAT
        {{kLoadShaderIndex16bpb, VK_FORMAT_R16_SFLOAT},
         {kLoadShaderIndex16bpb, VK_FORMAT_R16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_16_16_FLOAT
        {{kLoadShaderIndex32bpb, VK_FORMAT_R16G16_SFLOAT},
         {kLoadShaderIndex32bpb, VK_FORMAT_R16G16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG,
         true},
        // k_16_16_16_16_FLOAT
        {{kLoadShaderIndex64bpb, VK_FORMAT_R16G16B16A16_SFLOAT},
         {kLoadShaderIndex64bpb, VK_FORMAT_R16G16B16A16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_32
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_32_32
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_32_32_32_32
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_32_FLOAT
        {{kLoadShaderIndex32bpb, VK_FORMAT_R32_SFLOAT},
         {kLoadShaderIndex32bpb, VK_FORMAT_R32_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_32_32_FLOAT
        {{kLoadShaderIndex64bpb, VK_FORMAT_R32G32_SFLOAT},
         {kLoadShaderIndex64bpb, VK_FORMAT_R32G32_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG,
         true},
        // k_32_32_32_32_FLOAT
        {{kLoadShaderIndex128bpb, VK_FORMAT_R32G32B32A32_SFLOAT},
         {kLoadShaderIndex128bpb, VK_FORMAT_R32G32B32A32_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_32_AS_8
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_32_AS_8_8
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_16_MPEG
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_16_16_MPEG
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_8_INTERLACED
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_32_AS_8_INTERLACED
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_32_AS_8_8_INTERLACED
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_16_INTERLACED
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_16_MPEG_INTERLACED
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_16_16_MPEG_INTERLACED
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_DXN
        // VK_FORMAT_BC5_UNORM_BLOCK is optional.
        {{kLoadShaderIndex128bpb, VK_FORMAT_BC5_UNORM_BLOCK, true},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_8_8_8_8_AS_16_16_16_16
        {{kLoadShaderIndex32bpb, VK_FORMAT_R8G8B8A8_UNORM},
         {kLoadShaderIndex32bpb, VK_FORMAT_R8G8B8A8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_DXT1_AS_16_16_16_16
        // VK_FORMAT_BC1_RGBA_UNORM_BLOCK is optional.
        {{kLoadShaderIndex64bpb, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, true},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_DXT2_3_AS_16_16_16_16
        // VK_FORMAT_BC2_UNORM_BLOCK is optional.
        {{kLoadShaderIndex128bpb, VK_FORMAT_BC2_UNORM_BLOCK, true},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_DXT4_5_AS_16_16_16_16
        // VK_FORMAT_BC3_UNORM_BLOCK is optional.
        {{kLoadShaderIndex128bpb, VK_FORMAT_BC3_UNORM_BLOCK, true},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_2_10_10_10_AS_16_16_16_16
        // VK_FORMAT_A2B10G10R10_SNORM_PACK32 is optional.
        {{kLoadShaderIndex32bpb, VK_FORMAT_A2B10G10R10_UNORM_PACK32},
         {kLoadShaderIndex32bpb, VK_FORMAT_A2B10G10R10_SNORM_PACK32},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_10_11_11_AS_16_16_16_16
        // TODO(Triang3l): 16_UNORM/SNORM are optional, convert to float16
        // instead.
        {{kLoadShaderIndexR11G11B10ToRGBA16, VK_FORMAT_R16G16B16A16_UNORM},
         {kLoadShaderIndexR11G11B10ToRGBA16SNorm, VK_FORMAT_R16G16B16A16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_11_11_10_AS_16_16_16_16
        // TODO(Triang3l): 16_UNORM/SNORM are optional, convert to float16
        // instead.
        {{kLoadShaderIndexR10G11B11ToRGBA16, VK_FORMAT_R16G16B16A16_UNORM},
         {kLoadShaderIndexR10G11B11ToRGBA16SNorm, VK_FORMAT_R16G16B16A16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_32_32_32_FLOAT
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_DXT3A
        {{kLoadShaderIndexDXT3A, VK_FORMAT_R8_UNORM},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_DXT5A
        // VK_FORMAT_BC4_UNORM_BLOCK is optional.
        {{kLoadShaderIndex64bpb, VK_FORMAT_BC4_UNORM_BLOCK, true},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_CTX1
        {{kLoadShaderIndexCTX1, VK_FORMAT_R8G8_UNORM},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_DXT3A_AS_1_1_1_1
        {{kLoadShaderIndexDXT3AAs1111ToARGB4, VK_FORMAT_B4G4R4A4_UNORM_PACK16},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_8_8_8_8_GAMMA_EDRAM
        // Not usable as a texture.
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_2_10_10_10_FLOAT_EDRAM
        // Not usable as a texture.
        {{kLoadShaderIndexUnknown},
         {kLoadShaderIndexUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
};

// Vulkan requires 2x1 (4:2:2) subsampled images to have an even width.
// Always decompressing them to RGBA8, which is required to be linear-filterable
// as UNORM and SNORM.

const VulkanTextureCache::HostFormatPair
    VulkanTextureCache::kHostFormatGBGRUnaligned = {
        {kLoadShaderIndexGBGR8ToRGB8, VK_FORMAT_R8G8B8A8_UNORM, false, true},
        {kLoadShaderIndexGBGR8ToRGB8, VK_FORMAT_R8G8B8A8_SNORM, false, true},
        xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB,
        true};

const VulkanTextureCache::HostFormatPair
    VulkanTextureCache::kHostFormatBGRGUnaligned = {
        {kLoadShaderIndexBGRG8ToRGB8, VK_FORMAT_R8G8B8A8_UNORM, false, true},
        {kLoadShaderIndexBGRG8ToRGB8, VK_FORMAT_R8G8B8A8_SNORM, false, true},
        xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB,
        true};

VulkanTextureCache::~VulkanTextureCache() {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  for (const std::pair<const SamplerParameters, Sampler>& sampler_pair :
       samplers_) {
    dfn.vkDestroySampler(device, sampler_pair.second.sampler, nullptr);
  }
  samplers_.clear();
  COUNT_profile_set("gpu/texture_cache/vulkan/samplers", 0);
  sampler_used_last_ = nullptr;
  sampler_used_first_ = nullptr;

  if (null_image_view_3d_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, null_image_view_3d_, nullptr);
  }
  if (null_image_view_cube_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, null_image_view_cube_, nullptr);
  }
  if (null_image_view_2d_array_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, null_image_view_2d_array_, nullptr);
  }
  if (null_image_3d_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, null_image_3d_, nullptr);
  }
  if (null_image_2d_array_cube_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, null_image_2d_array_cube_, nullptr);
  }
  for (VkDeviceMemory null_images_memory : null_images_memory_) {
    if (null_images_memory != VK_NULL_HANDLE) {
      dfn.vkFreeMemory(device, null_images_memory, nullptr);
    }
  }
  for (VkPipeline load_pipeline : load_pipelines_scaled_) {
    if (load_pipeline != VK_NULL_HANDLE) {
      dfn.vkDestroyPipeline(device, load_pipeline, nullptr);
    }
  }
  for (VkPipeline load_pipeline : load_pipelines_) {
    if (load_pipeline != VK_NULL_HANDLE) {
      dfn.vkDestroyPipeline(device, load_pipeline, nullptr);
    }
  }
  if (load_pipeline_layout_ != VK_NULL_HANDLE) {
    dfn.vkDestroyPipelineLayout(device, load_pipeline_layout_, nullptr);
  }

  // Textures memory is allocated using the Vulkan Memory Allocator, destroy all
  // textures before destroying VMA.
  DestroyAllTextures(true);

  if (vma_allocator_ != VK_NULL_HANDLE) {
    vmaDestroyAllocator(vma_allocator_);
  }
}

void VulkanTextureCache::BeginSubmission(uint64_t new_submission_index) {
  TextureCache::BeginSubmission(new_submission_index);

  if (!null_images_cleared_) {
    VkImage null_images[] = {null_image_2d_array_cube_, null_image_3d_};
    VkImageSubresourceRange null_image_subresource_range(
        ui::vulkan::util::InitializeSubresourceRange());
    for (size_t i = 0; i < xe::countof(null_images); ++i) {
      command_processor_.PushImageMemoryBarrier(
          null_images[i], null_image_subresource_range, 0,
          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, false);
    }
    command_processor_.SubmitBarriers(true);
    DeferredCommandBuffer& command_buffer =
        command_processor_.deferred_command_buffer();
    // TODO(Triang3l): Find the return value for invalid texture fetch constants
    // on the real hardware.
    VkClearColorValue null_image_clear_color;
    null_image_clear_color.float32[0] = 0.0f;
    null_image_clear_color.float32[1] = 0.0f;
    null_image_clear_color.float32[2] = 0.0f;
    null_image_clear_color.float32[3] = 0.0f;
    for (size_t i = 0; i < xe::countof(null_images); ++i) {
      command_buffer.CmdVkClearColorImage(
          null_images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          &null_image_clear_color, 1, &null_image_subresource_range);
    }
    for (size_t i = 0; i < xe::countof(null_images); ++i) {
      command_processor_.PushImageMemoryBarrier(
          null_images[i], null_image_subresource_range,
          VK_PIPELINE_STAGE_TRANSFER_BIT, guest_shader_pipeline_stages_,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_QUEUE_FAMILY_IGNORED,
          VK_QUEUE_FAMILY_IGNORED, false);
    }
    null_images_cleared_ = true;
  }
}

void VulkanTextureCache::RequestTextures(uint32_t used_texture_mask) {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  TextureCache::RequestTextures(used_texture_mask);

  // Transition the textures into the needed usage.
  VkPipelineStageFlags dst_stage_mask;
  VkAccessFlags dst_access_mask;
  VkImageLayout new_layout;
  GetTextureUsageMasks(VulkanTexture::Usage::kGuestShaderSampled,
                       dst_stage_mask, dst_access_mask, new_layout);
  uint32_t textures_remaining = used_texture_mask;
  uint32_t index;
  while (xe::bit_scan_forward(textures_remaining, &index)) {
    textures_remaining &= ~(uint32_t(1) << index);
    const TextureBinding* binding = GetValidTextureBinding(index);
    if (!binding) {
      continue;
    }
    VulkanTexture* binding_texture =
        static_cast<VulkanTexture*>(binding->texture);
    if (binding_texture != nullptr) {
      // Will be referenced by the command buffer, so mark as used.
      binding_texture->MarkAsUsed();
      VulkanTexture::Usage old_usage =
          binding_texture->SetUsage(VulkanTexture::Usage::kGuestShaderSampled);
      if (old_usage != VulkanTexture::Usage::kGuestShaderSampled) {
        VkPipelineStageFlags src_stage_mask;
        VkAccessFlags src_access_mask;
        VkImageLayout old_layout;
        GetTextureUsageMasks(old_usage, src_stage_mask, src_access_mask,
                             old_layout);
        command_processor_.PushImageMemoryBarrier(
            binding_texture->image(),
            ui::vulkan::util::InitializeSubresourceRange(), src_stage_mask,
            dst_stage_mask, src_access_mask, dst_access_mask, old_layout,
            new_layout);
      }
    }
    VulkanTexture* binding_texture_signed =
        static_cast<VulkanTexture*>(binding->texture_signed);
    if (binding_texture_signed != nullptr) {
      binding_texture_signed->MarkAsUsed();
      VulkanTexture::Usage old_usage = binding_texture_signed->SetUsage(
          VulkanTexture::Usage::kGuestShaderSampled);
      if (old_usage != VulkanTexture::Usage::kGuestShaderSampled) {
        VkPipelineStageFlags src_stage_mask;
        VkAccessFlags src_access_mask;
        VkImageLayout old_layout;
        GetTextureUsageMasks(old_usage, src_stage_mask, src_access_mask,
                             old_layout);
        command_processor_.PushImageMemoryBarrier(
            binding_texture_signed->image(),
            ui::vulkan::util::InitializeSubresourceRange(), src_stage_mask,
            dst_stage_mask, src_access_mask, dst_access_mask, old_layout,
            new_layout);
      }
    }
  }
}

VkImageView VulkanTextureCache::GetActiveBindingOrNullImageView(
    uint32_t fetch_constant_index, xenos::FetchOpDimension dimension,
    bool is_signed) const {
  VkImageView image_view = VK_NULL_HANDLE;
  const TextureBinding* binding = GetValidTextureBinding(fetch_constant_index);
  if (binding && AreDimensionsCompatible(dimension, binding->key.dimension)) {
    const VulkanTextureBinding& vulkan_binding =
        vulkan_texture_bindings_[fetch_constant_index];
    image_view = is_signed ? vulkan_binding.image_view_signed
                           : vulkan_binding.image_view_unsigned;
  }
  if (image_view != VK_NULL_HANDLE) {
    return image_view;
  }
  switch (dimension) {
    case xenos::FetchOpDimension::k3DOrStacked:
      return null_image_view_3d_;
    case xenos::FetchOpDimension::kCube:
      return null_image_view_cube_;
    default:
      return null_image_view_2d_array_;
  }
}

VulkanTextureCache::SamplerParameters VulkanTextureCache::GetSamplerParameters(
    const VulkanShader::SamplerBinding& binding) const {
  const auto& regs = register_file();
  const auto& fetch = regs.Get<xenos::xe_gpu_texture_fetch_t>(
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + binding.fetch_constant * 6);

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
  xenos::TextureFilter mip_filter =
      binding.mip_filter == xenos::TextureFilter::kUseFetchConst
          ? fetch.mip_filter
          : binding.mip_filter;
  parameters.mip_linear = mip_filter == xenos::TextureFilter::kLinear;
  if (parameters.mag_linear || parameters.min_linear || parameters.mip_linear) {
    // Check if the texture is actually filterable on the host.
    bool linear_filterable = true;
    TextureKey texture_key;
    uint8_t texture_swizzled_signs;
    BindingInfoFromFetchConstant(fetch, texture_key, &texture_swizzled_signs);
    if (texture_key.is_valid) {
      const HostFormatPair& host_format_pair = GetHostFormatPair(texture_key);
      if ((texture_util::IsAnySignNotSigned(texture_swizzled_signs) &&
           !host_format_pair.format_unsigned.linear_filterable) ||
          (texture_util::IsAnySignSigned(texture_swizzled_signs) &&
           !host_format_pair.format_signed.linear_filterable)) {
        linear_filterable = false;
      }
    } else {
      linear_filterable = false;
    }
    if (!linear_filterable) {
      parameters.mag_linear = 0;
      parameters.min_linear = 0;
      parameters.mip_linear = 0;
    }
  }
  xenos::AnisoFilter aniso_filter =
      binding.aniso_filter == xenos::AnisoFilter::kUseFetchConst
          ? fetch.aniso_filter
          : binding.aniso_filter;
  parameters.aniso_filter = std::min(aniso_filter, max_anisotropy_);
  parameters.mip_base_map = mip_filter == xenos::TextureFilter::kBaseMap;

  uint32_t mip_min_level;
  texture_util::GetSubresourcesFromFetchConstant(fetch, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr,
                                                 &mip_min_level, nullptr);
  parameters.mip_min_level = mip_min_level;

  return parameters;
}

VkSampler VulkanTextureCache::UseSampler(SamplerParameters parameters,
                                         bool& has_overflown_out) {
  assert_true(command_processor_.submission_open());
  uint64_t submission_current = command_processor_.GetCurrentSubmission();

  // Try to find an existing sampler.
  auto it_existing = samplers_.find(parameters);
  if (it_existing != samplers_.end()) {
    std::pair<const SamplerParameters, Sampler>& sampler = *it_existing;
    assert_true(sampler.second.last_usage_submission <= submission_current);
    // This is called very frequently, don't relink unless needed for caching.
    if (sampler.second.last_usage_submission < submission_current) {
      // Move to the front of the LRU queue.
      sampler.second.last_usage_submission = submission_current;
      if (sampler.second.used_next) {
        if (sampler.second.used_previous) {
          sampler.second.used_previous->second.used_next =
              sampler.second.used_next;
        } else {
          sampler_used_first_ = sampler.second.used_next;
        }
        sampler.second.used_next->second.used_previous =
            sampler.second.used_previous;
        sampler.second.used_previous = sampler_used_last_;
        sampler.second.used_next = nullptr;
        sampler_used_last_->second.used_next = &sampler;
        sampler_used_last_ = &sampler;
      }
    }
    has_overflown_out = false;
    return sampler.second.sampler;
  }

  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // See if an existing sampler can be destroyed to create space for the new
  // one.
  if (samplers_.size() >= sampler_max_count_) {
    assert_not_null(sampler_used_first_);
    if (!sampler_used_first_) {
      has_overflown_out = false;
      return VK_NULL_HANDLE;
    }
    if (sampler_used_first_->second.last_usage_submission >
        command_processor_.GetCompletedSubmission()) {
      has_overflown_out = true;
      return VK_NULL_HANDLE;
    }
    auto it_reuse = samplers_.find(sampler_used_first_->first);
    dfn.vkDestroySampler(device, sampler_used_first_->second.sampler, nullptr);
    if (sampler_used_first_->second.used_next) {
      sampler_used_first_->second.used_next->second.used_previous =
          sampler_used_first_->second.used_previous;
    } else {
      sampler_used_last_ = sampler_used_first_->second.used_previous;
    }
    sampler_used_first_ = sampler_used_first_->second.used_next;
    assert_true(it_reuse != samplers_.end());
    if (it_reuse != samplers_.end()) {
      // This destroys the Sampler object.
      samplers_.erase(it_reuse);
      COUNT_profile_set("gpu/texture_cache/vulkan/samplers", samplers_.size());
    } else {
      has_overflown_out = false;
      return VK_NULL_HANDLE;
    }
  }

  // Create a new sampler and make it the least recently used.
  // The values are normalized, and unsupported ones are excluded, in
  // GetSamplerParameters.
  VkSamplerCreateInfo sampler_create_info = {};
  sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  // TODO(Triang3l): VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT if
  // VK_EXT_non_seamless_cube_map and the nonSeamlessCubeMap feature are
  // supported.
  sampler_create_info.magFilter =
      parameters.mag_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  sampler_create_info.minFilter =
      parameters.mag_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  sampler_create_info.mipmapMode = parameters.mag_linear
                                       ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                       : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  static const VkSamplerAddressMode kAddressModeMap[] = {
      // kRepeat
      VK_SAMPLER_ADDRESS_MODE_REPEAT,
      // kMirroredRepeat
      VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
      // kClampToEdge
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      // kMirrorClampToEdge
      VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE_KHR,
      // kClampToHalfway
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      // kMirrorClampToHalfway
      VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE_KHR,
      // kClampToBorder
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      // kMirrorClampToBorder
      VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE_KHR,
  };
  sampler_create_info.addressModeU =
      kAddressModeMap[uint32_t(parameters.clamp_x)];
  sampler_create_info.addressModeV =
      kAddressModeMap[uint32_t(parameters.clamp_y)];
  sampler_create_info.addressModeW =
      kAddressModeMap[uint32_t(parameters.clamp_z)];
  // LOD biasing is performed in shaders.
  if (parameters.aniso_filter != xenos::AnisoFilter::kDisabled) {
    sampler_create_info.anisotropyEnable = VK_TRUE;
    sampler_create_info.maxAnisotropy =
        float(UINT32_C(1) << (uint32_t(parameters.aniso_filter) -
                              uint32_t(xenos::AnisoFilter::kMax_1_1)));
  }
  sampler_create_info.minLod = float(parameters.mip_min_level);
  if (parameters.mip_base_map) {
    assert_false(parameters.mip_linear);
    sampler_create_info.maxLod = sampler_create_info.minLod + 0.25f;
  } else {
    sampler_create_info.maxLod = VK_LOD_CLAMP_NONE;
  }
  // TODO(Triang3l): Custom border colors for CrYCb / YCrCb.
  switch (parameters.border_color) {
    case xenos::BorderColor::k_ABGR_White:
      sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
      break;
    default:
      sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
      break;
  }
  VkSampler vulkan_sampler;
  if (dfn.vkCreateSampler(device, &sampler_create_info, nullptr,
                          &vulkan_sampler) != VK_SUCCESS) {
    XELOGE(
        "VulkanTextureCache: Failed to create the sampler for parameters "
        "0x{:08X}",
        parameters.value);
    has_overflown_out = false;
    return VK_NULL_HANDLE;
  }
  std::pair<const SamplerParameters, Sampler>& new_sampler =
      *(samplers_
            .emplace(std::piecewise_construct,
                     std::forward_as_tuple(parameters), std::forward_as_tuple())
            .first);
  COUNT_profile_set("gpu/texture_cache/vulkan/samplers", samplers_.size());
  new_sampler.second.sampler = vulkan_sampler;
  new_sampler.second.last_usage_submission = submission_current;
  new_sampler.second.used_previous = sampler_used_last_;
  new_sampler.second.used_next = nullptr;
  if (sampler_used_last_) {
    sampler_used_last_->second.used_next = &new_sampler;
  } else {
    sampler_used_first_ = &new_sampler;
  }
  sampler_used_last_ = &new_sampler;
  return vulkan_sampler;
}

uint64_t VulkanTextureCache::GetSubmissionToAwaitOnSamplerOverflow(
    uint32_t overflowed_sampler_count) const {
  if (!overflowed_sampler_count) {
    return 0;
  }
  std::pair<const SamplerParameters, Sampler>* sampler_used =
      sampler_used_first_;
  if (!sampler_used_first_) {
    return 0;
  }
  for (uint32_t samplers_remaining = overflowed_sampler_count - 1;
       samplers_remaining; --samplers_remaining) {
    std::pair<const SamplerParameters, Sampler>* sampler_used_next =
        sampler_used->second.used_next;
    if (!sampler_used_next) {
      break;
    }
    sampler_used = sampler_used_next;
  }
  return sampler_used->second.last_usage_submission;
}

VkImageView VulkanTextureCache::RequestSwapTexture(
    uint32_t& width_scaled_out, uint32_t& height_scaled_out,
    xenos::TextureFormat& format_out) {
  const auto& regs = register_file();
  const auto& fetch = regs.Get<xenos::xe_gpu_texture_fetch_t>(
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0);
  TextureKey key;
  BindingInfoFromFetchConstant(fetch, key, nullptr);
  if (!key.is_valid || key.base_page == 0 ||
      key.dimension != xenos::DataDimension::k2DOrStacked) {
    return nullptr;
  }
  VulkanTexture* texture =
      static_cast<VulkanTexture*>(FindOrCreateTexture(key));
  if (!texture) {
    return VK_NULL_HANDLE;
  }
  VkImageView texture_view = texture->GetView(
      false, GuestToHostSwizzle(fetch.swizzle, GetHostFormatSwizzle(key)),
      false);
  if (texture_view == VK_NULL_HANDLE) {
    return VK_NULL_HANDLE;
  }
  if (!LoadTextureData(*texture)) {
    return VK_NULL_HANDLE;
  }
  texture->MarkAsUsed();
  VulkanTexture::Usage old_usage =
      texture->SetUsage(VulkanTexture::Usage::kSwapSampled);
  if (old_usage != VulkanTexture::Usage::kSwapSampled) {
    VkPipelineStageFlags src_stage_mask, dst_stage_mask;
    VkAccessFlags src_access_mask, dst_access_mask;
    VkImageLayout old_layout, new_layout;
    GetTextureUsageMasks(old_usage, src_stage_mask, src_access_mask,
                         old_layout);
    GetTextureUsageMasks(VulkanTexture::Usage::kSwapSampled, dst_stage_mask,
                         dst_access_mask, new_layout);
    command_processor_.PushImageMemoryBarrier(
        texture->image(), ui::vulkan::util::InitializeSubresourceRange(),
        src_stage_mask, dst_stage_mask, src_access_mask, dst_access_mask,
        old_layout, new_layout);
  }
  // Only texture->key, not the result of BindingInfoFromFetchConstant, contains
  // whether the texture is scaled.
  key = texture->key();
  width_scaled_out =
      key.GetWidth() * (key.scaled_resolve ? draw_resolution_scale_x() : 1);
  height_scaled_out =
      key.GetHeight() * (key.scaled_resolve ? draw_resolution_scale_y() : 1);
  format_out = key.format;
  return texture_view;
}

bool VulkanTextureCache::IsSignedVersionSeparateForFormat(
    TextureKey key) const {
  const HostFormatPair& host_format_pair = GetHostFormatPair(key);
  if (host_format_pair.format_unsigned.format == VK_FORMAT_UNDEFINED ||
      host_format_pair.format_signed.format == VK_FORMAT_UNDEFINED) {
    // Just one signedness.
    return false;
  }
  return !host_format_pair.unsigned_signed_compatible;
}

uint32_t VulkanTextureCache::GetHostFormatSwizzle(TextureKey key) const {
  return GetHostFormatPair(key).swizzle;
}

uint32_t VulkanTextureCache::GetMaxHostTextureWidthHeight(
    xenos::DataDimension dimension) const {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const VkPhysicalDeviceLimits& device_limits =
      provider.device_properties().limits;
  switch (dimension) {
    case xenos::DataDimension::k1D:
    case xenos::DataDimension::k2DOrStacked:
      // 1D and 2D are emulated as 2D arrays.
      return device_limits.maxImageDimension2D;
    case xenos::DataDimension::k3D:
      return device_limits.maxImageDimension3D;
    case xenos::DataDimension::kCube:
      return device_limits.maxImageDimensionCube;
    default:
      assert_unhandled_case(dimension);
      return 0;
  }
}

uint32_t VulkanTextureCache::GetMaxHostTextureDepthOrArraySize(
    xenos::DataDimension dimension) const {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const VkPhysicalDeviceLimits& device_limits =
      provider.device_properties().limits;
  switch (dimension) {
    case xenos::DataDimension::k1D:
    case xenos::DataDimension::k2DOrStacked:
      // 1D and 2D are emulated as 2D arrays.
      return device_limits.maxImageArrayLayers;
    case xenos::DataDimension::k3D:
      return device_limits.maxImageDimension3D;
    case xenos::DataDimension::kCube:
      // Not requesting the imageCubeArray feature, and the Xenos doesn't
      // support cube map arrays.
      return 6;
    default:
      assert_unhandled_case(dimension);
      return 0;
  }
}

std::unique_ptr<TextureCache::Texture> VulkanTextureCache::CreateTexture(
    TextureKey key) {
  VkFormat formats[] = {VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED};
  const HostFormatPair& host_format = GetHostFormatPair(key);
  if (host_format.format_signed.format == VK_FORMAT_UNDEFINED) {
    // Only the unsigned format may be available, if at all.
    formats[0] = host_format.format_unsigned.format;
  } else if (host_format.format_unsigned.format == VK_FORMAT_UNDEFINED) {
    // Only the signed format may be available, if at all.
    formats[0] = host_format.format_signed.format;
  } else {
    // Both unsigned and signed formats are available.
    if (IsSignedVersionSeparateForFormat(key)) {
      formats[0] = key.signed_separate ? host_format.format_signed.format
                                       : host_format.format_unsigned.format;
    } else {
      // Same format for unsigned and signed, or compatible formats.
      formats[0] = host_format.format_unsigned.format;
      if (host_format.format_signed.format !=
          host_format.format_unsigned.format) {
        assert_not_zero(host_format.unsigned_signed_compatible);
        formats[1] = host_format.format_signed.format;
      }
    }
  }
  if (formats[0] == VK_FORMAT_UNDEFINED) {
    // TODO(Triang3l): If there's no best format, set that a format unsupported
    // by the emulator completely is used to report at the end of the frame.
    return nullptr;
  }

  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  bool is_3d = key.dimension == xenos::DataDimension::k3D;
  uint32_t depth_or_array_size = key.GetDepthOrArraySize();

  VkImageCreateInfo image_create_info;
  VkImageCreateInfo* image_create_info_last = &image_create_info;
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.pNext = nullptr;
  image_create_info.flags = 0;
  if (formats[1] != VK_FORMAT_UNDEFINED) {
    image_create_info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
  }
  if (key.dimension == xenos::DataDimension::kCube) {
    image_create_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  }
  image_create_info.imageType = is_3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
  image_create_info.format = formats[0];
  image_create_info.extent.width = key.GetWidth();
  image_create_info.extent.height = key.GetHeight();
  if (key.scaled_resolve) {
    image_create_info.extent.width *= draw_resolution_scale_x();
    image_create_info.extent.height *= draw_resolution_scale_y();
  }
  image_create_info.extent.depth = is_3d ? depth_or_array_size : 1;
  image_create_info.mipLevels = key.mip_max_level + 1;
  image_create_info.arrayLayers = is_3d ? 1 : depth_or_array_size;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = nullptr;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageFormatListCreateInfoKHR image_format_list_create_info;
  if (formats[1] != VK_FORMAT_UNDEFINED &&
      provider.device_extensions().khr_image_format_list) {
    image_create_info_last->pNext = &image_format_list_create_info;
    image_create_info_last =
        reinterpret_cast<VkImageCreateInfo*>(&image_format_list_create_info);
    image_format_list_create_info.sType =
        VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
    image_format_list_create_info.pNext = nullptr;
    image_format_list_create_info.viewFormatCount = 2;
    image_format_list_create_info.pViewFormats = formats;
  }

  VmaAllocationCreateInfo allocation_create_info = {};
  allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

  VkImage image;
  VmaAllocation allocation;
  if (vmaCreateImage(vma_allocator_, &image_create_info,
                     &allocation_create_info, &image, &allocation, nullptr)) {
    return nullptr;
  }

  return std::unique_ptr<Texture>(
      new VulkanTexture(*this, key, image, allocation));
}

bool VulkanTextureCache::LoadTextureDataFromResidentMemoryImpl(Texture& texture,
                                                               bool load_base,
                                                               bool load_mips) {
  VulkanTexture& vulkan_texture = static_cast<VulkanTexture&>(texture);
  TextureKey texture_key = vulkan_texture.key();

  // Get the pipeline.
  const HostFormatPair& host_format_pair = GetHostFormatPair(texture_key);
  bool host_format_is_signed;
  if (IsSignedVersionSeparateForFormat(texture_key)) {
    host_format_is_signed = bool(texture_key.signed_separate);
  } else {
    host_format_is_signed =
        host_format_pair.format_unsigned.load_shader == kLoadShaderIndexUnknown;
  }
  const HostFormat& host_format = host_format_is_signed
                                      ? host_format_pair.format_signed
                                      : host_format_pair.format_unsigned;
  LoadShaderIndex load_shader = host_format.load_shader;
  if (load_shader == kLoadShaderIndexUnknown) {
    return false;
  }
  VkPipeline pipeline = texture_key.scaled_resolve
                            ? load_pipelines_scaled_[load_shader]
                            : load_pipelines_[load_shader];
  if (pipeline == VK_NULL_HANDLE) {
    return false;
  }
  const LoadShaderInfo& load_shader_info = GetLoadShaderInfo(load_shader);

  // Get the guest layout.
  const texture_util::TextureGuestLayout& guest_layout =
      vulkan_texture.guest_layout();
  xenos::DataDimension dimension = texture_key.dimension;
  bool is_3d = dimension == xenos::DataDimension::k3D;
  uint32_t width = texture_key.GetWidth();
  uint32_t height = texture_key.GetHeight();
  uint32_t depth_or_array_size = texture_key.GetDepthOrArraySize();
  uint32_t depth = is_3d ? depth_or_array_size : 1;
  uint32_t array_size = is_3d ? 1 : depth_or_array_size;
  xenos::TextureFormat guest_format = texture_key.format;
  const FormatInfo* guest_format_info = FormatInfo::Get(guest_format);
  uint32_t block_width = guest_format_info->block_width;
  uint32_t block_height = guest_format_info->block_height;
  uint32_t bytes_per_block = guest_format_info->bytes_per_block();
  uint32_t level_first = load_base ? 0 : 1;
  uint32_t level_last = load_mips ? texture_key.mip_max_level : 0;
  assert_true(level_first <= level_last);
  uint32_t level_packed = guest_layout.packed_level;
  uint32_t level_stored_first = std::min(level_first, level_packed);
  uint32_t level_stored_last = std::min(level_last, level_packed);
  uint32_t texture_resolution_scale_x =
      texture_key.scaled_resolve ? draw_resolution_scale_x() : 1;
  uint32_t texture_resolution_scale_y =
      texture_key.scaled_resolve ? draw_resolution_scale_y() : 1;

  // The loop counter can mean two things depending on whether the packed mip
  // tail is stored as mip 0, because in this case, it would be ambiguous since
  // both the base and the mips would be on "level 0", but stored in separate
  // places.
  uint32_t loop_level_first, loop_level_last;
  if (level_packed == 0) {
    // Packed mip tail is the level 0 - may need to load mip tails for the base,
    // the mips, or both.
    // Loop iteration 0 - base packed mip tail.
    // Loop iteration 1 - mips packed mip tail.
    loop_level_first = uint32_t(level_first != 0);
    loop_level_last = uint32_t(level_last != 0);
  } else {
    // Packed mip tail is not the level 0.
    // Loop iteration is the actual level being loaded.
    loop_level_first = level_stored_first;
    loop_level_last = level_stored_last;
  }

  // Get the host layout and the buffer.
  uint32_t host_block_width = host_format.block_compressed ? block_width : 1;
  uint32_t host_block_height = host_format.block_compressed ? block_height : 1;
  uint32_t host_x_blocks_per_thread =
      UINT32_C(1) << load_shader_info.guest_x_blocks_per_thread_log2;
  if (!host_format.block_compressed) {
    // Decompressing guest blocks.
    host_x_blocks_per_thread *= block_width;
  }
  VkDeviceSize host_buffer_size = 0;
  struct HostLayout {
    VkDeviceSize offset_bytes;
    VkDeviceSize slice_size_bytes;
    uint32_t x_pitch_blocks;
    uint32_t y_pitch_blocks;
  };
  HostLayout host_layout_base;
  // Indexing is the same as for guest stored mips:
  // 1...min(level_last, level_packed) if level_packed is not 0, or only 0 if
  // level_packed == 0.
  HostLayout host_layout_mips[xenos::kTextureMaxMips];
  for (uint32_t loop_level = loop_level_first; loop_level <= loop_level_last;
       ++loop_level) {
    bool is_base = loop_level == 0;
    uint32_t level = (level_packed == 0) ? 0 : loop_level;
    HostLayout& level_host_layout =
        is_base ? host_layout_base : host_layout_mips[level];
    level_host_layout.offset_bytes = host_buffer_size;
    uint32_t level_guest_x_extent_texels_unscaled;
    uint32_t level_guest_y_extent_texels_unscaled;
    uint32_t level_guest_z_extent_texels;
    if (level == level_packed) {
      // Loading the packed tail for the base or the mips - load the whole tail
      // to copy regions out of it.
      const texture_util::TextureGuestLayout::Level& guest_layout_packed =
          is_base ? guest_layout.base : guest_layout.mips[level];
      level_guest_x_extent_texels_unscaled =
          guest_layout_packed.x_extent_blocks * block_width;
      level_guest_y_extent_texels_unscaled =
          guest_layout_packed.y_extent_blocks * block_height;
      level_guest_z_extent_texels = guest_layout_packed.z_extent;
    } else {
      level_guest_x_extent_texels_unscaled =
          std::max(width >> level, UINT32_C(1));
      level_guest_y_extent_texels_unscaled =
          std::max(height >> level, UINT32_C(1));
      level_guest_z_extent_texels = std::max(depth >> level, UINT32_C(1));
    }
    level_host_layout.x_pitch_blocks = xe::round_up(
        (level_guest_x_extent_texels_unscaled * texture_resolution_scale_x +
         (host_block_width - 1)) /
            host_block_width,
        host_x_blocks_per_thread);
    level_host_layout.y_pitch_blocks =
        (level_guest_y_extent_texels_unscaled * texture_resolution_scale_y +
         (host_block_height - 1)) /
        host_block_height;
    level_host_layout.slice_size_bytes =
        VkDeviceSize(load_shader_info.bytes_per_host_block) *
        level_host_layout.x_pitch_blocks * level_host_layout.y_pitch_blocks *
        level_guest_z_extent_texels;
    host_buffer_size += level_host_layout.slice_size_bytes * array_size;
  }
  VulkanCommandProcessor::ScratchBufferAcquisition scratch_buffer_acquisition(
      command_processor_.AcquireScratchGpuBuffer(
          host_buffer_size, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_WRITE_BIT));
  VkBuffer scratch_buffer = scratch_buffer_acquisition.buffer();
  if (scratch_buffer == VK_NULL_HANDLE) {
    return false;
  }

  // Begin loading.
  // TODO(Triang3l): Going from one descriptor to another on per-array-layer
  // or even per-8-depth-slices level to stay within maxStorageBufferRange.
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  VulkanSharedMemory& vulkan_shared_memory =
      static_cast<VulkanSharedMemory&>(shared_memory());
  std::array<VkWriteDescriptorSet, 3> write_descriptor_sets;
  uint32_t write_descriptor_set_count = 0;
  VkDescriptorSet descriptor_set_dest =
      command_processor_.AllocateSingleTransientDescriptor(
          VulkanCommandProcessor::SingleTransientDescriptorLayout ::
              kStorageBufferCompute);
  if (!descriptor_set_dest) {
    return false;
  }
  VkDescriptorBufferInfo write_descriptor_set_dest_buffer_info;
  {
    write_descriptor_set_dest_buffer_info.buffer = scratch_buffer;
    write_descriptor_set_dest_buffer_info.offset = 0;
    write_descriptor_set_dest_buffer_info.range = host_buffer_size;
    VkWriteDescriptorSet& write_descriptor_set_dest =
        write_descriptor_sets[write_descriptor_set_count++];
    write_descriptor_set_dest.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set_dest.pNext = nullptr;
    write_descriptor_set_dest.dstSet = descriptor_set_dest;
    write_descriptor_set_dest.dstBinding = 0;
    write_descriptor_set_dest.dstArrayElement = 0;
    write_descriptor_set_dest.descriptorCount = 1;
    write_descriptor_set_dest.descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_descriptor_set_dest.pImageInfo = nullptr;
    write_descriptor_set_dest.pBufferInfo =
        &write_descriptor_set_dest_buffer_info;
    write_descriptor_set_dest.pTexelBufferView = nullptr;
  }
  // TODO(Triang3l): Use a single 512 MB shared memory binding if possible.
  // TODO(Triang3l): Scaled resolve buffer bindings.
  // Aligning because if the data for a vector in a storage buffer is provided
  // partially, the value read may still be (0, 0, 0, 0), and small (especially
  // linear) textures won't be loaded correctly.
  uint32_t source_length_alignment = UINT32_C(1)
                                     << load_shader_info.source_bpe_log2;
  VkDescriptorSet descriptor_set_source_base = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set_source_mips = VK_NULL_HANDLE;
  VkDescriptorBufferInfo write_descriptor_set_source_base_buffer_info;
  VkDescriptorBufferInfo write_descriptor_set_source_mips_buffer_info;
  if (level_first == 0) {
    descriptor_set_source_base =
        command_processor_.AllocateSingleTransientDescriptor(
            VulkanCommandProcessor::SingleTransientDescriptorLayout ::
                kStorageBufferCompute);
    if (!descriptor_set_source_base) {
      return false;
    }
    write_descriptor_set_source_base_buffer_info.buffer =
        vulkan_shared_memory.buffer();
    write_descriptor_set_source_base_buffer_info.offset = texture_key.base_page
                                                          << 12;
    write_descriptor_set_source_base_buffer_info.range =
        xe::align(vulkan_texture.GetGuestBaseSize(), source_length_alignment);
    VkWriteDescriptorSet& write_descriptor_set_source_base =
        write_descriptor_sets[write_descriptor_set_count++];
    write_descriptor_set_source_base.sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set_source_base.pNext = nullptr;
    write_descriptor_set_source_base.dstSet = descriptor_set_source_base;
    write_descriptor_set_source_base.dstBinding = 0;
    write_descriptor_set_source_base.dstArrayElement = 0;
    write_descriptor_set_source_base.descriptorCount = 1;
    write_descriptor_set_source_base.descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_descriptor_set_source_base.pImageInfo = nullptr;
    write_descriptor_set_source_base.pBufferInfo =
        &write_descriptor_set_source_base_buffer_info;
    write_descriptor_set_source_base.pTexelBufferView = nullptr;
  }
  if (level_last != 0) {
    descriptor_set_source_mips =
        command_processor_.AllocateSingleTransientDescriptor(
            VulkanCommandProcessor::SingleTransientDescriptorLayout ::
                kStorageBufferCompute);
    if (!descriptor_set_source_mips) {
      return false;
    }
    write_descriptor_set_source_mips_buffer_info.buffer =
        vulkan_shared_memory.buffer();
    write_descriptor_set_source_mips_buffer_info.offset = texture_key.mip_page
                                                          << 12;
    write_descriptor_set_source_mips_buffer_info.range =
        xe::align(vulkan_texture.GetGuestMipsSize(), source_length_alignment);
    VkWriteDescriptorSet& write_descriptor_set_source_mips =
        write_descriptor_sets[write_descriptor_set_count++];
    write_descriptor_set_source_mips.sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set_source_mips.pNext = nullptr;
    write_descriptor_set_source_mips.dstSet = descriptor_set_source_mips;
    write_descriptor_set_source_mips.dstBinding = 0;
    write_descriptor_set_source_mips.dstArrayElement = 0;
    write_descriptor_set_source_mips.descriptorCount = 1;
    write_descriptor_set_source_mips.descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_descriptor_set_source_mips.pImageInfo = nullptr;
    write_descriptor_set_source_mips.pBufferInfo =
        &write_descriptor_set_source_mips_buffer_info;
    write_descriptor_set_source_mips.pTexelBufferView = nullptr;
  }
  if (write_descriptor_set_count) {
    dfn.vkUpdateDescriptorSets(device, write_descriptor_set_count,
                               write_descriptor_sets.data(), 0, nullptr);
  }
  vulkan_shared_memory.Use(VulkanSharedMemory::Usage::kRead);

  // Submit the copy buffer population commands.

  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();

  command_processor_.BindExternalComputePipeline(pipeline);

  command_buffer.CmdVkBindDescriptorSets(
      VK_PIPELINE_BIND_POINT_COMPUTE, load_pipeline_layout_,
      kLoadDescriptorSetIndexDestination, 1, &descriptor_set_dest, 0, nullptr);

  VkDescriptorSet descriptor_set_source_current = VK_NULL_HANDLE;

  LoadConstants load_constants;
  // 3 bits for each.
  assert_true(texture_resolution_scale_x <= 7);
  assert_true(texture_resolution_scale_y <= 7);
  load_constants.is_tiled_3d_endian_scale =
      uint32_t(texture_key.tiled) | (uint32_t(is_3d) << 1) |
      (uint32_t(texture_key.endianness) << 2) |
      (texture_resolution_scale_x << 4) | (texture_resolution_scale_y << 7);

  uint32_t guest_x_blocks_per_group_log2 =
      load_shader_info.GetGuestXBlocksPerGroupLog2();
  for (uint32_t loop_level = loop_level_first; loop_level <= loop_level_last;
       ++loop_level) {
    bool is_base = loop_level == 0;
    uint32_t level = (level_packed == 0) ? 0 : loop_level;

    VkDescriptorSet descriptor_set_source =
        is_base ? descriptor_set_source_base : descriptor_set_source_mips;
    if (descriptor_set_source_current != descriptor_set_source) {
      descriptor_set_source_current = descriptor_set_source;
      command_buffer.CmdVkBindDescriptorSets(
          VK_PIPELINE_BIND_POINT_COMPUTE, load_pipeline_layout_,
          kLoadDescriptorSetIndexSource, 1, &descriptor_set_source, 0, nullptr);
    }

    // TODO(Triang3l): guest_offset relative to the storage buffer origin.
    load_constants.guest_offset = 0;
    if (!is_base) {
      load_constants.guest_offset +=
          guest_layout.mip_offsets_bytes[level] *
          (texture_resolution_scale_x * texture_resolution_scale_y);
    }
    const texture_util::TextureGuestLayout::Level& level_guest_layout =
        is_base ? guest_layout.base : guest_layout.mips[level];
    uint32_t level_guest_pitch = level_guest_layout.row_pitch_bytes;
    if (texture_key.tiled) {
      // Shaders expect pitch in blocks for tiled textures.
      level_guest_pitch /= bytes_per_block;
      assert_zero(level_guest_pitch & (xenos::kTextureTileWidthHeight - 1));
    }
    load_constants.guest_pitch_aligned = level_guest_pitch;
    load_constants.guest_z_stride_block_rows_aligned =
        level_guest_layout.z_slice_stride_block_rows;
    assert_true(dimension != xenos::DataDimension::k3D ||
                !(load_constants.guest_z_stride_block_rows_aligned &
                  (xenos::kTextureTileWidthHeight - 1)));

    uint32_t level_width, level_height, level_depth;
    if (level == level_packed) {
      // This is the packed mip tail, containing not only the specified level,
      // but also other levels at different offsets - load the entire needed
      // extents.
      level_width = level_guest_layout.x_extent_blocks * block_width;
      level_height = level_guest_layout.y_extent_blocks * block_height;
      level_depth = level_guest_layout.z_extent;
    } else {
      level_width = std::max(width >> level, UINT32_C(1));
      level_height = std::max(height >> level, UINT32_C(1));
      level_depth = std::max(depth >> level, UINT32_C(1));
    }
    load_constants.size_blocks[0] = (level_width + (block_width - 1)) /
                                    block_width * texture_resolution_scale_x;
    load_constants.size_blocks[1] = (level_height + (block_height - 1)) /
                                    block_height * texture_resolution_scale_y;
    load_constants.size_blocks[2] = level_depth;
    load_constants.height_texels = level_height;

    uint32_t group_count_x =
        (load_constants.size_blocks[0] +
         ((UINT32_C(1) << guest_x_blocks_per_group_log2) - 1)) >>
        guest_x_blocks_per_group_log2;
    uint32_t group_count_y =
        (load_constants.size_blocks[1] +
         ((UINT32_C(1) << kLoadGuestYBlocksPerGroupLog2) - 1)) >>
        kLoadGuestYBlocksPerGroupLog2;

    // TODO(Triang3l): host_offset relative to the storage buffer origin.
    const HostLayout& level_host_layout =
        is_base ? host_layout_base : host_layout_mips[level];
    load_constants.host_offset = uint32_t(level_host_layout.offset_bytes);
    load_constants.host_pitch = load_shader_info.bytes_per_host_block *
                                level_host_layout.x_pitch_blocks;

    uint32_t level_array_slice_stride_bytes_scaled =
        level_guest_layout.array_slice_stride_bytes *
        (texture_resolution_scale_x * texture_resolution_scale_y);
    for (uint32_t slice = 0; slice < array_size; ++slice) {
      VkDescriptorSet descriptor_set_constants;
      void* constants_mapping =
          command_processor_.WriteTransientUniformBufferBinding(
              sizeof(load_constants),
              VulkanCommandProcessor::SingleTransientDescriptorLayout ::
                  kUniformBufferCompute,
              descriptor_set_constants);
      if (!constants_mapping) {
        return false;
      }
      std::memcpy(constants_mapping, &load_constants, sizeof(load_constants));
      command_buffer.CmdVkBindDescriptorSets(
          VK_PIPELINE_BIND_POINT_COMPUTE, load_pipeline_layout_,
          kLoadDescriptorSetIndexConstants, 1, &descriptor_set_constants, 0,
          nullptr);
      command_processor_.SubmitBarriers(true);
      command_buffer.CmdVkDispatch(group_count_x, group_count_y,
                                   load_constants.size_blocks[2]);
      load_constants.guest_offset += level_array_slice_stride_bytes_scaled;
      load_constants.host_offset +=
          uint32_t(level_host_layout.slice_size_bytes);
    }
  }

  // Submit copying from the copy buffer to the host texture.
  command_processor_.PushBufferMemoryBarrier(
      scratch_buffer, 0, VK_WHOLE_SIZE,
      scratch_buffer_acquisition.SetStageMask(VK_PIPELINE_STAGE_TRANSFER_BIT),
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      scratch_buffer_acquisition.SetAccessMask(VK_ACCESS_TRANSFER_READ_BIT),
      VK_ACCESS_TRANSFER_READ_BIT);
  vulkan_texture.MarkAsUsed();
  VulkanTexture::Usage texture_old_usage =
      vulkan_texture.SetUsage(VulkanTexture::Usage::kTransferDestination);
  if (texture_old_usage != VulkanTexture::Usage::kTransferDestination) {
    VkPipelineStageFlags texture_src_stage_mask, texture_dst_stage_mask;
    VkAccessFlags texture_src_access_mask, texture_dst_access_mask;
    VkImageLayout texture_old_layout, texture_new_layout;
    GetTextureUsageMasks(texture_old_usage, texture_src_stage_mask,
                         texture_src_access_mask, texture_old_layout);
    GetTextureUsageMasks(VulkanTexture::Usage::kTransferDestination,
                         texture_dst_stage_mask, texture_dst_access_mask,
                         texture_new_layout);
    command_processor_.PushImageMemoryBarrier(
        vulkan_texture.image(), ui::vulkan::util::InitializeSubresourceRange(),
        texture_src_stage_mask, texture_dst_stage_mask, texture_src_access_mask,
        texture_dst_access_mask, texture_old_layout, texture_new_layout);
  }
  command_processor_.SubmitBarriers(true);
  VkBufferImageCopy* copy_regions = command_buffer.CmdCopyBufferToImageEmplace(
      scratch_buffer, vulkan_texture.image(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, level_last - level_first + 1);
  for (uint32_t level = level_first; level <= level_last; ++level) {
    VkBufferImageCopy& copy_region = copy_regions[level - level_first];
    const HostLayout& level_host_layout =
        level != 0 ? host_layout_mips[std::min(level, level_packed)]
                   : host_layout_base;
    copy_region.bufferOffset = level_host_layout.offset_bytes;
    if (level >= level_packed) {
      uint32_t level_offset_blocks_x, level_offset_blocks_y, level_offset_z;
      texture_util::GetPackedMipOffset(width, height, depth, guest_format,
                                       level, level_offset_blocks_x,
                                       level_offset_blocks_y, level_offset_z);
      uint32_t level_offset_host_blocks_x =
          texture_resolution_scale_x * level_offset_blocks_x;
      uint32_t level_offset_host_blocks_y =
          texture_resolution_scale_y * level_offset_blocks_y;
      if (!host_format.block_compressed) {
        level_offset_host_blocks_x *= block_width;
        level_offset_host_blocks_y *= block_height;
      }
      copy_region.bufferOffset +=
          load_shader_info.bytes_per_host_block *
          (level_offset_host_blocks_x +
           level_host_layout.x_pitch_blocks *
               (level_offset_host_blocks_y + level_host_layout.y_pitch_blocks *
                                                 VkDeviceSize(level_offset_z)));
    }
    copy_region.bufferRowLength =
        level_host_layout.x_pitch_blocks * host_block_width;
    copy_region.bufferImageHeight =
        level_host_layout.y_pitch_blocks * host_block_height;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = level;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = array_size;
    copy_region.imageOffset.x = 0;
    copy_region.imageOffset.y = 0;
    copy_region.imageOffset.z = 0;
    copy_region.imageExtent.width =
        std::max((width * texture_resolution_scale_x) >> level, UINT32_C(1));
    copy_region.imageExtent.height =
        std::max((height * texture_resolution_scale_y) >> level, UINT32_C(1));
    copy_region.imageExtent.depth = std::max(depth >> level, UINT32_C(1));
  }

  return true;
}

void VulkanTextureCache::UpdateTextureBindingsImpl(
    uint32_t fetch_constant_mask) {
  uint32_t bindings_remaining = fetch_constant_mask;
  uint32_t binding_index;
  while (xe::bit_scan_forward(bindings_remaining, &binding_index)) {
    bindings_remaining &= ~(UINT32_C(1) << binding_index);
    VulkanTextureBinding& vulkan_binding =
        vulkan_texture_bindings_[binding_index];
    vulkan_binding.Reset();
    const TextureBinding* binding = GetValidTextureBinding(binding_index);
    if (!binding) {
      continue;
    }
    if (IsSignedVersionSeparateForFormat(binding->key)) {
      if (binding->texture &&
          texture_util::IsAnySignNotSigned(binding->swizzled_signs)) {
        vulkan_binding.image_view_unsigned =
            static_cast<VulkanTexture*>(binding->texture)
                ->GetView(false, binding->host_swizzle);
      }
      if (binding->texture_signed &&
          texture_util::IsAnySignSigned(binding->swizzled_signs)) {
        vulkan_binding.image_view_signed =
            static_cast<VulkanTexture*>(binding->texture_signed)
                ->GetView(true, binding->host_swizzle);
      }
    } else {
      VulkanTexture* texture = static_cast<VulkanTexture*>(binding->texture);
      if (texture) {
        if (texture_util::IsAnySignNotSigned(binding->swizzled_signs)) {
          vulkan_binding.image_view_unsigned =
              texture->GetView(false, binding->host_swizzle);
        }
        if (texture_util::IsAnySignSigned(binding->swizzled_signs)) {
          vulkan_binding.image_view_signed =
              texture->GetView(true, binding->host_swizzle);
        }
      }
    }
  }
}

VulkanTextureCache::VulkanTexture::VulkanTexture(
    VulkanTextureCache& texture_cache, const TextureKey& key, VkImage image,
    VmaAllocation allocation)
    : Texture(texture_cache, key), image_(image), allocation_(allocation) {
  VmaAllocationInfo allocation_info;
  vmaGetAllocationInfo(texture_cache.vma_allocator_, allocation_,
                       &allocation_info);
  SetHostMemoryUsage(uint64_t(allocation_info.size));
}

VulkanTextureCache::VulkanTexture::~VulkanTexture() {
  const VulkanTextureCache& vulkan_texture_cache =
      static_cast<const VulkanTextureCache&>(texture_cache());
  const ui::vulkan::VulkanProvider& provider =
      vulkan_texture_cache.command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  for (const auto& view_pair : views_) {
    dfn.vkDestroyImageView(device, view_pair.second, nullptr);
  }
  vmaDestroyImage(vulkan_texture_cache.vma_allocator_, image_, allocation_);
}

VkImageView VulkanTextureCache::VulkanTexture::GetView(bool is_signed,
                                                       uint32_t host_swizzle,
                                                       bool is_array) {
  xenos::DataDimension dimension = key().dimension;
  if (dimension == xenos::DataDimension::k3D ||
      dimension == xenos::DataDimension::kCube) {
    is_array = false;
  }

  const VulkanTextureCache& vulkan_texture_cache =
      static_cast<const VulkanTextureCache&>(texture_cache());

  ViewKey view_key;

  const HostFormatPair& host_format_pair =
      vulkan_texture_cache.GetHostFormatPair(key());
  VkFormat format = (is_signed ? host_format_pair.format_signed
                               : host_format_pair.format_unsigned)
                        .format;
  if (format == VK_FORMAT_UNDEFINED) {
    return VK_NULL_HANDLE;
  }
  // If not distinguishing between unsigned and signed formats for the same
  // image, don't create two views. As this happens within an image, no need to
  // care about whether unsigned and signed images are separate - if they are
  // (or if there are only unsigned or only signed images), this image will have
  // either all views unsigned or all views signed.
  view_key.is_signed_separate_view =
      is_signed && (host_format_pair.format_signed.format !=
                    host_format_pair.format_unsigned.format);

  const ui::vulkan::VulkanProvider& provider =
      vulkan_texture_cache.command_processor_.GetVulkanProvider();
  const VkPhysicalDevicePortabilitySubsetFeaturesKHR*
      device_portability_subset_features =
          provider.device_portability_subset_features();
  if (device_portability_subset_features &&
      !device_portability_subset_features->imageViewFormatSwizzle) {
    host_swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
  }
  view_key.host_swizzle = host_swizzle;

  view_key.is_array = uint32_t(is_array);

  // Try to find an existing view.
  auto it = views_.find(view_key);
  if (it != views_.end()) {
    return it->second;
  }

  // Create a new view.
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  VkImageViewCreateInfo view_create_info;
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.pNext = nullptr;
  view_create_info.flags = 0;
  view_create_info.image = image();
  view_create_info.format = format;
  view_create_info.components.r = GetComponentSwizzle(host_swizzle, 0);
  view_create_info.components.g = GetComponentSwizzle(host_swizzle, 1);
  view_create_info.components.b = GetComponentSwizzle(host_swizzle, 2);
  view_create_info.components.a = GetComponentSwizzle(host_swizzle, 3);
  view_create_info.subresourceRange =
      ui::vulkan::util::InitializeSubresourceRange();
  switch (dimension) {
    case xenos::DataDimension::k3D:
      view_create_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
      break;
    case xenos::DataDimension::kCube:
      view_create_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
      break;
    default:
      if (is_array) {
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      } else {
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_create_info.subresourceRange.layerCount = 1;
      }
      break;
  }
  VkImageView view;
  if (dfn.vkCreateImageView(device, &view_create_info, nullptr, &view) !=
      VK_SUCCESS) {
    XELOGE(
        "VulkanTextureCache: Failed to create an image view for Vulkan format "
        "{} ({}signed) with swizzle 0x{:3X}",
        uint32_t(format), is_signed ? "" : "un", host_swizzle);
    return VK_NULL_HANDLE;
  }
  views_.emplace(view_key, view);
  return view;
}

VulkanTextureCache::VulkanTextureCache(
    const RegisterFile& register_file, VulkanSharedMemory& shared_memory,
    uint32_t draw_resolution_scale_x, uint32_t draw_resolution_scale_y,
    VulkanCommandProcessor& command_processor,
    VkPipelineStageFlags guest_shader_pipeline_stages)
    : TextureCache(register_file, shared_memory, draw_resolution_scale_x,
                   draw_resolution_scale_y),
      command_processor_(command_processor),
      guest_shader_pipeline_stages_(guest_shader_pipeline_stages) {
  // TODO(Triang3l): Support draw resolution scaling.
  assert_true(draw_resolution_scale_x == 1 && draw_resolution_scale_y == 1);
}

bool VulkanTextureCache::Initialize() {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::InstanceFunctions& ifn = provider.ifn();
  VkPhysicalDevice physical_device = provider.physical_device();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  const VkPhysicalDevicePortabilitySubsetFeaturesKHR*
      device_portability_subset_features =
          provider.device_portability_subset_features();

  // Vulkan Memory Allocator.

  vma_allocator_ = ui::vulkan::CreateVmaAllocator(provider, true);
  if (vma_allocator_ == VK_NULL_HANDLE) {
    return false;
  }

  // Image formats.

  // Initialize to the best formats.
  for (size_t i = 0; i < xe::countof(host_formats_); ++i) {
    host_formats_[i] = kBestHostFormats[i];
  }

  // Check format support and switch to fallbacks if needed.
  constexpr VkFormatFeatureFlags kLinearFilterFeatures =
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  VkFormatProperties r16_unorm_properties;
  ifn.vkGetPhysicalDeviceFormatProperties(physical_device, VK_FORMAT_R16_UNORM,
                                          &r16_unorm_properties);
  VkFormatProperties r16_snorm_properties;
  ifn.vkGetPhysicalDeviceFormatProperties(physical_device, VK_FORMAT_R16_SNORM,
                                          &r16_snorm_properties);
  VkFormatProperties r16g16_unorm_properties;
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_R16G16_UNORM, &r16g16_unorm_properties);
  VkFormatProperties r16g16_snorm_properties;
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_R16G16_SNORM, &r16g16_snorm_properties);
  VkFormatProperties r16g16b16a16_unorm_properties;
  ifn.vkGetPhysicalDeviceFormatProperties(physical_device,
                                          VK_FORMAT_R16G16B16A16_UNORM,
                                          &r16g16b16a16_unorm_properties);
  VkFormatProperties r16g16b16a16_snorm_properties;
  ifn.vkGetPhysicalDeviceFormatProperties(physical_device,
                                          VK_FORMAT_R16G16B16A16_SNORM,
                                          &r16g16b16a16_snorm_properties);
  VkFormatProperties format_properties;
  // TODO(Triang3l): k_2_10_10_10 signed -> filterable R16G16B16A16_SFLOAT
  // (enough storage precision, possibly unwanted filtering precision change).
  // k_Cr_Y1_Cb_Y0_REP, k_Y1_Cr_Y0_Cb_REP.
  HostFormatPair& host_format_gbgr =
      host_formats_[uint32_t(xenos::TextureFormat::k_Cr_Y1_Cb_Y0_REP)];
  assert_true(host_format_gbgr.format_unsigned.format ==
              VK_FORMAT_G8B8G8R8_422_UNORM_KHR);
  assert_true(host_format_gbgr.format_signed.format ==
              VK_FORMAT_R8G8B8A8_SNORM);
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_G8B8G8R8_422_UNORM_KHR, &format_properties);
  if ((format_properties.optimalTilingFeatures & kLinearFilterFeatures) !=
      kLinearFilterFeatures) {
    host_format_gbgr.format_unsigned.load_shader = kLoadShaderIndexGBGR8ToRGB8;
    host_format_gbgr.format_unsigned.format = VK_FORMAT_R8G8B8A8_UNORM;
    host_format_gbgr.format_unsigned.block_compressed = false;
    host_format_gbgr.unsigned_signed_compatible = true;
  }
  HostFormatPair& host_format_bgrg =
      host_formats_[uint32_t(xenos::TextureFormat::k_Y1_Cr_Y0_Cb_REP)];
  assert_true(host_format_bgrg.format_unsigned.format ==
              VK_FORMAT_B8G8R8G8_422_UNORM_KHR);
  assert_true(host_format_bgrg.format_signed.format ==
              VK_FORMAT_R8G8B8A8_SNORM);
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_B8G8R8G8_422_UNORM_KHR, &format_properties);
  if ((format_properties.optimalTilingFeatures & kLinearFilterFeatures) !=
      kLinearFilterFeatures) {
    host_format_bgrg.format_unsigned.load_shader = kLoadShaderIndexBGRG8ToRGB8;
    host_format_bgrg.format_unsigned.format = VK_FORMAT_R8G8B8A8_UNORM;
    host_format_bgrg.format_unsigned.block_compressed = false;
    host_format_bgrg.unsigned_signed_compatible = true;
  }
  // TODO(Triang3l): k_10_11_11 -> filterable R16G16B16A16_SFLOAT (enough
  // storage precision, possibly unwanted filtering precision change).
  // TODO(Triang3l): k_11_11_10 -> filterable R16G16B16A16_SFLOAT (enough
  // storage precision, possibly unwanted filtering precision change).
  // S3TC.
  // Not checking the textureCompressionBC feature because its availability
  // means that all BC formats are supported, however, the device may expose
  // some BC formats without this feature. Xenia doesn't use BC6H and BC7 at
  // all, and has fallbacks for each used format.
  // TODO(Triang3l): Raise the host texture memory usage limit if S3TC has to be
  // decompressed.
  // TODO(Triang3l): S3TC -> 5551 or 4444 as an option.
  // TODO(Triang3l): S3TC -> ETC2 / EAC (a huge research topic).
  HostFormatPair& host_format_dxt1 =
      host_formats_[uint32_t(xenos::TextureFormat::k_DXT1)];
  assert_true(host_format_dxt1.format_unsigned.format ==
              VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, &format_properties);
  if ((format_properties.optimalTilingFeatures & kLinearFilterFeatures) !=
      kLinearFilterFeatures) {
    host_format_dxt1.format_unsigned.load_shader = kLoadShaderIndexDXT1ToRGBA8;
    host_format_dxt1.format_unsigned.format = VK_FORMAT_R8G8B8A8_UNORM;
    host_format_dxt1.format_unsigned.block_compressed = false;
    host_formats_[uint32_t(xenos::TextureFormat::k_DXT1_AS_16_16_16_16)] =
        host_format_dxt1;
  }
  HostFormatPair& host_format_dxt2_3 =
      host_formats_[uint32_t(xenos::TextureFormat::k_DXT2_3)];
  assert_true(host_format_dxt2_3.format_unsigned.format ==
              VK_FORMAT_BC2_UNORM_BLOCK);
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_BC2_UNORM_BLOCK, &format_properties);
  if ((format_properties.optimalTilingFeatures & kLinearFilterFeatures) !=
      kLinearFilterFeatures) {
    host_format_dxt2_3.format_unsigned.load_shader =
        kLoadShaderIndexDXT3ToRGBA8;
    host_format_dxt2_3.format_unsigned.format = VK_FORMAT_R8G8B8A8_UNORM;
    host_format_dxt2_3.format_unsigned.block_compressed = false;
    host_formats_[uint32_t(xenos::TextureFormat::k_DXT2_3_AS_16_16_16_16)] =
        host_format_dxt2_3;
  }
  HostFormatPair& host_format_dxt4_5 =
      host_formats_[uint32_t(xenos::TextureFormat::k_DXT4_5)];
  assert_true(host_format_dxt4_5.format_unsigned.format ==
              VK_FORMAT_BC3_UNORM_BLOCK);
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_BC3_UNORM_BLOCK, &format_properties);
  if ((format_properties.optimalTilingFeatures & kLinearFilterFeatures) !=
      kLinearFilterFeatures) {
    host_format_dxt4_5.format_unsigned.load_shader =
        kLoadShaderIndexDXT5ToRGBA8;
    host_format_dxt4_5.format_unsigned.format = VK_FORMAT_R8G8B8A8_UNORM;
    host_format_dxt4_5.format_unsigned.block_compressed = false;
    host_formats_[uint32_t(xenos::TextureFormat::k_DXT4_5_AS_16_16_16_16)] =
        host_format_dxt4_5;
  }
  HostFormatPair& host_format_dxn =
      host_formats_[uint32_t(xenos::TextureFormat::k_DXN)];
  assert_true(host_format_dxn.format_unsigned.format ==
              VK_FORMAT_BC5_UNORM_BLOCK);
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_BC5_UNORM_BLOCK, &format_properties);
  if ((format_properties.optimalTilingFeatures & kLinearFilterFeatures) !=
      kLinearFilterFeatures) {
    host_format_dxn.format_unsigned.load_shader = kLoadShaderIndexDXNToRG8;
    host_format_dxn.format_unsigned.format = VK_FORMAT_R8G8_UNORM;
    host_format_dxn.format_unsigned.block_compressed = false;
  }
  HostFormatPair& host_format_dxt5a =
      host_formats_[uint32_t(xenos::TextureFormat::k_DXT5A)];
  assert_true(host_format_dxt5a.format_unsigned.format ==
              VK_FORMAT_BC4_UNORM_BLOCK);
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_BC4_UNORM_BLOCK, &format_properties);
  if ((format_properties.optimalTilingFeatures & kLinearFilterFeatures) !=
      kLinearFilterFeatures) {
    host_format_dxt5a.format_unsigned.load_shader = kLoadShaderIndexDXT5AToR8;
    host_format_dxt5a.format_unsigned.format = VK_FORMAT_R8_UNORM;
    host_format_dxt5a.format_unsigned.block_compressed = false;
  }
  // k_16, k_16_16, k_16_16_16_16 - UNORM / SNORM are optional, fall back to
  // SFLOAT, which is mandatory and is always filterable (the guest 16-bit
  // format is filterable, 16-bit fixed-point is the full texture filtering
  // precision on the Xenos overall). Let the user choose what's more important,
  // precision (use host UNORM / SNORM if available even if they're not
  // filterable) or filterability (use host UNORM / SNORM only if they're
  // available and filterable).
  // TODO(Triang3l): Expose a cvar for selecting the preference (filterability
  // or precision).
  VkFormatFeatureFlags norm16_required_features =
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  HostFormatPair& host_format_16 =
      host_formats_[uint32_t(xenos::TextureFormat::k_16)];
  assert_true(host_format_16.format_unsigned.format == VK_FORMAT_R16_UNORM);
  if ((r16_unorm_properties.optimalTilingFeatures & norm16_required_features) !=
      norm16_required_features) {
    host_format_16.format_unsigned.load_shader =
        kLoadShaderIndexR16UNormToFloat;
    host_format_16.format_unsigned.format = VK_FORMAT_R16_SFLOAT;
  }
  assert_true(host_format_16.format_signed.format == VK_FORMAT_R16_SNORM);
  if ((r16_snorm_properties.optimalTilingFeatures & norm16_required_features) !=
      norm16_required_features) {
    host_format_16.format_signed.load_shader = kLoadShaderIndexR16SNormToFloat;
    host_format_16.format_signed.format = VK_FORMAT_R16_SFLOAT;
  }
  host_format_16.unsigned_signed_compatible =
      (host_format_16.format_unsigned.format == VK_FORMAT_R16_UNORM &&
       host_format_16.format_signed.format == VK_FORMAT_R16_SNORM) ||
      (host_format_16.format_unsigned.format == VK_FORMAT_R16_SFLOAT &&
       host_format_16.format_signed.format == VK_FORMAT_R16_SFLOAT);
  HostFormatPair& host_format_16_16 =
      host_formats_[uint32_t(xenos::TextureFormat::k_16_16)];
  assert_true(host_format_16_16.format_unsigned.format ==
              VK_FORMAT_R16G16_UNORM);
  if ((r16g16_unorm_properties.optimalTilingFeatures &
       norm16_required_features) != norm16_required_features) {
    host_format_16_16.format_unsigned.load_shader =
        kLoadShaderIndexRG16UNormToFloat;
    host_format_16_16.format_unsigned.format = VK_FORMAT_R16G16_SFLOAT;
  }
  assert_true(host_format_16_16.format_signed.format == VK_FORMAT_R16G16_SNORM);
  if ((r16g16_snorm_properties.optimalTilingFeatures &
       norm16_required_features) != norm16_required_features) {
    host_format_16_16.format_signed.load_shader =
        kLoadShaderIndexRG16SNormToFloat;
    host_format_16_16.format_signed.format = VK_FORMAT_R16G16_SFLOAT;
  }
  host_format_16_16.unsigned_signed_compatible =
      (host_format_16_16.format_unsigned.format == VK_FORMAT_R16G16_UNORM &&
       host_format_16_16.format_signed.format == VK_FORMAT_R16G16_SNORM) ||
      (host_format_16_16.format_unsigned.format == VK_FORMAT_R16G16_SFLOAT &&
       host_format_16_16.format_signed.format == VK_FORMAT_R16G16_SFLOAT);
  HostFormatPair& host_format_16_16_16_16 =
      host_formats_[uint32_t(xenos::TextureFormat::k_16_16_16_16)];
  assert_true(host_format_16_16_16_16.format_unsigned.format ==
              VK_FORMAT_R16G16B16A16_UNORM);
  if ((r16g16b16a16_unorm_properties.optimalTilingFeatures &
       norm16_required_features) != norm16_required_features) {
    host_format_16_16_16_16.format_unsigned.load_shader =
        kLoadShaderIndexRGBA16UNormToFloat;
    host_format_16_16_16_16.format_unsigned.format =
        VK_FORMAT_R16G16B16A16_SFLOAT;
  }
  assert_true(host_format_16_16_16_16.format_signed.format ==
              VK_FORMAT_R16G16B16A16_SNORM);
  if ((r16g16b16a16_snorm_properties.optimalTilingFeatures &
       norm16_required_features) != norm16_required_features) {
    host_format_16_16_16_16.format_signed.load_shader =
        kLoadShaderIndexRGBA16SNormToFloat;
    host_format_16_16_16_16.format_signed.format =
        VK_FORMAT_R16G16B16A16_SFLOAT;
  }
  host_format_16_16_16_16.unsigned_signed_compatible =
      (host_format_16_16_16_16.format_unsigned.format ==
           VK_FORMAT_R16G16B16A16_UNORM &&
       host_format_16_16_16_16.format_signed.format ==
           VK_FORMAT_R16G16B16A16_SNORM) ||
      (host_format_16_16_16_16.format_unsigned.format ==
           VK_FORMAT_R16G16B16A16_SFLOAT &&
       host_format_16_16_16_16.format_signed.format ==
           VK_FORMAT_R16G16B16A16_SFLOAT);

  // Normalize format information structures.
  for (size_t i = 0; i < xe::countof(host_formats_); ++i) {
    HostFormatPair& host_format = host_formats_[i];
    // load_shader_index is left uninitialized for the tail (non-existent
    // formats), kLoadShaderIndexUnknown may be non-zero, and format support may
    // be disabled by setting the format to VK_FORMAT_UNDEFINED.
    if (host_format.format_unsigned.format == VK_FORMAT_UNDEFINED) {
      host_format.format_unsigned.load_shader = kLoadShaderIndexUnknown;
    }
    assert_false(host_format.format_unsigned.load_shader ==
                     kLoadShaderIndexUnknown &&
                 host_format.format_unsigned.format != VK_FORMAT_UNDEFINED);
    if (host_format.format_unsigned.load_shader == kLoadShaderIndexUnknown) {
      host_format.format_unsigned.format = VK_FORMAT_UNDEFINED;
      // Surely known it's unsupported with these two conditions.
      host_format.format_unsigned.linear_filterable = false;
    }
    if (host_format.format_signed.format == VK_FORMAT_UNDEFINED) {
      host_format.format_signed.load_shader = kLoadShaderIndexUnknown;
    }
    assert_false(host_format.format_signed.load_shader ==
                     kLoadShaderIndexUnknown &&
                 host_format.format_signed.format != VK_FORMAT_UNDEFINED);
    if (host_format.format_signed.load_shader == kLoadShaderIndexUnknown) {
      host_format.format_signed.format = VK_FORMAT_UNDEFINED;
      // Surely known it's unsupported with these two conditions.
      host_format.format_signed.linear_filterable = false;
    }

    // Check if the formats are supported and are linear-filterable.
    if (host_format.format_unsigned.format != VK_FORMAT_UNDEFINED) {
      ifn.vkGetPhysicalDeviceFormatProperties(
          physical_device, host_format.format_unsigned.format,
          &format_properties);
      if (format_properties.optimalTilingFeatures &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
        host_format.format_unsigned.linear_filterable =
            (format_properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
      } else {
        host_format.format_unsigned.format = VK_FORMAT_UNDEFINED;
        host_format.format_unsigned.load_shader = kLoadShaderIndexUnknown;
        host_format.format_unsigned.linear_filterable = false;
      }
    }
    if (host_format.format_signed.format != VK_FORMAT_UNDEFINED) {
      ifn.vkGetPhysicalDeviceFormatProperties(physical_device,
                                              host_format.format_signed.format,
                                              &format_properties);
      if (format_properties.optimalTilingFeatures &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
        host_format.format_signed.linear_filterable =
            (format_properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
      } else {
        host_format.format_signed.format = VK_FORMAT_UNDEFINED;
        host_format.format_signed.load_shader = kLoadShaderIndexUnknown;
        host_format.format_signed.linear_filterable = false;
      }
    }

    // Log which formats are not supported or supported via fallbacks.
    const HostFormatPair& best_host_format = kBestHostFormats[i];
    const char* guest_format_name =
        FormatInfo::GetName(xenos::TextureFormat(i));
    if (best_host_format.format_unsigned.format != VK_FORMAT_UNDEFINED) {
      assert_not_null(guest_format_name);
      if (host_format.format_unsigned.format != VK_FORMAT_UNDEFINED) {
        if (host_format.format_unsigned.format !=
            best_host_format.format_unsigned.format) {
          XELOGGPU(
              "VulkanTextureCache: Format {} (unsigned) is supported via a "
              "fallback format (using the Vulkan format {} instead of the "
              "preferred {})",
              guest_format_name, uint32_t(host_format.format_unsigned.format),
              uint32_t(best_host_format.format_unsigned.format));
        }
      } else {
        XELOGGPU(
            "VulkanTextureCache: Format {} (unsigned) is not supported by the "
            "device (preferred Vulkan format is {})",
            guest_format_name,
            uint32_t(best_host_format.format_unsigned.format));
      }
    }
    if (best_host_format.format_signed.format != VK_FORMAT_UNDEFINED) {
      assert_not_null(guest_format_name);
      if (host_format.format_signed.format != VK_FORMAT_UNDEFINED) {
        if (host_format.format_signed.format !=
            best_host_format.format_signed.format) {
          XELOGGPU(
              "VulkanTextureCache: Format {} (signed) is supported via a "
              "fallback format (using the Vulkan format {} instead of the "
              "preferred {})",
              guest_format_name, uint32_t(host_format.format_signed.format),
              uint32_t(best_host_format.format_signed.format));
        }
      } else {
        XELOGGPU(
            "VulkanTextureCache: Format {} (signed) is not supported by the "
            "device (preferred Vulkan format is {})",
            guest_format_name, uint32_t(best_host_format.format_signed.format));
      }
    }

    // Signednesses with different load shaders must have the data loaded
    // differently, therefore can't share the image even if the format is the
    // same. Also, if there's only one version, simplify the logic - there can't
    // be compatibility between two formats when one of them is undefined.
    if (host_format.format_unsigned.format != VK_FORMAT_UNDEFINED &&
        host_format.format_signed.format != VK_FORMAT_UNDEFINED) {
      if (host_format.format_unsigned.load_shader ==
          host_format.format_signed.load_shader) {
        if (host_format.format_unsigned.format ==
            host_format.format_signed.format) {
          // Same format after all the fallbacks - force compatibilty.
          host_format.unsigned_signed_compatible = true;
        }
      } else {
        host_format.unsigned_signed_compatible = false;
      }
      // Formats within the same compatibility class must have the same block
      // size, though the fallbacks are configured incorrectly if that's not the
      // case (since such formats just can't be in one compatibility class).
      assert_false(host_format.unsigned_signed_compatible &&
                   host_format.format_unsigned.block_compressed !=
                       host_format.format_signed.block_compressed);
      if (host_format.unsigned_signed_compatible &&
          host_format.format_unsigned.block_compressed !=
              host_format.format_signed.block_compressed) {
        host_format.unsigned_signed_compatible = false;
      }
    } else {
      host_format.unsigned_signed_compatible = false;
    }
  }

  // Load pipeline layout.

  VkDescriptorSetLayout load_descriptor_set_layouts[kLoadDescriptorSetCount] =
      {};
  VkDescriptorSetLayout load_descriptor_set_layout_storage_buffer =
      command_processor_.GetSingleTransientDescriptorLayout(
          VulkanCommandProcessor::SingleTransientDescriptorLayout ::
              kStorageBufferCompute);
  assert_true(load_descriptor_set_layout_storage_buffer != VK_NULL_HANDLE);
  load_descriptor_set_layouts[kLoadDescriptorSetIndexDestination] =
      load_descriptor_set_layout_storage_buffer;
  load_descriptor_set_layouts[kLoadDescriptorSetIndexSource] =
      load_descriptor_set_layout_storage_buffer;
  load_descriptor_set_layouts[kLoadDescriptorSetIndexConstants] =
      command_processor_.GetSingleTransientDescriptorLayout(
          VulkanCommandProcessor::SingleTransientDescriptorLayout ::
              kUniformBufferCompute);
  assert_true(load_descriptor_set_layouts[kLoadDescriptorSetIndexConstants] !=
              VK_NULL_HANDLE);
  VkPipelineLayoutCreateInfo load_pipeline_layout_create_info;
  load_pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  load_pipeline_layout_create_info.pNext = nullptr;
  load_pipeline_layout_create_info.flags = 0;
  load_pipeline_layout_create_info.setLayoutCount = kLoadDescriptorSetCount;
  load_pipeline_layout_create_info.pSetLayouts = load_descriptor_set_layouts;
  load_pipeline_layout_create_info.pushConstantRangeCount = 0;
  load_pipeline_layout_create_info.pPushConstantRanges = nullptr;
  if (dfn.vkCreatePipelineLayout(device, &load_pipeline_layout_create_info,
                                 nullptr, &load_pipeline_layout_)) {
    XELOGE("VulkanTexture: Failed to create the texture load pipeline layout");
    return false;
  }

  // Load pipelines, only the ones needed for the formats that will be used.

  bool load_shaders_needed[kLoadShaderCount] = {};
  for (size_t i = 0; i < xe::countof(host_formats_); ++i) {
    const HostFormatPair& host_format = host_formats_[i];
    if (host_format.format_unsigned.load_shader != kLoadShaderIndexUnknown) {
      load_shaders_needed[host_format.format_unsigned.load_shader] = true;
    }
    if (host_format.format_signed.load_shader != kLoadShaderIndexUnknown) {
      load_shaders_needed[host_format.format_signed.load_shader] = true;
    }
  }
  if (kHostFormatGBGRUnaligned.format_unsigned.load_shader !=
      kLoadShaderIndexUnknown) {
    load_shaders_needed[kHostFormatGBGRUnaligned.format_unsigned.load_shader] =
        true;
  }
  if (kHostFormatGBGRUnaligned.format_signed.load_shader !=
      kLoadShaderIndexUnknown) {
    load_shaders_needed[kHostFormatGBGRUnaligned.format_signed.load_shader] =
        true;
  }
  if (kHostFormatBGRGUnaligned.format_unsigned.load_shader !=
      kLoadShaderIndexUnknown) {
    load_shaders_needed[kHostFormatBGRGUnaligned.format_unsigned.load_shader] =
        true;
  }
  if (kHostFormatBGRGUnaligned.format_signed.load_shader !=
      kLoadShaderIndexUnknown) {
    load_shaders_needed[kHostFormatBGRGUnaligned.format_signed.load_shader] =
        true;
  }

  std::pair<const uint32_t*, size_t> load_shader_code[kLoadShaderCount] = {};
  load_shader_code[kLoadShaderIndex8bpb] = std::make_pair(
      shaders::texture_load_8bpb_cs, sizeof(shaders::texture_load_8bpb_cs));
  load_shader_code[kLoadShaderIndex16bpb] = std::make_pair(
      shaders::texture_load_16bpb_cs, sizeof(shaders::texture_load_16bpb_cs));
  load_shader_code[kLoadShaderIndex32bpb] = std::make_pair(
      shaders::texture_load_32bpb_cs, sizeof(shaders::texture_load_32bpb_cs));
  load_shader_code[kLoadShaderIndex64bpb] = std::make_pair(
      shaders::texture_load_64bpb_cs, sizeof(shaders::texture_load_64bpb_cs));
  load_shader_code[kLoadShaderIndex128bpb] = std::make_pair(
      shaders::texture_load_128bpb_cs, sizeof(shaders::texture_load_128bpb_cs));
  load_shader_code[kLoadShaderIndexR5G5B5A1ToB5G5R5A1] =
      std::make_pair(shaders::texture_load_r5g5b5a1_b5g5r5a1_cs,
                     sizeof(shaders::texture_load_r5g5b5a1_b5g5r5a1_cs));
  load_shader_code[kLoadShaderIndexR5G6B5ToB5G6R5] =
      std::make_pair(shaders::texture_load_r5g6b5_b5g6r5_cs,
                     sizeof(shaders::texture_load_r5g6b5_b5g6r5_cs));
  load_shader_code[kLoadShaderIndexR5G5B6ToB5G6R5WithRBGASwizzle] =
      std::make_pair(
          shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs,
          sizeof(shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs));
  load_shader_code[kLoadShaderIndexRGBA4ToARGB4] =
      std::make_pair(shaders::texture_load_r4g4b4a4_a4r4g4b4_cs,
                     sizeof(shaders::texture_load_r4g4b4a4_a4r4g4b4_cs));
  load_shader_code[kLoadShaderIndexGBGR8ToRGB8] =
      std::make_pair(shaders::texture_load_gbgr8_rgb8_cs,
                     sizeof(shaders::texture_load_gbgr8_rgb8_cs));
  load_shader_code[kLoadShaderIndexBGRG8ToRGB8] =
      std::make_pair(shaders::texture_load_bgrg8_rgb8_cs,
                     sizeof(shaders::texture_load_bgrg8_rgb8_cs));
  load_shader_code[kLoadShaderIndexR10G11B11ToRGBA16] =
      std::make_pair(shaders::texture_load_r10g11b11_rgba16_cs,
                     sizeof(shaders::texture_load_r10g11b11_rgba16_cs));
  load_shader_code[kLoadShaderIndexR10G11B11ToRGBA16SNorm] =
      std::make_pair(shaders::texture_load_r10g11b11_rgba16_snorm_cs,
                     sizeof(shaders::texture_load_r10g11b11_rgba16_snorm_cs));
  load_shader_code[kLoadShaderIndexR11G11B10ToRGBA16] =
      std::make_pair(shaders::texture_load_r11g11b10_rgba16_cs,
                     sizeof(shaders::texture_load_r11g11b10_rgba16_cs));
  load_shader_code[kLoadShaderIndexR11G11B10ToRGBA16SNorm] =
      std::make_pair(shaders::texture_load_r11g11b10_rgba16_snorm_cs,
                     sizeof(shaders::texture_load_r11g11b10_rgba16_snorm_cs));
  load_shader_code[kLoadShaderIndexR16UNormToFloat] =
      std::make_pair(shaders::texture_load_r16_unorm_float_cs,
                     sizeof(shaders::texture_load_r16_unorm_float_cs));
  load_shader_code[kLoadShaderIndexR16SNormToFloat] =
      std::make_pair(shaders::texture_load_r16_snorm_float_cs,
                     sizeof(shaders::texture_load_r16_snorm_float_cs));
  load_shader_code[kLoadShaderIndexRG16UNormToFloat] =
      std::make_pair(shaders::texture_load_rg16_unorm_float_cs,
                     sizeof(shaders::texture_load_rg16_unorm_float_cs));
  load_shader_code[kLoadShaderIndexRG16SNormToFloat] =
      std::make_pair(shaders::texture_load_rg16_snorm_float_cs,
                     sizeof(shaders::texture_load_rg16_snorm_float_cs));
  load_shader_code[kLoadShaderIndexRGBA16UNormToFloat] =
      std::make_pair(shaders::texture_load_rgba16_unorm_float_cs,
                     sizeof(shaders::texture_load_rgba16_unorm_float_cs));
  load_shader_code[kLoadShaderIndexRGBA16SNormToFloat] =
      std::make_pair(shaders::texture_load_rgba16_snorm_float_cs,
                     sizeof(shaders::texture_load_rgba16_snorm_float_cs));
  load_shader_code[kLoadShaderIndexDXT1ToRGBA8] =
      std::make_pair(shaders::texture_load_dxt1_rgba8_cs,
                     sizeof(shaders::texture_load_dxt1_rgba8_cs));
  load_shader_code[kLoadShaderIndexDXT3ToRGBA8] =
      std::make_pair(shaders::texture_load_dxt3_rgba8_cs,
                     sizeof(shaders::texture_load_dxt3_rgba8_cs));
  load_shader_code[kLoadShaderIndexDXT5ToRGBA8] =
      std::make_pair(shaders::texture_load_dxt5_rgba8_cs,
                     sizeof(shaders::texture_load_dxt5_rgba8_cs));
  load_shader_code[kLoadShaderIndexDXNToRG8] =
      std::make_pair(shaders::texture_load_dxn_rg8_cs,
                     sizeof(shaders::texture_load_dxn_rg8_cs));
  load_shader_code[kLoadShaderIndexDXT3A] = std::make_pair(
      shaders::texture_load_dxt3a_cs, sizeof(shaders::texture_load_dxt3a_cs));
  load_shader_code[kLoadShaderIndexDXT3AAs1111ToARGB4] =
      std::make_pair(shaders::texture_load_dxt3aas1111_argb4_cs,
                     sizeof(shaders::texture_load_dxt3aas1111_argb4_cs));
  load_shader_code[kLoadShaderIndexDXT5AToR8] =
      std::make_pair(shaders::texture_load_dxt5a_r8_cs,
                     sizeof(shaders::texture_load_dxt5a_r8_cs));
  load_shader_code[kLoadShaderIndexCTX1] = std::make_pair(
      shaders::texture_load_ctx1_cs, sizeof(shaders::texture_load_ctx1_cs));
  load_shader_code[kLoadShaderIndexDepthUnorm] =
      std::make_pair(shaders::texture_load_depth_unorm_cs,
                     sizeof(shaders::texture_load_depth_unorm_cs));
  load_shader_code[kLoadShaderIndexDepthFloat] =
      std::make_pair(shaders::texture_load_depth_float_cs,
                     sizeof(shaders::texture_load_depth_float_cs));
  std::pair<const uint32_t*, size_t> load_shader_code_scaled[kLoadShaderCount] =
      {};
  if (IsDrawResolutionScaled()) {
    load_shader_code_scaled[kLoadShaderIndex8bpb] =
        std::make_pair(shaders::texture_load_8bpb_scaled_cs,
                       sizeof(shaders::texture_load_8bpb_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndex16bpb] =
        std::make_pair(shaders::texture_load_16bpb_scaled_cs,
                       sizeof(shaders::texture_load_16bpb_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndex32bpb] =
        std::make_pair(shaders::texture_load_32bpb_scaled_cs,
                       sizeof(shaders::texture_load_32bpb_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndex64bpb] =
        std::make_pair(shaders::texture_load_64bpb_scaled_cs,
                       sizeof(shaders::texture_load_64bpb_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndex128bpb] =
        std::make_pair(shaders::texture_load_128bpb_scaled_cs,
                       sizeof(shaders::texture_load_128bpb_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexR5G5B5A1ToB5G5R5A1] =
        std::make_pair(
            shaders::texture_load_r5g5b5a1_b5g5r5a1_scaled_cs,
            sizeof(shaders::texture_load_r5g5b5a1_b5g5r5a1_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexR5G6B5ToB5G6R5] =
        std::make_pair(shaders::texture_load_r5g6b5_b5g6r5_scaled_cs,
                       sizeof(shaders::texture_load_r5g6b5_b5g6r5_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexR5G5B6ToB5G6R5WithRBGASwizzle] =
        std::make_pair(
            shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_scaled_cs,
            sizeof(shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexRGBA4ToARGB4] = std::make_pair(
        shaders::texture_load_r4g4b4a4_a4r4g4b4_scaled_cs,
        sizeof(shaders::texture_load_r4g4b4a4_a4r4g4b4_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexR10G11B11ToRGBA16] = std::make_pair(
        shaders::texture_load_r10g11b11_rgba16_scaled_cs,
        sizeof(shaders::texture_load_r10g11b11_rgba16_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexR10G11B11ToRGBA16SNorm] =
        std::make_pair(
            shaders::texture_load_r10g11b11_rgba16_snorm_scaled_cs,
            sizeof(shaders::texture_load_r10g11b11_rgba16_snorm_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexR11G11B10ToRGBA16] = std::make_pair(
        shaders::texture_load_r11g11b10_rgba16_scaled_cs,
        sizeof(shaders::texture_load_r11g11b10_rgba16_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexR11G11B10ToRGBA16SNorm] =
        std::make_pair(
            shaders::texture_load_r11g11b10_rgba16_snorm_scaled_cs,
            sizeof(shaders::texture_load_r11g11b10_rgba16_snorm_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexR16UNormToFloat] =
        std::make_pair(shaders::texture_load_r16_unorm_float_scaled_cs,
                       sizeof(shaders::texture_load_r16_unorm_float_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexR16SNormToFloat] =
        std::make_pair(shaders::texture_load_r16_snorm_float_scaled_cs,
                       sizeof(shaders::texture_load_r16_snorm_float_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexRG16UNormToFloat] = std::make_pair(
        shaders::texture_load_rg16_unorm_float_scaled_cs,
        sizeof(shaders::texture_load_rg16_unorm_float_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexRG16SNormToFloat] = std::make_pair(
        shaders::texture_load_rg16_snorm_float_scaled_cs,
        sizeof(shaders::texture_load_rg16_snorm_float_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexRGBA16UNormToFloat] =
        std::make_pair(
            shaders::texture_load_rgba16_unorm_float_scaled_cs,
            sizeof(shaders::texture_load_rgba16_unorm_float_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexRGBA16SNormToFloat] =
        std::make_pair(
            shaders::texture_load_rgba16_snorm_float_scaled_cs,
            sizeof(shaders::texture_load_rgba16_snorm_float_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexDepthUnorm] =
        std::make_pair(shaders::texture_load_depth_unorm_scaled_cs,
                       sizeof(shaders::texture_load_depth_unorm_scaled_cs));
    load_shader_code_scaled[kLoadShaderIndexDepthFloat] =
        std::make_pair(shaders::texture_load_depth_float_scaled_cs,
                       sizeof(shaders::texture_load_depth_float_scaled_cs));
  }

  for (size_t i = 0; i < kLoadShaderCount; ++i) {
    if (!load_shaders_needed[i]) {
      continue;
    }
    const std::pair<const uint32_t*, size_t>& current_load_shader_code =
        load_shader_code[i];
    assert_not_null(current_load_shader_code.first);
    load_pipelines_[i] = ui::vulkan::util::CreateComputePipeline(
        provider, load_pipeline_layout_, current_load_shader_code.first,
        current_load_shader_code.second);
    if (load_pipelines_[i] == VK_NULL_HANDLE) {
      XELOGE(
          "VulkanTextureCache: Failed to create the texture loading pipeline "
          "for shader {}",
          i);
      return false;
    }
    if (IsDrawResolutionScaled()) {
      const std::pair<const uint32_t*, size_t>&
          current_load_shader_code_scaled = load_shader_code_scaled[i];
      if (current_load_shader_code_scaled.first) {
        load_pipelines_scaled_[i] = ui::vulkan::util::CreateComputePipeline(
            provider, load_pipeline_layout_,
            current_load_shader_code_scaled.first,
            current_load_shader_code_scaled.second);
        if (load_pipelines_scaled_[i] == VK_NULL_HANDLE) {
          XELOGE(
              "VulkanTextureCache: Failed to create the resolution-scaled "
              "texture loading pipeline for shader {}",
              i);
          return false;
        }
      }
    }
  }

  // Null images as a replacement for unneeded bindings and for bindings for
  // which the real image hasn't been created.
  // TODO(Triang3l): Use VK_EXT_robustness2 null descriptors.

  VkImageCreateInfo null_image_create_info;
  null_image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  null_image_create_info.pNext = nullptr;
  null_image_create_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  null_image_create_info.imageType = VK_IMAGE_TYPE_2D;
  // Four components to return (0, 0, 0, 0).
  // TODO(Triang3l): Find the return value for invalid texture fetch constants
  // on the real hardware.
  null_image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  null_image_create_info.extent.width = 1;
  null_image_create_info.extent.height = 1;
  null_image_create_info.extent.depth = 1;
  null_image_create_info.mipLevels = 1;
  null_image_create_info.arrayLayers = 6;
  null_image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  null_image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  null_image_create_info.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  null_image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  null_image_create_info.queueFamilyIndexCount = 0;
  null_image_create_info.pQueueFamilyIndices = nullptr;
  null_image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (dfn.vkCreateImage(device, &null_image_create_info, nullptr,
                        &null_image_2d_array_cube_) != VK_SUCCESS) {
    XELOGE(
        "VulkanTextureCache: Failed to create the null 2D array and cube "
        "image");
    return false;
  }

  null_image_create_info.flags &= ~VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  null_image_create_info.imageType = VK_IMAGE_TYPE_3D;
  null_image_create_info.arrayLayers = 1;
  if (dfn.vkCreateImage(device, &null_image_create_info, nullptr,
                        &null_image_3d_) != VK_SUCCESS) {
    XELOGE("VulkanTextureCache: Failed to create the null 3D image");
    return false;
  }

  VkMemoryRequirements null_image_memory_requirements_2d_array_cube_;
  dfn.vkGetImageMemoryRequirements(
      device, null_image_2d_array_cube_,
      &null_image_memory_requirements_2d_array_cube_);
  VkMemoryRequirements null_image_memory_requirements_3d_;
  dfn.vkGetImageMemoryRequirements(device, null_image_3d_,
                                   &null_image_memory_requirements_3d_);
  uint32_t null_image_memory_type_common = ui::vulkan::util::ChooseMemoryType(
      provider,
      null_image_memory_requirements_2d_array_cube_.memoryTypeBits &
          null_image_memory_requirements_3d_.memoryTypeBits,
      ui::vulkan::util::MemoryPurpose::kDeviceLocal);
  if (null_image_memory_type_common != UINT32_MAX) {
    // Place both null images in one memory allocation because maximum total
    // memory allocation count is limited.
    VkDeviceSize null_image_memory_offset_3d_ =
        xe::align(null_image_memory_requirements_2d_array_cube_.size,
                  std::max(null_image_memory_requirements_3d_.alignment,
                           VkDeviceSize(1)));
    VkMemoryAllocateInfo null_image_memory_allocate_info;
    null_image_memory_allocate_info.sType =
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    null_image_memory_allocate_info.pNext = nullptr;
    null_image_memory_allocate_info.allocationSize =
        null_image_memory_offset_3d_ + null_image_memory_requirements_3d_.size;
    null_image_memory_allocate_info.memoryTypeIndex =
        null_image_memory_type_common;
    if (dfn.vkAllocateMemory(device, &null_image_memory_allocate_info, nullptr,
                             &null_images_memory_[0]) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to allocate the memory for the null "
          "images");
      return false;
    }
    if (dfn.vkBindImageMemory(device, null_image_2d_array_cube_,
                              null_images_memory_[0], 0) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to bind the memory to the null 2D array "
          "and cube image");
      return false;
    }
    if (dfn.vkBindImageMemory(device, null_image_3d_, null_images_memory_[0],
                              null_image_memory_offset_3d_) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to bind the memory to the null 3D image");
      return false;
    }
  } else {
    // Place each null image in separate allocations.
    uint32_t null_image_memory_type_2d_array_cube_ =
        ui::vulkan::util::ChooseMemoryType(
            provider,
            null_image_memory_requirements_2d_array_cube_.memoryTypeBits,
            ui::vulkan::util::MemoryPurpose::kDeviceLocal);
    uint32_t null_image_memory_type_3d_ = ui::vulkan::util::ChooseMemoryType(
        provider, null_image_memory_requirements_3d_.memoryTypeBits,
        ui::vulkan::util::MemoryPurpose::kDeviceLocal);
    if (null_image_memory_type_2d_array_cube_ == UINT32_MAX ||
        null_image_memory_type_3d_ == UINT32_MAX) {
      XELOGE(
          "VulkanTextureCache: Failed to get the memory types for the null "
          "images");
      return false;
    }

    VkMemoryAllocateInfo null_image_memory_allocate_info;
    VkMemoryAllocateInfo* null_image_memory_allocate_info_last =
        &null_image_memory_allocate_info;
    null_image_memory_allocate_info.sType =
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    null_image_memory_allocate_info.pNext = nullptr;
    null_image_memory_allocate_info.allocationSize =
        null_image_memory_requirements_2d_array_cube_.size;
    null_image_memory_allocate_info.memoryTypeIndex =
        null_image_memory_type_2d_array_cube_;
    VkMemoryDedicatedAllocateInfoKHR null_image_memory_dedicated_allocate_info;
    if (provider.device_extensions().khr_dedicated_allocation) {
      null_image_memory_allocate_info_last->pNext =
          &null_image_memory_dedicated_allocate_info;
      null_image_memory_allocate_info_last =
          reinterpret_cast<VkMemoryAllocateInfo*>(
              &null_image_memory_dedicated_allocate_info);
      null_image_memory_dedicated_allocate_info.sType =
          VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
      null_image_memory_dedicated_allocate_info.pNext = nullptr;
      null_image_memory_dedicated_allocate_info.image =
          null_image_2d_array_cube_;
      null_image_memory_dedicated_allocate_info.buffer = VK_NULL_HANDLE;
    }
    if (dfn.vkAllocateMemory(device, &null_image_memory_allocate_info, nullptr,
                             &null_images_memory_[0]) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to allocate the memory for the null 2D "
          "array and cube image");
      return false;
    }
    if (dfn.vkBindImageMemory(device, null_image_2d_array_cube_,
                              null_images_memory_[0], 0) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to bind the memory to the null 2D array "
          "and cube image");
      return false;
    }

    null_image_memory_allocate_info.allocationSize =
        null_image_memory_requirements_3d_.size;
    null_image_memory_allocate_info.memoryTypeIndex =
        null_image_memory_type_3d_;
    null_image_memory_dedicated_allocate_info.image = null_image_3d_;
    if (dfn.vkAllocateMemory(device, &null_image_memory_allocate_info, nullptr,
                             &null_images_memory_[1]) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to allocate the memory for the null 3D "
          "image");
      return false;
    }
    if (dfn.vkBindImageMemory(device, null_image_3d_, null_images_memory_[1],
                              0) != VK_SUCCESS) {
      XELOGE(
          "VulkanTextureCache: Failed to bind the memory to the null 3D image");
      return false;
    }
  }

  VkImageViewCreateInfo null_image_view_create_info;
  null_image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  null_image_view_create_info.pNext = nullptr;
  null_image_view_create_info.flags = 0;
  null_image_view_create_info.image = null_image_2d_array_cube_;
  null_image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  null_image_view_create_info.format = null_image_create_info.format;
  // TODO(Triang3l): Find the return value for invalid texture fetch constants
  // on the real hardware.
  // Micro-optimization if this has any effect on the host GPU at all, use only
  // constant components instead of the real texels. The image will be cleared
  // to (0, 0, 0, 0) anyway.
  VkComponentSwizzle null_image_view_swizzle =
      (!device_portability_subset_features ||
       device_portability_subset_features->imageViewFormatSwizzle)
          ? VK_COMPONENT_SWIZZLE_ZERO
          : VK_COMPONENT_SWIZZLE_IDENTITY;
  null_image_view_create_info.components.r = null_image_view_swizzle;
  null_image_view_create_info.components.g = null_image_view_swizzle;
  null_image_view_create_info.components.b = null_image_view_swizzle;
  null_image_view_create_info.components.a = null_image_view_swizzle;
  null_image_view_create_info.subresourceRange =
      ui::vulkan::util::InitializeSubresourceRange(
          VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1);
  if (dfn.vkCreateImageView(device, &null_image_view_create_info, nullptr,
                            &null_image_view_2d_array_) != VK_SUCCESS) {
    XELOGE("VulkanTextureCache: Failed to create the null 2D array image view");
    return false;
  }
  null_image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
  null_image_view_create_info.subresourceRange.layerCount = 6;
  if (dfn.vkCreateImageView(device, &null_image_view_create_info, nullptr,
                            &null_image_view_cube_) != VK_SUCCESS) {
    XELOGE("VulkanTextureCache: Failed to create the null cube image view");
    return false;
  }
  null_image_view_create_info.image = null_image_3d_;
  null_image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
  null_image_view_create_info.subresourceRange.layerCount = 1;
  if (dfn.vkCreateImageView(device, &null_image_view_create_info, nullptr,
                            &null_image_view_3d_) != VK_SUCCESS) {
    XELOGE("VulkanTextureCache: Failed to create the null 3D image view");
    return false;
  }

  null_images_cleared_ = false;

  // Samplers.

  const VkPhysicalDeviceFeatures& device_features = provider.device_features();
  const VkPhysicalDeviceLimits& device_limits =
      provider.device_properties().limits;

  // Some MoltenVK devices have a maximum of 2048, 1024, or even 96 samplers,
  // below Vulkan's minimum requirement of 4000.
  // Assuming that the current VulkanTextureCache is the only one on this
  // VkDevice (true in a regular emulation scenario), so taking over all the
  // allocation slots exclusively.
  // Also leaving a few slots for use by things like overlay applications.
  sampler_max_count_ =
      device_limits.maxSamplerAllocationCount -
      uint32_t(ui::vulkan::VulkanProvider::HostSampler::kCount) - 16;

  if (device_features.samplerAnisotropy) {
    max_anisotropy_ = xenos::AnisoFilter(
        uint32_t(xenos::AnisoFilter::kMax_1_1) +
        (31 -
         xe::lzcnt(uint32_t(std::min(
             16.0f, std::max(1.0f, device_limits.maxSamplerAnisotropy))))));
  } else {
    max_anisotropy_ = xenos::AnisoFilter::kDisabled;
  }

  return true;
}

const VulkanTextureCache::HostFormatPair& VulkanTextureCache::GetHostFormatPair(
    TextureKey key) const {
  if (key.format == xenos::TextureFormat::k_Cr_Y1_Cb_Y0_REP &&
      (key.GetWidth() & 1)) {
    return kHostFormatGBGRUnaligned;
  }
  if (key.format == xenos::TextureFormat::k_Y1_Cr_Y0_Cb_REP &&
      (key.GetWidth() & 1)) {
    return kHostFormatBGRGUnaligned;
  }
  return host_formats_[uint32_t(key.format)];
}

void VulkanTextureCache::GetTextureUsageMasks(VulkanTexture::Usage usage,
                                              VkPipelineStageFlags& stage_mask,
                                              VkAccessFlags& access_mask,
                                              VkImageLayout& layout) {
  stage_mask = 0;
  access_mask = 0;
  layout = VK_IMAGE_LAYOUT_UNDEFINED;
  switch (usage) {
    case VulkanTexture::Usage::kUndefined:
      break;
    case VulkanTexture::Usage::kTransferDestination:
      stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
      access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
      layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      break;
    case VulkanTexture::Usage::kGuestShaderSampled:
      stage_mask = guest_shader_pipeline_stages_;
      access_mask = VK_ACCESS_SHADER_READ_BIT;
      layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      break;
    case VulkanTexture::Usage::kSwapSampled:
      // The swap texture is likely to be used only for the presentation
      // fragment shader, and not during emulation, where it'd be used in other
      // stages.
      stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      access_mask = VK_ACCESS_SHADER_READ_BIT;
      layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      break;
  }
}

xenos::ClampMode VulkanTextureCache::NormalizeClampMode(
    xenos::ClampMode clamp_mode) const {
  if (clamp_mode == xenos::ClampMode::kClampToHalfway) {
    // No GL_CLAMP (clamp to half edge, half border) equivalent in Vulkan, but
    // there's no Direct3D 9 equivalent anyway, and too weird to be suitable for
    // intentional real usage.
    return xenos::ClampMode::kClampToEdge;
  }
  if (clamp_mode == xenos::ClampMode::kMirrorClampToEdge ||
      clamp_mode == xenos::ClampMode::kMirrorClampToHalfway ||
      clamp_mode == xenos::ClampMode::kMirrorClampToBorder) {
    // TODO(Triang3l): VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE_KHR if
    // VK_KHR_sampler_mirror_clamp_to_edge (or Vulkan 1.2) and the
    // samplerMirrorClampToEdge feature are supported.
    return xenos::ClampMode::kMirroredRepeat;
  }
  return clamp_mode;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

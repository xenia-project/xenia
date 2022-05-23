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

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
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

const VulkanTextureCache::HostFormatPair
    VulkanTextureCache::kBestHostFormats[64] = {
        // k_1_REVERSE
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_1
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_8
        {{LoadMode::k8bpb, VK_FORMAT_R8_UNORM},
         {LoadMode::k8bpb, VK_FORMAT_R8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_1_5_5_5
        // Red and blue swapped in the load shader for simplicity.
        {{LoadMode::kR5G5B5A1ToB5G5R5A1, VK_FORMAT_A1R5G5B5_UNORM_PACK16},
         {},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_5_6_5
        // Red and blue swapped in the load shader for simplicity.
        {{LoadMode::kR5G6B5ToB5G6R5, VK_FORMAT_R5G6B5_UNORM_PACK16},
         {},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_6_5_5
        // On the host, green bits in blue, blue bits in green.
        {{LoadMode::kR5G5B6ToB5G6R5WithRBGASwizzle,
          VK_FORMAT_R5G6B5_UNORM_PACK16},
         {},
         XE_GPU_MAKE_TEXTURE_SWIZZLE(R, B, G, G)},
        // k_8_8_8_8
        {{LoadMode::k32bpb, VK_FORMAT_R8G8B8A8_UNORM},
         {LoadMode::k32bpb, VK_FORMAT_R8G8B8A8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_2_10_10_10
        // VK_FORMAT_A2B10G10R10_SNORM_PACK32 is optional.
        {{LoadMode::k32bpb, VK_FORMAT_A2B10G10R10_UNORM_PACK32},
         {LoadMode::k32bpb, VK_FORMAT_A2B10G10R10_SNORM_PACK32},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_8_A
        {{LoadMode::k8bpb, VK_FORMAT_R8_UNORM},
         {LoadMode::k8bpb, VK_FORMAT_R8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_8_B
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_8_8
        {{LoadMode::k16bpb, VK_FORMAT_R8G8_UNORM},
         {LoadMode::k16bpb, VK_FORMAT_R8G8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG,
         true},
        // k_Cr_Y1_Cb_Y0_REP
        // VK_FORMAT_G8B8G8R8_422_UNORM_KHR (added in
        // VK_KHR_sampler_ycbcr_conversion and promoted to Vulkan 1.1) is
        // optional.
        {{LoadMode::k32bpb, VK_FORMAT_G8B8G8R8_422_UNORM_KHR, true},
         {LoadMode::kGBGR8ToRGB8, VK_FORMAT_R8G8B8A8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_Y1_Cr_Y0_Cb_REP
        // VK_FORMAT_B8G8R8G8_422_UNORM_KHR (added in
        // VK_KHR_sampler_ycbcr_conversion and promoted to Vulkan 1.1) is
        // optional.
        {{LoadMode::k32bpb, VK_FORMAT_B8G8R8G8_422_UNORM_KHR, true},
         {LoadMode::kBGRG8ToRGB8, VK_FORMAT_R8G8B8A8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_16_16_EDRAM
        // Not usable as a texture, also has -32...32 range.
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_8_8_8_8_A
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_4_4_4_4
        // Components swapped in the load shader for simplicity.
        {{LoadMode::kRGBA4ToARGB4, VK_FORMAT_B4G4R4A4_UNORM_PACK16},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_10_11_11
        // TODO(Triang3l): 16_UNORM/SNORM are optional, convert to float16
        // instead.
        {{LoadMode::kR11G11B10ToRGBA16, VK_FORMAT_R16G16B16A16_UNORM},
         {LoadMode::kR11G11B10ToRGBA16SNorm, VK_FORMAT_R16G16B16A16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_11_11_10
        // TODO(Triang3l): 16_UNORM/SNORM are optional, convert to float16
        // instead.
        {{LoadMode::kR10G11B11ToRGBA16, VK_FORMAT_R16G16B16A16_UNORM},
         {LoadMode::kR10G11B11ToRGBA16SNorm, VK_FORMAT_R16G16B16A16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_DXT1
        // VK_FORMAT_BC1_RGBA_UNORM_BLOCK is optional.
        {{LoadMode::k64bpb, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, true},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_DXT2_3
        // VK_FORMAT_BC2_UNORM_BLOCK is optional.
        {{LoadMode::k128bpb, VK_FORMAT_BC2_UNORM_BLOCK, true},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_DXT4_5
        // VK_FORMAT_BC3_UNORM_BLOCK is optional.
        {{LoadMode::k128bpb, VK_FORMAT_BC3_UNORM_BLOCK, true},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_16_16_16_16_EDRAM
        // Not usable as a texture, also has -32...32 range.
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_24_8
        {{LoadMode::kDepthUnorm, VK_FORMAT_R32_SFLOAT},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_24_8_FLOAT
        {{LoadMode::kDepthFloat, VK_FORMAT_R32_SFLOAT},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_16
        // VK_FORMAT_R16_UNORM and VK_FORMAT_R16_SNORM are optional.
        {{LoadMode::k16bpb, VK_FORMAT_R16_UNORM},
         {LoadMode::k16bpb, VK_FORMAT_R16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_16_16
        // VK_FORMAT_R16G16_UNORM and VK_FORMAT_R16G16_SNORM are optional.
        {{LoadMode::k32bpb, VK_FORMAT_R16G16_UNORM},
         {LoadMode::k32bpb, VK_FORMAT_R16G16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG,
         true},
        // k_16_16_16_16
        // VK_FORMAT_R16G16B16A16_UNORM and VK_FORMAT_R16G16B16A16_SNORM are
        // optional.
        {{LoadMode::k64bpb, VK_FORMAT_R16G16B16A16_UNORM},
         {LoadMode::k64bpb, VK_FORMAT_R16G16B16A16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_16_EXPAND
        {{LoadMode::k16bpb, VK_FORMAT_R16_SFLOAT},
         {LoadMode::k16bpb, VK_FORMAT_R16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_16_16_EXPAND
        {{LoadMode::k32bpb, VK_FORMAT_R16G16_SFLOAT},
         {LoadMode::k32bpb, VK_FORMAT_R16G16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG,
         true},
        // k_16_16_16_16_EXPAND
        {{LoadMode::k64bpb, VK_FORMAT_R16G16B16A16_SFLOAT},
         {LoadMode::k64bpb, VK_FORMAT_R16G16B16A16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_16_FLOAT
        {{LoadMode::k16bpb, VK_FORMAT_R16_SFLOAT},
         {LoadMode::k16bpb, VK_FORMAT_R16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_16_16_FLOAT
        {{LoadMode::k32bpb, VK_FORMAT_R16G16_SFLOAT},
         {LoadMode::k32bpb, VK_FORMAT_R16G16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG,
         true},
        // k_16_16_16_16_FLOAT
        {{LoadMode::k64bpb, VK_FORMAT_R16G16B16A16_SFLOAT},
         {LoadMode::k64bpb, VK_FORMAT_R16G16B16A16_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_32
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_32_32
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_32_32_32_32
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_32_FLOAT
        {{LoadMode::k32bpb, VK_FORMAT_R32_SFLOAT},
         {LoadMode::k32bpb, VK_FORMAT_R32_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR,
         true},
        // k_32_32_FLOAT
        {{LoadMode::k64bpb, VK_FORMAT_R32G32_SFLOAT},
         {LoadMode::k64bpb, VK_FORMAT_R32G32_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG,
         true},
        // k_32_32_32_32_FLOAT
        {{LoadMode::k128bpb, VK_FORMAT_R32G32B32A32_SFLOAT},
         {LoadMode::k128bpb, VK_FORMAT_R32G32B32A32_SFLOAT},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_32_AS_8
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_32_AS_8_8
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_16_MPEG
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_16_16_MPEG
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_8_INTERLACED
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_32_AS_8_INTERLACED
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_32_AS_8_8_INTERLACED
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_16_INTERLACED
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_16_MPEG_INTERLACED
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_16_16_MPEG_INTERLACED
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_DXN
        // VK_FORMAT_BC5_UNORM_BLOCK is optional.
        {{LoadMode::k128bpb, VK_FORMAT_BC5_UNORM_BLOCK, true},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_8_8_8_8_AS_16_16_16_16
        {{LoadMode::k32bpb, VK_FORMAT_R8G8B8A8_UNORM},
         {LoadMode::k32bpb, VK_FORMAT_R8G8B8A8_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_DXT1_AS_16_16_16_16
        // VK_FORMAT_BC1_RGBA_UNORM_BLOCK is optional.
        {{LoadMode::k64bpb, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, true},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_DXT2_3_AS_16_16_16_16
        // VK_FORMAT_BC2_UNORM_BLOCK is optional.
        {{LoadMode::k128bpb, VK_FORMAT_BC2_UNORM_BLOCK, true},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_DXT4_5_AS_16_16_16_16
        // VK_FORMAT_BC3_UNORM_BLOCK is optional.
        {{LoadMode::k128bpb, VK_FORMAT_BC3_UNORM_BLOCK, true},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_2_10_10_10_AS_16_16_16_16
        // VK_FORMAT_A2B10G10R10_SNORM_PACK32 is optional.
        {{LoadMode::k32bpb, VK_FORMAT_A2B10G10R10_UNORM_PACK32},
         {LoadMode::k32bpb, VK_FORMAT_A2B10G10R10_SNORM_PACK32},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA,
         true},
        // k_10_11_11_AS_16_16_16_16
        // TODO(Triang3l): 16_UNORM/SNORM are optional, convert to float16
        // instead.
        {{LoadMode::kR11G11B10ToRGBA16, VK_FORMAT_R16G16B16A16_UNORM},
         {LoadMode::kR11G11B10ToRGBA16SNorm, VK_FORMAT_R16G16B16A16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_11_11_10_AS_16_16_16_16
        // TODO(Triang3l): 16_UNORM/SNORM are optional, convert to float16
        // instead.
        {{LoadMode::kR10G11B11ToRGBA16, VK_FORMAT_R16G16B16A16_UNORM},
         {LoadMode::kR10G11B11ToRGBA16SNorm, VK_FORMAT_R16G16B16A16_SNORM},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_32_32_32_FLOAT
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
        // k_DXT3A
        {{LoadMode::kDXT3A, VK_FORMAT_R8_UNORM},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_DXT5A
        // VK_FORMAT_BC4_UNORM_BLOCK is optional.
        {{LoadMode::k64bpb, VK_FORMAT_BC4_UNORM_BLOCK, true},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
        // k_CTX1
        {{LoadMode::kCTX1, VK_FORMAT_R8G8_UNORM},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
        // k_DXT3A_AS_1_1_1_1
        {{LoadMode::kDXT3AAs1111ToARGB4, VK_FORMAT_B4G4R4A4_UNORM_PACK16},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_8_8_8_8_GAMMA_EDRAM
        // Not usable as a texture.
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
        // k_2_10_10_10_FLOAT_EDRAM
        // Not usable as a texture.
        {{LoadMode::kUnknown},
         {LoadMode::kUnknown},
         xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
};

VulkanTextureCache::~VulkanTextureCache() {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

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

bool VulkanTextureCache::IsSignedVersionSeparateForFormat(
    TextureKey key) const {
  const HostFormatPair& host_format_pair = host_formats_[uint32_t(key.format)];
  if (host_format_pair.format_unsigned.format == VK_FORMAT_UNDEFINED ||
      host_format_pair.format_signed.format == VK_FORMAT_UNDEFINED) {
    // Just one signedness.
    return false;
  }
  return !host_format_pair.unsigned_signed_compatible;
}

uint32_t VulkanTextureCache::GetHostFormatSwizzle(TextureKey key) const {
  return host_formats_[uint32_t(key.format)].swizzle;
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
  const HostFormatPair& host_format = host_formats_[uint32_t(key.format)];
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
  // TODO(Triang3l): Suballocate due to the low memory allocation count limit on
  // Windows (use VMA or a custom allocator, possibly based on two-level
  // segregated fit just like VMA).
  VkImage image;
  VkDeviceMemory memory;
  VkDeviceSize memory_size;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          provider, image_create_info,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, image, memory, nullptr,
          &memory_size)) {
    return nullptr;
  }

  return std::unique_ptr<Texture>(
      new VulkanTexture(*this, key, image, memory, memory_size));
}

bool VulkanTextureCache::LoadTextureDataFromResidentMemoryImpl(Texture& texture,
                                                               bool load_base,
                                                               bool load_mips) {
  // TODO(Triang3l): Implement LoadTextureDataFromResidentMemoryImpl.
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
    VkDeviceMemory memory, VkDeviceSize memory_size)
    : Texture(texture_cache, key), image_(image), memory_(memory) {
  SetHostMemoryUsage(uint64_t(memory_size));
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
  dfn.vkDestroyImage(device, image_, nullptr);
  dfn.vkFreeMemory(device, memory_, nullptr);
}

VkImageView VulkanTextureCache::VulkanTexture::GetView(bool is_signed,
                                                       uint32_t host_swizzle) {
  const VulkanTextureCache& vulkan_texture_cache =
      static_cast<const VulkanTextureCache&>(texture_cache());

  ViewKey view_key;

  const HostFormatPair& host_format_pair =
      vulkan_texture_cache.host_formats_[uint32_t(key().format)];
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
  switch (key().dimension) {
    case xenos::DataDimension::k3D:
      view_create_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
      break;
    case xenos::DataDimension::kCube:
      view_create_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
      break;
    default:
      view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      break;
  }
  view_create_info.format = format;
  view_create_info.components.r = GetComponentSwizzle(host_swizzle, 0);
  view_create_info.components.g = GetComponentSwizzle(host_swizzle, 1);
  view_create_info.components.b = GetComponentSwizzle(host_swizzle, 2);
  view_create_info.components.a = GetComponentSwizzle(host_swizzle, 3);
  view_create_info.subresourceRange =
      ui::vulkan::util::InitializeSubresourceRange();
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

  // Image formats.

  // Initialize to the best formats.
  for (size_t i = 0; i < 64; ++i) {
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
    host_format_gbgr.format_unsigned.load_mode = LoadMode::kGBGR8ToRGB8;
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
    host_format_bgrg.format_unsigned.load_mode = LoadMode::kBGRG8ToRGB8;
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
  // some BC formats without this feature. Xenia doesn't BC6H and BC7 at all,
  // and has fallbacks for each used format.
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
    host_format_dxt1.format_unsigned.load_mode = LoadMode::kDXT1ToRGBA8;
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
    host_format_dxt2_3.format_unsigned.load_mode = LoadMode::kDXT3ToRGBA8;
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
    host_format_dxt4_5.format_unsigned.load_mode = LoadMode::kDXT5ToRGBA8;
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
    host_format_dxn.format_unsigned.load_mode = LoadMode::kDXNToRG8;
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
    host_format_dxt5a.format_unsigned.load_mode = LoadMode::kDXT5AToR8;
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
    host_format_16.format_unsigned.load_mode = LoadMode::kR16UNormToFloat;
    host_format_16.format_unsigned.format = VK_FORMAT_R16_SFLOAT;
  }
  assert_true(host_format_16.format_signed.format == VK_FORMAT_R16_SNORM);
  if ((r16_snorm_properties.optimalTilingFeatures & norm16_required_features) !=
      norm16_required_features) {
    host_format_16.format_signed.load_mode = LoadMode::kR16SNormToFloat;
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
    host_format_16_16.format_unsigned.load_mode = LoadMode::kRG16UNormToFloat;
    host_format_16_16.format_unsigned.format = VK_FORMAT_R16G16_SFLOAT;
  }
  assert_true(host_format_16_16.format_signed.format == VK_FORMAT_R16G16_SNORM);
  if ((r16g16_snorm_properties.optimalTilingFeatures &
       norm16_required_features) != norm16_required_features) {
    host_format_16_16.format_signed.load_mode = LoadMode::kRG16SNormToFloat;
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
    host_format_16_16_16_16.format_unsigned.load_mode =
        LoadMode::kRGBA16UNormToFloat;
    host_format_16_16_16_16.format_unsigned.format =
        VK_FORMAT_R16G16B16A16_SFLOAT;
  }
  assert_true(host_format_16_16_16_16.format_signed.format ==
              VK_FORMAT_R16G16B16A16_SNORM);
  if ((r16g16b16a16_snorm_properties.optimalTilingFeatures &
       norm16_required_features) != norm16_required_features) {
    host_format_16_16_16_16.format_signed.load_mode =
        LoadMode::kRGBA16SNormToFloat;
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
  for (size_t i = 0; i < 64; ++i) {
    HostFormatPair& host_format = host_formats_[i];
    // LoadMode is left uninitialized for the tail (non-existent formats),
    // kUnknown may be non-zero, and format support may be disabled by setting
    // the format to VK_FORMAT_UNDEFINED.
    if (host_format.format_unsigned.format == VK_FORMAT_UNDEFINED) {
      host_format.format_unsigned.load_mode = LoadMode::kUnknown;
    }
    assert_false(host_format.format_unsigned.load_mode == LoadMode::kUnknown &&
                 host_format.format_unsigned.format != VK_FORMAT_UNDEFINED);
    if (host_format.format_unsigned.load_mode == LoadMode::kUnknown) {
      host_format.format_unsigned.format = VK_FORMAT_UNDEFINED;
      // Surely known it's unsupported with these two conditions.
      host_format.format_unsigned.linear_filterable = false;
    }
    if (host_format.format_signed.format == VK_FORMAT_UNDEFINED) {
      host_format.format_signed.load_mode = LoadMode::kUnknown;
    }
    assert_false(host_format.format_signed.load_mode == LoadMode::kUnknown &&
                 host_format.format_signed.format != VK_FORMAT_UNDEFINED);
    if (host_format.format_signed.load_mode == LoadMode::kUnknown) {
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
        host_format.format_unsigned.load_mode = LoadMode::kUnknown;
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
        host_format.format_signed.load_mode = LoadMode::kUnknown;
        host_format.format_signed.linear_filterable = false;
      }
    }

    // Log which formats are not supported or supported via fallbacks.
    const HostFormatPair& best_host_format = kBestHostFormats[i];
    const char* guest_format_name =
        FormatInfo::Get(xenos::TextureFormat(i))->name;
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

    // Signednesses with different load modes must have the data loaded
    // differently, therefore can't share the image even if the format is the
    // same. Also, if there's only one version, simplify the logic - there can't
    // be compatibility between two formats when one of them is undefined.
    if (host_format.format_unsigned.format != VK_FORMAT_UNDEFINED &&
        host_format.format_signed.format != VK_FORMAT_UNDEFINED) {
      if (host_format.format_unsigned.load_mode ==
          host_format.format_signed.load_mode) {
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

  return true;
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
      // The swap texture is likely to be used only for the presentation compute
      // shader, and not during emulation, where it'd be used in other stages.
      stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      access_mask = VK_ACCESS_SHADER_READ_BIT;
      layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      break;
  }
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

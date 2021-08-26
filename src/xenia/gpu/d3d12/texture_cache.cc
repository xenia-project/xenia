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
#include <array>
#include <cfloat>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/xxhash.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/d3d12/d3d12_shared_memory.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/ui/d3d12/d3d12_upload_buffer_pool.h"
#include "xenia/ui/d3d12/d3d12_util.h"

DEFINE_uint32(
    texture_cache_memory_limit_soft, 384,
    "Maximum host texture memory usage (in megabytes) above which old textures "
    "will be destroyed.",
    "GPU");
DEFINE_uint32(
    texture_cache_memory_limit_soft_lifetime, 30,
    "Seconds a texture should be unused to be considered old enough to be "
    "deleted if texture memory usage exceeds texture_cache_memory_limit_soft.",
    "GPU");
DEFINE_uint32(
    texture_cache_memory_limit_hard, 768,
    "Maximum host texture memory usage (in megabytes) above which textures "
    "will be destroyed as soon as possible.",
    "GPU");
DEFINE_uint32(
    texture_cache_memory_limit_render_to_texture, 24,
    "Part of the host texture memory budget (in megabytes) that will be scaled "
    "by the current drawing resolution scale.\n"
    "If texture_cache_memory_limit_soft, for instance, is 384, and this is 24, "
    "it will be assumed that the game will be using roughly 24 MB of "
    "render-to-texture (resolve) targets and 384 - 24 = 360 MB of regular "
    "textures - so with 2x resolution scaling, the soft limit will be 360 + 96 "
    "MB, and with 3x, it will be 360 + 216 MB.",
    "GPU");

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_128bpb_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_128bpb_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_128bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_16bpb_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_16bpb_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_16bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_32bpb_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_32bpb_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_32bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_64bpb_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_64bpb_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_64bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_8bpb_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_8bpb_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_8bpb_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_ctx1_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_depth_float_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_depth_float_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_depth_float_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_depth_unorm_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_depth_unorm_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_depth_unorm_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxn_rg8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt1_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt3_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt3a_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt3aas1111_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt5_rgba8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_dxt5a_r8_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r10g11b11_rgba16_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r10g11b11_rgba16_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r10g11b11_rgba16_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r10g11b11_rgba16_snorm_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r10g11b11_rgba16_snorm_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r10g11b11_rgba16_snorm_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r11g11b10_rgba16_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r11g11b10_rgba16_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r11g11b10_rgba16_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r11g11b10_rgba16_snorm_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r11g11b10_rgba16_snorm_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r11g11b10_rgba16_snorm_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r4g4b4a4_b4g4r4a4_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r4g4b4a4_b4g4r4a4_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r4g4b4a4_b4g4r4a4_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g5b5a1_b5g5r5a1_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g5b5a1_b5g5r5a1_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g5b5a1_b5g5r5a1_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g5b6_b5g6r5_swizzle_rbga_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g5b6_b5g6r5_swizzle_rbga_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g6b5_b5g6r5_2x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g6b5_b5g6r5_3x_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/texture_load_r5g6b5_b5g6r5_cs.h"
}  // namespace shaders

// For formats with less than 4 components, assuming the last component is
// replicated into the non-existent ones, similar to what is done for unused
// components of operands in shaders.
// For DXT3A and DXT5A, RRRR swizzle is specified in:
// http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
// Halo 3 also expects replicated components in k_8 sprites.
// DXN is read as RG in Halo 3, but as RA in Call of Duty.
// TODO(Triang3l): Find out the correct contents of unused texture components.
const TextureCache::HostFormat TextureCache::host_formats_[64] = {
    // k_1_REVERSE
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_1
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_8
    {DXGI_FORMAT_R8_TYPELESS,
     DXGI_FORMAT_R8_UNORM,
     LoadMode::k8bpb,
     DXGI_FORMAT_R8_SNORM,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_1_5_5_5
    // Red and blue swapped in the load shader for simplicity.
    {DXGI_FORMAT_B5G5R5A1_UNORM,
     DXGI_FORMAT_B5G5R5A1_UNORM,
     LoadMode::kR5G5B5A1ToB5G5R5A1,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_5_6_5
    // Red and blue swapped in the load shader for simplicity.
    {DXGI_FORMAT_B5G6R5_UNORM,
     DXGI_FORMAT_B5G6R5_UNORM,
     LoadMode::kR5G6B5ToB5G6R5,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 2}},
    // k_6_5_5
    // On the host, green bits in blue, blue bits in green.
    {DXGI_FORMAT_B5G6R5_UNORM,
     DXGI_FORMAT_B5G6R5_UNORM,
     LoadMode::kR5G5B6ToB5G6R5WithRBGASwizzle,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 2, 1, 1}},
    // k_8_8_8_8
    {DXGI_FORMAT_R8G8B8A8_TYPELESS,
     DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::k32bpb,
     DXGI_FORMAT_R8G8B8A8_SNORM,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_2_10_10_10
    {DXGI_FORMAT_R10G10B10A2_TYPELESS,
     DXGI_FORMAT_R10G10B10A2_UNORM,
     LoadMode::k32bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_8_A
    {DXGI_FORMAT_R8_TYPELESS,
     DXGI_FORMAT_R8_UNORM,
     LoadMode::k8bpb,
     DXGI_FORMAT_R8_SNORM,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_8_B
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_8_8
    {DXGI_FORMAT_R8G8_TYPELESS,
     DXGI_FORMAT_R8G8_UNORM,
     LoadMode::k16bpb,
     DXGI_FORMAT_R8G8_SNORM,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_Cr_Y1_Cb_Y0_REP
    // Red and blue probably must be swapped, similar to k_Y1_Cr_Y0_Cb_REP.
    {DXGI_FORMAT_G8R8_G8B8_UNORM,
     DXGI_FORMAT_G8R8_G8B8_UNORM,
     LoadMode::k32bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     true,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {2, 1, 0, 3}},
    // k_Y1_Cr_Y0_Cb_REP
    // Used for videos in NBA 2K9. Red and blue must be swapped.
    // TODO(Triang3l): D3DFMT_G8R8_G8B8 is DXGI_FORMAT_R8G8_B8G8_UNORM * 255.0f,
    // watch out for num_format int, division in shaders, etc., in NBA 2K9 it
    // works as is. Also need to decompress if the size is uneven, but should be
    // a very rare case.
    {DXGI_FORMAT_R8G8_B8G8_UNORM,
     DXGI_FORMAT_R8G8_B8G8_UNORM,
     LoadMode::k32bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     true,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {2, 1, 0, 3}},
    // k_16_16_EDRAM
    // Not usable as a texture, also has -32...32 range.
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_8_8_8_8_A
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_4_4_4_4
    // Red and blue swapped in the load shader for simplicity.
    {DXGI_FORMAT_B4G4R4A4_UNORM,
     DXGI_FORMAT_B4G4R4A4_UNORM,
     LoadMode::kR4G4B4A4ToB4G4R4A4,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_10_11_11
    {DXGI_FORMAT_R16G16B16A16_TYPELESS,
     DXGI_FORMAT_R16G16B16A16_UNORM,
     LoadMode::kR11G11B10ToRGBA16,
     DXGI_FORMAT_R16G16B16A16_SNORM,
     LoadMode::kR11G11B10ToRGBA16SNorm,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 2}},
    // k_11_11_10
    {DXGI_FORMAT_R16G16B16A16_TYPELESS,
     DXGI_FORMAT_R16G16B16A16_UNORM,
     LoadMode::kR10G11B11ToRGBA16,
     DXGI_FORMAT_R16G16B16A16_SNORM,
     LoadMode::kR10G11B11ToRGBA16SNorm,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 2}},
    // k_DXT1
    {DXGI_FORMAT_BC1_UNORM,
     DXGI_FORMAT_BC1_UNORM,
     LoadMode::k64bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     true,
     DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT1ToRGBA8,
     {0, 1, 2, 3}},
    // k_DXT2_3
    {DXGI_FORMAT_BC2_UNORM,
     DXGI_FORMAT_BC2_UNORM,
     LoadMode::k128bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     true,
     DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT3ToRGBA8,
     {0, 1, 2, 3}},
    // k_DXT4_5
    {DXGI_FORMAT_BC3_UNORM,
     DXGI_FORMAT_BC3_UNORM,
     LoadMode::k128bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     true,
     DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT5ToRGBA8,
     {0, 1, 2, 3}},
    // k_16_16_16_16_EDRAM
    // Not usable as a texture, also has -32...32 range.
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // R32_FLOAT for depth because shaders would require an additional SRV to
    // sample stencil, which we don't provide.
    // k_24_8
    {DXGI_FORMAT_R32_FLOAT,
     DXGI_FORMAT_R32_FLOAT,
     LoadMode::kDepthUnorm,
     DXGI_FORMAT_R32_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_24_8_FLOAT
    {DXGI_FORMAT_R32_FLOAT,
     DXGI_FORMAT_R32_FLOAT,
     LoadMode::kDepthFloat,
     DXGI_FORMAT_R32_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_16
    {DXGI_FORMAT_R16_TYPELESS,
     DXGI_FORMAT_R16_UNORM,
     LoadMode::k16bpb,
     DXGI_FORMAT_R16_SNORM,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_16_16
    {DXGI_FORMAT_R16G16_TYPELESS,
     DXGI_FORMAT_R16G16_UNORM,
     LoadMode::k32bpb,
     DXGI_FORMAT_R16G16_SNORM,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_16_16_16_16
    {DXGI_FORMAT_R16G16B16A16_TYPELESS,
     DXGI_FORMAT_R16G16B16A16_UNORM,
     LoadMode::k64bpb,
     DXGI_FORMAT_R16G16B16A16_SNORM,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_16_EXPAND
    {DXGI_FORMAT_R16_FLOAT,
     DXGI_FORMAT_R16_FLOAT,
     LoadMode::k16bpb,
     DXGI_FORMAT_R16_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_16_16_EXPAND
    {DXGI_FORMAT_R16G16_FLOAT,
     DXGI_FORMAT_R16G16_FLOAT,
     LoadMode::k32bpb,
     DXGI_FORMAT_R16G16_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_16_16_16_16_EXPAND
    {DXGI_FORMAT_R16G16B16A16_FLOAT,
     DXGI_FORMAT_R16G16B16A16_FLOAT,
     LoadMode::k64bpb,
     DXGI_FORMAT_R16G16B16A16_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_16_FLOAT
    {DXGI_FORMAT_R16_FLOAT,
     DXGI_FORMAT_R16_FLOAT,
     LoadMode::k16bpb,
     DXGI_FORMAT_R16_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_16_16_FLOAT
    {DXGI_FORMAT_R16G16_FLOAT,
     DXGI_FORMAT_R16G16_FLOAT,
     LoadMode::k32bpb,
     DXGI_FORMAT_R16G16_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_16_16_16_16_FLOAT
    {DXGI_FORMAT_R16G16B16A16_FLOAT,
     DXGI_FORMAT_R16G16B16A16_FLOAT,
     LoadMode::k64bpb,
     DXGI_FORMAT_R16G16B16A16_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_32
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_32_32
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_32_32_32_32
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_32_FLOAT
    {DXGI_FORMAT_R32_FLOAT,
     DXGI_FORMAT_R32_FLOAT,
     LoadMode::k32bpb,
     DXGI_FORMAT_R32_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_32_32_FLOAT
    {DXGI_FORMAT_R32G32_FLOAT,
     DXGI_FORMAT_R32G32_FLOAT,
     LoadMode::k64bpb,
     DXGI_FORMAT_R32G32_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_32_32_32_32_FLOAT
    {DXGI_FORMAT_R32G32B32A32_FLOAT,
     DXGI_FORMAT_R32G32B32A32_FLOAT,
     LoadMode::k128bpb,
     DXGI_FORMAT_R32G32B32A32_FLOAT,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_32_AS_8
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_32_AS_8_8
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_16_MPEG
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_16_16_MPEG
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_32_AS_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_32_AS_8_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_16_INTERLACED
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_16_MPEG_INTERLACED
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_16_16_MPEG_INTERLACED
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_DXN
    {DXGI_FORMAT_BC5_UNORM,
     DXGI_FORMAT_BC5_UNORM,
     LoadMode::k128bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     true,
     DXGI_FORMAT_R8G8_UNORM,
     LoadMode::kDXNToRG8,
     {0, 1, 1, 1}},
    // k_8_8_8_8_AS_16_16_16_16
    {DXGI_FORMAT_R8G8B8A8_TYPELESS,
     DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::k32bpb,
     DXGI_FORMAT_R8G8B8A8_SNORM,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_DXT1_AS_16_16_16_16
    {DXGI_FORMAT_BC1_UNORM,
     DXGI_FORMAT_BC1_UNORM,
     LoadMode::k64bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     true,
     DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT1ToRGBA8,
     {0, 1, 2, 3}},
    // k_DXT2_3_AS_16_16_16_16
    {DXGI_FORMAT_BC2_UNORM,
     DXGI_FORMAT_BC2_UNORM,
     LoadMode::k128bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     true,
     DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT3ToRGBA8,
     {0, 1, 2, 3}},
    // k_DXT4_5_AS_16_16_16_16
    {DXGI_FORMAT_BC3_UNORM,
     DXGI_FORMAT_BC3_UNORM,
     LoadMode::k128bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     true,
     DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT5ToRGBA8,
     {0, 1, 2, 3}},
    // k_2_10_10_10_AS_16_16_16_16
    {DXGI_FORMAT_R10G10B10A2_UNORM,
     DXGI_FORMAT_R10G10B10A2_UNORM,
     LoadMode::k32bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_10_11_11_AS_16_16_16_16
    {DXGI_FORMAT_R16G16B16A16_TYPELESS,
     DXGI_FORMAT_R16G16B16A16_UNORM,
     LoadMode::kR11G11B10ToRGBA16,
     DXGI_FORMAT_R16G16B16A16_SNORM,
     LoadMode::kR11G11B10ToRGBA16SNorm,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 2}},
    // k_11_11_10_AS_16_16_16_16
    {DXGI_FORMAT_R16G16B16A16_TYPELESS,
     DXGI_FORMAT_R16G16B16A16_UNORM,
     LoadMode::kR10G11B11ToRGBA16,
     DXGI_FORMAT_R16G16B16A16_SNORM,
     LoadMode::kR10G11B11ToRGBA16SNorm,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 2}},
    // k_32_32_32_FLOAT
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 2}},
    // k_DXT3A
    // R8_UNORM has the same size as BC2, but doesn't have the 4x4 size
    // alignment requirement.
    {DXGI_FORMAT_R8_UNORM,
     DXGI_FORMAT_R8_UNORM,
     LoadMode::kDXT3A,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 0, 0, 0}},
    // k_DXT5A
    {DXGI_FORMAT_BC4_UNORM,
     DXGI_FORMAT_BC4_UNORM,
     LoadMode::k64bpb,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     true,
     DXGI_FORMAT_R8_UNORM,
     LoadMode::kDXT5AToR8,
     {0, 0, 0, 0}},
    // k_CTX1
    {DXGI_FORMAT_R8G8_UNORM,
     DXGI_FORMAT_R8G8_UNORM,
     LoadMode::kCTX1,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 1, 1}},
    // k_DXT3A_AS_1_1_1_1
    {DXGI_FORMAT_B4G4R4A4_UNORM,
     DXGI_FORMAT_B4G4R4A4_UNORM,
     LoadMode::kDXT3AAs1111,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_8_8_8_8_GAMMA_EDRAM
    // Not usable as a texture.
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
    // k_2_10_10_10_FLOAT_EDRAM
    // Not usable as a texture.
    {DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     false,
     DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown,
     {0, 1, 2, 3}},
};

const char* const TextureCache::dimension_names_[4] = {"1D", "2D", "3D",
                                                       "cube"};

const TextureCache::LoadModeInfo TextureCache::load_mode_info_[] = {
    {{{shaders::texture_load_8bpb_cs, sizeof(shaders::texture_load_8bpb_cs), 3,
       4, 16},
      {shaders::texture_load_8bpb_2x_cs,
       sizeof(shaders::texture_load_8bpb_2x_cs), 4, 4, 16 * 2},
      {shaders::texture_load_8bpb_3x_cs,
       sizeof(shaders::texture_load_8bpb_3x_cs), 3, 3, 16 * 3}}},
    {{{shaders::texture_load_16bpb_cs, sizeof(shaders::texture_load_16bpb_cs),
       4, 4, 16},
      {shaders::texture_load_16bpb_2x_cs,
       sizeof(shaders::texture_load_16bpb_2x_cs), 4, 4, 16 * 2},
      {shaders::texture_load_16bpb_3x_cs,
       sizeof(shaders::texture_load_16bpb_3x_cs), 3, 3, 16 * 3}}},
    {{{shaders::texture_load_32bpb_cs, sizeof(shaders::texture_load_32bpb_cs),
       4, 4, 8},
      {shaders::texture_load_32bpb_2x_cs,
       sizeof(shaders::texture_load_32bpb_2x_cs), 4, 4, 8 * 2},
      {shaders::texture_load_32bpb_3x_cs,
       sizeof(shaders::texture_load_32bpb_3x_cs), 3, 3, 2 * 3}}},
    {{{shaders::texture_load_64bpb_cs, sizeof(shaders::texture_load_64bpb_cs),
       4, 4, 4},
      {shaders::texture_load_64bpb_2x_cs,
       sizeof(shaders::texture_load_64bpb_2x_cs), 4, 4, 4 * 2},
      {shaders::texture_load_64bpb_3x_cs,
       sizeof(shaders::texture_load_64bpb_3x_cs), 3, 3, 4 * 3}}},
    {{{shaders::texture_load_128bpb_cs, sizeof(shaders::texture_load_128bpb_cs),
       4, 4, 2},
      {shaders::texture_load_128bpb_2x_cs,
       sizeof(shaders::texture_load_128bpb_2x_cs), 4, 4, 2 * 2},
      {shaders::texture_load_128bpb_3x_cs,
       sizeof(shaders::texture_load_128bpb_3x_cs), 4, 4, 2 * 3}}},
    {{{shaders::texture_load_r5g5b5a1_b5g5r5a1_cs,
       sizeof(shaders::texture_load_r5g5b5a1_b5g5r5a1_cs), 4, 4, 16},
      {shaders::texture_load_r5g5b5a1_b5g5r5a1_2x_cs,
       sizeof(shaders::texture_load_r5g5b5a1_b5g5r5a1_2x_cs), 4, 4, 16 * 2},
      {shaders::texture_load_r5g5b5a1_b5g5r5a1_3x_cs,
       sizeof(shaders::texture_load_r5g5b5a1_b5g5r5a1_3x_cs), 3, 3, 16 * 3}}},
    {{{shaders::texture_load_r5g6b5_b5g6r5_cs,
       sizeof(shaders::texture_load_r5g6b5_b5g6r5_cs), 4, 4, 16},
      {shaders::texture_load_r5g6b5_b5g6r5_2x_cs,
       sizeof(shaders::texture_load_r5g6b5_b5g6r5_2x_cs), 4, 4, 16 * 2},
      {shaders::texture_load_r5g6b5_b5g6r5_3x_cs,
       sizeof(shaders::texture_load_r5g6b5_b5g6r5_3x_cs), 3, 3, 16 * 3}}},
    {{{shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs,
       sizeof(shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_cs), 4, 4, 16},
      {shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_2x_cs,
       sizeof(shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_2x_cs), 4, 4,
       16 * 2},
      {shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_3x_cs,
       sizeof(shaders::texture_load_r5g5b6_b5g6r5_swizzle_rbga_3x_cs), 3, 3,
       16 * 3}}},
    {{{shaders::texture_load_r4g4b4a4_b4g4r4a4_cs,
       sizeof(shaders::texture_load_r4g4b4a4_b4g4r4a4_cs), 4, 4, 16},
      {shaders::texture_load_r4g4b4a4_b4g4r4a4_2x_cs,
       sizeof(shaders::texture_load_r4g4b4a4_b4g4r4a4_2x_cs), 4, 4, 16 * 2},
      {shaders::texture_load_r4g4b4a4_b4g4r4a4_3x_cs,
       sizeof(shaders::texture_load_r4g4b4a4_b4g4r4a4_3x_cs), 3, 3, 16 * 3}}},
    {{{shaders::texture_load_r10g11b11_rgba16_cs,
       sizeof(shaders::texture_load_r10g11b11_rgba16_cs), 4, 4, 8},
      {shaders::texture_load_r10g11b11_rgba16_2x_cs,
       sizeof(shaders::texture_load_r10g11b11_rgba16_2x_cs), 4, 4, 8 * 2},
      {shaders::texture_load_r10g11b11_rgba16_3x_cs,
       sizeof(shaders::texture_load_r10g11b11_rgba16_3x_cs), 3, 3, 2 * 3}}},
    {{{shaders::texture_load_r10g11b11_rgba16_snorm_cs,
       sizeof(shaders::texture_load_r10g11b11_rgba16_snorm_cs), 4, 4, 8},
      {shaders::texture_load_r10g11b11_rgba16_snorm_2x_cs,
       sizeof(shaders::texture_load_r10g11b11_rgba16_snorm_2x_cs), 4, 4, 8 * 2},
      {shaders::texture_load_r10g11b11_rgba16_snorm_3x_cs,
       sizeof(shaders::texture_load_r10g11b11_rgba16_snorm_3x_cs), 3, 3,
       2 * 3}}},
    {{{shaders::texture_load_r11g11b10_rgba16_cs,
       sizeof(shaders::texture_load_r11g11b10_rgba16_cs), 4, 4, 8},
      {shaders::texture_load_r11g11b10_rgba16_2x_cs,
       sizeof(shaders::texture_load_r11g11b10_rgba16_2x_cs), 4, 4, 8 * 2},
      {shaders::texture_load_r11g11b10_rgba16_3x_cs,
       sizeof(shaders::texture_load_r11g11b10_rgba16_3x_cs), 3, 3, 2 * 3}}},
    {{{shaders::texture_load_r11g11b10_rgba16_snorm_cs,
       sizeof(shaders::texture_load_r11g11b10_rgba16_snorm_cs), 4, 4, 8},
      {shaders::texture_load_r11g11b10_rgba16_snorm_2x_cs,
       sizeof(shaders::texture_load_r11g11b10_rgba16_snorm_2x_cs), 4, 4, 8 * 2},
      {shaders::texture_load_r11g11b10_rgba16_snorm_3x_cs,
       sizeof(shaders::texture_load_r11g11b10_rgba16_snorm_3x_cs), 3, 3,
       2 * 3}}},
    {{{shaders::texture_load_dxt1_rgba8_cs,
       sizeof(shaders::texture_load_dxt1_rgba8_cs), 4, 4, 4},
      {},
      {}}},
    {{{shaders::texture_load_dxt3_rgba8_cs,
       sizeof(shaders::texture_load_dxt3_rgba8_cs), 4, 4, 2},
      {},
      {}}},
    {{{shaders::texture_load_dxt5_rgba8_cs,
       sizeof(shaders::texture_load_dxt5_rgba8_cs), 4, 4, 2},
      {},
      {}}},
    {{{shaders::texture_load_dxn_rg8_cs,
       sizeof(shaders::texture_load_dxn_rg8_cs), 4, 4, 2},
      {},
      {}}},
    {{{shaders::texture_load_dxt3a_cs, sizeof(shaders::texture_load_dxt3a_cs),
       4, 4, 4},
      {},
      {}}},
    {{{shaders::texture_load_dxt3aas1111_cs,
       sizeof(shaders::texture_load_dxt3aas1111_cs), 4, 4, 4},
      {},
      {}}},
    {{{shaders::texture_load_dxt5a_r8_cs,
       sizeof(shaders::texture_load_dxt5a_r8_cs), 4, 4, 4},
      {},
      {}}},
    {{{shaders::texture_load_ctx1_cs, sizeof(shaders::texture_load_ctx1_cs), 4,
       4, 4},
      {},
      {}}},
    {{{shaders::texture_load_depth_unorm_cs,
       sizeof(shaders::texture_load_depth_unorm_cs), 4, 4, 8},
      {shaders::texture_load_depth_unorm_2x_cs,
       sizeof(shaders::texture_load_depth_unorm_2x_cs), 4, 4, 8 * 2},
      {shaders::texture_load_depth_unorm_3x_cs,
       sizeof(shaders::texture_load_depth_unorm_3x_cs), 3, 3, 2 * 3}}},
    {{{shaders::texture_load_depth_float_cs,
       sizeof(shaders::texture_load_depth_float_cs), 4, 4, 8},
      {shaders::texture_load_depth_float_2x_cs,
       sizeof(shaders::texture_load_depth_float_2x_cs), 4, 4, 8 * 2},
      {shaders::texture_load_depth_float_3x_cs,
       sizeof(shaders::texture_load_depth_float_3x_cs), 3, 3, 2 * 3}}},
};

TextureCache::TextureCache(D3D12CommandProcessor& command_processor,
                           const RegisterFile& register_file,
                           D3D12SharedMemory& shared_memory,
                           bool bindless_resources_used,
                           uint32_t draw_resolution_scale)
    : command_processor_(command_processor),
      register_file_(register_file),
      shared_memory_(shared_memory),
      bindless_resources_used_(bindless_resources_used),
      draw_resolution_scale_(draw_resolution_scale) {
  assert_true(draw_resolution_scale >= 1);
  assert_true(draw_resolution_scale <= 3);
}

TextureCache::~TextureCache() { Shutdown(); }

bool TextureCache::Initialize() {
  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  auto device = provider.GetDevice();

  if (draw_resolution_scale_ > 1) {
    assert_true(draw_resolution_scale_ <= GetMaxDrawResolutionScale(provider));
    // Buffers not used yet - no need aliasing barriers to change ownership of
    // gigabytes between even and odd buffers.
    std::memset(scaled_resolve_1gb_buffer_indices_, UINT8_MAX,
                sizeof(scaled_resolve_1gb_buffer_indices_));
    assert_true(scaled_resolve_heaps_.empty());
    uint64_t scaled_resolve_address_space_size =
        uint64_t(SharedMemory::kBufferSize) *
        (draw_resolution_scale_ * draw_resolution_scale_);
    scaled_resolve_heaps_.resize(size_t(scaled_resolve_address_space_size >>
                                        kScaledResolveHeapSizeLog2));
    constexpr uint32_t kScaledResolvePageDwordCount =
        SharedMemory::kBufferSize / 4096 / 32;
    scaled_resolve_pages_ = new uint32_t[kScaledResolvePageDwordCount];
    std::memset(scaled_resolve_pages_, 0,
                kScaledResolvePageDwordCount * sizeof(uint32_t));
    std::memset(scaled_resolve_pages_l2_, 0, sizeof(scaled_resolve_pages_l2_));
  }
  scaled_resolve_heap_count_ = 0;

  // Create the loading root signature.
  D3D12_ROOT_PARAMETER root_parameters[3];
  // Parameter 0 is constants (changed multiple times when untiling).
  root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_parameters[0].Descriptor.ShaderRegister = 0;
  root_parameters[0].Descriptor.RegisterSpace = 0;
  root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // Parameter 1 is the source (may be changed multiple times for the same
  // destination).
  D3D12_DESCRIPTOR_RANGE root_dest_range;
  root_dest_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  root_dest_range.NumDescriptors = 1;
  root_dest_range.BaseShaderRegister = 0;
  root_dest_range.RegisterSpace = 0;
  root_dest_range.OffsetInDescriptorsFromTableStart = 0;
  root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  root_parameters[1].DescriptorTable.pDescriptorRanges = &root_dest_range;
  root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // Parameter 2 is the destination.
  D3D12_DESCRIPTOR_RANGE root_source_range;
  root_source_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  root_source_range.NumDescriptors = 1;
  root_source_range.BaseShaderRegister = 0;
  root_source_range.RegisterSpace = 0;
  root_source_range.OffsetInDescriptorsFromTableStart = 0;
  root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
  root_parameters[2].DescriptorTable.pDescriptorRanges = &root_source_range;
  root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.NumParameters = UINT(xe::countof(root_parameters));
  root_signature_desc.pParameters = root_parameters;
  root_signature_desc.NumStaticSamplers = 0;
  root_signature_desc.pStaticSamplers = nullptr;
  root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  load_root_signature_ =
      ui::d3d12::util::CreateRootSignature(provider, root_signature_desc);
  if (load_root_signature_ == nullptr) {
    XELOGE(
        "D3D12TextureCache: Failed to create the texture loading root "
        "signature");
    Shutdown();
    return false;
  }

  // Create the loading pipelines.
  for (uint32_t i = 0; i < uint32_t(LoadMode::kCount); ++i) {
    const LoadModeInfo& mode_info = load_mode_info_[i];
    load_pipelines_[i] = ui::d3d12::util::CreateComputePipeline(
        device, mode_info.shaders[0].shader, mode_info.shaders[0].shader_size,
        load_root_signature_);
    if (load_pipelines_[i] == nullptr) {
      XELOGE(
          "D3D12TextureCache: Failed to create the texture loading pipeline "
          "for mode {}",
          i);
      Shutdown();
      return false;
    }
    if (draw_resolution_scale_ > 1) {
      const LoadShaderInfo& scaled_load_shader_info =
          mode_info.shaders[draw_resolution_scale_ - 1];
      if (scaled_load_shader_info.shader) {
        load_pipelines_scaled_[i] = ui::d3d12::util::CreateComputePipeline(
            device, scaled_load_shader_info.shader,
            scaled_load_shader_info.shader_size, load_root_signature_);
        if (load_pipelines_scaled_[i] == nullptr) {
          XELOGE(
              "D3D12TextureCache: Failed to create the resolution-scaled "
              "texture loading pipeline for mode {}",
              i);
          Shutdown();
          return false;
        }
      }
    }
  }

  srv_descriptor_cache_allocated_ = 0;

  // Create a heap with null SRV descriptors, since it's faster to copy a
  // descriptor than to create an SRV, and null descriptors are used a lot (for
  // the signed version when only unsigned is used, for instance).
  D3D12_DESCRIPTOR_HEAP_DESC null_srv_descriptor_heap_desc;
  null_srv_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  null_srv_descriptor_heap_desc.NumDescriptors =
      uint32_t(NullSRVDescriptorIndex::kCount);
  null_srv_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  null_srv_descriptor_heap_desc.NodeMask = 0;
  if (FAILED(device->CreateDescriptorHeap(
          &null_srv_descriptor_heap_desc,
          IID_PPV_ARGS(&null_srv_descriptor_heap_)))) {
    XELOGE(
        "D3D12TextureCache: Failed to create the descriptor heap for null "
        "SRVs");
    Shutdown();
    return false;
  }
  null_srv_descriptor_heap_start_ =
      null_srv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc;
  null_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  null_srv_desc.Shader4ComponentMapping =
      D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
          D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
          D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
          D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
          D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0);
  null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
  null_srv_desc.Texture2DArray.MostDetailedMip = 0;
  null_srv_desc.Texture2DArray.MipLevels = 1;
  null_srv_desc.Texture2DArray.FirstArraySlice = 0;
  null_srv_desc.Texture2DArray.ArraySize = 1;
  null_srv_desc.Texture2DArray.PlaneSlice = 0;
  null_srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(
      nullptr, &null_srv_desc,
      provider.OffsetViewDescriptor(
          null_srv_descriptor_heap_start_,
          uint32_t(NullSRVDescriptorIndex::k2DArray)));
  null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  null_srv_desc.Texture3D.MostDetailedMip = 0;
  null_srv_desc.Texture3D.MipLevels = 1;
  null_srv_desc.Texture3D.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(
      nullptr, &null_srv_desc,
      provider.OffsetViewDescriptor(null_srv_descriptor_heap_start_,
                                    uint32_t(NullSRVDescriptorIndex::k3D)));
  null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
  null_srv_desc.TextureCube.MostDetailedMip = 0;
  null_srv_desc.TextureCube.MipLevels = 1;
  null_srv_desc.TextureCube.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(
      nullptr, &null_srv_desc,
      provider.OffsetViewDescriptor(null_srv_descriptor_heap_start_,
                                    uint32_t(NullSRVDescriptorIndex::kCube)));

  if (draw_resolution_scale_ > 1) {
    scaled_resolve_global_watch_handle_ = shared_memory_.RegisterGlobalWatch(
        ScaledResolveGlobalWatchCallbackThunk, this);
  }

  texture_current_usage_time_ = xe::Clock::QueryHostUptimeMillis();

  return true;
}

void TextureCache::Shutdown() {
  ClearCache();

  if (scaled_resolve_global_watch_handle_ != nullptr) {
    shared_memory_.UnregisterGlobalWatch(scaled_resolve_global_watch_handle_);
    scaled_resolve_global_watch_handle_ = nullptr;
  }

  ui::d3d12::util::ReleaseAndNull(null_srv_descriptor_heap_);

  for (uint32_t i = 0; i < uint32_t(LoadMode::kCount); ++i) {
    ui::d3d12::util::ReleaseAndNull(load_pipelines_scaled_[i]);
    ui::d3d12::util::ReleaseAndNull(load_pipelines_[i]);
  }
  ui::d3d12::util::ReleaseAndNull(load_root_signature_);

  if (scaled_resolve_pages_ != nullptr) {
    delete[] scaled_resolve_pages_;
    scaled_resolve_pages_ = nullptr;
  }
  // First free the buffers to detach them from the heaps.
  for (size_t i = 0; i < xe::countof(scaled_resolve_2gb_buffers_); ++i) {
    ScaledResolveVirtualBuffer*& scaled_resolve_buffer =
        scaled_resolve_2gb_buffers_[i];
    if (scaled_resolve_buffer) {
      delete scaled_resolve_buffer;
      scaled_resolve_buffer = nullptr;
    }
  }
  for (ID3D12Heap* scaled_resolve_heap : scaled_resolve_heaps_) {
    if (scaled_resolve_heap) {
      scaled_resolve_heap->Release();
    }
  }
  scaled_resolve_heaps_.clear();
  scaled_resolve_heap_count_ = 0;
  COUNT_profile_set("gpu/texture_cache/scaled_resolve_buffer_used_mb", 0);
}

void TextureCache::ClearCache() {
  // Destroy all the textures.
  for (auto texture_pair : textures_) {
    Texture* texture = texture_pair.second;
    shared_memory_.UnwatchMemoryRange(texture->base_watch_handle);
    shared_memory_.UnwatchMemoryRange(texture->mip_watch_handle);
    // Bindful descriptor cache will be cleared entirely now, so only release
    // bindless descriptors.
    if (bindless_resources_used_) {
      for (auto descriptor_pair : texture->srv_descriptors) {
        command_processor_.ReleaseViewBindlessDescriptorImmediately(
            descriptor_pair.second);
      }
    }
    texture->resource->Release();
    delete texture;
  }
  textures_.clear();
  COUNT_profile_set("gpu/texture_cache/textures", 0);
  textures_total_size_ = 0;
  COUNT_profile_set("gpu/texture_cache/total_size_mb", 0);
  texture_used_first_ = texture_used_last_ = nullptr;

  // Clear texture descriptor cache.
  srv_descriptor_cache_free_.clear();
  srv_descriptor_cache_allocated_ = 0;
  for (auto& page : srv_descriptor_cache_) {
    page.heap->Release();
  }
  srv_descriptor_cache_.clear();
}

void TextureCache::TextureFetchConstantWritten(uint32_t index) {
  texture_bindings_in_sync_ &= ~(1u << index);
}

void TextureCache::BeginSubmission() {
  // ExecuteCommandLists is a full UAV and aliasing barrier.
  if (draw_resolution_scale_ > 1) {
    size_t scaled_resolve_buffer_count = GetScaledResolveBufferCount();
    for (size_t i = 0; i < scaled_resolve_buffer_count; ++i) {
      ScaledResolveVirtualBuffer* scaled_resolve_buffer =
          scaled_resolve_2gb_buffers_[i];
      if (scaled_resolve_buffer) {
        scaled_resolve_buffer->ClearUAVBarrierPending();
      }
    }
    std::memset(scaled_resolve_1gb_buffer_indices_, UINT8_MAX,
                sizeof(scaled_resolve_1gb_buffer_indices_));
  }
}

void TextureCache::BeginFrame() {
  // In case there was a failure creating something in the previous frame, make
  // sure bindings are reset so a new attempt will surely be made if the texture
  // is requested again.
  ClearBindings();

  std::memset(unsupported_format_features_used_, 0,
              sizeof(unsupported_format_features_used_));

  texture_current_usage_time_ = xe::Clock::QueryHostUptimeMillis();

  // If memory usage is too high, destroy unused textures.
  uint64_t completed_frame = command_processor_.GetCompletedFrame();
  // texture_cache_memory_limit_render_to_texture is assumed to be included in
  // texture_cache_memory_limit_soft and texture_cache_memory_limit_hard, at 1x,
  // so subtracting 1 from the scale.
  uint32_t limit_scaled_resolve_add_mb =
      cvars::texture_cache_memory_limit_render_to_texture *
      (draw_resolution_scale_ * draw_resolution_scale_ - 1);
  uint32_t limit_soft_mb =
      cvars::texture_cache_memory_limit_soft + limit_scaled_resolve_add_mb;
  uint32_t limit_hard_mb =
      cvars::texture_cache_memory_limit_hard + limit_scaled_resolve_add_mb;
  uint32_t limit_soft_lifetime =
      cvars::texture_cache_memory_limit_soft_lifetime * 1000;
  bool destroyed_any = false;
  while (texture_used_first_ != nullptr) {
    uint64_t total_size_mb = textures_total_size_ >> 20;
    bool limit_hard_exceeded = total_size_mb >= limit_hard_mb;
    if (total_size_mb < limit_soft_mb && !limit_hard_exceeded) {
      break;
    }
    Texture* texture = texture_used_first_;
    if (texture->last_usage_frame > completed_frame) {
      break;
    }
    if (!limit_hard_exceeded &&
        (texture->last_usage_time + limit_soft_lifetime) >
            texture_current_usage_time_) {
      break;
    }
    destroyed_any = true;
    // Remove the texture from the map.
    auto found_texture_it = textures_.find(texture->key);
    assert_true(found_texture_it != textures_.end());
    if (found_texture_it != textures_.end()) {
      assert_true(found_texture_it->second == texture);
      textures_.erase(found_texture_it);
    }
    // Unlink the texture.
    texture_used_first_ = texture->used_next;
    if (texture_used_first_ != nullptr) {
      texture_used_first_->used_previous = nullptr;
    } else {
      texture_used_last_ = nullptr;
    }
    // Exclude the texture from the memory usage counter.
    textures_total_size_ -= texture->resource_size;
    // Destroy the texture.
    shared_memory_.UnwatchMemoryRange(texture->base_watch_handle);
    shared_memory_.UnwatchMemoryRange(texture->mip_watch_handle);
    if (bindless_resources_used_) {
      for (auto descriptor_pair : texture->srv_descriptors) {
        command_processor_.ReleaseViewBindlessDescriptorImmediately(
            descriptor_pair.second);
      }
    } else {
      for (auto descriptor_pair : texture->srv_descriptors) {
        srv_descriptor_cache_free_.push_back(descriptor_pair.second);
      }
    }
    texture->resource->Release();
    delete texture;
  }
  if (destroyed_any) {
    COUNT_profile_set("gpu/texture_cache/textures", textures_.size());
    COUNT_profile_set("gpu/texture_cache/total_size_mb",
                      uint32_t(textures_total_size_ >> 20));
  }
}

void TextureCache::EndFrame() {
  // Report used unsupported texture formats.
  bool unsupported_header_written = false;
  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t unsupported_features = unsupported_format_features_used_[i];
    if (unsupported_features == 0) {
      continue;
    }
    if (!unsupported_header_written) {
      XELOGE("Unsupported texture formats used in the frame:");
      unsupported_header_written = true;
    }
    XELOGE("* {}{}{}{}", FormatInfo::Get(xenos::TextureFormat(i))->name,
           unsupported_features & kUnsupportedResourceBit ? " resource" : "",
           unsupported_features & kUnsupportedUnormBit ? " unorm" : "",
           unsupported_features & kUnsupportedSnormBit ? " snorm" : "");
    unsupported_format_features_used_[i] = 0;
  }
}

void TextureCache::RequestTextures(uint32_t used_texture_mask) {
  const auto& regs = register_file_;

#if XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES

  if (texture_invalidated_.exchange(false, std::memory_order_acquire)) {
    // Clear the bindings not only for this draw call, but entirely, because
    // loading may be needed in some draw call later, which may have the same
    // key for some binding as before the invalidation, but texture_invalidated_
    // being false (menu background in Halo 3).
    for (size_t i = 0; i < xe::countof(texture_bindings_); ++i) {
      texture_bindings_[i].Clear();
    }
    texture_bindings_in_sync_ = 0;
  }

  // Update the texture keys and the textures.
  uint32_t textures_remaining = used_texture_mask;
  uint32_t index = 0;
  while (xe::bit_scan_forward(textures_remaining, &index)) {
    uint32_t index_bit = uint32_t(1) << index;
    textures_remaining &= ~index_bit;
    if (texture_bindings_in_sync_ & index_bit) {
      continue;
    }
    TextureBinding& binding = texture_bindings_[index];
    const auto& fetch = regs.Get<xenos::xe_gpu_texture_fetch_t>(
        XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + index * 6);
    TextureKey old_key = binding.key;
    uint8_t old_swizzled_signs = binding.swizzled_signs;
    BindingInfoFromFetchConstant(fetch, binding.key, &binding.host_swizzle,
                                 &binding.swizzled_signs);
    texture_bindings_in_sync_ |= index_bit;
    if (binding.key.IsInvalid()) {
      binding.texture = nullptr;
      binding.texture_signed = nullptr;
      binding.descriptor_index = UINT32_MAX;
      binding.descriptor_index_signed = UINT32_MAX;
      continue;
    }

    // Check if need to load the unsigned and the signed versions of the texture
    // (if the format is emulated with different host bit representations for
    // signed and unsigned - otherwise only the unsigned one is loaded).
    bool key_changed = binding.key != old_key;
    bool load_unsigned_data = false, load_signed_data = false;
    if (IsSignedVersionSeparate(binding.key.format)) {
      // Can reuse previously loaded unsigned/signed versions if the key is the
      // same and the texture was previously bound as unsigned/signed
      // respectively (checking the previous values of signedness rather than
      // binding.texture != nullptr and binding.texture_signed != nullptr also
      // prevents repeated attempts to load the texture if it has failed to
      // load).
      if (texture_util::IsAnySignNotSigned(binding.swizzled_signs)) {
        if (key_changed ||
            !texture_util::IsAnySignNotSigned(old_swizzled_signs)) {
          binding.texture = FindOrCreateTexture(binding.key);
          binding.descriptor_index =
              binding.texture
                  ? FindOrCreateTextureDescriptor(*binding.texture, false,
                                                  binding.host_swizzle)
                  : UINT32_MAX;
          load_unsigned_data = true;
        }
      } else {
        binding.texture = nullptr;
        binding.descriptor_index = UINT32_MAX;
      }
      if (texture_util::IsAnySignSigned(binding.swizzled_signs)) {
        if (key_changed || !texture_util::IsAnySignSigned(old_swizzled_signs)) {
          TextureKey signed_key = binding.key;
          signed_key.signed_separate = 1;
          binding.texture_signed = FindOrCreateTexture(signed_key);
          binding.descriptor_index_signed =
              binding.texture_signed
                  ? FindOrCreateTextureDescriptor(*binding.texture_signed, true,
                                                  binding.host_swizzle)
                  : UINT32_MAX;
          load_signed_data = true;
        }
      } else {
        binding.texture_signed = nullptr;
        binding.descriptor_index_signed = UINT32_MAX;
      }
    } else {
      // Same resource for both unsigned and signed, but descriptor formats may
      // be different.
      if (key_changed) {
        binding.texture = FindOrCreateTexture(binding.key);
        load_unsigned_data = true;
      }
      binding.texture_signed = nullptr;
      if (texture_util::IsAnySignNotSigned(binding.swizzled_signs)) {
        if (key_changed ||
            !texture_util::IsAnySignNotSigned(old_swizzled_signs)) {
          binding.descriptor_index =
              binding.texture
                  ? FindOrCreateTextureDescriptor(*binding.texture, false,
                                                  binding.host_swizzle)
                  : UINT32_MAX;
        }
      } else {
        binding.descriptor_index = UINT32_MAX;
      }
      if (texture_util::IsAnySignSigned(binding.swizzled_signs)) {
        if (key_changed || !texture_util::IsAnySignSigned(old_swizzled_signs)) {
          binding.descriptor_index_signed =
              binding.texture
                  ? FindOrCreateTextureDescriptor(*binding.texture, true,
                                                  binding.host_swizzle)
                  : UINT32_MAX;
        }
      } else {
        binding.descriptor_index_signed = UINT32_MAX;
      }
    }
    if (load_unsigned_data && binding.texture != nullptr) {
      LoadTextureData(binding.texture);
    }
    if (load_signed_data && binding.texture_signed != nullptr) {
      LoadTextureData(binding.texture_signed);
    }
  }

  // Transition the textures to the needed usage - always in
  // NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE states because barriers
  // between read-only stages, if needed, are discouraged (also if these were
  // tracked separately, checks would be needed to make sure, if the same
  // texture is bound through different fetch constants to both VS and PS, it
  // would be in both states).
  textures_remaining = used_texture_mask;
  while (xe::bit_scan_forward(textures_remaining, &index)) {
    textures_remaining &= ~(uint32_t(1) << index);
    TextureBinding& binding = texture_bindings_[index];
    if (binding.texture != nullptr) {
      // Will be referenced by the command list, so mark as used.
      MarkTextureUsed(binding.texture);
      command_processor_.PushTransitionBarrier(
          binding.texture->resource, binding.texture->state,
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      binding.texture->state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (binding.texture_signed != nullptr) {
      MarkTextureUsed(binding.texture_signed);
      command_processor_.PushTransitionBarrier(
          binding.texture_signed->resource, binding.texture_signed->state,
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      binding.texture_signed->state =
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
  }
}

bool TextureCache::AreActiveTextureSRVKeysUpToDate(
    const TextureSRVKey* keys,
    const D3D12Shader::TextureBinding* host_shader_bindings,
    size_t host_shader_binding_count) const {
  for (size_t i = 0; i < host_shader_binding_count; ++i) {
    const TextureSRVKey& key = keys[i];
    const TextureBinding& binding =
        texture_bindings_[host_shader_bindings[i].fetch_constant];
    if (key.key != binding.key || key.host_swizzle != binding.host_swizzle ||
        key.swizzled_signs != binding.swizzled_signs) {
      return false;
    }
  }
  return true;
}

void TextureCache::WriteActiveTextureSRVKeys(
    TextureSRVKey* keys,
    const D3D12Shader::TextureBinding* host_shader_bindings,
    size_t host_shader_binding_count) const {
  for (size_t i = 0; i < host_shader_binding_count; ++i) {
    TextureSRVKey& key = keys[i];
    const TextureBinding& binding =
        texture_bindings_[host_shader_bindings[i].fetch_constant];
    key.key = binding.key;
    key.host_swizzle = binding.host_swizzle;
    key.swizzled_signs = binding.swizzled_signs;
  }
}

void TextureCache::WriteActiveTextureBindfulSRV(
    const D3D12Shader::TextureBinding& host_shader_binding,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  assert_false(bindless_resources_used_);
  const TextureBinding& binding =
      texture_bindings_[host_shader_binding.fetch_constant];
  uint32_t descriptor_index = UINT32_MAX;
  Texture* texture = nullptr;
  if (!binding.key.IsInvalid() &&
      AreDimensionsCompatible(host_shader_binding.dimension,
                              binding.key.dimension)) {
    if (host_shader_binding.is_signed) {
      // Not supporting signed compressed textures - hopefully DXN and DXT5A are
      // not used as signed.
      if (texture_util::IsAnySignSigned(binding.swizzled_signs)) {
        descriptor_index = binding.descriptor_index_signed;
        texture = IsSignedVersionSeparate(binding.key.format)
                      ? binding.texture_signed
                      : binding.texture;
      }
    } else {
      if (texture_util::IsAnySignNotSigned(binding.swizzled_signs)) {
        descriptor_index = binding.descriptor_index;
        texture = binding.texture;
      }
    }
  }
  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  D3D12_CPU_DESCRIPTOR_HANDLE source_handle;
  if (descriptor_index != UINT32_MAX) {
    assert_not_null(texture);
    MarkTextureUsed(texture);
    source_handle = GetTextureDescriptorCPUHandle(descriptor_index);
  } else {
    NullSRVDescriptorIndex null_descriptor_index;
    switch (host_shader_binding.dimension) {
      case xenos::FetchOpDimension::k3DOrStacked:
        null_descriptor_index = NullSRVDescriptorIndex::k3D;
        break;
      case xenos::FetchOpDimension::kCube:
        null_descriptor_index = NullSRVDescriptorIndex::kCube;
        break;
      default:
        assert_true(
            host_shader_binding.dimension == xenos::FetchOpDimension::k1D ||
            host_shader_binding.dimension == xenos::FetchOpDimension::k2D);
        null_descriptor_index = NullSRVDescriptorIndex::k2DArray;
    }
    source_handle = provider.OffsetViewDescriptor(
        null_srv_descriptor_heap_start_, uint32_t(null_descriptor_index));
  }
  auto device = provider.GetDevice();
  {
#if XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
    SCOPE_profile_cpu_i(
        "gpu",
        "xe::gpu::d3d12::TextureCache::WriteActiveTextureBindfulSRV->"
        "CopyDescriptorsSimple");
#endif  // XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
    device->CopyDescriptorsSimple(1, handle, source_handle,
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }
}

uint32_t TextureCache::GetActiveTextureBindlessSRVIndex(
    const D3D12Shader::TextureBinding& host_shader_binding) {
  assert_true(bindless_resources_used_);
  uint32_t descriptor_index = UINT32_MAX;
  const TextureBinding& binding =
      texture_bindings_[host_shader_binding.fetch_constant];
  if (!binding.key.IsInvalid() &&
      AreDimensionsCompatible(host_shader_binding.dimension,
                              binding.key.dimension)) {
    descriptor_index = host_shader_binding.is_signed
                           ? binding.descriptor_index_signed
                           : binding.descriptor_index;
  }
  if (descriptor_index == UINT32_MAX) {
    switch (host_shader_binding.dimension) {
      case xenos::FetchOpDimension::k3DOrStacked:
        descriptor_index =
            uint32_t(D3D12CommandProcessor::SystemBindlessView::kNullTexture3D);
        break;
      case xenos::FetchOpDimension::kCube:
        descriptor_index = uint32_t(
            D3D12CommandProcessor::SystemBindlessView::kNullTextureCube);
        break;
      default:
        assert_true(
            host_shader_binding.dimension == xenos::FetchOpDimension::k1D ||
            host_shader_binding.dimension == xenos::FetchOpDimension::k2D);
        descriptor_index = uint32_t(
            D3D12CommandProcessor::SystemBindlessView::kNullTexture2DArray);
    }
  }
  return descriptor_index;
}

TextureCache::SamplerParameters TextureCache::GetSamplerParameters(
    const D3D12Shader::SamplerBinding& binding) const {
  const auto& regs = register_file_;
  const auto& fetch = regs.Get<xenos::xe_gpu_texture_fetch_t>(
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + binding.fetch_constant * 6);

  SamplerParameters parameters;

  parameters.clamp_x = fetch.clamp_x;
  parameters.clamp_y = fetch.clamp_y;
  parameters.clamp_z = fetch.clamp_z;
  parameters.border_color = fetch.border_color;

  uint32_t mip_min_level;
  texture_util::GetSubresourcesFromFetchConstant(
      fetch, nullptr, nullptr, nullptr, nullptr, nullptr, &mip_min_level,
      nullptr, binding.mip_filter);
  parameters.mip_min_level = mip_min_level;

  xenos::AnisoFilter aniso_filter =
      binding.aniso_filter == xenos::AnisoFilter::kUseFetchConst
          ? fetch.aniso_filter
          : binding.aniso_filter;
  aniso_filter = std::min(aniso_filter, xenos::AnisoFilter::kMax_16_1);
  parameters.aniso_filter = aniso_filter;
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
    xenos::TextureFilter mip_filter =
        binding.mip_filter == xenos::TextureFilter::kUseFetchConst
            ? fetch.mip_filter
            : binding.mip_filter;
    parameters.mip_linear = mip_filter == xenos::TextureFilter::kLinear;
  }

  return parameters;
}

void TextureCache::WriteSampler(SamplerParameters parameters,
                                D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
  D3D12_SAMPLER_DESC desc;
  if (parameters.aniso_filter != xenos::AnisoFilter::kDisabled) {
    desc.Filter = D3D12_FILTER_ANISOTROPIC;
    desc.MaxAnisotropy = 1u << (uint32_t(parameters.aniso_filter) - 1);
  } else {
    D3D12_FILTER_TYPE d3d_filter_min = parameters.min_linear
                                           ? D3D12_FILTER_TYPE_LINEAR
                                           : D3D12_FILTER_TYPE_POINT;
    D3D12_FILTER_TYPE d3d_filter_mag = parameters.mag_linear
                                           ? D3D12_FILTER_TYPE_LINEAR
                                           : D3D12_FILTER_TYPE_POINT;
    D3D12_FILTER_TYPE d3d_filter_mip = parameters.mip_linear
                                           ? D3D12_FILTER_TYPE_LINEAR
                                           : D3D12_FILTER_TYPE_POINT;
    desc.Filter = D3D12_ENCODE_BASIC_FILTER(
        d3d_filter_min, d3d_filter_mag, d3d_filter_mip,
        D3D12_FILTER_REDUCTION_TYPE_STANDARD);
    desc.MaxAnisotropy = 1;
  }
  static const D3D12_TEXTURE_ADDRESS_MODE kAddressModeMap[] = {
      /* kRepeat               */ D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      /* kMirroredRepeat       */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
      /* kClampToEdge          */ D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      /* kMirrorClampToEdge    */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
      // No GL_CLAMP (clamp to half edge, half border) equivalent in Direct3D
      // 12, but there's no Direct3D 9 equivalent anyway, and too weird to be
      // suitable for intentional real usage.
      /* kClampToHalfway       */ D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      // No mirror and clamp to border equivalents in Direct3D 12, but they
      // aren't there in Direct3D 9 either.
      /* kMirrorClampToHalfway */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
      /* kClampToBorder        */ D3D12_TEXTURE_ADDRESS_MODE_BORDER,
      /* kMirrorClampToBorder  */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
  };
  desc.AddressU = kAddressModeMap[uint32_t(parameters.clamp_x)];
  desc.AddressV = kAddressModeMap[uint32_t(parameters.clamp_y)];
  desc.AddressW = kAddressModeMap[uint32_t(parameters.clamp_z)];
  // LOD is calculated in shaders.
  desc.MipLODBias = 0.0f;
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  // TODO(Triang3l): Border colors k_ACBYCR_BLACK and k_ACBCRY_BLACK.
  if (parameters.border_color == xenos::BorderColor::k_AGBR_White) {
    desc.BorderColor[0] = 1.0f;
    desc.BorderColor[1] = 1.0f;
    desc.BorderColor[2] = 1.0f;
    desc.BorderColor[3] = 1.0f;
  } else {
    desc.BorderColor[0] = 0.0f;
    desc.BorderColor[1] = 0.0f;
    desc.BorderColor[2] = 0.0f;
    desc.BorderColor[3] = 0.0f;
  }
  desc.MinLOD = float(parameters.mip_min_level);
  // Maximum mip level is in the texture resource itself.
  desc.MaxLOD = FLT_MAX;
  auto device =
      command_processor_.GetD3D12Context().GetD3D12Provider().GetDevice();
  device->CreateSampler(&desc, handle);
}

void TextureCache::MarkRangeAsResolved(uint32_t start_unscaled,
                                       uint32_t length_unscaled) {
  if (length_unscaled == 0) {
    return;
  }
  start_unscaled &= 0x1FFFFFFF;
  length_unscaled = std::min(length_unscaled, 0x20000000 - start_unscaled);

  if (draw_resolution_scale_ > 1) {
    uint32_t page_first = start_unscaled >> 12;
    uint32_t page_last = (start_unscaled + length_unscaled - 1) >> 12;
    uint32_t block_first = page_first >> 5;
    uint32_t block_last = page_last >> 5;
    auto global_lock = global_critical_region_.Acquire();
    for (uint32_t i = block_first; i <= block_last; ++i) {
      uint32_t add_bits = UINT32_MAX;
      if (i == block_first) {
        add_bits &= ~((1u << (page_first & 31)) - 1);
      }
      if (i == block_last && (page_last & 31) != 31) {
        add_bits &= (1u << ((page_last & 31) + 1)) - 1;
      }
      scaled_resolve_pages_[i] |= add_bits;
      scaled_resolve_pages_l2_[i >> 6] |= 1ull << (i & 63);
    }
  }

  // Invalidate textures. Toggling individual textures between scaled and
  // unscaled also relies on invalidation through shared memory.
  shared_memory_.RangeWrittenByGpu(start_unscaled, length_unscaled, true);
}

bool TextureCache::EnsureScaledResolveMemoryCommitted(
    uint32_t start_unscaled, uint32_t length_unscaled) {
  assert_true(draw_resolution_scale_ > 1);

  if (length_unscaled == 0) {
    return true;
  }
  if (start_unscaled > SharedMemory::kBufferSize ||
      (SharedMemory::kBufferSize - start_unscaled) < length_unscaled) {
    // Exceeds the physical address space.
    return false;
  }

  uint32_t draw_resolution_scale_square =
      draw_resolution_scale_ * draw_resolution_scale_;
  uint64_t first_scaled =
      uint64_t(start_unscaled) * draw_resolution_scale_square;
  uint64_t last_scaled = uint64_t(start_unscaled + (length_unscaled - 1)) *
                         draw_resolution_scale_square;

  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  auto device = provider.GetDevice();

  // Ensure GPU virtual memory for buffers that may be used to access the range
  // is allocated - buffers are created. Always creating both buffers for all
  // addresses before creating the heaps so when creating a new buffer, it can
  // be safely assumed that no existing heaps should be mapped to it.
  std::array<size_t, 2> possible_buffers_first =
      GetPossibleScaledResolveBufferIndices(first_scaled);
  std::array<size_t, 2> possible_buffers_last =
      GetPossibleScaledResolveBufferIndices(last_scaled);
  size_t possible_buffer_first =
      std::min(possible_buffers_first[0], possible_buffers_first[1]);
  size_t possible_buffer_last =
      std::max(possible_buffers_last[0], possible_buffers_last[1]);
  for (size_t i = possible_buffer_first; i <= possible_buffer_last; ++i) {
    if (scaled_resolve_2gb_buffers_[i]) {
      continue;
    }
    D3D12_RESOURCE_DESC scaled_resolve_buffer_desc;
    // Buffer indices are gigabytes.
    ui::d3d12::util::FillBufferResourceDesc(
        scaled_resolve_buffer_desc,
        std::min(uint64_t(1) << 31, uint64_t(SharedMemory::kBufferSize) *
                                            draw_resolution_scale_square -
                                        (uint64_t(i) << 30)),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    // The first access will be a resolve.
    constexpr D3D12_RESOURCE_STATES kScaledResolveVirtualBufferInitialState =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12Resource* scaled_resolve_buffer_resource;
    if (FAILED(device->CreateReservedResource(
            &scaled_resolve_buffer_desc,
            kScaledResolveVirtualBufferInitialState, nullptr,
            IID_PPV_ARGS(&scaled_resolve_buffer_resource)))) {
      XELOGE(
          "D3D12TextureCache: Failed to create a 2 GB tiled buffer for draw "
          "resolution scaling");
      return false;
    }
    scaled_resolve_2gb_buffers_[i] =
        new ScaledResolveVirtualBuffer(scaled_resolve_buffer_resource,
                                       kScaledResolveVirtualBufferInitialState);
    scaled_resolve_buffer_resource->Release();
  }

  uint32_t heap_first = uint32_t(first_scaled >> kScaledResolveHeapSizeLog2);
  uint32_t heap_last = uint32_t(last_scaled >> kScaledResolveHeapSizeLog2);
  for (uint32_t i = heap_first; i <= heap_last; ++i) {
    if (scaled_resolve_heaps_[i] != nullptr) {
      continue;
    }
    auto direct_queue = provider.GetDirectQueue();
    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = kScaledResolveHeapSize;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS |
                      provider.GetHeapFlagCreateNotZeroed();
    ID3D12Heap* scaled_resolve_heap;
    if (FAILED(device->CreateHeap(&heap_desc,
                                  IID_PPV_ARGS(&scaled_resolve_heap)))) {
      XELOGE("Texture cache: Failed to create a scaled resolve tile heap");
      return false;
    }
    scaled_resolve_heaps_[i] = scaled_resolve_heap;
    ++scaled_resolve_heap_count_;
    COUNT_profile_set(
        "gpu/texture_cache/scaled_resolve_buffer_used_mb",
        scaled_resolve_heap_count_ << (kScaledResolveHeapSizeLog2 - 20));
    D3D12_TILED_RESOURCE_COORDINATE region_start_coordinates;
    region_start_coordinates.Y = 0;
    region_start_coordinates.Z = 0;
    region_start_coordinates.Subresource = 0;
    D3D12_TILE_REGION_SIZE region_size;
    region_size.NumTiles =
        kScaledResolveHeapSize / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    region_size.UseBox = FALSE;
    D3D12_TILE_RANGE_FLAGS range_flags = D3D12_TILE_RANGE_FLAG_NONE;
    UINT heap_range_start_offset = 0;
    UINT range_tile_count =
        kScaledResolveHeapSize / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    std::array<size_t, 2> buffer_indices =
        GetPossibleScaledResolveBufferIndices(uint64_t(i)
                                              << kScaledResolveHeapSizeLog2);
    for (size_t j = 0; j < 2; ++j) {
      size_t buffer_index = buffer_indices[j];
      if (j && buffer_index == buffer_indices[0]) {
        break;
      }
      region_start_coordinates.X =
          UINT(((uint64_t(i) << kScaledResolveHeapSizeLog2) -
                (uint64_t(buffer_index) << 30)) /
               D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
      direct_queue->UpdateTileMappings(
          scaled_resolve_2gb_buffers_[buffer_index]->resource(), 1,
          &region_start_coordinates, &region_size, scaled_resolve_heap, 1,
          &range_flags, &heap_range_start_offset, &range_tile_count,
          D3D12_TILE_MAPPING_FLAG_NONE);
    }
    command_processor_.NotifyQueueOperationsDoneDirectly();
  }
  return true;
}

bool TextureCache::MakeScaledResolveRangeCurrent(uint32_t start_unscaled,
                                                 uint32_t length_unscaled) {
  assert_true(draw_resolution_scale_ > 1);

  if (!length_unscaled || start_unscaled >= SharedMemory::kBufferSize ||
      (SharedMemory::kBufferSize - start_unscaled) < length_unscaled) {
    // If length is 0, the needed buffer can't be chosen because no buffer is
    // needed.
    return false;
  }

  uint32_t draw_resolution_scale_square =
      draw_resolution_scale_ * draw_resolution_scale_;
  uint64_t start_scaled =
      uint64_t(start_unscaled) * draw_resolution_scale_square;
  uint64_t length_scaled =
      uint64_t(length_unscaled) * draw_resolution_scale_square;
  uint64_t last_scaled = start_scaled + (length_scaled - 1);

  // Get one or two buffers that can hold the whole range.
  std::array<size_t, 2> possible_buffer_indices_first =
      GetPossibleScaledResolveBufferIndices(start_scaled);
  std::array<size_t, 2> possible_buffer_indices_last =
      GetPossibleScaledResolveBufferIndices(last_scaled);
  size_t possible_buffer_indices_common[2];
  size_t possible_buffer_indices_common_count = 0;
  for (size_t i = 0; i <= size_t(possible_buffer_indices_first[0] !=
                                 possible_buffer_indices_first[1]);
       ++i) {
    size_t possible_buffer_index_first = possible_buffer_indices_first[i];
    for (size_t j = 0; j <= size_t(possible_buffer_indices_last[0] !=
                                   possible_buffer_indices_last[1]);
         ++j) {
      if (possible_buffer_indices_last[j] == possible_buffer_index_first) {
        bool possible_buffer_index_already_added = false;
        for (size_t k = 0; k < possible_buffer_indices_common_count; ++k) {
          if (possible_buffer_indices_common[k] ==
              possible_buffer_index_first) {
            possible_buffer_index_already_added = true;
            break;
          }
        }
        if (!possible_buffer_index_already_added) {
          assert_true(possible_buffer_indices_common_count < 2);
          possible_buffer_indices_common
              [possible_buffer_indices_common_count++] =
                  possible_buffer_index_first;
        }
      }
    }
  }
  if (!possible_buffer_indices_common_count) {
    // Too wide range requested - no buffer that contains both the start and the
    // end.
    return false;
  }

  size_t gigabyte_first = size_t(start_scaled >> 30);
  size_t gigabyte_last = size_t(last_scaled >> 30);

  // Choose the buffer that the range will be accessed through.
  size_t new_buffer_index;
  if (possible_buffer_indices_common_count >= 2) {
    // Prefer the buffer that is already used to make less aliasing barriers.
    assert_true(gigabyte_first + 1 >= gigabyte_last);
    size_t possible_buffer_indices_already_used[2] = {};
    for (size_t i = gigabyte_first; i <= gigabyte_last; ++i) {
      size_t gigabyte_current_buffer_index =
          scaled_resolve_1gb_buffer_indices_[i];
      for (size_t j = 0; j < possible_buffer_indices_common_count; ++j) {
        if (possible_buffer_indices_common[j] ==
            gigabyte_current_buffer_index) {
          ++possible_buffer_indices_already_used[j];
        }
      }
    }
    new_buffer_index = possible_buffer_indices_common[size_t(
        possible_buffer_indices_already_used[1] >
        possible_buffer_indices_already_used[0])];
  } else {
    // The range can be accessed only by one buffer.
    new_buffer_index = possible_buffer_indices_common[0];
  }

  // Switch the current buffer for the range.
  const ScaledResolveVirtualBuffer* new_buffer =
      scaled_resolve_2gb_buffers_[new_buffer_index];
  assert_not_null(new_buffer);
  ID3D12Resource* new_buffer_resource = new_buffer->resource();
  for (size_t i = gigabyte_first; i <= gigabyte_last; ++i) {
    size_t gigabyte_current_buffer_index =
        scaled_resolve_1gb_buffer_indices_[i];
    if (gigabyte_current_buffer_index == new_buffer_index) {
      continue;
    }
    if (gigabyte_current_buffer_index != SIZE_MAX) {
      ScaledResolveVirtualBuffer* gigabyte_current_buffer =
          scaled_resolve_2gb_buffers_[gigabyte_current_buffer_index];
      assert_not_null(gigabyte_current_buffer);
      command_processor_.PushAliasingBarrier(
          gigabyte_current_buffer->resource(), new_buffer_resource);
      // An aliasing barrier synchronizes and flushes everything.
      gigabyte_current_buffer->ClearUAVBarrierPending();
    }
    scaled_resolve_1gb_buffer_indices_[i] = new_buffer_index;
  }

  scaled_resolve_current_range_start_scaled_ = start_scaled;
  scaled_resolve_current_range_length_scaled_ = length_scaled;
  return true;
}

void TextureCache::TransitionCurrentScaledResolveRange(
    D3D12_RESOURCE_STATES new_state) {
  assert_true(draw_resolution_scale_ > 1);
  ScaledResolveVirtualBuffer& buffer = GetCurrentScaledResolveBuffer();
  command_processor_.PushTransitionBarrier(
      buffer.resource(), buffer.SetResourceState(new_state), new_state);
}

void TextureCache::CreateCurrentScaledResolveRangeUintPow2SRV(
    D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t element_size_bytes_pow2) {
  assert_true(draw_resolution_scale_ > 1);
  size_t buffer_index = GetCurrentScaledResolveBufferIndex();
  const ScaledResolveVirtualBuffer* buffer =
      scaled_resolve_2gb_buffers_[buffer_index];
  assert_not_null(buffer);
  ui::d3d12::util::CreateBufferTypedSRV(
      command_processor_.GetD3D12Context().GetD3D12Provider().GetDevice(),
      handle, buffer->resource(),
      ui::d3d12::util::GetUintPow2DXGIFormat(element_size_bytes_pow2),
      uint32_t(scaled_resolve_current_range_length_scaled_ >>
               element_size_bytes_pow2),
      (scaled_resolve_current_range_start_scaled_ -
       (uint64_t(buffer_index) << 30)) >>
          element_size_bytes_pow2);
}

void TextureCache::CreateCurrentScaledResolveRangeUintPow2UAV(
    D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t element_size_bytes_pow2) {
  assert_true(draw_resolution_scale_ > 1);
  size_t buffer_index = GetCurrentScaledResolveBufferIndex();
  const ScaledResolveVirtualBuffer* buffer =
      scaled_resolve_2gb_buffers_[buffer_index];
  assert_not_null(buffer);
  ui::d3d12::util::CreateBufferTypedUAV(
      command_processor_.GetD3D12Context().GetD3D12Provider().GetDevice(),
      handle, buffer->resource(),
      ui::d3d12::util::GetUintPow2DXGIFormat(element_size_bytes_pow2),
      uint32_t(scaled_resolve_current_range_length_scaled_ >>
               element_size_bytes_pow2),
      (scaled_resolve_current_range_start_scaled_ -
       (uint64_t(buffer_index) << 30)) >>
          element_size_bytes_pow2);
}

ID3D12Resource* TextureCache::RequestSwapTexture(
    D3D12_SHADER_RESOURCE_VIEW_DESC& srv_desc_out,
    xenos::TextureFormat& format_out) {
  const auto& regs = register_file_;
  const auto& fetch = regs.Get<xenos::xe_gpu_texture_fetch_t>(
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0);
  TextureKey key;
  uint32_t swizzle;
  BindingInfoFromFetchConstant(fetch, key, &swizzle, nullptr);
  if (key.base_page == 0 ||
      key.dimension != xenos::DataDimension::k2DOrStacked) {
    return nullptr;
  }
  Texture* texture = FindOrCreateTexture(key);
  if (texture == nullptr || !LoadTextureData(texture)) {
    return nullptr;
  }
  MarkTextureUsed(texture);
  // The swap texture is likely to be used only for the presentation pixel
  // shader, and not during emulation, where it'd be NON_PIXEL_SHADER_RESOURCE |
  // PIXEL_SHADER_RESOURCE.
  command_processor_.PushTransitionBarrier(
      texture->resource, texture->state,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  texture->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  srv_desc_out.Format = GetDXGIUnormFormat(key);
  srv_desc_out.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc_out.Shader4ComponentMapping =
      swizzle |
      D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES;
  srv_desc_out.Texture2D.MostDetailedMip = 0;
  srv_desc_out.Texture2D.MipLevels = 1;
  srv_desc_out.Texture2D.PlaneSlice = 0;
  srv_desc_out.Texture2D.ResourceMinLODClamp = 0.0f;
  format_out = key.format;
  return texture->resource;
}

bool TextureCache::IsDecompressionNeeded(xenos::TextureFormat format,
                                         uint32_t width, uint32_t height) {
  DXGI_FORMAT dxgi_format_uncompressed =
      host_formats_[uint32_t(format)].dxgi_format_uncompressed;
  if (dxgi_format_uncompressed == DXGI_FORMAT_UNKNOWN) {
    return false;
  }
  const FormatInfo* format_info = FormatInfo::Get(format);
  return (width & (format_info->block_width - 1)) != 0 ||
         (height & (format_info->block_height - 1)) != 0;
}

TextureCache::LoadMode TextureCache::GetLoadMode(TextureKey key) {
  const HostFormat& host_format = host_formats_[uint32_t(key.format)];
  if (key.signed_separate) {
    return host_format.load_mode_snorm;
  }
  if (IsDecompressionNeeded(key.format, key.width, key.height)) {
    return host_format.decompress_mode;
  }
  return host_format.load_mode;
}

void TextureCache::BindingInfoFromFetchConstant(
    const xenos::xe_gpu_texture_fetch_t& fetch, TextureKey& key_out,
    uint32_t* host_swizzle_out, uint8_t* swizzled_signs_out) {
  // Reset the key and the swizzle.
  key_out.MakeInvalid();
  if (host_swizzle_out != nullptr) {
    *host_swizzle_out =
        xenos::XE_GPU_SWIZZLE_0 | (xenos::XE_GPU_SWIZZLE_0 << 3) |
        (xenos::XE_GPU_SWIZZLE_0 << 6) | (xenos::XE_GPU_SWIZZLE_0 << 9);
  }
  if (swizzled_signs_out != nullptr) {
    *swizzled_signs_out =
        uint8_t(xenos::TextureSign::kUnsigned) * uint8_t(0b01010101);
  }

  switch (fetch.type) {
    case xenos::FetchConstantType::kTexture:
      break;
    case xenos::FetchConstantType::kInvalidTexture:
      if (cvars::gpu_allow_invalid_fetch_constants) {
        break;
      }
      XELOGW(
          "Texture fetch constant ({:08X} {:08X} {:08X} {:08X} {:08X} {:08X}) "
          "has \"invalid\" type! This is incorrect behavior, but you can try "
          "bypassing this by launching Xenia with "
          "--gpu_allow_invalid_fetch_constants=true.",
          fetch.dword_0, fetch.dword_1, fetch.dword_2, fetch.dword_3,
          fetch.dword_4, fetch.dword_5);
      return;
    default:
      XELOGW(
          "Texture fetch constant ({:08X} {:08X} {:08X} {:08X} {:08X} {:08X}) "
          "is completely invalid!",
          fetch.dword_0, fetch.dword_1, fetch.dword_2, fetch.dword_3,
          fetch.dword_4, fetch.dword_5);
      return;
  }

  uint32_t width, height, depth_or_faces;
  uint32_t base_page, mip_page, mip_max_level;
  texture_util::GetSubresourcesFromFetchConstant(
      fetch, &width, &height, &depth_or_faces, &base_page, &mip_page, nullptr,
      &mip_max_level);
  if (base_page == 0 && mip_page == 0) {
    // No texture data at all.
    return;
  }
  if (fetch.dimension == xenos::DataDimension::k1D) {
    bool is_invalid_1d = false;
    // TODO(Triang3l): Support long 1D textures.
    if (width > xenos::kTexture2DCubeMaxWidthHeight) {
      XELOGE(
          "1D texture is too wide ({}) - ignoring! Report the game to Xenia "
          "developers",
          width);
      is_invalid_1d = true;
    }
    assert_false(fetch.tiled);
    if (fetch.tiled) {
      XELOGE(
          "1D texture has tiling enabled in the fetch constant, but this "
          "appears to be completely wrong - ignoring! Report the game to Xenia "
          "developers");
      is_invalid_1d = true;
    }
    assert_false(fetch.packed_mips);
    if (fetch.packed_mips) {
      XELOGE(
          "1D texture has packed mips enabled in the fetch constant, but this "
          "appears to be completely wrong - ignoring! Report the game to Xenia "
          "developers");
      is_invalid_1d = true;
    }
    if (is_invalid_1d) {
      return;
    }
  }

  xenos::TextureFormat format = GetBaseFormat(fetch.format);

  key_out.base_page = base_page;
  key_out.mip_page = mip_page;
  key_out.dimension = fetch.dimension;
  key_out.width = width;
  key_out.height = height;
  key_out.depth = depth_or_faces;
  key_out.pitch = fetch.pitch;
  key_out.mip_max_level = mip_max_level;
  key_out.tiled = fetch.tiled;
  key_out.packed_mips = fetch.packed_mips;
  key_out.format = format;
  key_out.endianness = fetch.endianness;

  if (host_swizzle_out != nullptr) {
    uint32_t host_swizzle = 0;
    for (uint32_t i = 0; i < 4; ++i) {
      uint32_t host_swizzle_component = (fetch.swizzle >> (i * 3)) & 0b111;
      if (host_swizzle_component >= 4) {
        // Get rid of 6 and 7 values (to prevent device losses if the game has
        // something broken) the quick and dirty way - by changing them to 4 (0)
        // and 5 (1).
        host_swizzle_component &= 0b101;
      } else {
        host_swizzle_component =
            host_formats_[uint32_t(format)].swizzle[host_swizzle_component];
      }
      host_swizzle |= host_swizzle_component << (i * 3);
    }
    *host_swizzle_out = host_swizzle;
  }

  if (swizzled_signs_out != nullptr) {
    *swizzled_signs_out = texture_util::SwizzleSigns(fetch);
  }
}

void TextureCache::LogTextureKeyAction(TextureKey key, const char* action) {
  XELOGGPU(
      "{} {} {}{}x{}x{} {} {} texture with {} {}packed mip level{}, "
      "base at 0x{:08X} (pitch {}), mips at 0x{:08X}",
      action, key.tiled ? "tiled" : "linear",
      key.scaled_resolve ? "scaled " : "", key.width, key.height, key.depth,
      dimension_names_[uint32_t(key.dimension)],
      FormatInfo::Get(key.format)->name, key.mip_max_level + 1,
      key.packed_mips ? "" : "un", key.mip_max_level != 0 ? "s" : "",
      key.base_page << 12, key.pitch << 5, key.mip_page << 12);
}

void TextureCache::LogTextureAction(const Texture* texture,
                                    const char* action) {
  XELOGGPU(
      "{} {} {}{}x{}x{} {} {} texture with {} {}packed mip level{}, "
      "base at 0x{:08X} (pitch {}, size 0x{:08X}), mips at 0x{:08X} (size "
      "0x{:08X})",
      action, texture->key.tiled ? "tiled" : "linear",
      texture->key.scaled_resolve ? "scaled " : "", texture->key.width,
      texture->key.height, texture->key.depth,
      dimension_names_[uint32_t(texture->key.dimension)],
      FormatInfo::Get(texture->key.format)->name,
      texture->key.mip_max_level + 1, texture->key.packed_mips ? "" : "un",
      texture->key.mip_max_level != 0 ? "s" : "", texture->key.base_page << 12,
      texture->key.pitch << 5, texture->GetGuestBaseSize(),
      texture->key.mip_page << 12, texture->GetGuestMipsSize());
}

TextureCache::Texture* TextureCache::FindOrCreateTexture(TextureKey key) {
  // Check if the texture is a scaled resolve texture.
  if (draw_resolution_scale_ > 1 && key.tiled) {
    LoadMode load_mode = GetLoadMode(key);
    if (load_mode != LoadMode::kUnknown &&
        load_pipelines_scaled_[uint32_t(load_mode)] != nullptr) {
      texture_util::TextureGuestLayout scaled_resolve_guest_layout =
          texture_util::GetGuestTextureLayout(
              key.dimension, key.pitch, key.width, key.height, key.depth,
              key.tiled, key.format, key.packed_mips, key.base_page != 0,
              key.mip_max_level);
      if ((scaled_resolve_guest_layout.base.level_data_extent_bytes &&
           IsRangeScaledResolved(
               key.base_page << 12,
               scaled_resolve_guest_layout.base.level_data_extent_bytes)) ||
          (scaled_resolve_guest_layout.mips_total_extent_bytes &&
           IsRangeScaledResolved(
               key.mip_page << 12,
               scaled_resolve_guest_layout.mips_total_extent_bytes))) {
        key.scaled_resolve = 1;
      }
    }
  }
  uint32_t host_width = key.width;
  uint32_t host_height = key.height;
  if (key.scaled_resolve) {
    host_width *= draw_resolution_scale_;
    host_height *= draw_resolution_scale_;
  }
  // With 3x resolution scaling, a 2D texture may become bigger than the
  // Direct3D 11 limit, and with 2x, a 3D one as well.
  uint32_t max_host_width_height = GetMaxHostTextureWidthHeight(key.dimension);
  uint32_t max_host_depth = GetMaxHostTextureDepth(key.dimension);
  if (host_width > max_host_width_height ||
      host_height > max_host_width_height || key.depth > max_host_depth) {
    return nullptr;
  }

  // Try to find an existing texture.
  // TODO(Triang3l): Reuse a texture with mip_page unchanged, but base_page
  // previously 0, now not 0, to save memory - common case in streaming.
  auto found_texture_it = textures_.find(key);
  if (found_texture_it != textures_.end()) {
    return found_texture_it->second;
  }

  // Create the resource. If failed to create one, don't create a texture object
  // at all so it won't be in indeterminate state.
  D3D12_RESOURCE_DESC desc;
  desc.Format = GetDXGIResourceFormat(key);
  if (desc.Format == DXGI_FORMAT_UNKNOWN) {
    unsupported_format_features_used_[uint32_t(key.format)] |=
        kUnsupportedResourceBit;
    return nullptr;
  }
  if (key.dimension == xenos::DataDimension::k3D) {
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  } else {
    // 1D textures are treated as 2D for simplicity.
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  }
  desc.Alignment = 0;
  desc.Width = host_width;
  desc.Height = host_height;
  desc.DepthOrArraySize = key.depth;
  desc.MipLevels = key.mip_max_level + 1;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  // Untiling through a buffer instead of using unordered access because copying
  // is not done that often.
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  auto device = provider.GetDevice();
  // Assuming untiling will be the next operation.
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
  ID3D12Resource* resource;
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault,
          provider.GetHeapFlagCreateNotZeroed(), &desc, state, nullptr,
          IID_PPV_ARGS(&resource)))) {
    LogTextureKeyAction(key, "Failed to create");
    return nullptr;
  }

  // Create the texture object and add it to the map.
  Texture* texture = new Texture;
  texture->key = key;
  texture->resource = resource;
  texture->resource_size =
      device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
  texture->state = state;
  texture->last_usage_frame = command_processor_.GetCurrentFrame();
  texture->last_usage_time = texture_current_usage_time_;
  texture->used_previous = texture_used_last_;
  texture->used_next = nullptr;
  if (texture_used_last_ != nullptr) {
    texture_used_last_->used_next = texture;
  } else {
    texture_used_first_ = texture;
  }
  texture_used_last_ = texture;
  texture->base_resolved = key.scaled_resolve;
  texture->mips_resolved = key.scaled_resolve;
  texture->guest_layout = texture_util::GetGuestTextureLayout(
      key.dimension, key.pitch, key.width, key.height, key.depth, key.tiled,
      key.format, key.packed_mips, key.base_page != 0, key.mip_max_level);
  // Never try to upload data that doesn't exist.
  texture->base_in_sync = !texture->guest_layout.base.level_data_extent_bytes;
  texture->mips_in_sync = !texture->guest_layout.mips_total_extent_bytes;
  texture->base_watch_handle = nullptr;
  texture->mip_watch_handle = nullptr;
  textures_.emplace(key, texture);
  COUNT_profile_set("gpu/texture_cache/textures", textures_.size());
  textures_total_size_ += texture->resource_size;
  COUNT_profile_set("gpu/texture_cache/total_size_mb",
                    uint32_t(textures_total_size_ >> 20));
  LogTextureAction(texture, "Created");

  return texture;
}

bool TextureCache::LoadTextureData(Texture* texture) {
  // See what we need to upload.
  bool base_in_sync, mips_in_sync;
  {
    auto global_lock = global_critical_region_.Acquire();
    base_in_sync = texture->base_in_sync;
    mips_in_sync = texture->mips_in_sync;
  }
  if (base_in_sync && mips_in_sync) {
    return true;
  }

  auto& command_list = command_processor_.GetDeferredCommandList();
  auto device =
      command_processor_.GetD3D12Context().GetD3D12Provider().GetDevice();

  // Get the pipeline.
  LoadMode load_mode = GetLoadMode(texture->key);
  if (load_mode == LoadMode::kUnknown) {
    return false;
  }
  uint32_t texture_resolution_scale =
      texture->key.scaled_resolve ? draw_resolution_scale_ : 1;
  ID3D12PipelineState* pipeline =
      texture_resolution_scale > 1 ? load_pipelines_scaled_[uint32_t(load_mode)]
                                   : load_pipelines_[uint32_t(load_mode)];
  if (pipeline == nullptr) {
    return false;
  }
  const LoadModeInfo& load_mode_info = load_mode_info_[uint32_t(load_mode)];
  const LoadShaderInfo& load_shader_info =
      load_mode_info.shaders[texture_resolution_scale - 1];

  // Request uploading of the texture data to the shared memory.
  // This is also necessary when resolution scale is used - the texture cache
  // relies on shared memory for invalidation of both unscaled and scaled
  // textures! Plus a texture may be unscaled partially, when only a portion of
  // its pages is invalidated, in this case we'll need the texture from the
  // shared memory to load the unscaled parts.
  // TODO(Triang3l): Load unscaled parts.
  bool base_resolved = texture->base_resolved;
  if (!base_in_sync) {
    if (!shared_memory_.RequestRange(
            texture->key.base_page << 12, texture->GetGuestBaseSize(),
            texture->key.scaled_resolve ? nullptr : &base_resolved)) {
      return false;
    }
  }
  bool mips_resolved = texture->mips_resolved;
  if (!mips_in_sync) {
    if (!shared_memory_.RequestRange(
            texture->key.mip_page << 12, texture->GetGuestMipsSize(),
            texture->key.scaled_resolve ? nullptr : &mips_resolved)) {
      return false;
    }
  }
  if (texture_resolution_scale > 1) {
    // Make sure all heaps are created.
    if (!EnsureScaledResolveMemoryCommitted(texture->key.base_page << 12,
                                            texture->GetGuestBaseSize())) {
      return false;
    }
    if (!EnsureScaledResolveMemoryCommitted(texture->key.mip_page << 12,
                                            texture->GetGuestMipsSize())) {
      return false;
    }
  }

  // Get the guest layout.
  xenos::DataDimension dimension = texture->key.dimension;
  bool is_3d = dimension == xenos::DataDimension::k3D;
  uint32_t width = texture->key.width;
  uint32_t height = texture->key.height;
  uint32_t depth_or_array_size = texture->key.depth;
  uint32_t depth = is_3d ? depth_or_array_size : 1;
  uint32_t array_size = is_3d ? 1 : depth_or_array_size;
  xenos::TextureFormat guest_format = texture->key.format;
  const FormatInfo* guest_format_info = FormatInfo::Get(guest_format);
  uint32_t block_width = guest_format_info->block_width;
  uint32_t block_height = guest_format_info->block_height;
  uint32_t bytes_per_block = guest_format_info->bytes_per_block();
  uint32_t level_first = base_in_sync ? 1 : 0;
  uint32_t level_last = mips_in_sync ? 0 : texture->key.mip_max_level;
  assert_true(level_first <= level_last);
  uint32_t level_packed = texture->guest_layout.packed_level;
  uint32_t level_stored_first = std::min(level_first, level_packed);
  uint32_t level_stored_last = std::min(level_last, level_packed);

  // Get the host layout and the buffer.
  UINT64 copy_buffer_size = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT host_slice_layout_base;
  UINT64 host_slice_size_base;
  // Indexing is the same as for guest stored mips:
  // 1...min(level_last, level_packed) if level_packed is not 0, or only 0 if
  // level_packed == 0.
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT
  host_slice_layouts_mips[xenos::kTexture2DCubeMaxWidthHeightLog2 + 1];
  UINT64 host_slice_sizes_mips[xenos::kTexture2DCubeMaxWidthHeightLog2 + 1];
  {
    // Using custom calculations instead of GetCopyableFootprints because
    // shaders may copy multiple blocks per thread for simplicity. For 3x
    // resolution scaling, the number becomes a multiple of 3 rather than a
    // power of 2 - so the 256-byte alignment required anyway by Direct3D 12 is
    // not enough. GetCopyableFootprints would be needed to be called with an
    // overaligned width - but it may exceed 16384 (the maximum Direct3D 12
    // texture size) for 3x resolution scaling, and the function will fail.
    DXGI_FORMAT host_copy_format;
    uint32_t host_block_width;
    uint32_t host_block_height;
    uint32_t host_bytes_per_block;
    ui::d3d12::util::GetFormatCopyInfo(
        GetDXGIResourceFormat(guest_format, width, height), 0, host_copy_format,
        host_block_width, host_block_height, host_bytes_per_block);
    if (!level_first) {
      host_slice_layout_base.Offset = copy_buffer_size;
      host_slice_layout_base.Footprint.Format = host_copy_format;
      if (!level_packed) {
        // Loading the packed tail for the base - load the whole tail to copy
        // regions out of it.
        const texture_util::TextureGuestLayout::Level& guest_layout_base =
            texture->guest_layout.base;
        host_slice_layout_base.Footprint.Width =
            guest_layout_base.x_extent_blocks * block_width;
        host_slice_layout_base.Footprint.Height =
            guest_layout_base.y_extent_blocks * block_height;
        host_slice_layout_base.Footprint.Depth = guest_layout_base.z_extent;
      } else {
        host_slice_layout_base.Footprint.Width = width;
        host_slice_layout_base.Footprint.Height = height;
        host_slice_layout_base.Footprint.Depth = depth;
      }
      host_slice_layout_base.Footprint.Width = xe::round_up(
          host_slice_layout_base.Footprint.Width * texture_resolution_scale,
          UINT(host_block_width));
      host_slice_layout_base.Footprint.Height = xe::round_up(
          host_slice_layout_base.Footprint.Height * texture_resolution_scale,
          UINT(host_block_height));
      host_slice_layout_base.Footprint.RowPitch =
          xe::align(xe::round_up(host_slice_layout_base.Footprint.Width /
                                     host_block_width,
                                 load_shader_info.host_x_blocks_per_thread) *
                        host_bytes_per_block,
                    uint32_t(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
      host_slice_size_base = xe::align(
          UINT64(host_slice_layout_base.Footprint.RowPitch) *
              (host_slice_layout_base.Footprint.Height / host_block_height) *
              host_slice_layout_base.Footprint.Depth,
          UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT));
      copy_buffer_size += host_slice_size_base * array_size;
    }
    if (level_last) {
      for (uint32_t level = level_stored_first; level <= level_stored_last;
           ++level) {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT& host_slice_layout_mip =
            host_slice_layouts_mips[level];
        host_slice_layout_mip.Offset = copy_buffer_size;
        host_slice_layout_mip.Footprint.Format = host_copy_format;
        if (level == level_packed) {
          // Loading the packed tail for the mips - load the whole tail to copy
          // regions out of it.
          const texture_util::TextureGuestLayout::Level&
              guest_layout_packed_mips = texture->guest_layout.mips[level];
          host_slice_layout_mip.Footprint.Width =
              guest_layout_packed_mips.x_extent_blocks * block_width;
          host_slice_layout_mip.Footprint.Height =
              guest_layout_packed_mips.y_extent_blocks * block_height;
          host_slice_layout_mip.Footprint.Depth =
              guest_layout_packed_mips.z_extent;
        } else {
          host_slice_layout_mip.Footprint.Width =
              std::max(width >> level, uint32_t(1));
          host_slice_layout_mip.Footprint.Height =
              std::max(height >> level, uint32_t(1));
          host_slice_layout_mip.Footprint.Depth =
              std::max(depth >> level, uint32_t(1));
        }
        host_slice_layout_mip.Footprint.Width = xe::round_up(
            host_slice_layout_mip.Footprint.Width * texture_resolution_scale,
            UINT(host_block_width));
        host_slice_layout_mip.Footprint.Height = xe::round_up(
            host_slice_layout_mip.Footprint.Height * texture_resolution_scale,
            UINT(host_block_height));
        host_slice_layout_mip.Footprint.RowPitch =
            xe::align(xe::round_up(host_slice_layout_mip.Footprint.Width /
                                       host_block_width,
                                   load_shader_info.host_x_blocks_per_thread) *
                          host_bytes_per_block,
                      uint32_t(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
        UINT64 host_slice_sizes_mip = xe::align(
            UINT64(host_slice_layout_mip.Footprint.RowPitch) *
                (host_slice_layout_mip.Footprint.Height / host_block_height) *
                host_slice_layout_mip.Footprint.Depth,
            UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT));
        host_slice_sizes_mips[level] = host_slice_sizes_mip;
        copy_buffer_size += host_slice_sizes_mip * array_size;
      }
    }
  }
  D3D12_RESOURCE_STATES copy_buffer_state =
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ID3D12Resource* copy_buffer = command_processor_.RequestScratchGPUBuffer(
      uint32_t(copy_buffer_size), copy_buffer_state);
  if (copy_buffer == nullptr) {
    return false;
  }
  uint32_t host_block_width = 1;
  uint32_t host_block_height = 1;
  if (host_formats_[uint32_t(guest_format)].dxgi_format_block_aligned &&
      !IsDecompressionNeeded(guest_format, width, height)) {
    host_block_width = block_width;
    host_block_height = block_height;
  }

  // Begin loading.
  // May use different buffers for scaled base and mips, and also can't address
  // more than 128 megatexels directly on Nvidia - need two separate UAV
  // descriptors for base and mips.
  // Destination.
  uint32_t descriptor_count = 1;
  if (texture_resolution_scale > 1) {
    // Source - base and mips, one or both.
    descriptor_count += (level_first == 0 && level_last != 0) ? 2 : 1;
  } else {
    // Source - shared memory.
    if (!bindless_resources_used_) {
      ++descriptor_count;
    }
  }
  ui::d3d12::util::DescriptorCpuGpuHandlePair descriptors_allocated[3];
  if (!command_processor_.RequestOneUseSingleViewDescriptors(
          descriptor_count, descriptors_allocated)) {
    command_processor_.ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);
    return false;
  }
  uint32_t descriptor_write_index = 0;
  command_processor_.SetExternalPipeline(pipeline);
  command_list.D3DSetComputeRootSignature(load_root_signature_);
  // Set up the destination descriptor.
  assert_true(descriptor_write_index < descriptor_count);
  ui::d3d12::util::DescriptorCpuGpuHandlePair descriptor_dest =
      descriptors_allocated[descriptor_write_index++];
  ui::d3d12::util::CreateBufferTypedUAV(
      device, descriptor_dest.first, copy_buffer,
      ui::d3d12::util::GetUintPow2DXGIFormat(load_shader_info.uav_bpe_log2),
      uint32_t(copy_buffer_size) >> load_shader_info.uav_bpe_log2);
  command_list.D3DSetComputeRootDescriptorTable(2, descriptor_dest.second);
  // Set up the unscaled source descriptor (scaled needs two descriptors that
  // depend on the buffer being current, so they will be set later - for mips,
  // after loading the base is done).
  if (texture_resolution_scale <= 1) {
    shared_memory_.UseForReading();
    ui::d3d12::util::DescriptorCpuGpuHandlePair descriptor_unscaled_source;
    if (bindless_resources_used_) {
      descriptor_unscaled_source =
          command_processor_.GetSharedMemoryUintPow2BindlessSRVHandlePair(
              load_shader_info.srv_bpe_log2);
    } else {
      assert_true(descriptor_write_index < descriptor_count);
      descriptor_unscaled_source =
          descriptors_allocated[descriptor_write_index++];
      shared_memory_.WriteUintPow2SRVDescriptor(
          descriptor_unscaled_source.first, load_shader_info.srv_bpe_log2);
    }
    command_list.D3DSetComputeRootDescriptorTable(
        1, descriptor_unscaled_source.second);
  }

  // Submit the copy buffer population commands.

  auto& cbuffer_pool = command_processor_.GetConstantBufferPool();
  LoadConstants load_constants;
  load_constants.is_tiled_3d_endian = uint32_t(texture->key.tiled) |
                                      (uint32_t(is_3d) << 1) |
                                      (uint32_t(texture->key.endianness) << 2);

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
  // The loop is slices within levels because the base and the levels may need
  // different portions of the scaled resolve virtual address space to be
  // available through buffers, and to create a descriptor, the buffer start
  // address is required - which may be different for base and mips.
  bool scaled_mips_source_set_up = false;
  for (uint32_t loop_level = loop_level_first; loop_level <= loop_level_last;
       ++loop_level) {
    bool is_base = loop_level == 0;
    uint32_t level = (level_packed == 0) ? 0 : loop_level;

    uint32_t guest_address =
        (is_base ? texture->key.base_page : texture->key.mip_page) << 12;

    // Set up the base or mips source, also making it accessible if loading from
    // scaled resolve memory.
    if (texture_resolution_scale > 1 &&
        (is_base || !scaled_mips_source_set_up)) {
      uint32_t guest_size_unscaled =
          is_base ? texture->GetGuestBaseSize() : texture->GetGuestMipsSize();
      if (!MakeScaledResolveRangeCurrent(guest_address, guest_size_unscaled)) {
        command_processor_.ReleaseScratchGPUBuffer(copy_buffer,
                                                   copy_buffer_state);
        return false;
      }
      TransitionCurrentScaledResolveRange(
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      assert_true(descriptor_write_index < descriptor_count);
      ui::d3d12::util::DescriptorCpuGpuHandlePair descriptor_scaled_source =
          descriptors_allocated[descriptor_write_index++];
      CreateCurrentScaledResolveRangeUintPow2SRV(descriptor_scaled_source.first,
                                                 load_shader_info.srv_bpe_log2);
      command_list.D3DSetComputeRootDescriptorTable(
          1, descriptor_scaled_source.second);
      if (!is_base) {
        scaled_mips_source_set_up = true;
      }
    }

    if (texture_resolution_scale > 1) {
      // Offset already applied in the buffer because more than 512 MB can't be
      // directly addresses on Nvidia as R32.
      load_constants.guest_offset = 0;
    } else {
      load_constants.guest_offset = guest_address;
    }
    if (!is_base) {
      load_constants.guest_offset +=
          texture->guest_layout.mip_offsets_bytes[level];
    }
    const texture_util::TextureGuestLayout::Level& level_guest_layout =
        is_base ? texture->guest_layout.base
                : texture->guest_layout.mips[level];
    uint32_t level_guest_pitch = level_guest_layout.row_pitch_bytes;
    if (texture->key.tiled) {
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
      level_width = std::max(width >> level, uint32_t(1));
      level_height = std::max(height >> level, uint32_t(1));
      level_depth = std::max(depth >> level, uint32_t(1));
    }
    load_constants.size_blocks[0] =
        (level_width + (block_width - 1)) / block_width;
    load_constants.size_blocks[1] =
        (level_height + (block_height - 1)) / block_height;
    load_constants.size_blocks[2] = level_depth;
    load_constants.height_texels = level_height;

    // Each thread group processes 32x32x1 guest blocks.
    uint32_t group_count_x = (load_constants.size_blocks[0] + 31) >> 5;
    uint32_t group_count_y = (load_constants.size_blocks[1] + 31) >> 5;

    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& host_slice_layout =
        is_base ? host_slice_layout_base : host_slice_layouts_mips[level];
    uint32_t host_slice_size =
        uint32_t(is_base ? host_slice_size_base : host_slice_sizes_mips[level]);
    load_constants.host_offset = uint32_t(host_slice_layout.Offset);
    load_constants.host_pitch = host_slice_layout.Footprint.RowPitch;

    for (uint32_t slice = 0; slice < array_size; ++slice) {
      D3D12_GPU_VIRTUAL_ADDRESS cbuffer_gpu_address;
      uint8_t* cbuffer_mapping = cbuffer_pool.Request(
          command_processor_.GetCurrentFrame(), sizeof(load_constants),
          D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
          &cbuffer_gpu_address);
      if (cbuffer_mapping == nullptr) {
        command_processor_.ReleaseScratchGPUBuffer(copy_buffer,
                                                   copy_buffer_state);
        return false;
      }
      std::memcpy(cbuffer_mapping, &load_constants, sizeof(load_constants));
      command_list.D3DSetComputeRootConstantBufferView(0, cbuffer_gpu_address);
      assert_true(copy_buffer_state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      command_processor_.SubmitBarriers();
      command_list.D3DDispatch(group_count_x, group_count_y,
                               load_constants.size_blocks[2]);
      load_constants.guest_offset +=
          level_guest_layout.array_slice_stride_bytes;
      load_constants.host_offset += host_slice_size;
    }
  }

  // Update LRU caching because the texture will be used by the command list.
  MarkTextureUsed(texture);

  // Submit copying from the copy buffer to the host texture.
  command_processor_.PushTransitionBarrier(texture->resource, texture->state,
                                           D3D12_RESOURCE_STATE_COPY_DEST);
  texture->state = D3D12_RESOURCE_STATE_COPY_DEST;
  command_processor_.PushTransitionBarrier(copy_buffer, copy_buffer_state,
                                           D3D12_RESOURCE_STATE_COPY_SOURCE);
  copy_buffer_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
  command_processor_.SubmitBarriers();
  uint32_t texture_level_count = texture->key.mip_max_level + 1;
  D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
  location_source.pResource = copy_buffer;
  location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  location_dest.pResource = texture->resource;
  location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  for (uint32_t level = level_first; level <= level_last; ++level) {
    uint32_t guest_level = std::min(level, level_packed);
    location_source.PlacedFootprint =
        level ? host_slice_layouts_mips[guest_level] : host_slice_layout_base;
    location_dest.SubresourceIndex = level;
    UINT64 host_slice_size =
        level ? host_slice_sizes_mips[guest_level] : host_slice_size_base;
    D3D12_BOX source_box;
    const D3D12_BOX* source_box_ptr;
    if (level >= level_packed) {
      uint32_t level_offset_blocks_x, level_offset_blocks_y, level_offset_z;
      texture_util::GetPackedMipOffset(width, height, depth, guest_format,
                                       level, level_offset_blocks_x,
                                       level_offset_blocks_y, level_offset_z);
      source_box.left = level_offset_blocks_x * block_width;
      source_box.top = level_offset_blocks_y * block_height;
      source_box.front = level_offset_z;
      source_box.right =
          source_box.left +
          xe::align(std::max(width >> level, uint32_t(1)), host_block_width);
      source_box.bottom =
          source_box.top +
          xe::align(std::max(height >> level, uint32_t(1)), host_block_height);
      source_box.back =
          source_box.front + std::max(depth >> level, uint32_t(1));
      source_box_ptr = &source_box;
    } else {
      source_box_ptr = nullptr;
    }
    for (uint32_t slice = 0; slice < array_size; ++slice) {
      command_list.D3DCopyTextureRegion(&location_dest, 0, 0, 0,
                                        &location_source, source_box_ptr);
      location_dest.SubresourceIndex += texture_level_count;
      location_source.PlacedFootprint.Offset += host_slice_size;
    }
  }

  command_processor_.ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);

  // Update the source of the texture (resolve vs. CPU or memexport) for
  // purposes of handling piecewise gamma emulation via sRGB and for resolution
  // scale in sampling offsets.
  texture->base_resolved = base_resolved;
  texture->mips_resolved = mips_resolved;

  // Mark the ranges as uploaded and watch them. This is needed for scaled
  // resolves as well to detect when the CPU wants to reuse the memory for a
  // regular texture or a vertex buffer, and thus the scaled resolve version is
  // not up to date anymore.
  {
    auto global_lock = global_critical_region_.Acquire();
    texture->base_in_sync = true;
    texture->mips_in_sync = true;
    if (!base_in_sync) {
      texture->base_watch_handle = shared_memory_.WatchMemoryRange(
          texture->key.base_page << 12, texture->GetGuestBaseSize(),
          WatchCallbackThunk, this, texture, 0);
    }
    if (!mips_in_sync) {
      texture->mip_watch_handle = shared_memory_.WatchMemoryRange(
          texture->key.mip_page << 12, texture->GetGuestMipsSize(),
          WatchCallbackThunk, this, texture, 1);
    }
  }

  LogTextureAction(texture, "Loaded");
  return true;
}

uint32_t TextureCache::FindOrCreateTextureDescriptor(Texture& texture,
                                                     bool is_signed,
                                                     uint32_t host_swizzle) {
  uint32_t descriptor_key = uint32_t(is_signed) | (host_swizzle << 1);

  // Try to find an existing descriptor.
  auto it = texture.srv_descriptors.find(descriptor_key);
  if (it != texture.srv_descriptors.end()) {
    return it->second;
  }

  // Create a new bindless or cached descriptor if supported.
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;

  xenos::TextureFormat format = texture.key.format;
  if (IsSignedVersionSeparate(format) &&
      texture.key.signed_separate != uint32_t(is_signed)) {
    // Not the version with the needed signedness.
    return UINT32_MAX;
  }
  if (is_signed) {
    // Not supporting signed compressed textures - hopefully DXN and DXT5A are
    // not used as signed.
    desc.Format = host_formats_[uint32_t(format)].dxgi_format_snorm;
  } else {
    desc.Format = GetDXGIUnormFormat(texture.key);
  }
  if (desc.Format == DXGI_FORMAT_UNKNOWN) {
    unsupported_format_features_used_[uint32_t(format)] |=
        is_signed ? kUnsupportedSnormBit : kUnsupportedUnormBit;
    return UINT32_MAX;
  }

  uint32_t mip_levels = texture.key.mip_max_level + 1;
  switch (texture.key.dimension) {
    case xenos::DataDimension::k1D:
    case xenos::DataDimension::k2DOrStacked:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      desc.Texture2DArray.MostDetailedMip = 0;
      desc.Texture2DArray.MipLevels = mip_levels;
      desc.Texture2DArray.FirstArraySlice = 0;
      desc.Texture2DArray.ArraySize = texture.key.depth;
      desc.Texture2DArray.PlaneSlice = 0;
      desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
      break;
    case xenos::DataDimension::k3D:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      desc.Texture3D.MostDetailedMip = 0;
      desc.Texture3D.MipLevels = mip_levels;
      desc.Texture3D.ResourceMinLODClamp = 0.0f;
      break;
    case xenos::DataDimension::kCube:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      desc.TextureCube.MostDetailedMip = 0;
      desc.TextureCube.MipLevels = mip_levels;
      desc.TextureCube.ResourceMinLODClamp = 0.0f;
      break;
    default:
      assert_unhandled_case(texture.key.dimension);
      return UINT32_MAX;
  }

  desc.Shader4ComponentMapping =
      host_swizzle |
      D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES;

  auto device =
      command_processor_.GetD3D12Context().GetD3D12Provider().GetDevice();
  uint32_t descriptor_index;
  if (bindless_resources_used_) {
    descriptor_index =
        command_processor_.RequestPersistentViewBindlessDescriptor();
    if (descriptor_index == UINT32_MAX) {
      XELOGE(
          "Failed to create a texture descriptor - no free bindless view "
          "descriptors");
      return UINT32_MAX;
    }
  } else {
    if (!srv_descriptor_cache_free_.empty()) {
      descriptor_index = srv_descriptor_cache_free_.back();
      srv_descriptor_cache_free_.pop_back();
    } else {
      // Allocated + 1 (including the descriptor that is being added), rounded
      // up to SRVDescriptorCachePage::kHeapSize, (allocated + 1 + size - 1).
      uint32_t cache_pages_needed = (srv_descriptor_cache_allocated_ +
                                     SRVDescriptorCachePage::kHeapSize) /
                                    SRVDescriptorCachePage::kHeapSize;
      if (srv_descriptor_cache_.size() < cache_pages_needed) {
        D3D12_DESCRIPTOR_HEAP_DESC cache_heap_desc;
        cache_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cache_heap_desc.NumDescriptors = SRVDescriptorCachePage::kHeapSize;
        cache_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        cache_heap_desc.NodeMask = 0;
        while (srv_descriptor_cache_.size() < cache_pages_needed) {
          SRVDescriptorCachePage cache_page;
          if (FAILED(device->CreateDescriptorHeap(
                  &cache_heap_desc, IID_PPV_ARGS(&cache_page.heap)))) {
            XELOGE(
                "Failed to create a texture descriptor - couldn't create a "
                "descriptor cache heap");
            return UINT32_MAX;
          }
          cache_page.heap_start =
              cache_page.heap->GetCPUDescriptorHandleForHeapStart();
          srv_descriptor_cache_.push_back(cache_page);
        }
      }
      descriptor_index = srv_descriptor_cache_allocated_++;
    }
  }
  device->CreateShaderResourceView(
      texture.resource, &desc, GetTextureDescriptorCPUHandle(descriptor_index));
  texture.srv_descriptors.emplace(descriptor_key, descriptor_index);
  return descriptor_index;
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureCache::GetTextureDescriptorCPUHandle(
    uint32_t descriptor_index) const {
  auto& provider = command_processor_.GetD3D12Context().GetD3D12Provider();
  if (bindless_resources_used_) {
    return provider.OffsetViewDescriptor(
        command_processor_.GetViewBindlessHeapCPUStart(), descriptor_index);
  }
  D3D12_CPU_DESCRIPTOR_HANDLE heap_start =
      srv_descriptor_cache_[descriptor_index /
                            SRVDescriptorCachePage::kHeapSize]
          .heap_start;
  uint32_t heap_offset = descriptor_index % SRVDescriptorCachePage::kHeapSize;
  return provider.OffsetViewDescriptor(heap_start, heap_offset);
}

void TextureCache::MarkTextureUsed(Texture* texture) {
  uint64_t current_frame = command_processor_.GetCurrentFrame();
  // This is called very frequently, don't relink unless needed for caching.
  if (texture->last_usage_frame != current_frame) {
    texture->last_usage_frame = current_frame;
    texture->last_usage_time = texture_current_usage_time_;
    if (texture->used_next == nullptr) {
      // Simplify the code a bit - already in the end of the list.
      return;
    }
    if (texture->used_previous != nullptr) {
      texture->used_previous->used_next = texture->used_next;
    } else {
      texture_used_first_ = texture->used_next;
    }
    texture->used_next->used_previous = texture->used_previous;
    texture->used_previous = texture_used_last_;
    texture->used_next = nullptr;
    if (texture_used_last_ != nullptr) {
      texture_used_last_->used_next = texture;
    }
    texture_used_last_ = texture;
  }
}

void TextureCache::WatchCallbackThunk(void* context, void* data,
                                      uint64_t argument,
                                      bool invalidated_by_gpu) {
  TextureCache* texture_cache = reinterpret_cast<TextureCache*>(context);
  texture_cache->WatchCallback(reinterpret_cast<Texture*>(data), argument != 0);
}

void TextureCache::WatchCallback(Texture* texture, bool is_mip) {
  // Mutex already locked here.
  if (is_mip) {
    texture->mips_in_sync = false;
    texture->mip_watch_handle = nullptr;
  } else {
    texture->base_in_sync = false;
    texture->base_watch_handle = nullptr;
  }
  texture_invalidated_.store(true, std::memory_order_release);
}

void TextureCache::ClearBindings() {
  for (size_t i = 0; i < xe::countof(texture_bindings_); ++i) {
    texture_bindings_[i].Clear();
  }
  texture_bindings_in_sync_ = 0;
  // Already reset everything.
  texture_invalidated_.store(false, std::memory_order_relaxed);
}

bool TextureCache::IsRangeScaledResolved(uint32_t start_unscaled,
                                         uint32_t length_unscaled) {
  if (draw_resolution_scale_ <= 1) {
    return false;
  }

  start_unscaled = std::min(start_unscaled, SharedMemory::kBufferSize);
  length_unscaled =
      std::min(length_unscaled, SharedMemory::kBufferSize - start_unscaled);
  if (!length_unscaled) {
    return false;
  }

  // Two-level check for faster rejection since resolve targets are usually
  // placed in relatively small and localized memory portions (confirmed by
  // testing - pretty much all times the deeper level was entered, the texture
  // was a resolve target).
  uint32_t page_first = start_unscaled >> 12;
  uint32_t page_last = (start_unscaled + length_unscaled - 1) >> 12;
  uint32_t block_first = page_first >> 5;
  uint32_t block_last = page_last >> 5;
  uint32_t l2_block_first = block_first >> 6;
  uint32_t l2_block_last = block_last >> 6;
  auto global_lock = global_critical_region_.Acquire();
  for (uint32_t i = l2_block_first; i <= l2_block_last; ++i) {
    uint64_t l2_block = scaled_resolve_pages_l2_[i];
    if (i == l2_block_first) {
      l2_block &= ~((1ull << (block_first & 63)) - 1);
    }
    if (i == l2_block_last && (block_last & 63) != 63) {
      l2_block &= (1ull << ((block_last & 63) + 1)) - 1;
    }
    uint32_t block_relative_index;
    while (xe::bit_scan_forward(l2_block, &block_relative_index)) {
      l2_block &= ~(1ull << block_relative_index);
      uint32_t block_index = (i << 6) + block_relative_index;
      uint32_t check_bits = UINT32_MAX;
      if (block_index == block_first) {
        check_bits &= ~((1u << (page_first & 31)) - 1);
      }
      if (block_index == block_last && (page_last & 31) != 31) {
        check_bits &= (1u << ((page_last & 31) + 1)) - 1;
      }
      if (scaled_resolve_pages_[block_index] & check_bits) {
        return true;
      }
    }
  }
  return false;
}

void TextureCache::ScaledResolveGlobalWatchCallbackThunk(
    void* context, uint32_t address_first, uint32_t address_last,
    bool invalidated_by_gpu) {
  TextureCache* texture_cache = reinterpret_cast<TextureCache*>(context);
  texture_cache->ScaledResolveGlobalWatchCallback(address_first, address_last,
                                                  invalidated_by_gpu);
}

void TextureCache::ScaledResolveGlobalWatchCallback(uint32_t address_first,
                                                    uint32_t address_last,
                                                    bool invalidated_by_gpu) {
  assert_true(draw_resolution_scale_ > 1);
  if (invalidated_by_gpu) {
    // Resolves themselves do exactly the opposite of what this should do.
    return;
  }
  // Mark scaled resolve ranges as non-scaled. Textures themselves will be
  // invalidated by their own per-range watches.
  uint32_t resolve_page_first = address_first >> 12;
  uint32_t resolve_page_last = address_last >> 12;
  uint32_t resolve_block_first = resolve_page_first >> 5;
  uint32_t resolve_block_last = resolve_page_last >> 5;
  uint32_t resolve_l2_block_first = resolve_block_first >> 6;
  uint32_t resolve_l2_block_last = resolve_block_last >> 6;
  for (uint32_t i = resolve_l2_block_first; i <= resolve_l2_block_last; ++i) {
    uint64_t resolve_l2_block = scaled_resolve_pages_l2_[i];
    uint32_t resolve_block_relative_index;
    while (
        xe::bit_scan_forward(resolve_l2_block, &resolve_block_relative_index)) {
      resolve_l2_block &= ~(1ull << resolve_block_relative_index);
      uint32_t resolve_block_index = (i << 6) + resolve_block_relative_index;
      uint32_t resolve_keep_bits = 0;
      if (resolve_block_index == resolve_block_first) {
        resolve_keep_bits |= (1u << (resolve_page_first & 31)) - 1;
      }
      if (resolve_block_index == resolve_block_last &&
          (resolve_page_last & 31) != 31) {
        resolve_keep_bits |= ~((1u << ((resolve_page_last & 31) + 1)) - 1);
      }
      scaled_resolve_pages_[resolve_block_index] &= resolve_keep_bits;
      if (scaled_resolve_pages_[resolve_block_index] == 0) {
        scaled_resolve_pages_l2_[i] &= ~(1ull << resolve_block_relative_index);
      }
    }
  }
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

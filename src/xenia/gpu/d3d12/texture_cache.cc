/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/texture_cache.h"

#include <gflags/gflags.h>
#include "third_party/xxhash/xxhash.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/ui/d3d12/d3d12_util.h"

DEFINE_int32(d3d12_resolution_scale, 1,
             "Scale of rendering width and height (currently only 1 and 2 "
             "are available).");

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildhlsl`.
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_128bpb_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_128bpb_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_16bpb_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_16bpb_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_32bpb_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_32bpb_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_64bpb_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_64bpb_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_8bpb_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_8bpb_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_ctx1_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_depth_float_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_depth_float_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_depth_unorm_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_depth_unorm_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_dxn_rg8_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_dxt1_rgba8_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_dxt3_rgba8_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_dxt3a_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_dxt5_rgba8_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_dxt5a_r8_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_r10g11b11_rgba16_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_r10g11b11_rgba16_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_r10g11b11_rgba16_snorm_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_r10g11b11_rgba16_snorm_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_r11g11b10_rgba16_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_r11g11b10_rgba16_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_r11g11b10_rgba16_snorm_2x_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_load_r11g11b10_rgba16_snorm_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_tile_128bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_tile_16bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_tile_16bpp_rgba_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_tile_32bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_tile_64bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_tile_8bpp_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_tile_r10g11b11_rgba16_cs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/texture_tile_r11g11b10_rgba16_cs.h"

constexpr uint32_t TextureCache::SRVDescriptorCachePage::kHeapSize;
constexpr uint32_t TextureCache::LoadConstants::kGuestPitchTiled;
constexpr uint32_t TextureCache::kScaledResolveBufferSizeLog2;
constexpr uint32_t TextureCache::kScaledResolveBufferSize;
constexpr uint32_t TextureCache::kScaledResolveHeapSizeLog2;
constexpr uint32_t TextureCache::kScaledResolveHeapSize;

const TextureCache::HostFormat TextureCache::host_formats_[64] = {
    // k_1_REVERSE
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_1
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_8
    {DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UNORM, LoadMode::k8bpb,
     DXGI_FORMAT_R8_SNORM, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R8_UNORM, ResolveTileMode::k8bpp, true},
    // k_1_5_5_5
    {DXGI_FORMAT_B5G5R5A1_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM, LoadMode::k16bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R8G8B8A8_UNORM,
     ResolveTileMode::k16bppRGBA, false},
    // k_5_6_5
    {DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G6R5_UNORM, LoadMode::k16bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_B5G6R5_UNORM, ResolveTileMode::k16bpp,
     false},
    // k_6_5_5
    // Green bits in blue, blue bits in green - RBGA swizzle must be used.
    {DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G6R5_UNORM, LoadMode::k16bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_B5G6R5_UNORM, ResolveTileMode::k16bpp,
     false},
    // k_8_8_8_8
    {DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::k32bpb, DXGI_FORMAT_R8G8B8A8_SNORM, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R8G8B8A8_UNORM,
     ResolveTileMode::k32bpp, false},
    // k_2_10_10_10
    {DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R10G10B10A2_UNORM,
     LoadMode::k32bpb, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R10G10B10A2_UNORM,
     ResolveTileMode::k32bpp, false},
    // k_8_A
    {DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UNORM, LoadMode::k8bpb,
     DXGI_FORMAT_R8_SNORM, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R8_UNORM, ResolveTileMode::k8bpp, true},
    // k_8_B
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_8_8
    {DXGI_FORMAT_R8G8_TYPELESS, DXGI_FORMAT_R8G8_UNORM, LoadMode::k16bpb,
     DXGI_FORMAT_R8G8_SNORM, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R8G8_UNORM, ResolveTileMode::k16bpp,
     false},
    // k_Cr_Y1_Cb_Y0_REP
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_Y1_Cr_Y0_Cb_REP
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_16_16_EDRAM
    // Not usable as a texture, also has -32...32 range.
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_8_8_8_8_A
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_4_4_4_4
    {DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM, LoadMode::k16bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R8G8B8A8_UNORM,
     ResolveTileMode::k16bppRGBA, false},
    // k_10_11_11
    {DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM,
     LoadMode::kR11G11B10ToRGBA16, DXGI_FORMAT_R16G16B16A16_SNORM,
     LoadMode::kR11G11B10ToRGBA16SNorm, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_R16G16B16A16_UNORM, ResolveTileMode::kR11G11B10AsRGBA16,
     false},
    // k_11_11_10
    {DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM,
     LoadMode::kR10G11B11ToRGBA16, DXGI_FORMAT_R16G16B16A16_SNORM,
     LoadMode::kR10G11B11ToRGBA16SNorm, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_R16G16B16A16_UNORM, ResolveTileMode::kR10G11B11AsRGBA16,
     false},
    // k_DXT1
    {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM, LoadMode::k64bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT1ToRGBA8, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown,
     false},
    // k_DXT2_3
    {DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM, LoadMode::k128bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT3ToRGBA8, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown,
     false},
    // k_DXT4_5
    {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM, LoadMode::k128bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT5ToRGBA8, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown,
     false},
    // k_16_16_16_16_EDRAM
    // Not usable as a texture, also has -32...32 range.
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // R32_FLOAT for depth because shaders would require an additional SRV to
    // sample stencil, which we don't provide.
    // k_24_8
    {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, LoadMode::kDepthUnorm,
     DXGI_FORMAT_R32_FLOAT, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_24_8_FLOAT
    {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, LoadMode::kDepthFloat,
     DXGI_FORMAT_R32_FLOAT, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_16
    {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UNORM, LoadMode::k16bpb,
     DXGI_FORMAT_R16_SNORM, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R16_UNORM, ResolveTileMode::k16bpp, true},
    // k_16_16
    // TODO(Triang3l): Check if this is the correct way of specifying a signed
    // resolve destination format.
    {DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_UNORM, LoadMode::k32bpb,
     DXGI_FORMAT_R16G16_SNORM, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R16G16_SNORM, ResolveTileMode::k32bpp,
     false},
    // k_16_16_16_16
    // TODO(Triang3l): Check if this is the correct way of specifying a signed
    // resolve destination format.
    {DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM,
     LoadMode::k64bpb, DXGI_FORMAT_R16G16B16A16_SNORM, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R16G16B16A16_SNORM,
     ResolveTileMode::k64bpp, false},
    // k_16_EXPAND
    {DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_FLOAT, LoadMode::k16bpb,
     DXGI_FORMAT_R16_FLOAT, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R16_FLOAT, ResolveTileMode::k16bpp, true},
    // k_16_16_EXPAND
    {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, LoadMode::k32bpb,
     DXGI_FORMAT_R16G16_FLOAT, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R16G16_FLOAT, ResolveTileMode::k32bpp,
     false},
    // k_16_16_16_16_EXPAND
    {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
     LoadMode::k64bpb, DXGI_FORMAT_R16G16B16A16_FLOAT, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R16G16B16A16_FLOAT,
     ResolveTileMode::k64bpp, false},
    // k_16_FLOAT
    {DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_FLOAT, LoadMode::k16bpb,
     DXGI_FORMAT_R16_FLOAT, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R16_FLOAT, ResolveTileMode::k16bpp, true},
    // k_16_16_FLOAT
    {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, LoadMode::k32bpb,
     DXGI_FORMAT_R16G16_FLOAT, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R16G16_FLOAT, ResolveTileMode::k32bpp,
     false},
    // k_16_16_16_16_FLOAT
    {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
     LoadMode::k64bpb, DXGI_FORMAT_R16G16B16A16_FLOAT, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R16G16B16A16_FLOAT,
     ResolveTileMode::k64bpp, false},
    // k_32
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_32_32
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_32_32_32_32
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_32_FLOAT
    {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, LoadMode::k32bpb,
     DXGI_FORMAT_R32_FLOAT, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R32_FLOAT, ResolveTileMode::k32bpp, true},
    // k_32_32_FLOAT
    {DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, LoadMode::k64bpb,
     DXGI_FORMAT_R32G32_FLOAT, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_R32G32_FLOAT, ResolveTileMode::k64bpp,
     false},
    // k_32_32_32_32_FLOAT
    {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
     LoadMode::k128bpb, DXGI_FORMAT_R32G32B32A32_FLOAT, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R32G32B32A32_FLOAT,
     ResolveTileMode::k128bpp, false},
    // k_32_AS_8
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_32_AS_8_8
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_16_MPEG
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_16_16_MPEG
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_32_AS_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_32_AS_8_8_INTERLACED
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_16_INTERLACED
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_16_MPEG_INTERLACED
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_16_16_MPEG_INTERLACED
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_DXN
    {DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_UNORM, LoadMode::k128bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R8G8_UNORM,
     LoadMode::kDXNToRG8, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown,
     false},
    // k_8_8_8_8_AS_16_16_16_16
    {DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::k32bpb, DXGI_FORMAT_R8G8B8A8_SNORM, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R8G8B8A8_UNORM,
     ResolveTileMode::k32bpp, false},
    // k_DXT1_AS_16_16_16_16
    {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM, LoadMode::k64bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT1ToRGBA8, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown,
     false},
    // k_DXT2_3_AS_16_16_16_16
    {DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM, LoadMode::k128bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT3ToRGBA8, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown,
     false},
    // k_DXT4_5_AS_16_16_16_16
    {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM, LoadMode::k128bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R8G8B8A8_UNORM,
     LoadMode::kDXT5ToRGBA8, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown,
     false},
    // k_2_10_10_10_AS_16_16_16_16
    {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM,
     LoadMode::k32bpb, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R10G10B10A2_UNORM,
     ResolveTileMode::k32bpp, false},
    // k_10_11_11_AS_16_16_16_16
    {DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM,
     LoadMode::kR11G11B10ToRGBA16, DXGI_FORMAT_R16G16B16A16_SNORM,
     LoadMode::kR11G11B10ToRGBA16SNorm, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_R16G16B16A16_UNORM, ResolveTileMode::kR11G11B10AsRGBA16,
     false},
    // k_11_11_10_AS_16_16_16_16
    {DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM,
     LoadMode::kR10G11B11ToRGBA16, DXGI_FORMAT_R16G16B16A16_SNORM,
     LoadMode::kR10G11B11ToRGBA16SNorm, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_R16G16B16A16_UNORM, ResolveTileMode::kR10G11B11AsRGBA16,
     false},
    // k_32_32_32_FLOAT
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_DXT3A
    {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, LoadMode::kDXT3A,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, true},
    // k_DXT5A
    {DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_UNORM, LoadMode::k64bpb,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_R8_UNORM,
     LoadMode::kDXT5AToR8, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown,
     true},
    // k_CTX1
    {DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, LoadMode::kCTX1,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_DXT3A_AS_1_1_1_1
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_8_8_8_8_GAMMA_EDRAM
    // Not usable as a texture.
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
    // k_2_10_10_10_FLOAT_EDRAM
    // Not usable as a texture.
    {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown,
     DXGI_FORMAT_UNKNOWN, LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN,
     LoadMode::kUnknown, DXGI_FORMAT_UNKNOWN, ResolveTileMode::kUnknown, false},
};

const char* const TextureCache::dimension_names_[4] = {"1D", "2D", "3D",
                                                       "cube"};

const TextureCache::LoadModeInfo TextureCache::load_mode_info_[] = {
    {texture_load_8bpb_cs, sizeof(texture_load_8bpb_cs),
     texture_load_8bpb_2x_cs, sizeof(texture_load_8bpb_2x_cs)},
    {texture_load_16bpb_cs, sizeof(texture_load_16bpb_cs),
     texture_load_16bpb_2x_cs, sizeof(texture_load_16bpb_2x_cs)},
    {texture_load_32bpb_cs, sizeof(texture_load_32bpb_cs),
     texture_load_32bpb_2x_cs, sizeof(texture_load_32bpb_2x_cs)},
    {texture_load_64bpb_cs, sizeof(texture_load_64bpb_cs),
     texture_load_64bpb_2x_cs, sizeof(texture_load_64bpb_2x_cs)},
    {texture_load_128bpb_cs, sizeof(texture_load_128bpb_cs),
     texture_load_128bpb_2x_cs, sizeof(texture_load_128bpb_2x_cs)},
    {texture_load_r11g11b10_rgba16_cs, sizeof(texture_load_r11g11b10_rgba16_cs),
     texture_load_r11g11b10_rgba16_2x_cs,
     sizeof(texture_load_r11g11b10_rgba16_2x_cs)},
    {texture_load_r11g11b10_rgba16_snorm_cs,
     sizeof(texture_load_r11g11b10_rgba16_snorm_cs),
     texture_load_r11g11b10_rgba16_snorm_2x_cs,
     sizeof(texture_load_r11g11b10_rgba16_snorm_2x_cs)},
    {texture_load_r10g11b11_rgba16_cs, sizeof(texture_load_r10g11b11_rgba16_cs),
     texture_load_r10g11b11_rgba16_2x_cs,
     sizeof(texture_load_r10g11b11_rgba16_2x_cs)},
    {texture_load_r10g11b11_rgba16_snorm_cs,
     sizeof(texture_load_r10g11b11_rgba16_snorm_cs),
     texture_load_r10g11b11_rgba16_snorm_2x_cs,
     sizeof(texture_load_r10g11b11_rgba16_snorm_2x_cs)},
    {texture_load_dxt1_rgba8_cs, sizeof(texture_load_dxt1_rgba8_cs), nullptr,
     0},
    {texture_load_dxt3_rgba8_cs, sizeof(texture_load_dxt3_rgba8_cs), nullptr,
     0},
    {texture_load_dxt5_rgba8_cs, sizeof(texture_load_dxt5_rgba8_cs), nullptr,
     0},
    {texture_load_dxn_rg8_cs, sizeof(texture_load_dxn_rg8_cs), nullptr, 0},
    {texture_load_dxt3a_cs, sizeof(texture_load_dxt3a_cs), nullptr, 0},
    {texture_load_dxt5a_r8_cs, sizeof(texture_load_dxt5a_r8_cs), nullptr, 0},
    {texture_load_ctx1_cs, sizeof(texture_load_ctx1_cs), nullptr, 0},
    {texture_load_depth_unorm_cs, sizeof(texture_load_depth_unorm_cs),
     texture_load_depth_unorm_2x_cs, sizeof(texture_load_depth_unorm_2x_cs)},
    {texture_load_depth_float_cs, sizeof(texture_load_depth_float_cs),
     texture_load_depth_float_2x_cs, sizeof(texture_load_depth_float_2x_cs)},
};

const TextureCache::ResolveTileModeInfo
    TextureCache::resolve_tile_mode_info_[] = {
        {texture_tile_8bpp_cs, sizeof(texture_tile_8bpp_cs),
         DXGI_FORMAT_R8_UINT, 0},
        {texture_tile_16bpp_cs, sizeof(texture_tile_16bpp_cs),
         DXGI_FORMAT_R16_UINT, 1},
        {texture_tile_32bpp_cs, sizeof(texture_tile_32bpp_cs),
         DXGI_FORMAT_UNKNOWN, 0},
        {texture_tile_64bpp_cs, sizeof(texture_tile_64bpp_cs),
         DXGI_FORMAT_UNKNOWN, 0},
        {texture_tile_128bpp_cs, sizeof(texture_tile_128bpp_cs),
         DXGI_FORMAT_UNKNOWN, 0},
        {texture_tile_16bpp_rgba_cs, sizeof(texture_tile_16bpp_rgba_cs),
         DXGI_FORMAT_R16_UINT, 1},
        {texture_tile_r11g11b10_rgba16_cs,
         sizeof(texture_tile_r11g11b10_rgba16_cs), DXGI_FORMAT_UNKNOWN, 0},
        {texture_tile_r10g11b11_rgba16_cs,
         sizeof(texture_tile_r10g11b11_rgba16_cs), DXGI_FORMAT_UNKNOWN, 0},
};

TextureCache::TextureCache(D3D12CommandProcessor* command_processor,
                           RegisterFile* register_file,
                           SharedMemory* shared_memory)
    : command_processor_(command_processor),
      register_file_(register_file),
      shared_memory_(shared_memory) {}

TextureCache::~TextureCache() { Shutdown(); }

bool TextureCache::Initialize() {
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();

  // Try to create the tiled buffer 2x resolution scaling.
  // Not currently supported with the RTV/DSV output path for various reasons.
  // As of November 27th, 2018, PIX doesn't support tiled buffers.
  if (FLAGS_d3d12_resolution_scale >= 2 &&
      command_processor_->IsROVUsedForEDRAM() &&
      provider->GetTiledResourcesTier() >= 1 &&
      provider->GetGraphicsAnalysis() == nullptr &&
      provider->GetVirtualAddressBitsPerResource() >=
          kScaledResolveBufferSizeLog2) {
    D3D12_RESOURCE_DESC scaled_resolve_buffer_desc;
    ui::d3d12::util::FillBufferResourceDesc(
        scaled_resolve_buffer_desc, kScaledResolveBufferSize,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    scaled_resolve_buffer_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if (FAILED(device->CreateReservedResource(
            &scaled_resolve_buffer_desc, scaled_resolve_buffer_state_, nullptr,
            IID_PPV_ARGS(&scaled_resolve_buffer_)))) {
      XELOGE(
          "Texture cache: Failed to create the 2 GB tiled buffer for 2x "
          "resolution scale - switching to 1x");
    }
    const uint32_t scaled_resolve_page_dword_count =
        (512 * 1024 * 1024) / 4096 / 32;
    scaled_resolve_pages_ = new uint32_t[scaled_resolve_page_dword_count];
    std::memset(scaled_resolve_pages_, 0,
                scaled_resolve_page_dword_count * sizeof(uint32_t));
    std::memset(scaled_resolve_pages_l2_, 0, sizeof(scaled_resolve_pages_l2_));
  }
  std::memset(scaled_resolve_heaps_, 0, sizeof(scaled_resolve_heaps_));
  scaled_resolve_heap_count_ = 0;

  // Create the loading root signature.
  D3D12_ROOT_PARAMETER root_parameters[2];
  // Parameter 0 is constants (changed very often when untiling).
  root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_parameters[0].Descriptor.ShaderRegister = 0;
  root_parameters[0].Descriptor.RegisterSpace = 0;
  root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // Parameter 1 is source and target.
  D3D12_DESCRIPTOR_RANGE root_copy_ranges[2];
  root_copy_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  root_copy_ranges[0].NumDescriptors = 1;
  root_copy_ranges[0].BaseShaderRegister = 0;
  root_copy_ranges[0].RegisterSpace = 0;
  root_copy_ranges[0].OffsetInDescriptorsFromTableStart = 0;
  root_copy_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  root_copy_ranges[1].NumDescriptors = 1;
  root_copy_ranges[1].BaseShaderRegister = 0;
  root_copy_ranges[1].RegisterSpace = 0;
  root_copy_ranges[1].OffsetInDescriptorsFromTableStart = 1;
  root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[1].DescriptorTable.NumDescriptorRanges = 2;
  root_parameters[1].DescriptorTable.pDescriptorRanges = root_copy_ranges;
  root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.NumParameters = UINT(xe::countof(root_parameters));
  root_signature_desc.pParameters = root_parameters;
  root_signature_desc.NumStaticSamplers = 0;
  root_signature_desc.pStaticSamplers = nullptr;
  root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  load_root_signature_ =
      ui::d3d12::util::CreateRootSignature(provider, root_signature_desc);
  if (load_root_signature_ == nullptr) {
    XELOGE("Failed to create the texture loading root signature");
    Shutdown();
    return false;
  }
  // Create the tiling root signature (almost the same, but with root constants
  // in parameter 0).
  root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameters[0].Constants.ShaderRegister = 0;
  root_parameters[0].Constants.RegisterSpace = 0;
  root_parameters[0].Constants.Num32BitValues =
      sizeof(ResolveTileConstants) / sizeof(uint32_t);
  resolve_tile_root_signature_ =
      ui::d3d12::util::CreateRootSignature(provider, root_signature_desc);
  if (resolve_tile_root_signature_ == nullptr) {
    XELOGE("Failed to create the texture tiling root signature");
    Shutdown();
    return false;
  }

  // Create the loading and tiling pipelines.
  for (uint32_t i = 0; i < uint32_t(LoadMode::kCount); ++i) {
    const LoadModeInfo& mode_info = load_mode_info_[i];
    load_pipelines_[i] = ui::d3d12::util::CreateComputePipeline(
        device, mode_info.shader, mode_info.shader_size, load_root_signature_);
    if (load_pipelines_[i] == nullptr) {
      XELOGE("Failed to create the texture loading pipeline for mode %u", i);
      Shutdown();
      return false;
    }
    if (IsResolutionScale2X() && mode_info.shader_2x != nullptr) {
      load_pipelines_2x_[i] = ui::d3d12::util::CreateComputePipeline(
          device, mode_info.shader_2x, mode_info.shader_2x_size,
          load_root_signature_);
      if (load_pipelines_2x_[i] == nullptr) {
        XELOGE(
            "Failed to create the 2x-scaled texture loading pipeline for mode "
            "%u",
            i);
        Shutdown();
        return false;
      }
    }
  }
  for (uint32_t i = 0; i < uint32_t(ResolveTileMode::kCount); ++i) {
    const ResolveTileModeInfo& mode_info = resolve_tile_mode_info_[i];
    resolve_tile_pipelines_[i] = ui::d3d12::util::CreateComputePipeline(
        device, mode_info.shader, mode_info.shader_size,
        resolve_tile_root_signature_);
    if (resolve_tile_pipelines_[i] == nullptr) {
      XELOGE("Failed to create the texture tiling pipeline for mode %u", i);
      Shutdown();
      return false;
    }
  }

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
    XELOGE("Failed to create the descriptor heap for null SRVs");
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
      provider->OffsetViewDescriptor(
          null_srv_descriptor_heap_start_,
          uint32_t(NullSRVDescriptorIndex::k2DArray)));
  null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  null_srv_desc.Texture3D.MostDetailedMip = 0;
  null_srv_desc.Texture3D.MipLevels = 1;
  null_srv_desc.Texture3D.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(
      nullptr, &null_srv_desc,
      provider->OffsetViewDescriptor(null_srv_descriptor_heap_start_,
                                     uint32_t(NullSRVDescriptorIndex::k3D)));
  null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
  null_srv_desc.TextureCube.MostDetailedMip = 0;
  null_srv_desc.TextureCube.MipLevels = 1;
  null_srv_desc.TextureCube.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(
      nullptr, &null_srv_desc,
      provider->OffsetViewDescriptor(null_srv_descriptor_heap_start_,
                                     uint32_t(NullSRVDescriptorIndex::kCube)));

  if (IsResolutionScale2X()) {
    scaled_resolve_global_watch_handle_ = shared_memory_->RegisterGlobalWatch(
        ScaledResolveGlobalWatchCallbackThunk, this);
  }

  return true;
}

void TextureCache::Shutdown() {
  ClearCache();

  if (scaled_resolve_global_watch_handle_ != nullptr) {
    shared_memory_->UnregisterGlobalWatch(scaled_resolve_global_watch_handle_);
    scaled_resolve_global_watch_handle_ = nullptr;
  }

  ui::d3d12::util::ReleaseAndNull(null_srv_descriptor_heap_);

  for (uint32_t i = 0; i < uint32_t(ResolveTileMode::kCount); ++i) {
    ui::d3d12::util::ReleaseAndNull(resolve_tile_pipelines_[i]);
  }
  ui::d3d12::util::ReleaseAndNull(resolve_tile_root_signature_);
  for (uint32_t i = 0; i < uint32_t(LoadMode::kCount); ++i) {
    ui::d3d12::util::ReleaseAndNull(load_pipelines_2x_[i]);
    ui::d3d12::util::ReleaseAndNull(load_pipelines_[i]);
  }
  ui::d3d12::util::ReleaseAndNull(load_root_signature_);

  if (scaled_resolve_pages_ != nullptr) {
    delete[] scaled_resolve_pages_;
    scaled_resolve_pages_ = nullptr;
  }
  // First free the buffer to detach it from the heaps.
  ui::d3d12::util::ReleaseAndNull(scaled_resolve_buffer_);
  for (uint32_t i = 0; i < xe::countof(scaled_resolve_heaps_); ++i) {
    ui::d3d12::util::ReleaseAndNull(scaled_resolve_heaps_[i]);
  }
  scaled_resolve_heap_count_ = 0;
  COUNT_profile_set("gpu/texture_cache/scaled_resolve_buffer_mb_used", 0);
}

void TextureCache::ClearCache() {
  // Destroy all the textures.
  for (auto texture_pair : textures_) {
    Texture* texture = texture_pair.second;
    if (texture->resource != nullptr) {
      texture->resource->Release();
    }
    delete texture;
  }
  textures_.clear();
  COUNT_profile_set("gpu/texture_cache/textures", 0);

  // Clear texture descriptor cache.
  for (auto& page : srv_descriptor_cache_) {
    page.heap->Release();
  }
  srv_descriptor_cache_.clear();
}

void TextureCache::TextureFetchConstantWritten(uint32_t index) {
  texture_keys_in_sync_ &= ~(1u << index);
}

void TextureCache::BeginFrame() {
  // In case there was a failure creating something in the previous frame, make
  // sure bindings are reset so a new attempt will surely be made if the texture
  // is requested again.
  ClearBindings();

  std::memset(unsupported_format_features_used_, 0,
              sizeof(unsupported_format_features_used_));
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
    XELOGE("* %s%s%s%s", FormatInfo::Get(TextureFormat(i))->name,
           unsupported_features & kUnsupportedResourceBit ? " resource" : "",
           unsupported_features & kUnsupportedUnormBit ? " unorm" : "",
           unsupported_features & kUnsupportedSnormBit ? " snorm" : "");
    unsupported_format_features_used_[i] = 0;
  }
}

void TextureCache::RequestTextures(uint32_t used_vertex_texture_mask,
                                   uint32_t used_pixel_texture_mask) {
  auto& regs = *register_file_;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  if (texture_invalidated_.exchange(false, std::memory_order_acquire)) {
    // Clear the bindings not only for this draw call, but entirely, because
    // loading may be needed in some draw call later, which may have the same
    // key for some binding as before the invalidation, but texture_invalidated_
    // being false (menu background in Halo 3).
    std::memset(texture_bindings_, 0, sizeof(texture_bindings_));
    texture_keys_in_sync_ = 0;
  }

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
    bool old_has_unsigned = binding.has_unsigned;
    bool old_has_signed = binding.has_signed;
    BindingInfoFromFetchConstant(group->texture_fetch, binding.key,
                                 &binding.swizzle, &binding.has_unsigned,
                                 &binding.has_signed);
    texture_keys_in_sync_ |= index_bit;
    if (binding.key.IsInvalid()) {
      binding.texture = nullptr;
      binding.texture_signed = nullptr;
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
      // respectively (checking the previous values of has_unsigned/has_signed
      // rather than binding.texture != nullptr and binding.texture_signed !=
      // nullptr also prevents repeated attempts to load the texture if it has
      // failed to load).
      if (binding.has_unsigned) {
        if (key_changed || !old_has_unsigned) {
          binding.texture = FindOrCreateTexture(binding.key);
          load_unsigned_data = true;
        }
      } else {
        binding.texture = nullptr;
      }
      if (binding.has_signed) {
        if (key_changed || !old_has_signed) {
          TextureKey signed_key = binding.key;
          signed_key.signed_separate = 1;
          binding.texture_signed = FindOrCreateTexture(signed_key);
          load_signed_data = true;
        }
      } else {
        binding.texture_signed = nullptr;
      }
    } else {
      if (key_changed) {
        binding.texture = FindOrCreateTexture(binding.key);
        load_unsigned_data = true;
      }
      binding.texture_signed = nullptr;
    }
    if (load_unsigned_data && binding.texture != nullptr) {
      LoadTextureData(binding.texture);
    }
    if (load_signed_data && binding.texture_signed != nullptr) {
      LoadTextureData(binding.texture_signed);
    }
  }

  // Transition the textures to the needed usage.
  used_texture_mask = used_vertex_texture_mask | used_pixel_texture_mask;
  while (xe::bit_scan_forward(used_texture_mask, &index)) {
    uint32_t index_bit = 1u << index;
    used_texture_mask &= ~index_bit;

    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATES(0);
    if (used_vertex_texture_mask & index_bit) {
      state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    if (used_pixel_texture_mask & index_bit) {
      state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    TextureBinding& binding = texture_bindings_[index];
    if (binding.texture != nullptr) {
      command_processor_->PushTransitionBarrier(binding.texture->resource,
                                                binding.texture->state, state);
      binding.texture->state = state;
    }
    if (binding.texture_signed != nullptr) {
      command_processor_->PushTransitionBarrier(
          binding.texture_signed->resource, binding.texture_signed->state,
          state);
      binding.texture_signed->state = state;
    }
  }
}

uint64_t TextureCache::GetDescriptorHashForActiveTextures(
    const D3D12Shader::TextureSRV* texture_srvs,
    uint32_t texture_srv_count) const {
  XXH64_state_t hash_state;
  XXH64_reset(&hash_state, 0);
  for (uint32_t i = 0; i < texture_srv_count; ++i) {
    const D3D12Shader::TextureSRV& texture_srv = texture_srvs[i];
    // There can be multiple SRVs of the same texture.
    XXH64_update(&hash_state, &texture_srv.dimension,
                 sizeof(texture_srv.dimension));
    XXH64_update(&hash_state, &texture_srv.is_signed,
                 sizeof(texture_srv.is_signed));
    XXH64_update(&hash_state, &texture_srv.is_sign_required,
                 sizeof(texture_srv.is_sign_required));
    const TextureBinding& binding =
        texture_bindings_[texture_srv.fetch_constant];
    XXH64_update(&hash_state, &binding.key, sizeof(binding.key));
    XXH64_update(&hash_state, &binding.swizzle, sizeof(binding.swizzle));
    XXH64_update(&hash_state, &binding.has_unsigned,
                 sizeof(binding.has_unsigned));
    XXH64_update(&hash_state, &binding.has_signed, sizeof(binding.has_signed));
  }
  return XXH64_digest(&hash_state);
}

void TextureCache::WriteTextureSRV(const D3D12Shader::TextureSRV& texture_srv,
                                   D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  Dimension binding_dimension;
  uint32_t mip_max_level, array_size;
  Texture* texture = nullptr;
  ID3D12Resource* resource = nullptr;

  const TextureBinding& binding = texture_bindings_[texture_srv.fetch_constant];
  if (!binding.key.IsInvalid()) {
    TextureFormat format = binding.key.format;

    if (IsSignedVersionSeparate(format) && texture_srv.is_signed) {
      texture = binding.texture_signed;
    } else {
      texture = binding.texture;
    }
    if (texture != nullptr) {
      resource = texture->resource;
    }

    if (texture_srv.is_signed) {
      // Not supporting signed compressed textures - hopefully DXN and DXT5A are
      // not used as signed.
      if (binding.has_signed || texture_srv.is_sign_required) {
        desc.Format = host_formats_[uint32_t(format)].dxgi_format_snorm;
        if (desc.Format == DXGI_FORMAT_UNKNOWN) {
          unsupported_format_features_used_[uint32_t(format)] |=
              kUnsupportedSnormBit;
        }
      }
    } else {
      if (binding.has_unsigned || texture_srv.is_sign_required) {
        desc.Format = GetDXGIUnormFormat(binding.key);
        if (desc.Format == DXGI_FORMAT_UNKNOWN) {
          unsupported_format_features_used_[uint32_t(format)] |=
              kUnsupportedUnormBit;
        }
      }
    }

    binding_dimension = binding.key.dimension;
    mip_max_level = binding.key.mip_max_level;
    array_size = binding.key.depth;
    // XE_GPU_SWIZZLE and D3D12_SHADER_COMPONENT_MAPPING are the same except for
    // one bit.
    desc.Shader4ComponentMapping =
        binding.swizzle |
        D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES;
  } else {
    binding_dimension = Dimension::k2D;
    mip_max_level = 0;
    array_size = 1;
    desc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
        D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
        D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
        D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
        D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0);
  }

  if (desc.Format == DXGI_FORMAT_UNKNOWN) {
    // A null descriptor must still have a valid format.
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource = nullptr;
  }
  NullSRVDescriptorIndex null_descriptor_index;
  switch (texture_srv.dimension) {
    case TextureDimension::k3D:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      desc.Texture3D.MostDetailedMip = 0;
      desc.Texture3D.MipLevels = mip_max_level + 1;
      desc.Texture3D.ResourceMinLODClamp = 0.0f;
      if (binding_dimension != Dimension::k3D) {
        // Create a null descriptor so it's safe to sample this texture even
        // though it has different dimensions.
        resource = nullptr;
      }
      null_descriptor_index = NullSRVDescriptorIndex::k3D;
      break;
    case TextureDimension::kCube:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      desc.TextureCube.MostDetailedMip = 0;
      desc.TextureCube.MipLevels = mip_max_level + 1;
      desc.TextureCube.ResourceMinLODClamp = 0.0f;
      if (binding_dimension != Dimension::kCube) {
        resource = nullptr;
      }
      null_descriptor_index = NullSRVDescriptorIndex::kCube;
      break;
    default:
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      desc.Texture2DArray.MostDetailedMip = 0;
      desc.Texture2DArray.MipLevels = mip_max_level + 1;
      desc.Texture2DArray.FirstArraySlice = 0;
      desc.Texture2DArray.ArraySize = array_size;
      desc.Texture2DArray.PlaneSlice = 0;
      desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
      if (binding_dimension == Dimension::k3D ||
          binding_dimension == Dimension::kCube) {
        resource = nullptr;
      }
      null_descriptor_index = NullSRVDescriptorIndex::k2DArray;
      break;
  }

  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  if (resource == nullptr) {
    // Copy a pre-made null descriptor since it's faster than to create an SRV.
    device->CopyDescriptorsSimple(
        1, handle,
        provider->OffsetViewDescriptor(null_srv_descriptor_heap_start_,
                                       uint32_t(null_descriptor_index)),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return;
  }
  // Take the descriptor from the cache if it's cached, or create a new one in
  // the cache, or directly if this texture was already used with a different
  // swizzle. Profiling results say that CreateShaderResourceView takes the
  // longest time of draw call processing, and it's very noticeable in many
  // games.
  D3D12_CPU_DESCRIPTOR_HANDLE cached_handle = {};
  assert_not_null(texture);
  if (texture->cached_srv_descriptor.ptr) {
    // Use an existing cached descriptor if it has the needed swizzle.
    if (binding.swizzle == texture->cached_srv_descriptor_swizzle) {
      cached_handle = texture->cached_srv_descriptor;
    }
  } else {
    // Try to create a new cached descriptor if it doesn't exist yet.
    if (srv_descriptor_cache_.empty() ||
        srv_descriptor_cache_.back().current_usage >=
            SRVDescriptorCachePage::kHeapSize) {
      D3D12_DESCRIPTOR_HEAP_DESC new_heap_desc;
      new_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      new_heap_desc.NumDescriptors = SRVDescriptorCachePage::kHeapSize;
      new_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
      new_heap_desc.NodeMask = 0;
      ID3D12DescriptorHeap* new_heap;
      if (SUCCEEDED(device->CreateDescriptorHeap(&new_heap_desc,
                                                 IID_PPV_ARGS(&new_heap)))) {
        SRVDescriptorCachePage new_page;
        new_page.heap = new_heap;
        new_page.heap_start = new_heap->GetCPUDescriptorHandleForHeapStart();
        new_page.current_usage = 1;
        cached_handle = new_page.heap_start;
        srv_descriptor_cache_.push_back(new_page);
      }
    } else {
      SRVDescriptorCachePage& page = srv_descriptor_cache_.back();
      cached_handle =
          provider->OffsetViewDescriptor(page.heap_start, page.current_usage);
      ++page.current_usage;
    }
    if (cached_handle.ptr) {
      device->CreateShaderResourceView(resource, &desc, cached_handle);
      texture->cached_srv_descriptor = cached_handle;
      texture->cached_srv_descriptor_swizzle = binding.swizzle;
    }
  }
  if (cached_handle.ptr) {
    device->CopyDescriptorsSimple(1, handle, cached_handle,
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  } else {
    device->CreateShaderResourceView(resource, &desc, handle);
  }
}

TextureCache::SamplerParameters TextureCache::GetSamplerParameters(
    const D3D12Shader::SamplerBinding& binding) const {
  auto& regs = *register_file_;
  uint32_t r =
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + binding.fetch_constant * 6;
  auto group =
      reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(&regs.values[r]);
  auto& fetch = group->texture_fetch;

  SamplerParameters parameters;

  parameters.clamp_x = ClampMode(fetch.clamp_x);
  parameters.clamp_y = ClampMode(fetch.clamp_y);
  parameters.clamp_z = ClampMode(fetch.clamp_z);
  parameters.border_color = BorderColor(fetch.border_color);

  uint32_t mip_min_level = fetch.mip_min_level;
  uint32_t mip_max_level = fetch.mip_max_level;
  uint32_t base_page = fetch.base_address & 0x1FFFF;
  uint32_t mip_page = fetch.mip_address & 0x1FFFF;
  if (base_page == 0 || base_page == mip_page) {
    // Games should clamp mip level in this case anyway, but just for safety.
    mip_min_level = std::max(mip_min_level, 1u);
  }
  if (mip_page == 0) {
    mip_max_level = 0;
  }
  parameters.mip_min_level = mip_min_level;
  parameters.mip_max_level = std::max(mip_max_level, mip_min_level);
  parameters.lod_bias = fetch.lod_bias;

  AnisoFilter aniso_filter = binding.aniso_filter == AnisoFilter::kUseFetchConst
                                 ? AnisoFilter(fetch.aniso_filter)
                                 : binding.aniso_filter;
  aniso_filter = std::min(aniso_filter, AnisoFilter::kMax_16_1);
  parameters.aniso_filter = aniso_filter;
  if (aniso_filter != AnisoFilter::kDisabled) {
    parameters.mag_linear = 1;
    parameters.min_linear = 1;
    parameters.mip_linear = 1;
  } else {
    TextureFilter mag_filter =
        binding.mag_filter == TextureFilter::kUseFetchConst
            ? TextureFilter(fetch.mag_filter)
            : binding.mag_filter;
    parameters.mag_linear = mag_filter == TextureFilter::kLinear;
    TextureFilter min_filter =
        binding.min_filter == TextureFilter::kUseFetchConst
            ? TextureFilter(fetch.min_filter)
            : binding.min_filter;
    parameters.min_linear = min_filter == TextureFilter::kLinear;
    TextureFilter mip_filter =
        binding.mip_filter == TextureFilter::kUseFetchConst
            ? TextureFilter(fetch.mip_filter)
            : binding.mip_filter;
    parameters.mip_linear = mip_filter == TextureFilter::kLinear;
    // TODO(Triang3l): Investigate mip_filter TextureFilter::kBaseMap.
  }

  return parameters;
}

void TextureCache::WriteSampler(SamplerParameters parameters,
                                D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
  D3D12_SAMPLER_DESC desc;
  if (parameters.aniso_filter != AnisoFilter::kDisabled) {
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
    // TODO(Triang3l): Investigate mip_filter TextureFilter::kBaseMap.
    desc.Filter = D3D12_ENCODE_BASIC_FILTER(
        d3d_filter_min, d3d_filter_mag, d3d_filter_mip,
        D3D12_FILTER_REDUCTION_TYPE_STANDARD);
    desc.MaxAnisotropy = 1;
  }
  // FIXME(Triang3l): Halfway and mirror clamp to border aren't mapped properly.
  static const D3D12_TEXTURE_ADDRESS_MODE kAddressModeMap[] = {
      /* kRepeat               */ D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      /* kMirroredRepeat       */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
      /* kClampToEdge          */ D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      /* kMirrorClampToEdge    */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
      /* kClampToHalfway       */ D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      /* kMirrorClampToHalfway */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
      /* kClampToBorder        */ D3D12_TEXTURE_ADDRESS_MODE_BORDER,
      /* kMirrorClampToBorder  */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
  };
  desc.AddressU = kAddressModeMap[uint32_t(parameters.clamp_x)];
  desc.AddressV = kAddressModeMap[uint32_t(parameters.clamp_y)];
  desc.AddressW = kAddressModeMap[uint32_t(parameters.clamp_z)];
  desc.MipLODBias = parameters.lod_bias * (1.0f / 32.0f);
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  // TODO(Triang3l): Border colors k_ACBYCR_BLACK and k_ACBCRY_BLACK.
  if (parameters.border_color == BorderColor::k_AGBR_White) {
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
  desc.MaxLOD = float(parameters.mip_max_level);
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  device->CreateSampler(&desc, handle);
}

void TextureCache::MarkRangeAsResolved(uint32_t start_unscaled,
                                       uint32_t length_unscaled) {
  if (length_unscaled == 0) {
    return;
  }
  start_unscaled &= 0x1FFFFFFF;
  length_unscaled = std::min(length_unscaled, 0x20000000 - start_unscaled);

  if (IsResolutionScale2X()) {
    uint32_t page_first = start_unscaled >> 12;
    uint32_t page_last = (start_unscaled + length_unscaled - 1) >> 12;
    uint32_t block_first = page_first >> 5;
    uint32_t block_last = page_last >> 5;
    shared_memory_->LockWatchMutex();
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
    shared_memory_->UnlockWatchMutex();
  }

  // Invalidate textures. Toggling individual textures between scaled and
  // unscaled also relies on invalidation through shared memory.
  shared_memory_->RangeWrittenByGPU(start_unscaled, length_unscaled);
}

bool TextureCache::TileResolvedTexture(
    TextureFormat format, uint32_t texture_base, uint32_t texture_pitch,
    uint32_t offset_x, uint32_t offset_y, uint32_t resolve_width,
    uint32_t resolve_height, Endian128 endian, ID3D12Resource* buffer,
    uint32_t buffer_size, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint) {
  ResolveTileMode resolve_tile_mode =
      host_formats_[uint32_t(format)].resolve_tile_mode;
  if (resolve_tile_mode == ResolveTileMode::kUnknown) {
    assert_always();
    return false;
  }
  const ResolveTileModeInfo& resolve_tile_mode_info =
      resolve_tile_mode_info_[uint32_t(resolve_tile_mode)];

  auto command_list = command_processor_->GetDeferredCommandList();
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  uint32_t resolution_scale_log2 = IsResolutionScale2X() ? 1 : 0;

  texture_base &= 0x1FFFFFFF;
  if (resolve_tile_mode_info.typed_uav_format == DXGI_FORMAT_UNKNOWN) {
    assert_false(texture_base & (sizeof(uint32_t) - 1));
    texture_base &= ~(uint32_t(sizeof(uint32_t) - 1));
  } else {
    assert_false(texture_base &
                 ((1u << resolve_tile_mode_info.uav_texel_size_log2) - 1));
    texture_base &= ~((1u << resolve_tile_mode_info.uav_texel_size_log2) - 1);
  }

  assert_false(texture_pitch & 31);
  texture_pitch = xe::align(texture_pitch, 32u);

  // Calculate the address and the size of the region that specifically
  // is being resolved. Can't just use the texture height for size calculation
  // because it's sometimes bigger than needed (in Red Dead Redemption, an UI
  // texture used for the letterbox bars alpha is located within a 1280x720
  // resolve target, but only 1280x208 is being resolved, and with scaled
  // resolution the UI texture gets ignored).
  texture_base += texture_util::GetTiledOffset2D(
      offset_x & ~31u, offset_y & ~31u, texture_pitch,
      xe::log2_floor(FormatInfo::Get(format)->bits_per_pixel >> 3));
  offset_x &= 31;
  offset_y &= 31;
  uint32_t texture_size = texture_util::GetGuestMipSliceStorageSize(
      texture_pitch, xe::align(offset_y + resolve_height, 32u), 1, true, format,
      nullptr, false);
  if (texture_size == 0) {
    return true;
  }
  if (resolution_scale_log2) {
    if (!EnsureScaledResolveBufferResident(texture_base, texture_size)) {
      return false;
    }
  } else {
    if (!shared_memory_->MakeTilesResident(texture_base, texture_size)) {
      return false;
    }
  }

  // Tile the texture.
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
  D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
  if (command_processor_->RequestViewDescriptors(0, 2, 2, descriptor_cpu_start,
                                                 descriptor_gpu_start) == 0) {
    return false;
  }
  if (resolution_scale_log2) {
    UseScaledResolveBufferForWriting();
  } else {
    shared_memory_->UseForWriting();
  }
  command_processor_->SubmitBarriers();
  command_list->D3DSetComputeRootSignature(resolve_tile_root_signature_);
  ResolveTileConstants resolve_tile_constants;
  resolve_tile_constants.info = uint32_t(endian) | (uint32_t(format) << 3) |
                                (resolution_scale_log2 << 9) |
                                (texture_pitch << 10);
  resolve_tile_constants.offset = offset_x | (offset_y << 16);
  resolve_tile_constants.size = resolve_width | (resolve_height << 16);
  resolve_tile_constants.host_base = uint32_t(footprint.Offset);
  resolve_tile_constants.host_pitch = uint32_t(footprint.Footprint.RowPitch);
  ui::d3d12::util::CreateRawBufferSRV(device, descriptor_cpu_start, buffer,
                                      buffer_size);
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_uav =
      provider->OffsetViewDescriptor(descriptor_cpu_start, 1);
  if (resolve_tile_mode_info.typed_uav_format != DXGI_FORMAT_UNKNOWN) {
    // Not sure if this alignment is actually needed in Direct3D 12, but for
    // safety. Also not using the full 512 MB buffer as a typed UAV because
    // there can't be more than 128M texels in one
    // (D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP).
    resolve_tile_constants.guest_base =
        (texture_base & 0xFFFu) >> resolve_tile_mode_info.uav_texel_size_log2;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.Format = resolve_tile_mode_info.typed_uav_format;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement =
        (texture_base & ~0xFFFu) >> resolve_tile_mode_info.uav_texel_size_log2
                                        << (resolution_scale_log2 * 2);
    uav_desc.Buffer.NumElements =
        xe::align(texture_size + (texture_base & 0xFFFu), 0x1000u) >>
        resolve_tile_mode_info.uav_texel_size_log2
            << (resolution_scale_log2 * 2);
    uav_desc.Buffer.StructureByteStride = 0;
    uav_desc.Buffer.CounterOffsetInBytes = 0;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(resolution_scale_log2
                                          ? scaled_resolve_buffer_
                                          : shared_memory_->GetBuffer(),
                                      nullptr, &uav_desc, descriptor_cpu_uav);
  } else {
    if (resolution_scale_log2) {
      resolve_tile_constants.guest_base = texture_base & 0xFFF;
      CreateScaledResolveBufferRawUAV(
          descriptor_cpu_uav, texture_base >> 12,
          ((texture_base + texture_size - 1) >> 12) - (texture_base >> 12) + 1);
    } else {
      resolve_tile_constants.guest_base = texture_base;
      shared_memory_->WriteRawUAVDescriptor(descriptor_cpu_uav);
    }
  }
  command_list->D3DSetComputeRootDescriptorTable(1, descriptor_gpu_start);
  command_list->D3DSetComputeRoot32BitConstants(
      0, sizeof(resolve_tile_constants) / sizeof(uint32_t),
      &resolve_tile_constants, 0);
  command_processor_->SetComputePipeline(
      resolve_tile_pipelines_[uint32_t(resolve_tile_mode)]);
  // Each group processes 32x32 texels after resolution scaling has been
  // applied.
  command_list->D3DDispatch(
      ((resolve_width << resolution_scale_log2) + 31) >> 5,
      ((resolve_height << resolution_scale_log2) + 31) >> 5, 1);

  // Commit the write.
  command_processor_->PushUAVBarrier(resolution_scale_log2
                                         ? scaled_resolve_buffer_
                                         : shared_memory_->GetBuffer());

  // Invalidate textures and mark the range as scaled if needed.
  MarkRangeAsResolved(texture_base, texture_size);

  return true;
}

bool TextureCache::EnsureScaledResolveBufferResident(uint32_t start_unscaled,
                                                     uint32_t length_unscaled) {
  assert_true(IsResolutionScale2X());

  if (length_unscaled == 0) {
    return true;
  }
  start_unscaled &= 0x1FFFFFFF;
  if ((0x20000000 - start_unscaled) < length_unscaled) {
    // Exceeds the physical address space.
    return false;
  }

  uint32_t heap_first = (start_unscaled << 2) >> kScaledResolveHeapSizeLog2;
  uint32_t heap_last = ((start_unscaled + length_unscaled - 1) << 2) >>
                       kScaledResolveHeapSizeLog2;
  for (uint32_t i = heap_first; i <= heap_last; ++i) {
    if (scaled_resolve_heaps_[i] != nullptr) {
      continue;
    }
    auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
    auto device = provider->GetDevice();
    auto direct_queue = provider->GetDirectQueue();
    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = kScaledResolveHeapSize;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    if (FAILED(device->CreateHeap(&heap_desc,
                                  IID_PPV_ARGS(&scaled_resolve_heaps_[i])))) {
      XELOGE("Texture cache: Failed to create a scaled resolve tile heap");
      return false;
    }
    ++scaled_resolve_heap_count_;
    COUNT_profile_set(
        "gpu/texture_cache/scaled_resolve_buffer_mb_used",
        scaled_resolve_heap_count_ << (kScaledResolveHeapSizeLog2 - 20));
    D3D12_TILED_RESOURCE_COORDINATE region_start_coordinates;
    region_start_coordinates.X = (i << kScaledResolveHeapSizeLog2) /
                                 D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
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
    // FIXME(Triang3l): This may cause issues if the emulator is shut down
    // mid-frame and the heaps are destroyed before tile mappings are updated
    // (AwaitAllFramesCompletion won't catch this then). Defer this until the
    // actual command list submission at the end of the frame.
    direct_queue->UpdateTileMappings(
        scaled_resolve_buffer_, 1, &region_start_coordinates, &region_size,
        scaled_resolve_heaps_[i], 1, &range_flags, &heap_range_start_offset,
        &range_tile_count, D3D12_TILE_MAPPING_FLAG_NONE);
  }
  return true;
}

void TextureCache::UseScaledResolveBufferForReading() {
  assert_true(IsResolutionScale2X());
  command_processor_->PushTransitionBarrier(
      scaled_resolve_buffer_, scaled_resolve_buffer_state_,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  scaled_resolve_buffer_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

void TextureCache::UseScaledResolveBufferForWriting() {
  assert_true(IsResolutionScale2X());
  command_processor_->PushTransitionBarrier(
      scaled_resolve_buffer_, scaled_resolve_buffer_state_,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  scaled_resolve_buffer_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

void TextureCache::CreateScaledResolveBufferRawSRV(
    D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t first_unscaled_4kb_page,
    uint32_t unscaled_4kb_page_count) {
  assert_true(IsResolutionScale2X());
  first_unscaled_4kb_page = std::min(first_unscaled_4kb_page, 0x1FFFFu);
  unscaled_4kb_page_count = std::max(
      std::min(unscaled_4kb_page_count, 0x20000u - first_unscaled_4kb_page),
      1u);
  ui::d3d12::util::CreateRawBufferSRV(
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice(),
      handle, scaled_resolve_buffer_, unscaled_4kb_page_count << 14,
      first_unscaled_4kb_page << 14);
}

void TextureCache::CreateScaledResolveBufferRawUAV(
    D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t first_unscaled_4kb_page,
    uint32_t unscaled_4kb_page_count) {
  assert_true(IsResolutionScale2X());
  first_unscaled_4kb_page = std::min(first_unscaled_4kb_page, 0x1FFFFu);
  unscaled_4kb_page_count = std::max(
      std::min(unscaled_4kb_page_count, 0x20000u - first_unscaled_4kb_page),
      1u);
  ui::d3d12::util::CreateRawBufferUAV(
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice(),
      handle, scaled_resolve_buffer_, unscaled_4kb_page_count << 14,
      first_unscaled_4kb_page << 14);
}

bool TextureCache::RequestSwapTexture(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                      TextureFormat& format_out) {
  auto group = reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(
      &register_file_->values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0]);
  auto& fetch = group->texture_fetch;
  TextureKey key;
  uint32_t swizzle;
  BindingInfoFromFetchConstant(group->texture_fetch, key, &swizzle, nullptr,
                               nullptr);
  if (key.base_page == 0 || key.dimension != Dimension::k2D) {
    return false;
  }
  Texture* texture = FindOrCreateTexture(key);
  if (texture == nullptr || !LoadTextureData(texture)) {
    return false;
  }
  command_processor_->PushTransitionBarrier(
      texture->resource, texture->state,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  texture->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
  srv_desc.Format = GetDXGIUnormFormat(key);
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Shader4ComponentMapping =
      swizzle |
      D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES;
  srv_desc.Texture2D.MostDetailedMip = 0;
  srv_desc.Texture2D.MipLevels = 1;
  srv_desc.Texture2D.PlaneSlice = 0;
  srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
  auto device =
      command_processor_->GetD3D12Context()->GetD3D12Provider()->GetDevice();
  device->CreateShaderResourceView(texture->resource, &srv_desc, handle);
  format_out = key.format;
  return true;
}

bool TextureCache::IsDecompressionNeeded(TextureFormat format, uint32_t width,
                                         uint32_t height) {
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
    uint32_t* swizzle_out, bool* has_unsigned_out, bool* has_signed_out) {
  // Reset the key and the swizzle.
  key_out.MakeInvalid();
  if (swizzle_out != nullptr) {
    *swizzle_out = xenos::XE_GPU_SWIZZLE_0 | (xenos::XE_GPU_SWIZZLE_0 << 3) |
                   (xenos::XE_GPU_SWIZZLE_0 << 6) |
                   (xenos::XE_GPU_SWIZZLE_0 << 9);
  }
  if (has_unsigned_out != nullptr) {
    *has_unsigned_out = false;
  }
  if (has_signed_out != nullptr) {
    *has_signed_out = false;
  }

  if (fetch.type != 2) {
    XELOGW(
        "Texture fetch type is not 2 - ignoring (fetch constant is %.8X %.8X "
        "%.8X %.8X %.8X %.8X)!",
        fetch.dword_0, fetch.dword_1, fetch.dword_2, fetch.dword_3,
        fetch.dword_4, fetch.dword_5);
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

  TextureFormat format = GetBaseFormat(TextureFormat(fetch.format));

  key_out.base_page = base_page;
  key_out.mip_page = mip_page;
  key_out.dimension = dimension;
  key_out.width = width;
  key_out.height = height;
  key_out.depth = depth;
  key_out.mip_max_level = mip_max_level;
  key_out.tiled = fetch.tiled;
  key_out.packed_mips = fetch.packed_mips;
  key_out.format = format;
  key_out.endianness = Endian(fetch.endianness);

  if (swizzle_out != nullptr) {
    uint32_t swizzle = fetch.swizzle;
    const uint32_t swizzle_constant_mask = 4 | (4 << 3) | (4 << 6) | (4 << 9);
    uint32_t swizzle_constant = swizzle & swizzle_constant_mask;
    uint32_t swizzle_not_constant = swizzle_constant ^ swizzle_constant_mask;
    // Get rid of 6 and 7 values (to prevent device losses if the game has
    // something broken) the quick and dirty way - by changing them to 4 and 5.
    swizzle &= ~(swizzle_constant >> 1);
    // Remap the swizzle according to the texture format. k_1_5_5_5, k_5_6_5 and
    // k_4_4_4_4 already have red and blue swapped in the load shader for
    // simplicity.
    if (format == TextureFormat::k_6_5_5) {
      // Green bits of the texture used for blue, and blue bits used for green.
      // Swap 001 and 010 (XOR 011 if either 001 or 010).
      uint32_t swizzle_green_or_blue =
          ((swizzle & 0b001001001001) ^ ((swizzle >> 1) & 0b001001001001)) &
          swizzle_not_constant;
      swizzle ^= swizzle_green_or_blue | (swizzle_green_or_blue << 1);
    } else if (host_formats_[uint32_t(format)].replicate_component) {
      // Replicate the only component of single-component textures, which are
      // emulated with red formats (including DXT3A, which uses R8 rather than
      // DXT3 because the resulting size is the same, but there's no 4x4
      // alignment requirement). If not 0.0 or 1.0 (if the high bit isn't set),
      // make 0 (red).
      swizzle &= ~((swizzle_not_constant >> 1) | (swizzle_not_constant >> 2));
    }
    *swizzle_out = swizzle;
  }

  if (has_unsigned_out != nullptr) {
    *has_unsigned_out = TextureSign(fetch.sign_x) != TextureSign::kSigned ||
                        TextureSign(fetch.sign_y) != TextureSign::kSigned ||
                        TextureSign(fetch.sign_z) != TextureSign::kSigned ||
                        TextureSign(fetch.sign_w) != TextureSign::kSigned;
  }
  if (has_signed_out != nullptr) {
    *has_signed_out = TextureSign(fetch.sign_x) == TextureSign::kSigned ||
                      TextureSign(fetch.sign_y) == TextureSign::kSigned ||
                      TextureSign(fetch.sign_z) == TextureSign::kSigned ||
                      TextureSign(fetch.sign_w) == TextureSign::kSigned;
  }
}

void TextureCache::LogTextureKeyAction(TextureKey key, const char* action) {
  XELOGGPU(
      "%s %s %s%ux%ux%u %s %s texture with %u %spacked mip level%s, "
      "base at 0x%.8X, mips at 0x%.8X",
      action, key.tiled ? "tiled" : "linear",
      key.scaled_resolve ? "2x-scaled " : "", key.width, key.height, key.depth,
      dimension_names_[uint32_t(key.dimension)],
      FormatInfo::Get(key.format)->name, key.mip_max_level + 1,
      key.packed_mips ? "" : "un", key.mip_max_level != 0 ? "s" : "",
      key.base_page << 12, key.mip_page << 12);
}

void TextureCache::LogTextureAction(const Texture* texture,
                                    const char* action) {
  XELOGGPU(
      "%s %s %s%ux%ux%u %s %s texture with %u %spacked mip level%s, "
      "base at 0x%.8X (size %u), mips at 0x%.8X (size %u)",
      action, texture->key.tiled ? "tiled" : "linear",
      texture->key.scaled_resolve ? "2x-scaled " : "", texture->key.width,
      texture->key.height, texture->key.depth,
      dimension_names_[uint32_t(texture->key.dimension)],
      FormatInfo::Get(texture->key.format)->name,
      texture->key.mip_max_level + 1, texture->key.packed_mips ? "" : "un",
      texture->key.mip_max_level != 0 ? "s" : "", texture->key.base_page << 12,
      texture->base_size, texture->key.mip_page << 12, texture->mip_size);
}

TextureCache::Texture* TextureCache::FindOrCreateTexture(TextureKey key) {
  // Check if the texture is a 2x-scaled resolve texture.
  if (IsResolutionScale2X() && key.tiled) {
    LoadMode load_mode = GetLoadMode(key);
    if (load_mode != LoadMode::kUnknown &&
        load_pipelines_2x_[uint32_t(load_mode)] != nullptr) {
      uint32_t base_size = 0, mip_size = 0;
      texture_util::GetTextureTotalSize(
          key.dimension, key.width, key.height, key.depth, key.format,
          key.tiled, key.packed_mips, key.mip_max_level,
          key.base_page != 0 ? &base_size : nullptr,
          key.mip_page != 0 ? &mip_size : nullptr);
      if ((base_size != 0 &&
           IsRangeScaledResolved(key.base_page << 12, base_size)) ||
          (mip_size != 0 &&
           IsRangeScaledResolved(key.mip_page << 12, mip_size))) {
        key.scaled_resolve = 1;
      }
    }
  }

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
  desc.Format = GetDXGIResourceFormat(key);
  if (desc.Format == DXGI_FORMAT_UNKNOWN) {
    unsupported_format_features_used_[uint32_t(key.format)] |=
        kUnsupportedResourceBit;
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
  if (key.scaled_resolve) {
    desc.Width *= 2;
    desc.Height *= 2;
  }
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
  // Assuming untiling will be the next operation.
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
  ID3D12Resource* resource;
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE, &desc,
          state, nullptr, IID_PPV_ARGS(&resource)))) {
    LogTextureKeyAction(key, "Failed to create");
    return nullptr;
  }

  // Create the texture object and add it to the map.
  Texture* texture = new Texture;
  texture->key = key;
  texture->resource = resource;
  texture->state = state;
  texture->mip_offsets[0] = 0;
  uint32_t width_blocks, height_blocks, depth_blocks;
  uint32_t array_size = key.dimension != Dimension::k3D ? key.depth : 1;
  if (key.base_page != 0) {
    texture_util::GetGuestMipBlocks(key.dimension, key.width, key.height,
                                    key.depth, key.format, 0, width_blocks,
                                    height_blocks, depth_blocks);
    uint32_t slice_size = texture_util::GetGuestMipSliceStorageSize(
        width_blocks, height_blocks, depth_blocks, key.tiled, key.format,
        &texture->pitches[0]);
    texture->slice_sizes[0] = slice_size;
    texture->base_size = slice_size * array_size;
    texture->base_in_sync = false;
  } else {
    texture->base_size = 0;
    texture->slice_sizes[0] = 0;
    texture->pitches[0] = 0;
    // Never try to upload the base level if there is none.
    texture->base_in_sync = true;
  }
  texture->mip_size = 0;
  if (key.mip_page != 0) {
    uint32_t mip_max_storage_level = key.mip_max_level;
    if (key.packed_mips) {
      mip_max_storage_level =
          std::min(mip_max_storage_level,
                   texture_util::GetPackedMipLevel(key.width, key.height));
    }
    for (uint32_t i = 1; i <= mip_max_storage_level; ++i) {
      texture_util::GetGuestMipBlocks(key.dimension, key.width, key.height,
                                      key.depth, key.format, i, width_blocks,
                                      height_blocks, depth_blocks);
      texture->mip_offsets[i] = texture->mip_size;
      uint32_t slice_size = texture_util::GetGuestMipSliceStorageSize(
          width_blocks, height_blocks, depth_blocks, key.tiled, key.format,
          &texture->pitches[i]);
      texture->slice_sizes[i] = slice_size;
      texture->mip_size += slice_size * array_size;
    }
    // The rest are either packed levels or don't exist at all.
    for (uint32_t i = mip_max_storage_level + 1;
         i < xe::countof(texture->mip_offsets); ++i) {
      texture->mip_offsets[i] = texture->mip_offsets[mip_max_storage_level];
      texture->slice_sizes[i] = texture->slice_sizes[mip_max_storage_level];
      texture->pitches[i] = texture->pitches[mip_max_storage_level];
    }
    texture->mips_in_sync = false;
  } else {
    std::memset(&texture->mip_offsets[1], 0,
                (xe::countof(texture->mip_offsets) - 1) * sizeof(uint32_t));
    std::memset(&texture->slice_sizes[1], 0,
                (xe::countof(texture->slice_sizes) - 1) * sizeof(uint32_t));
    std::memset(&texture->pitches[1], 0,
                (xe::countof(texture->pitches) - 1) * sizeof(uint32_t));
    // Never try to upload the mipmaps if there are none.
    texture->mips_in_sync = true;
  }
  texture->base_watch_handle = nullptr;
  texture->mip_watch_handle = nullptr;
  texture->cached_srv_descriptor.ptr = 0;
  texture->cached_srv_descriptor_swizzle = 0b100100100100;
  textures_.insert(std::make_pair(map_key, texture));
  COUNT_profile_set("gpu/texture_cache/textures", textures_.size());
  LogTextureAction(texture, "Created");

  return texture;
}

bool TextureCache::LoadTextureData(Texture* texture) {
  // See what we need to upload.
  shared_memory_->LockWatchMutex();
  bool base_in_sync = texture->base_in_sync;
  bool mips_in_sync = texture->mips_in_sync;
  shared_memory_->UnlockWatchMutex();
  if (base_in_sync && mips_in_sync) {
    return true;
  }

  auto command_list = command_processor_->GetDeferredCommandList();
  auto provider = command_processor_->GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();

  // Get the pipeline.
  LoadMode load_mode = GetLoadMode(texture->key);
  if (load_mode == LoadMode::kUnknown) {
    return false;
  }
  bool scaled_resolve = texture->key.scaled_resolve ? true : false;
  ID3D12PipelineState* pipeline = scaled_resolve
                                      ? load_pipelines_2x_[uint32_t(load_mode)]
                                      : load_pipelines_[uint32_t(load_mode)];
  if (pipeline == nullptr) {
    return false;
  }

  // Request uploading of the texture data to the shared memory.
  // This is also necessary when resolution scale is used - the texture cache
  // relies on shared memory for invalidation of both unscaled and scaled
  // textures! Plus a texture may be unscaled partially, when only a portion of
  // its pages is invalidated, in this case we'll need the texture from the
  // shared memory to load the unscaled parts.
  if (!base_in_sync) {
    if (!shared_memory_->RequestRange(texture->key.base_page << 12,
                                      texture->base_size)) {
      return false;
    }
  }
  if (!mips_in_sync) {
    if (!shared_memory_->RequestRange(texture->key.mip_page << 12,
                                      texture->mip_size)) {
      return false;
    }
  }
  if (scaled_resolve) {
    // Make sure all heaps are created.
    if (!EnsureScaledResolveBufferResident(texture->key.base_page << 12,
                                           texture->base_size)) {
      return false;
    }
    if (!EnsureScaledResolveBufferResident(texture->key.mip_page << 12,
                                           texture->mip_size)) {
      return false;
    }
  }

  // Get the guest layout.
  bool is_3d = texture->key.dimension == Dimension::k3D;
  uint32_t width = texture->key.width;
  uint32_t height = texture->key.height;
  uint32_t depth = is_3d ? texture->key.depth : 1;
  uint32_t slice_count = is_3d ? 1 : texture->key.depth;
  TextureFormat guest_format = texture->key.format;
  const FormatInfo* guest_format_info = FormatInfo::Get(guest_format);
  uint32_t block_width = guest_format_info->block_width;
  uint32_t block_height = guest_format_info->block_height;

  // Get the host layout and the buffer.
  D3D12_RESOURCE_DESC resource_desc = texture->resource->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT host_layouts[D3D12_REQ_MIP_LEVELS];
  UINT64 host_slice_size;
  device->GetCopyableFootprints(&resource_desc, 0, resource_desc.MipLevels, 0,
                                host_layouts, nullptr, nullptr,
                                &host_slice_size);
  // The shaders deliberately overflow for simplicity, and GetCopyableFootprints
  // doesn't align the size of the last row (or the size if there's only one
  // row, not really sure) to row pitch, so add some excess bytes for safety.
  // 1x1 8-bit and 16-bit textures even give a device loss because the raw UAV
  // has a size of 0.
  host_slice_size =
      xe::align(host_slice_size, UINT64(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
  D3D12_RESOURCE_STATES copy_buffer_state =
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ID3D12Resource* copy_buffer = command_processor_->RequestScratchGPUBuffer(
      uint32_t(host_slice_size), copy_buffer_state);
  if (copy_buffer == nullptr) {
    return false;
  }

  // Begin loading.
  uint32_t mip_first = base_in_sync ? 1 : 0;
  uint32_t mip_last = mips_in_sync ? 0 : resource_desc.MipLevels - 1;
  // Can't address more than 512 MB directly on Nvidia - need two separate UAV
  // descriptors for base and mips.
  bool separate_base_and_mips_descriptors =
      scaled_resolve && mip_first == 0 && mip_last != 0;
  uint32_t descriptor_count = separate_base_and_mips_descriptors ? 4 : 2;
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
  D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
  if (command_processor_->RequestViewDescriptors(
          0, descriptor_count, descriptor_count, descriptor_cpu_start,
          descriptor_gpu_start) == 0) {
    command_processor_->ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);
    return false;
  }
  if (scaled_resolve) {
    // TODO(Triang3l): Allow partial invalidation of scaled textures - send a
    // part of scaled_resolve_pages_ to the shader and choose the source
    // according to whether a specific page contains scaled texture data. If
    // it's not, duplicate the texels from the unscaled version - will be
    // blocky with filtering, but better than nothing.
    UseScaledResolveBufferForReading();
    uint32_t srv_descriptor_offset = 0;
    if (mip_first == 0) {
      CreateScaledResolveBufferRawSRV(
          provider->OffsetViewDescriptor(descriptor_cpu_start,
                                         srv_descriptor_offset),
          texture->key.base_page, (texture->base_size + 0xFFF) >> 12);
      srv_descriptor_offset += 2;
    }
    if (mip_last != 0) {
      CreateScaledResolveBufferRawSRV(
          provider->OffsetViewDescriptor(descriptor_cpu_start,
                                         srv_descriptor_offset),
          texture->key.mip_page, (texture->mip_size + 0xFFF) >> 12);
    }
  } else {
    shared_memory_->UseForReading();
    shared_memory_->WriteRawSRVDescriptor(descriptor_cpu_start);
  }
  // Create two destination descriptors since the table has both.
  for (uint32_t i = 1; i < descriptor_count; i += 2) {
    ui::d3d12::util::CreateRawBufferUAV(
        device, provider->OffsetViewDescriptor(descriptor_cpu_start, i),
        copy_buffer, uint32_t(host_slice_size));
  }
  command_processor_->SetComputePipeline(pipeline);
  command_list->D3DSetComputeRootSignature(load_root_signature_);
  if (!separate_base_and_mips_descriptors) {
    // Will be bound later.
    command_list->D3DSetComputeRootDescriptorTable(1, descriptor_gpu_start);
  }

  // Submit commands.
  command_processor_->PushTransitionBarrier(texture->resource, texture->state,
                                            D3D12_RESOURCE_STATE_COPY_DEST);
  texture->state = D3D12_RESOURCE_STATE_COPY_DEST;
  auto cbuffer_pool = command_processor_->GetConstantBufferPool();
  LoadConstants load_constants;
  load_constants.is_3d = is_3d ? 1 : 0;
  load_constants.endianness = uint32_t(texture->key.endianness);
  load_constants.guest_format = uint32_t(guest_format);
  if (!texture->key.packed_mips) {
    load_constants.guest_mip_offset[0] = 0;
    load_constants.guest_mip_offset[1] = 0;
    load_constants.guest_mip_offset[2] = 0;
  }
  for (uint32_t i = 0; i < slice_count; ++i) {
    command_processor_->PushTransitionBarrier(
        copy_buffer, copy_buffer_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    copy_buffer_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    for (uint32_t j = mip_first; j <= mip_last; ++j) {
      if (scaled_resolve) {
        // Offset already applied in the buffer because more than 512 MB can't
        // be directly addresses on Nvidia.
        load_constants.guest_base = 0;
      } else {
        if (j == 0) {
          load_constants.guest_base = texture->key.base_page << 12;
        } else {
          load_constants.guest_base = texture->key.mip_page << 12;
        }
      }
      load_constants.guest_base +=
          texture->mip_offsets[j] + i * texture->slice_sizes[j];
      load_constants.guest_pitch = texture->key.tiled
                                       ? LoadConstants::kGuestPitchTiled
                                       : texture->pitches[j];
      load_constants.host_base = uint32_t(host_layouts[j].Offset);
      load_constants.host_pitch = host_layouts[j].Footprint.RowPitch;
      load_constants.size_texels[0] = std::max(width >> j, 1u);
      load_constants.size_texels[1] = std::max(height >> j, 1u);
      load_constants.size_texels[2] = std::max(depth >> j, 1u);
      load_constants.size_blocks[0] =
          (load_constants.size_texels[0] + (block_width - 1)) / block_width;
      load_constants.size_blocks[1] =
          (load_constants.size_texels[1] + (block_height - 1)) / block_height;
      load_constants.size_blocks[2] = load_constants.size_texels[2];
      if (j == 0) {
        load_constants.guest_storage_width_height[0] =
            xe::align(load_constants.size_blocks[0], 32u);
        load_constants.guest_storage_width_height[1] =
            xe::align(load_constants.size_blocks[1], 32u);
      } else {
        load_constants.guest_storage_width_height[0] =
            xe::align(xe::next_pow2(load_constants.size_blocks[0]), 32u);
        load_constants.guest_storage_width_height[1] =
            xe::align(xe::next_pow2(load_constants.size_blocks[1]), 32u);
      }
      if (texture->key.packed_mips) {
        texture_util::GetPackedMipOffset(width, height, depth, guest_format, j,
                                         load_constants.guest_mip_offset[0],
                                         load_constants.guest_mip_offset[1],
                                         load_constants.guest_mip_offset[2]);
      }
      D3D12_GPU_VIRTUAL_ADDRESS cbuffer_gpu_address;
      uint8_t* cbuffer_mapping = cbuffer_pool->RequestFull(
          xe::align(uint32_t(sizeof(load_constants)), 256u), nullptr, nullptr,
          &cbuffer_gpu_address);
      if (cbuffer_mapping == nullptr) {
        command_processor_->ReleaseScratchGPUBuffer(copy_buffer,
                                                    copy_buffer_state);
        return false;
      }
      std::memcpy(cbuffer_mapping, &load_constants, sizeof(load_constants));
      command_list->D3DSetComputeRootConstantBufferView(0, cbuffer_gpu_address);
      if (separate_base_and_mips_descriptors) {
        if (j == 0) {
          command_list->D3DSetComputeRootDescriptorTable(1,
                                                         descriptor_gpu_start);
        } else if (j == 1) {
          command_list->D3DSetComputeRootDescriptorTable(
              1, provider->OffsetViewDescriptor(descriptor_gpu_start, 2));
        }
      }
      command_processor_->SubmitBarriers();
      // Each thread group processes 32x32x1 blocks after resolution scaling has
      // been applied.
      uint32_t group_count_x = load_constants.size_blocks[0];
      uint32_t group_count_y = load_constants.size_blocks[1];
      if (texture->key.scaled_resolve) {
        group_count_x *= 2;
        group_count_y *= 2;
      }
      group_count_x = (group_count_x + 31) >> 5;
      group_count_y = (group_count_y + 31) >> 5;
      command_list->D3DDispatch(group_count_x, group_count_y,
                                load_constants.size_blocks[2]);
    }
    command_processor_->PushUAVBarrier(copy_buffer);
    command_processor_->PushTransitionBarrier(copy_buffer, copy_buffer_state,
                                              D3D12_RESOURCE_STATE_COPY_SOURCE);
    copy_buffer_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_processor_->SubmitBarriers();
    UINT slice_first_subresource = i * resource_desc.MipLevels;
    for (uint32_t j = mip_first; j <= mip_last; ++j) {
      D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
      location_source.pResource = copy_buffer;
      location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      location_source.PlacedFootprint = host_layouts[j];
      location_dest.pResource = texture->resource;
      location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      location_dest.SubresourceIndex = slice_first_subresource + j;
      command_list->CopyTexture(location_dest, location_source);
    }
  }

  command_processor_->ReleaseScratchGPUBuffer(copy_buffer, copy_buffer_state);

  // Mark the ranges as uploaded and watch them. This is needed for scaled
  // resolves as well to detect when the CPU wants to reuse the memory for a
  // regular texture or a vertex buffer, and thus the scaled resolve version is
  // not up to date anymore.
  shared_memory_->LockWatchMutex();
  texture->base_in_sync = true;
  texture->mips_in_sync = true;
  if (!base_in_sync) {
    texture->base_watch_handle = shared_memory_->WatchMemoryRange(
        texture->key.base_page << 12, texture->base_size, WatchCallbackThunk,
        this, texture, 0);
  }
  if (!mips_in_sync) {
    texture->mip_watch_handle = shared_memory_->WatchMemoryRange(
        texture->key.mip_page << 12, texture->mip_size, WatchCallbackThunk,
        this, texture, 1);
  }
  shared_memory_->UnlockWatchMutex();

  LogTextureAction(texture, "Loaded");
  return true;
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
  std::memset(texture_bindings_, 0, sizeof(texture_bindings_));
  texture_keys_in_sync_ = 0;
  // Already reset everything.
  texture_invalidated_.store(false, std::memory_order_relaxed);
}

bool TextureCache::IsRangeScaledResolved(uint32_t start_unscaled,
                                         uint32_t length_unscaled) {
  if (!IsResolutionScale2X() || length_unscaled == 0) {
    return false;
  }

  start_unscaled &= 0x1FFFFFFF;
  length_unscaled = std::min(length_unscaled, 0x20000000 - start_unscaled);

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
  shared_memory_->LockWatchMutex();
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
        shared_memory_->UnlockWatchMutex();
        return true;
      }
    }
  }
  shared_memory_->UnlockWatchMutex();
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
  assert_true(IsResolutionScale2X());
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

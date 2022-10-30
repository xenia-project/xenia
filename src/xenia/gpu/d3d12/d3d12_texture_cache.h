/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_D3D12_TEXTURE_CACHE_H_
#define XENIA_GPU_D3D12_D3D12_TEXTURE_CACHE_H_

#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/gpu/d3d12/d3d12_shader.h"
#include "xenia/gpu/d3d12/d3d12_shared_memory.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/texture_cache.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/d3d12/d3d12_provider.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor;

class D3D12TextureCache final : public TextureCache {
 public:
  // Keys that can be stored for checking validity whether descriptors for host
  // shader bindings are up to date.
  struct TextureSRVKey {
    TextureKey key;
    uint32_t host_swizzle;
    uint8_t swizzled_signs;
  };

  // Sampler parameters that can be directly converted to a host sampler or used
  // for binding checking validity whether samplers are up to date.
  union SamplerParameters {
    uint32_t value;
    struct {
      xenos::ClampMode clamp_x : 3;         // 3
      xenos::ClampMode clamp_y : 3;         // 6
      xenos::ClampMode clamp_z : 3;         // 9
      xenos::BorderColor border_color : 2;  // 11
      // For anisotropic, these are true.
      uint32_t mag_linear : 1;              // 12
      uint32_t min_linear : 1;              // 13
      uint32_t mip_linear : 1;              // 14
      xenos::AnisoFilter aniso_filter : 3;  // 17
      uint32_t mip_min_level : 4;           // 21
      uint32_t mip_base_map : 1;            // 22
      // Maximum mip level is in the texture resource itself, but mip_base_map
      // can be used to limit fetching to mip_min_level.
    };

    SamplerParameters() : value(0) { static_assert_size(*this, sizeof(value)); }
    bool operator==(const SamplerParameters& parameters) const {
      return value == parameters.value;
    }
    bool operator!=(const SamplerParameters& parameters) const {
      return value != parameters.value;
    }
  };

  static std::unique_ptr<D3D12TextureCache> Create(
      const RegisterFile& register_file, D3D12SharedMemory& shared_memory,
      uint32_t draw_resolution_scale_x, uint32_t draw_resolution_scale_y,
      D3D12CommandProcessor& command_processor, bool bindless_resources_used) {
    std::unique_ptr<D3D12TextureCache> texture_cache(new D3D12TextureCache(
        register_file, shared_memory, draw_resolution_scale_x,
        draw_resolution_scale_y, command_processor, bindless_resources_used));
    if (!texture_cache->Initialize()) {
      return nullptr;
    }
    return std::move(texture_cache);
  }

  ~D3D12TextureCache();

  void ClearCache() override;

  void BeginSubmission(uint64_t new_submission_index) override;
  void BeginFrame() override;
  void EndFrame();

  // Must be called within a submission - creates and untiles textures needed by
  // shaders and puts them in the SRV state. This may bind compute pipelines
  // (notifying the command processor about that), so this must be called before
  // binding the actual drawing pipeline.
  void RequestTextures(uint32_t used_texture_mask) override;

  // Returns whether texture SRV keys stored externally are still valid for the
  // current bindings and host shader binding layout. Both keys and
  // host_shader_bindings must have host_shader_binding_count elements
  // (otherwise they are incompatible - like if this function returned false).
  bool AreActiveTextureSRVKeysUpToDate(
      const TextureSRVKey* keys,
      const D3D12Shader::TextureBinding* host_shader_bindings,
      size_t host_shader_binding_count) const;
  // Exports the current binding data to texture SRV keys so they can be stored
  // for checking whether subsequent draw calls can keep using the same
  // bindings. Write host_shader_binding_count keys.
  void WriteActiveTextureSRVKeys(
      TextureSRVKey* keys,
      const D3D12Shader::TextureBinding* host_shader_bindings,
      size_t host_shader_binding_count) const;
  void WriteActiveTextureBindfulSRV(
      const D3D12Shader::TextureBinding& host_shader_binding,
      D3D12_CPU_DESCRIPTOR_HANDLE handle);
  uint32_t GetActiveTextureBindlessSRVIndex(
      const D3D12Shader::TextureBinding& host_shader_binding);
  void PrefetchSamplerParameters(
      const D3D12Shader::SamplerBinding& binding) const;
  SamplerParameters GetSamplerParameters(
      const D3D12Shader::SamplerBinding& binding) const;
  void WriteSampler(SamplerParameters parameters,
                    D3D12_CPU_DESCRIPTOR_HANDLE handle) const;

  // Returns whether the actual scale is not smaller than the requested one.
  static bool ClampDrawResolutionScaleToMaxSupported(
      uint32_t& scale_x, uint32_t& scale_y,
      const ui::d3d12::D3D12Provider& provider);
  // Ensures the tiles backing the range in the buffers are allocated.
  bool EnsureScaledResolveMemoryCommitted(
      uint32_t start_unscaled, uint32_t length_unscaled,
      uint32_t length_scaled_alignment_log2 = 0) override;
  // Makes the specified range of up to 1-2 GB currently accessible on the GPU.
  // One draw call can access only at most one range - the same memory is
  // accessible through different buffers based on the range needed, so aliasing
  // barriers are required.
  bool MakeScaledResolveRangeCurrent(uint32_t start_unscaled,
                                     uint32_t length_unscaled,
                                     uint32_t length_scaled_alignment_log2 = 0);
  // These functions create a view of the range specified in the last successful
  // MakeScaledResolveRangeCurrent call because that function must be called
  // before this.
  void CreateCurrentScaledResolveRangeUintPow2SRV(
      D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t element_size_bytes_pow2);
  void CreateCurrentScaledResolveRangeUintPow2UAV(
      D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t element_size_bytes_pow2);
  void TransitionCurrentScaledResolveRange(D3D12_RESOURCE_STATES new_state);
  void MarkCurrentScaledResolveRangeUAVWritesCommitNeeded() {
    assert_true(IsDrawResolutionScaled());
    GetCurrentScaledResolveBuffer().SetUAVBarrierPending();
  }

  // Returns the ID3D12Resource of the front buffer texture (in
  // NON_PIXEL_SHADER_RESOURCE state), or nullptr in case of failure, and writes
  // the description of its SRV. May call LoadTextureData, so the same
  // restrictions (such as about descriptor heap change possibility) apply.
  ID3D12Resource* RequestSwapTexture(
      D3D12_SHADER_RESOURCE_VIEW_DESC& srv_desc_out,
      xenos::TextureFormat& format_out);
  struct HostFormat {
    // Format info for the regular case.
    // DXGI format (typeless when different signedness or number representation
    // is used) for the texture resource.
    DXGI_FORMAT dxgi_format_resource;
    // DXGI format for unsigned normalized or unsigned/signed float SRV.
    DXGI_FORMAT dxgi_format_unsigned;
    // The regular load shader, used when special load shaders (like
    // signed-specific or decompressing) aren't needed.
    LoadShaderIndex load_shader;
    // DXGI format for signed normalized or unsigned/signed float SRV.
    DXGI_FORMAT dxgi_format_signed;
    // If the signed version needs a different bit representation on the host,
    // this is the load shader for the signed version. Otherwise the regular
    // load_shader will be used for the signed version, and a single copy will
    // be created if both unsigned and signed are used.
    LoadShaderIndex load_shader_signed;

    // Do NOT add integer DXGI formats to this - they are not filterable, can
    // only be read with Load, not Sample! If any game is seen using num_format
    // 1 for fixed-point formats (for floating-point, it's normally set to 1
    // though), add a constant buffer containing multipliers for the
    // textures and multiplication to the tfetch implementation.

    // Whether the DXGI format, if not uncompressing the texture, consists of
    // blocks, thus copy regions must be aligned to block size (assuming it's
    // the same as the guest block size).
    bool is_block_compressed;
    // Uncompression info for when the regular host format for this texture is
    // block-compressed, but the size is not block-aligned, and thus such
    // texture cannot be created in Direct3D on PC and needs decompression,
    // however, such textures are common, for instance, in 4D5307E6. This only
    // supports unsigned normalized formats - let's hope GPUSIGN_SIGNED was not
    // used for DXN and DXT5A.
    DXGI_FORMAT dxgi_format_uncompressed;
    LoadShaderIndex load_shader_decompress;

    // Mapping of Xenos swizzle components to DXGI format components.
    uint32_t swizzle;
  };
  static constexpr HostFormat host_formats_[64]{
      // k_1_REVERSE
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_1
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_8
      {DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UNORM, kLoadShaderIndex8bpb,
       DXGI_FORMAT_R8_SNORM, kLoadShaderIndexUnknown, false,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_1_5_5_5
      // Red and blue swapped in the load shader for simplicity.
      {DXGI_FORMAT_B5G5R5A1_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM,
       kLoadShaderIndexR5G5B5A1ToB5G5R5A1, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_5_6_5
      // Red and blue swapped in the load shader for simplicity.
      {DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G6R5_UNORM,
       kLoadShaderIndexR5G6B5ToB5G6R5, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
      // k_6_5_5
      // On the host, green bits in blue, blue bits in green.
      {DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G6R5_UNORM,
       kLoadShaderIndexR5G5B6ToB5G6R5WithRBGASwizzle, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, XE_GPU_MAKE_TEXTURE_SWIZZLE(R, B, G, G)},
      // k_8_8_8_8
      {DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM,
       kLoadShaderIndex32bpb, DXGI_FORMAT_R8G8B8A8_SNORM,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_2_10_10_10
      {DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R10G10B10A2_UNORM,
       kLoadShaderIndex32bpb, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       false, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_8_A
      {DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UNORM, kLoadShaderIndex8bpb,
       DXGI_FORMAT_R8_SNORM, kLoadShaderIndexUnknown, false,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_8_B
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_8_8
      {DXGI_FORMAT_R8G8_TYPELESS, DXGI_FORMAT_R8G8_UNORM, kLoadShaderIndex16bpb,
       DXGI_FORMAT_R8G8_SNORM, kLoadShaderIndexUnknown, false,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_Cr_Y1_Cb_Y0_REP
      // Red and blue swapped in the load shader for simplicity.
      // TODO(Triang3l): The DXGI_FORMAT_R8G8B8A8_U/SNORM conversion is
      // usable for
      // the signed version, separate unsigned and signed load shaders
      // completely
      // (as one doesn't need decompression for this format, while another
      // does).
      {DXGI_FORMAT_G8R8_G8B8_UNORM, DXGI_FORMAT_G8R8_G8B8_UNORM,
       kLoadShaderIndexGBGR8ToGRGB8, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, true, DXGI_FORMAT_R8G8B8A8_UNORM,
       kLoadShaderIndexGBGR8ToRGB8, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
      // k_Y1_Cr_Y0_Cb_REP
      // Red and blue swapped in the load shader for simplicity.
      // TODO(Triang3l): The DXGI_FORMAT_R8G8B8A8_U/SNORM conversion is
      // usable for
      // the signed version, separate unsigned and signed load shaders
      // completely
      // (as one doesn't need decompression for this format, while another
      // does).
      {DXGI_FORMAT_R8G8_B8G8_UNORM, DXGI_FORMAT_R8G8_B8G8_UNORM,
       kLoadShaderIndexBGRG8ToRGBG8, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, true, DXGI_FORMAT_R8G8B8A8_UNORM,
       kLoadShaderIndexBGRG8ToRGB8, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
      // k_16_16_EDRAM
      // Not usable as a texture, also has -32...32 range.
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_8_8_8_8_A
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_4_4_4_4
      // Red and blue swapped in the load shader for simplicity.
      {DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM,
       kLoadShaderIndexRGBA4ToBGRA4, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_10_11_11
      {DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM,
       kLoadShaderIndexR11G11B10ToRGBA16, DXGI_FORMAT_R16G16B16A16_SNORM,
       kLoadShaderIndexR11G11B10ToRGBA16SNorm, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
      // k_11_11_10
      {DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM,
       kLoadShaderIndexR10G11B11ToRGBA16, DXGI_FORMAT_R16G16B16A16_SNORM,
       kLoadShaderIndexR10G11B11ToRGBA16SNorm, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
      // k_DXT1
      {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM, kLoadShaderIndex64bpb,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, true,
       DXGI_FORMAT_R8G8B8A8_UNORM, kLoadShaderIndexDXT1ToRGBA8,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_DXT2_3
      {DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM, kLoadShaderIndex128bpb,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, true,
       DXGI_FORMAT_R8G8B8A8_UNORM, kLoadShaderIndexDXT3ToRGBA8,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_DXT4_5
      {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM, kLoadShaderIndex128bpb,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, true,
       DXGI_FORMAT_R8G8B8A8_UNORM, kLoadShaderIndexDXT5ToRGBA8,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_16_16_16_16_EDRAM
      // Not usable as a texture, also has -32...32 range.
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // R32_FLOAT for depth because shaders would require an additional SRV
      // to
      // sample stencil, which we don't provide.
      // k_24_8
      {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, kLoadShaderIndexDepthUnorm,
       DXGI_FORMAT_R32_FLOAT, kLoadShaderIndexUnknown, false,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_24_8_FLOAT
      {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, kLoadShaderIndexDepthFloat,
       DXGI_FORMAT_R32_FLOAT, kLoadShaderIndexUnknown, false,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_16
      {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UNORM, kLoadShaderIndex16bpb,
       DXGI_FORMAT_R16_SNORM, kLoadShaderIndexUnknown, false,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_16_16
      {DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_UNORM,
       kLoadShaderIndex32bpb, DXGI_FORMAT_R16G16_SNORM, kLoadShaderIndexUnknown,
       false, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_16_16_16_16
      {DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM,
       kLoadShaderIndex64bpb, DXGI_FORMAT_R16G16B16A16_SNORM,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_16_EXPAND
      {DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_FLOAT, kLoadShaderIndex16bpb,
       DXGI_FORMAT_R16_FLOAT, kLoadShaderIndexUnknown, false,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_16_16_EXPAND
      {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT,
       kLoadShaderIndex32bpb, DXGI_FORMAT_R16G16_FLOAT, kLoadShaderIndexUnknown,
       false, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_16_16_16_16_EXPAND
      {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
       kLoadShaderIndex64bpb, DXGI_FORMAT_R16G16B16A16_FLOAT,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_16_FLOAT
      {DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_FLOAT, kLoadShaderIndex16bpb,
       DXGI_FORMAT_R16_FLOAT, kLoadShaderIndexUnknown, false,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_16_16_FLOAT
      {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT,
       kLoadShaderIndex32bpb, DXGI_FORMAT_R16G16_FLOAT, kLoadShaderIndexUnknown,
       false, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_16_16_16_16_FLOAT
      {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
       kLoadShaderIndex64bpb, DXGI_FORMAT_R16G16B16A16_FLOAT,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_32
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_32_32
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_32_32_32_32
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_32_FLOAT
      {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, kLoadShaderIndex32bpb,
       DXGI_FORMAT_R32_FLOAT, kLoadShaderIndexUnknown, false,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_32_32_FLOAT
      {DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32_FLOAT,
       kLoadShaderIndex64bpb, DXGI_FORMAT_R32G32_FLOAT, kLoadShaderIndexUnknown,
       false, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_32_32_32_32_FLOAT
      {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
       kLoadShaderIndex128bpb, DXGI_FORMAT_R32G32B32A32_FLOAT,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_32_AS_8
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_32_AS_8_8
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_16_MPEG
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_16_16_MPEG
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_8_INTERLACED
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_32_AS_8_INTERLACED
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_32_AS_8_8_INTERLACED
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_16_INTERLACED
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_16_MPEG_INTERLACED
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_16_16_MPEG_INTERLACED
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_DXN
      {DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_UNORM, kLoadShaderIndex128bpb,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, true,
       DXGI_FORMAT_R8G8_UNORM, kLoadShaderIndexDXNToRG8,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_8_8_8_8_AS_16_16_16_16
      {DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM,
       kLoadShaderIndex32bpb, DXGI_FORMAT_R8G8B8A8_SNORM,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_DXT1_AS_16_16_16_16
      {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM, kLoadShaderIndex64bpb,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, true,
       DXGI_FORMAT_R8G8B8A8_UNORM, kLoadShaderIndexDXT1ToRGBA8,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_DXT2_3_AS_16_16_16_16
      {DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM, kLoadShaderIndex128bpb,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, true,
       DXGI_FORMAT_R8G8B8A8_UNORM, kLoadShaderIndexDXT3ToRGBA8,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_DXT4_5_AS_16_16_16_16
      {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM, kLoadShaderIndex128bpb,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, true,
       DXGI_FORMAT_R8G8B8A8_UNORM, kLoadShaderIndexDXT5ToRGBA8,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_2_10_10_10_AS_16_16_16_16
      {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM,
       kLoadShaderIndex32bpb, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       false, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_10_11_11_AS_16_16_16_16
      {DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM,
       kLoadShaderIndexR11G11B10ToRGBA16, DXGI_FORMAT_R16G16B16A16_SNORM,
       kLoadShaderIndexR11G11B10ToRGBA16SNorm, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
      // k_11_11_10_AS_16_16_16_16
      {DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM,
       kLoadShaderIndexR10G11B11ToRGBA16, DXGI_FORMAT_R16G16B16A16_SNORM,
       kLoadShaderIndexR10G11B11ToRGBA16SNorm, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
      // k_32_32_32_FLOAT
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB},
      // k_DXT3A
      // R8_UNORM has the same size as BC2, but doesn't have the 4x4 size
      // alignment requirement.
      {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, kLoadShaderIndexDXT3A,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_DXT5A
      {DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_UNORM, kLoadShaderIndex64bpb,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, true, DXGI_FORMAT_R8_UNORM,
       kLoadShaderIndexDXT5AToR8, xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR},
      // k_CTX1
      {DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, kLoadShaderIndexCTX1,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG},
      // k_DXT3A_AS_1_1_1_1
      {DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM,
       kLoadShaderIndexDXT3AAs1111ToBGRA4, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_8_8_8_8_GAMMA_EDRAM
      // Not usable as a texture.
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
      // k_2_10_10_10_FLOAT_EDRAM
      // Not usable as a texture.
      {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown,
       DXGI_FORMAT_UNKNOWN, kLoadShaderIndexUnknown, false, DXGI_FORMAT_UNKNOWN,
       kLoadShaderIndexUnknown, xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA},
  };

 protected:
  bool IsSignedVersionSeparateForFormat(TextureKey key) const override;
  bool IsScaledResolveSupportedForFormat(TextureKey key) const override;
  uint32_t GetHostFormatSwizzle(TextureKey key) const override;

  uint32_t GetMaxHostTextureWidthHeight(
      xenos::DataDimension dimension) const override;
  uint32_t GetMaxHostTextureDepthOrArraySize(
      xenos::DataDimension dimension) const override;

  std::unique_ptr<Texture> CreateTexture(TextureKey key) override;

  // This binds pipelines, allocates descriptors, and copies!
  bool LoadTextureDataFromResidentMemoryImpl(Texture& texture, bool load_base,
                                             bool load_mips) override;

  void UpdateTextureBindingsImpl(uint32_t fetch_constant_mask) override;

 private:
  static constexpr uint32_t kLoadGuestXThreadsPerGroupLog2 = 2;
  static constexpr uint32_t kLoadGuestYBlocksPerGroupLog2 = 5;

  class D3D12Texture final : public Texture {
   public:
    union SRVDescriptorKey {
      uint32_t key;
      struct {
        uint32_t is_signed : 1;
        uint32_t host_swizzle : 12;
      };

      SRVDescriptorKey() : key(0) { static_assert_size(*this, sizeof(key)); }

      struct Hasher {
        size_t operator()(const SRVDescriptorKey& key) const {
          return std::hash<decltype(key.key)>{}(key.key);
        }
      };
      bool operator==(const SRVDescriptorKey& other_key) const {
        return key == other_key.key;
      }
      bool operator!=(const SRVDescriptorKey& other_key) const {
        return !(*this == other_key);
      }
    };

    explicit D3D12Texture(D3D12TextureCache& texture_cache,
                          const TextureKey& key, ID3D12Resource* resource,
                          D3D12_RESOURCE_STATES resource_state);
    ~D3D12Texture();

    ID3D12Resource* resource() const { return resource_.Get(); }

    D3D12_RESOURCE_STATES SetResourceState(D3D12_RESOURCE_STATES new_state) {
      D3D12_RESOURCE_STATES old_state = resource_state_;
      resource_state_ = new_state;
      return old_state;
    }

    uint32_t GetSRVDescriptorIndex(SRVDescriptorKey descriptor_key) const {
      auto it = srv_descriptors_.find(descriptor_key);
      return it != srv_descriptors_.cend() ? it->second : UINT32_MAX;
    }

    void AddSRVDescriptorIndex(SRVDescriptorKey descriptor_key,
                               uint32_t descriptor_index) {
      srv_descriptors_.emplace(descriptor_key, descriptor_index);
    }

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES resource_state_;

    // For bindful - indices in the non-shader-visible descriptor cache for
    // copying to the shader-visible heap (much faster than recreating, which,
    // according to profiling, was often a bottleneck in many games).
    // For bindless - indices in the global shader-visible descriptor heap.
    std::unordered_map<SRVDescriptorKey, uint32_t, SRVDescriptorKey::Hasher>
        srv_descriptors_;
  };

  static constexpr uint32_t kSRVDescriptorCachePageSize = 65536;

  struct SRVDescriptorCachePage {
   public:
    explicit SRVDescriptorCachePage(ID3D12DescriptorHeap* heap)
        : heap_(heap),
          heap_start_(heap->GetCPUDescriptorHandleForHeapStart()) {}
    SRVDescriptorCachePage(const SRVDescriptorCachePage& page) = delete;
    SRVDescriptorCachePage& operator=(const SRVDescriptorCachePage& page) =
        delete;
    SRVDescriptorCachePage(SRVDescriptorCachePage&& page) {
      std::swap(heap_, page.heap_);
      std::swap(heap_start_, page.heap_start_);
    }
    SRVDescriptorCachePage& operator=(SRVDescriptorCachePage&& page) {
      std::swap(heap_, page.heap_);
      std::swap(heap_start_, page.heap_start_);
      return *this;
    }

    ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE heap_start() const { return heap_start_; }

   private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE heap_start_;
  };

  struct D3D12TextureBinding {
    // Descriptor indices of texture and texture_signed of the respective
    // TextureBinding returned from FindOrCreateTextureDescriptor.
    uint32_t descriptor_index;
    uint32_t descriptor_index_signed;

    D3D12TextureBinding() { Reset(); }

    void Reset() {
      descriptor_index = UINT32_MAX;
      descriptor_index_signed = UINT32_MAX;
    }
  };

  class ScaledResolveVirtualBuffer {
   public:
    explicit ScaledResolveVirtualBuffer(ID3D12Resource* resource,
                                        D3D12_RESOURCE_STATES resource_state)
        : resource_(resource), resource_state_(resource_state) {}
    ID3D12Resource* resource() const { return resource_.Get(); }
    D3D12_RESOURCE_STATES SetResourceState(D3D12_RESOURCE_STATES new_state) {
      D3D12_RESOURCE_STATES old_state = resource_state_;
      if (old_state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        uav_barrier_pending_ = false;
      }
      resource_state_ = new_state;
      return old_state;
    }
    // After writing through a UAV.
    void SetUAVBarrierPending() {
      if (resource_state_ == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        uav_barrier_pending_ = true;
      }
    }
    // After an aliasing barrier (which is even stronger than an UAV barrier).
    void ClearUAVBarrierPending() { uav_barrier_pending_ = false; }

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES resource_state_;
    bool uav_barrier_pending_ = false;
  };

  explicit D3D12TextureCache(const RegisterFile& register_file,
                             D3D12SharedMemory& shared_memory,
                             uint32_t draw_resolution_scale_x,
                             uint32_t draw_resolution_scale_y,
                             D3D12CommandProcessor& command_processor,
                             bool bindless_resources_used);

  bool Initialize();

  // Whether decompression is needed on the host (Direct3D only allows creation
  // of block-compressed textures with 4x4-aligned dimensions on PC).
  bool IsDecompressionNeeded(xenos::TextureFormat format, uint32_t width,
                             uint32_t height) const;
  DXGI_FORMAT GetDXGIResourceFormat(xenos::TextureFormat format, uint32_t width,
                                    uint32_t height) const {
    const HostFormat& host_format = host_formats_[uint32_t(format)];
    return IsDecompressionNeeded(format, width, height)
               ? host_format.dxgi_format_uncompressed
               : host_format.dxgi_format_resource;
  }
  DXGI_FORMAT GetDXGIResourceFormat(TextureKey key) const {
    return GetDXGIResourceFormat(key.format, key.GetWidth(), key.GetHeight());
  }
  DXGI_FORMAT GetDXGIUnormFormat(xenos::TextureFormat format, uint32_t width,
                                 uint32_t height) const {
    const HostFormat& host_format = host_formats_[uint32_t(format)];
    return IsDecompressionNeeded(format, width, height)
               ? host_format.dxgi_format_uncompressed
               : host_format.dxgi_format_unsigned;
  }
  DXGI_FORMAT GetDXGIUnormFormat(TextureKey key) const {
    return GetDXGIUnormFormat(key.format, key.GetWidth(), key.GetHeight());
  }

  LoadShaderIndex GetLoadShaderIndex(TextureKey key) const;
  // chrispy: todo, can use simple branchless tests here
  static constexpr bool AreDimensionsCompatible(
      xenos::FetchOpDimension binding_dimension,
      xenos::DataDimension resource_dimension) {
    switch (binding_dimension) {
      case xenos::FetchOpDimension::k1D:
      case xenos::FetchOpDimension::k2D:
        return resource_dimension == xenos::DataDimension::k1D ||
               resource_dimension == xenos::DataDimension::k2DOrStacked;
      case xenos::FetchOpDimension::k3DOrStacked:
        return resource_dimension == xenos::DataDimension::k3D;
      case xenos::FetchOpDimension::kCube:
        return resource_dimension == xenos::DataDimension::kCube;
      default:
        return false;
    }
  }

  // Returns the index of an existing of a newly created non-shader-visible
  // cached (for bindful) or a shader-visible global (for bindless) descriptor,
  // or UINT32_MAX if failed to create.
  uint32_t FindOrCreateTextureDescriptor(D3D12Texture& texture, bool is_signed,
                                         uint32_t host_swizzle);
  void ReleaseTextureDescriptor(uint32_t descriptor_index);
  D3D12_CPU_DESCRIPTOR_HANDLE GetTextureDescriptorCPUHandle(
      uint32_t descriptor_index) const;

  size_t GetScaledResolveBufferCount() const {
    assert_true(IsDrawResolutionScaled());
    // Make sure any range up to 1 GB is accessible through 1 or 2 buffers.
    // 2x2 scale buffers - just one 2 GB buffer for all 2 GB.
    // 3x3 scale buffers - 4 buffers:
    //  +0.0 +0.5 +1.0 +1.5 +2.0 +2.5 +3.0 +3.5 +4.0 +4.5
    // |___________________|___________________|
    //           |___________________|______________|
    // Buffer N has an offset of N * 1 GB in the scaled resolve address space.
    // The logic is:
    // - 2 GB can be accessed through a [0 GB ... 2 GB) buffer - only need one.
    // - 2.1 GB needs [0 GB ... 2 GB) and [1 GB ... 2.1 GB) - two buffers.
    // - 3 GB needs [0 GB ... 2 GB) and [1 GB ... 3 GB) - two buffers.
    // - 3.1 GB needs [0 GB ... 2 GB), [1 GB ... 3 GB) and [2 GB ... 3.1 GB) -
    //   three buffers.
    uint64_t address_space_size =
        uint64_t(SharedMemory::kBufferSize) *
        (draw_resolution_scale_x() * draw_resolution_scale_y());
    return size_t((address_space_size - 1) >> 30);
  }
  // Returns indices of two scaled resolve virtual buffers that the location in
  // memory may be accessible through. May be the same if it's a location near
  // the beginning or the end of the address represented only by one buffer.
  std::array<size_t, 2> GetPossibleScaledResolveBufferIndices(
      uint64_t address_scaled) const {
    assert_true(IsDrawResolutionScaled());
    size_t address_gb = size_t(address_scaled >> 30);
    size_t max_index = GetScaledResolveBufferCount() - 1;
    // In different cases for 3x3:
    //  +0.0 +0.5 +1.0 +1.5 +2.0 +2.5 +3.0 +3.5 +4.0 +4.5
    // |12________2________|1_________2________|
    //           |1_________2________|1_________12__|
    return std::array<size_t, 2>{
        std::min(address_gb, max_index),
        std::min(std::max(address_gb, size_t(1)) - size_t(1), max_index)};
  }
  // The index is also the gigabyte offset of the buffer from the start of the
  // scaled physical memory address space.
  size_t GetCurrentScaledResolveBufferIndex() const {
    return scaled_resolve_1gb_buffer_indices_
        [scaled_resolve_current_range_start_scaled_ >> 30];
  }
  ScaledResolveVirtualBuffer& GetCurrentScaledResolveBuffer() {
    ScaledResolveVirtualBuffer* scaled_resolve_buffer =
        scaled_resolve_2gb_buffers_[GetCurrentScaledResolveBufferIndex()].get();
    assert_not_null(scaled_resolve_buffer);
    return *scaled_resolve_buffer;
  }

  xenos::ClampMode NormalizeClampMode(xenos::ClampMode clamp_mode) const;

  D3D12CommandProcessor& command_processor_;
  bool bindless_resources_used_;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> load_root_signature_;
  std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kLoadShaderCount>
      load_pipelines_;
  // Load pipelines for resolution-scaled resolve targets.
  std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kLoadShaderCount>
      load_pipelines_scaled_;

  std::vector<SRVDescriptorCachePage> srv_descriptor_cache_;
  uint32_t srv_descriptor_cache_allocated_;
  // Indices of cached descriptors used by deleted textures, for reuse.
  std::vector<uint32_t> srv_descriptor_cache_free_;

  enum class NullSRVDescriptorIndex {
    k2DArray,
    k3D,
    kCube,

    kCount,
  };
  // Contains null SRV descriptors of dimensions from NullSRVDescriptorIndex.
  // For copying, not shader-visible.
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> null_srv_descriptor_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE null_srv_descriptor_heap_start_;

  std::array<D3D12TextureBinding, xenos::kTextureFetchConstantCount>
      d3d12_texture_bindings_;

  // Unsupported texture formats used during this frame (for research and
  // testing).
  enum : uint8_t {
    kUnsupportedResourceBit = 1,
    kUnsupportedUnormBit = kUnsupportedResourceBit << 1,
    kUnsupportedSnormBit = kUnsupportedUnormBit << 1,
  };
  uint8_t unsupported_format_features_used_[64];

  // The tiled buffer for resolved data with resolution scaling.
  // Because on Direct3D 12 (at least on Windows 10 2004) typed SRV or UAV
  // creation fails for offsets above 4 GB, a single tiled 4.5 GB buffer can't
  // be used for 3x3 resolution scaling.
  // Instead, "sliding window" buffers allowing to access a single range of up
  // to 1 GB (or up to 2 GB, depending on the low bits) at any moment are used.
  // Parts of 4.5 GB address space can be accessed through 2 GB buffers as:
  //  +0.0 +0.5 +1.0 +1.5 +2.0 +2.5 +3.0 +3.5 +4.0 +4.5
  // |___________________|___________________|      or
  //           |___________________|______________|
  // (2 GB is also the amount of scaled physical memory with 2x resolution
  // scale, and older Intel GPUs, while support tiled resources, only support 31
  // virtual address bits per resource).
  // Index is first gigabyte. Only including buffers containing over 1 GB
  // (because otherwise the data will be fully contained in another).
  // Size is calculated the same as in GetScaledResolveBufferCount.
  std::array<std::unique_ptr<ScaledResolveVirtualBuffer>,
             (uint64_t(SharedMemory::kBufferSize) *
                  (kMaxDrawResolutionScaleAlongAxis *
                   kMaxDrawResolutionScaleAlongAxis) -
              1) /
                 (UINT32_C(1) << 30)>
      scaled_resolve_2gb_buffers_;
  // Not very big heaps (16 MB) because they are needed pretty sparsely. One
  // 2x-scaled 1280x720x32bpp texture is slighly bigger than 14 MB.
  static constexpr uint32_t kScaledResolveHeapSizeLog2 = 24;
  static constexpr uint32_t kScaledResolveHeapSize =
      uint32_t(1) << kScaledResolveHeapSizeLog2;
  static_assert(
      (kScaledResolveHeapSize % D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES) == 0,
      "Scaled resolve heap size must be a multiple of Direct3D tile size");
  static_assert(
      kScaledResolveHeapSizeLog2 <= SharedMemory::kBufferSizeLog2,
      "Scaled resolve heaps are assumed to be wholly mappable irrespective of "
      "resolution scale, never truncated, for example, if the scaled resolve "
      "address space is 4.5 GB, but the heap size is 1 GB");
  static_assert(
      kScaledResolveHeapSizeLog2 <= 30,
      "Scaled resolve heaps are assumed to only be wholly mappable to up to "
      "two 2 GB buffers");
  // Resident portions of the tiled buffer.
  std::vector<Microsoft::WRL::ComPtr<ID3D12Heap>> scaled_resolve_heaps_;
  // Number of currently resident portions of the tiled buffer, for profiling.
  uint32_t scaled_resolve_heap_count_ = 0;
  // Current scaled resolve state.
  // For aliasing barrier placement, last owning buffer index for each of 1 GB.
  size_t
      scaled_resolve_1gb_buffer_indices_[(uint64_t(SharedMemory::kBufferSize) *
                                              kMaxDrawResolutionScaleAlongAxis *
                                              kMaxDrawResolutionScaleAlongAxis +
                                          ((uint32_t(1) << 30) - 1)) >>
                                         30];
  // Range used in the last successful MakeScaledResolveRangeCurrent call.
  uint64_t scaled_resolve_current_range_start_scaled_;
  uint64_t scaled_resolve_current_range_length_scaled_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_TEXTURE_CACHE_H_

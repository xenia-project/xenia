/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_TEXTURE_CACHE_H_
#define XENIA_GPU_D3D12_TEXTURE_CACHE_H_

#include <atomic>
#include <mutex>
#include <unordered_map>

#include "xenia/gpu/d3d12/d3d12_shader.h"
#include "xenia/gpu/d3d12/shared_memory.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/d3d12/d3d12_api.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor;

// Manages host copies of guest textures, performing untiling, format and endian
// conversion of textures stored in the shared memory, and also handling
// invalidation.
//
// Mipmaps are treated the following way, according to the GPU hang message
// found in game executables explaining the valid usage of BaseAddress when
// streaming the largest LOD (it says games should not use 0 as the base address
// when the largest LOD isn't loaded, but rather, either allocate a valid
// address for it or make it the same as MipAddress):
// - If the texture has a base address, but no mip address, it's not mipmapped -
//   the host texture has only the largest level too.
// - If the texture has different non-zero base address and mip address, a host
//   texture with mip_max_level+1 mipmaps is created - mip_min_level is ignored
//   and treated purely as sampler state because there are tfetch instructions
//   working directly with LOD values - including fetching with an explicit LOD.
//   However, the max level is not ignored because any mip count can be
//   specified when creating a texture, and another texture may be placed after
//   the last one.
// - If the texture has a mip address, but the base address is 0 or the same as
//   the mip address, a mipmapped texture is created, but min/max LOD is clamped
//   to the lower bound of 1 - the game is expected to do that anyway until the
//   largest LOD is loaded.
//   TODO(Triang3l): Check if there are any games with BaseAddress==MipAddress
//   but min or max LOD being 0, especially check Modern Warfare 2/3.
//   TODO(Triang3l): Attach the largest LOD to existing textures with a valid
//   MipAddress but no BaseAddress to save memory because textures are streamed
//   this way anyway.
class TextureCache {
 public:
  // Sampler parameters that can be directly converted to a host sampler or used
  // for binding hashing.
  union SamplerParameters {
    struct {
      ClampMode clamp_x : 3;         // 3
      ClampMode clamp_y : 3;         // 6
      ClampMode clamp_z : 3;         // 9
      BorderColor border_color : 2;  // 11
      // For anisotropic, these are true.
      uint32_t mag_linear : 1;       // 12
      uint32_t min_linear : 1;       // 13
      uint32_t mip_linear : 1;       // 14
      AnisoFilter aniso_filter : 3;  // 17
      uint32_t mip_min_level : 4;    // 21
      uint32_t mip_max_level : 4;    // 25

      int32_t lod_bias : 10;  // 42
    };
    uint64_t value;

    // Clearing the unused bits.
    SamplerParameters() : value(0) {}
    SamplerParameters(const SamplerParameters& parameters)
        : value(parameters.value) {}
    SamplerParameters& operator=(const SamplerParameters& parameters) {
      value = parameters.value;
      return *this;
    }
    bool operator==(const SamplerParameters& parameters) const {
      return value == parameters.value;
    }
    bool operator!=(const SamplerParameters& parameters) const {
      return value != parameters.value;
    }
  };

  TextureCache(D3D12CommandProcessor* command_processor,
               RegisterFile* register_file, SharedMemory* shared_memory);
  ~TextureCache();

  bool Initialize();
  void Shutdown();
  void ClearCache();

  void TextureFetchConstantWritten(uint32_t index);

  void BeginFrame();
  void EndFrame();

  // Must be called within a frame - creates and untiles textures needed by
  // shaders and puts them in the SRV state. This may bind compute pipelines
  // (notifying the command processor about that), so this must be called before
  // binding the actual drawing pipeline.
  void RequestTextures(uint32_t used_vertex_texture_mask,
                       uint32_t used_pixel_texture_mask);

  // Returns the hash of the current bindings (must be called after
  // RequestTextures) for the provided SRV descriptor layout.
  uint64_t GetDescriptorHashForActiveTextures(
      const D3D12Shader::TextureSRV* texture_srvs,
      uint32_t texture_srv_count) const;

  void WriteTextureSRV(const D3D12Shader::TextureSRV& texture_srv,
                       D3D12_CPU_DESCRIPTOR_HANDLE handle);

  SamplerParameters GetSamplerParameters(
      const D3D12Shader::SamplerBinding& binding) const;
  void WriteSampler(SamplerParameters parameters,
                    D3D12_CPU_DESCRIPTOR_HANDLE handle) const;

  void MarkRangeAsResolved(uint32_t start_unscaled, uint32_t length_unscaled);
  static inline DXGI_FORMAT GetResolveDXGIFormat(TextureFormat format) {
    return host_formats_[uint32_t(format)].dxgi_format_resolve_tile;
  }
  // The source buffer must be in the non-pixel-shader SRV state.
  bool TileResolvedTexture(TextureFormat format, uint32_t texture_base,
                           uint32_t texture_pitch, uint32_t offset_x,
                           uint32_t offset_y, uint32_t resolve_width,
                           uint32_t resolve_height, Endian128 endian,
                           ID3D12Resource* buffer, uint32_t buffer_size,
                           const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint);

  inline bool IsResolutionScale2X() const {
    return scaled_resolve_buffer_ != nullptr;
  }
  ID3D12Resource* GetScaledResolveBuffer() const {
    return scaled_resolve_buffer_;
  }
  // Ensures the buffer tiles backing the range are resident.
  bool EnsureScaledResolveBufferResident(uint32_t start_unscaled,
                                         uint32_t length_unscaled);
  void UseScaledResolveBufferForReading();
  void UseScaledResolveBufferForWriting();
  // Can't address more than 512 MB on Nvidia, so an offset is required.
  void CreateScaledResolveBufferRawSRV(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                       uint32_t first_unscaled_4kb_page,
                                       uint32_t unscaled_4kb_page_count);
  void CreateScaledResolveBufferRawUAV(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                       uint32_t first_unscaled_4kb_page,
                                       uint32_t unscaled_4kb_page_count);

  bool RequestSwapTexture(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                          TextureFormat& format_out);

 private:
  enum class LoadMode {
    k8bpb,
    k16bpb,
    k32bpb,
    k64bpb,
    k128bpb,
    kR11G11B10ToRGBA16,
    kR11G11B10ToRGBA16SNorm,
    kR10G11B11ToRGBA16,
    kR10G11B11ToRGBA16SNorm,
    kDXT1ToRGBA8,
    kDXT3ToRGBA8,
    kDXT5ToRGBA8,
    kDXNToRG8,
    kDXT3A,
    kDXT5AToR8,
    kCTX1,
    kDepthUnorm,
    kDepthFloat,

    kCount,

    kUnknown = kCount
  };

  struct LoadModeInfo {
    const void* shader;
    size_t shader_size;
    // Optional shader for loading 2x-scaled resolve targets.
    const void* shader_2x;
    size_t shader_2x_size;
  };

  // Tiling modes for storing textures after resolving - needed only for the
  // formats that can be resolved to.
  enum class ResolveTileMode {
    k8bpp,
    k16bpp,
    k32bpp,
    k64bpp,
    k128bpp,
    // B5G5R5A1 and B4G4R4A4 are optional in DXGI for render targets, and aren't
    // supported on Nvidia.
    k16bppRGBA,
    kR11G11B10AsRGBA16,
    kR10G11B11AsRGBA16,

    kCount,

    kUnknown = kCount
  };

  struct ResolveTileModeInfo {
    const void* shader;
    size_t shader_size;
    // DXGI_FORMAT_UNKNOWN for ByteAddressBuffer (usable only for textures with
    // texels that are 32-bit or larger).
    DXGI_FORMAT typed_uav_format;
    // For typed UAVs, log2 of the number of bytes in each UAV texel.
    uint32_t uav_texel_size_log2;
  };

  struct HostFormat {
    // Format info for the regular case.
    // DXGI format (typeless when different signedness or number representation
    // is used) for the texture resource.
    DXGI_FORMAT dxgi_format_resource;
    // DXGI format for unsigned normalized or unsigned/signed float SRV.
    DXGI_FORMAT dxgi_format_unorm;
    // The regular load mode, used when special modes (like signed-specific or
    // decompressing) aren't needed.
    LoadMode load_mode;
    // DXGI format for signed normalized or unsigned/signed float SRV.
    DXGI_FORMAT dxgi_format_snorm;
    // If the signed version needs a different bit representation on the host,
    // this is the load mode for the signed version. Otherwise the regular
    // load_mode will be used for the signed version, and a single copy will be
    // created if both unsigned and signed are used.
    LoadMode load_mode_snorm;

    // Do NOT add integer DXGI formats to this - they are not filterable, can
    // only be read with Load, not Sample! If any game is seen using num_format
    // 1 for fixed-point formats (for floating-point, it's normally set to 1
    // though), add a constant buffer containing multipliers for the
    // textures and multiplication to the tfetch implementation.

    // Uncompression info for when the regular host format for this texture is
    // block-compressed, but the size is not block-aligned, and thus such
    // texture cannot be created in Direct3D on PC and needs decompression,
    // however, such textures are common, for instance, in Halo 3. This only
    // supports unsigned normalized formats - let's hope GPUSIGN_SIGNED was not
    // used for DXN and DXT5A.
    DXGI_FORMAT dxgi_format_uncompressed;
    LoadMode decompress_mode;

    // For writing textures after resolving render targets. The format itself
    // must be renderable, because resolving is done by drawing a quad into a
    // texture of this format.
    DXGI_FORMAT dxgi_format_resolve_tile;
    ResolveTileMode resolve_tile_mode;

    // Whether the red component must be replicated in the SRV swizzle, for
    // single-component formats. At least for DXT3A/DXT5A, this is according to
    // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
    // k_8 is also used with RGBA swizzle, but assumes replicated components, in
    // Halo 3 sprites, thus it appears that all single-component formats should
    // have RRRR swizzle.
    bool replicate_component;
  };

  union TextureKey {
    struct {
      // Physical 4 KB page with the base mip level, disregarding A/C/E address
      // range prefix.
      uint32_t base_page : 17;  // 17 total
      Dimension dimension : 2;  // 19
      uint32_t width : 13;      // 32

      uint32_t height : 13;      // 45
      uint32_t tiled : 1;        // 46
      uint32_t packed_mips : 1;  // 47
      // Physical 4 KB page with mip 1 and smaller.
      uint32_t mip_page : 17;  // 64

      // Layers for stacked and 3D, 6 for cube, 1 for other dimensions.
      uint32_t depth : 10;         // 74
      uint32_t mip_max_level : 4;  // 78
      TextureFormat format : 6;    // 84
      Endian endianness : 2;       // 86
      // Whether this texture is signed and has a different host representation
      // than an unsigned view of the same guest texture.
      uint32_t signed_separate : 1;  // 87
      // Whether this texture is a 2x-scaled resolve target.
      uint32_t scaled_resolve : 1;  // 88
    };
    struct {
      // The key used for unordered_multimap lookup. Single uint32_t instead of
      // a uint64_t so XXH hash can be calculated in a stable way due to no
      // padding.
      uint32_t map_key[2];
      // The key used to identify one texture within unordered_multimap buckets.
      uint32_t bucket_key;
    };
    TextureKey() { MakeInvalid(); }
    TextureKey(const TextureKey& key) {
      SetMapKey(key.GetMapKey());
      bucket_key = key.bucket_key;
    }
    TextureKey& operator=(const TextureKey& key) {
      SetMapKey(key.GetMapKey());
      bucket_key = key.bucket_key;
      return *this;
    }
    bool operator==(const TextureKey& key) const {
      return GetMapKey() == key.GetMapKey() && bucket_key == key.bucket_key;
    }
    bool operator!=(const TextureKey& key) const {
      return GetMapKey() != key.GetMapKey() || bucket_key != key.bucket_key;
    }
    inline uint64_t GetMapKey() const {
      return uint64_t(map_key[0]) | (uint64_t(map_key[1]) << 32);
    }
    inline void SetMapKey(uint64_t key) {
      map_key[0] = uint32_t(key);
      map_key[1] = uint32_t(key >> 32);
    }
    inline bool IsInvalid() const {
      // Zero base and zero width is enough for a binding to be invalid.
      return map_key[0] == 0;
    }
    inline void MakeInvalid() {
      // Reset all for a stable hash.
      SetMapKey(0);
      bucket_key = 0;
    }
  };

  struct Texture {
    TextureKey key;
    ID3D12Resource* resource;
    D3D12_RESOURCE_STATES state;

    // Byte size of the top guest mip level.
    uint32_t base_size;
    // Byte size of mips between 1 and key.mip_max_level, containing all array
    // slices.
    uint32_t mip_size;
    // Offsets of all the array slices on a mip level relative to mips_address
    // (0 for mip 0, it's relative to base_address then, and for mip 1).
    uint32_t mip_offsets[14];
    // Byte sizes of an array slice on each mip level.
    uint32_t slice_sizes[14];
    // Row pitches on each mip level (for linear layout mainly).
    uint32_t pitches[14];

    // SRV descriptor from the cache, for the first swizzle the texture was used
    // with (which is usually determined by the format, such as RGBA or BGRA).
    D3D12_CPU_DESCRIPTOR_HANDLE cached_srv_descriptor;
    uint32_t cached_srv_descriptor_swizzle;

    // Watch handles for the memory ranges (protected by the shared memory watch
    // mutex).
    SharedMemory::WatchHandle base_watch_handle;
    SharedMemory::WatchHandle mip_watch_handle;
    // Whether the recent base level data has been loaded from the memory
    // (protected by the shared memory watch mutex).
    bool base_in_sync;
    // Whether the recent mip data has been loaded from the memory (protected by
    // the shared memory watch mutex).
    bool mips_in_sync;
  };

  struct SRVDescriptorCachePage {
    static constexpr uint32_t kHeapSize = 65536;
    ID3D12DescriptorHeap* heap;
    D3D12_CPU_DESCRIPTOR_HANDLE heap_start;
    uint32_t current_usage;
  };

  struct LoadConstants {
    // vec4 0.
    uint32_t guest_base;
    // For linear textures - row byte pitch.
    uint32_t guest_pitch;
    uint32_t host_base;
    uint32_t host_pitch;

    // vec4 1.
    uint32_t size_texels[3];
    uint32_t is_3d;

    // vec4 2.
    uint32_t size_blocks[3];
    uint32_t endianness;

    // vec4 3.
    // Block-aligned and, for mipmaps, power-of-two-aligned width and height.
    uint32_t guest_storage_width_height[2];
    uint32_t guest_format;
    uint32_t padding_3;

    // vec4 4.
    // Offset within the packed mip for small mips.
    uint32_t guest_mip_offset[3];

    static constexpr uint32_t kGuestPitchTiled = UINT32_MAX;
  };

  struct ResolveTileConstants {
    // Either from the start of the shared memory or from the start of the typed
    // UAV, in bytes (even for typed UAVs, so XeTextureTiledOffset2D is easier
    // to use).
    uint32_t guest_base;
    // 0:2 - endianness (up to Xin128).
    // 3:8 - guest format (primarily for 16-bit textures).
    // 10:31 - actual guest texture width.
    uint32_t info;
    // Origin of the written data in the destination texture. X in the lower 16
    // bits, Y in the upper.
    uint32_t offset;
    // Size to copy, texels with index bigger than this won't be written.
    // Width in the lower 16 bits, height in the upper.
    uint32_t size;
    // Byte offset to the first texel from the beginning of the source buffer.
    uint32_t host_base;
    // Row pitch of the source buffer.
    uint32_t host_pitch;
  };

  struct TextureBinding {
    TextureKey key;
    uint32_t swizzle;
    // Whether the fetch has unsigned/biased/gamma components.
    bool has_unsigned;
    // Whether the fetch has signed components.
    bool has_signed;
    // Unsigned version of the texture (or signed if they have the same data).
    Texture* texture;
    // Signed version of the texture if the data in the signed version is
    // different on the host.
    Texture* texture_signed;
  };

  // Whether the signed version of the texture has a different representation on
  // the host than its unsigned version (for example, if it's a fixed-point
  // texture emulated with a larger host pixel format).
  static inline bool IsSignedVersionSeparate(TextureFormat format) {
    const HostFormat& host_format = host_formats_[uint32_t(format)];
    return host_format.load_mode_snorm != LoadMode::kUnknown &&
           host_format.load_mode_snorm != host_format.load_mode;
  }
  // Whether decompression is needed on the host (Direct3D only allows creation
  // of block-compressed textures with 4x4-aligned dimensions on PC).
  static bool IsDecompressionNeeded(TextureFormat format, uint32_t width,
                                    uint32_t height);
  static inline DXGI_FORMAT GetDXGIResourceFormat(TextureFormat format,
                                                  uint32_t width,
                                                  uint32_t height) {
    const HostFormat& host_format = host_formats_[uint32_t(format)];
    return IsDecompressionNeeded(format, width, height)
               ? host_format.dxgi_format_uncompressed
               : host_format.dxgi_format_resource;
  }
  static inline DXGI_FORMAT GetDXGIResourceFormat(TextureKey key) {
    return GetDXGIResourceFormat(key.format, key.width, key.height);
  }
  static inline DXGI_FORMAT GetDXGIUnormFormat(TextureFormat format,
                                               uint32_t width,
                                               uint32_t height) {
    const HostFormat& host_format = host_formats_[uint32_t(format)];
    return IsDecompressionNeeded(format, width, height)
               ? host_format.dxgi_format_uncompressed
               : host_format.dxgi_format_unorm;
  }
  static inline DXGI_FORMAT GetDXGIUnormFormat(TextureKey key) {
    return GetDXGIUnormFormat(key.format, key.width, key.height);
  }

  static LoadMode GetLoadMode(TextureKey key);

  // Converts a texture fetch constant to a texture key, normalizing and
  // validating the values, or creating an invalid key, and also gets the
  // swizzle and used signedness.
  static void BindingInfoFromFetchConstant(
      const xenos::xe_gpu_texture_fetch_t& fetch, TextureKey& key_out,
      uint32_t* swizzle_out, bool* has_unsigned_out, bool* has_signed_out);

  static void LogTextureKeyAction(TextureKey key, const char* action);
  static void LogTextureAction(const Texture* texture, const char* action);

  // Returns nullptr if the key is not supported, but also if couldn't create
  // the texture - if it's nullptr, occasionally a recreation attempt should be
  // made.
  Texture* FindOrCreateTexture(TextureKey key);

  // Writes data from the shared memory to the texture. This binds pipelines,
  // allocates descriptors and copies!
  bool LoadTextureData(Texture* texture);

  // Shared memory callback for texture data invalidation.
  static void WatchCallbackThunk(void* context, void* data, uint64_t argument,
                                 bool invalidated_by_gpu);
  void WatchCallback(Texture* texture, bool is_mip);

  // Makes all bindings invalid. Also requesting textures after calling this
  // will cause another attempt to create a texture or to untile it if there was
  // an error.
  void ClearBindings();

  // Checks if there are any pages that contain scaled resolve data within the
  // range.
  bool IsRangeScaledResolved(uint32_t start_unscaled, uint32_t length_unscaled);
  // Global shared memory invalidation callback for invalidating scaled resolved
  // texture data.
  static void ScaledResolveGlobalWatchCallbackThunk(void* context,
                                                    uint32_t address_first,
                                                    uint32_t address_last,
                                                    bool invalidated_by_gpu);
  void ScaledResolveGlobalWatchCallback(uint32_t address_first,
                                        uint32_t address_last,
                                        bool invalidated_by_gpu);

  static const HostFormat host_formats_[64];

  static const char* const dimension_names_[4];

  D3D12CommandProcessor* command_processor_;
  RegisterFile* register_file_;
  SharedMemory* shared_memory_;

  static const LoadModeInfo load_mode_info_[];
  ID3D12RootSignature* load_root_signature_ = nullptr;
  ID3D12PipelineState* load_pipelines_[size_t(LoadMode::kCount)] = {};
  // Load pipelines for 2x-scaled resolved targets.
  ID3D12PipelineState* load_pipelines_2x_[size_t(LoadMode::kCount)] = {};
  static const ResolveTileModeInfo resolve_tile_mode_info_[];
  ID3D12RootSignature* resolve_tile_root_signature_ = nullptr;
  ID3D12PipelineState*
      resolve_tile_pipelines_[size_t(ResolveTileMode::kCount)] = {};

  std::unordered_multimap<uint64_t, Texture*> textures_;

  std::vector<SRVDescriptorCachePage> srv_descriptor_cache_;

  enum class NullSRVDescriptorIndex {
    k2DArray,
    k3D,
    kCube,

    kCount,
  };
  // Contains null SRV descriptors of dimensions from NullSRVDescriptorIndex.
  // For copying, not shader-visible.
  ID3D12DescriptorHeap* null_srv_descriptor_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE null_srv_descriptor_heap_start_;

  TextureBinding texture_bindings_[32] = {};
  // Bit vector with bits reset on fetch constant writes to avoid getting
  // texture keys from the fetch constants again and again.
  uint32_t texture_keys_in_sync_ = 0;

  // Whether a texture has been invalidated (a watch has been triggered), so
  // need to try to reload textures, disregarding whether fetch constants have
  // been changed.
  std::atomic<bool> texture_invalidated_ = false;

  // Unsupported texture formats used during this frame (for research and
  // testing).
  enum : uint8_t {
    kUnsupportedResourceBit = 1,
    kUnsupportedUnormBit = kUnsupportedResourceBit << 1,
    kUnsupportedSnormBit = kUnsupportedUnormBit << 1,
  };
  uint8_t unsupported_format_features_used_[64];

  // The 2 GB tiled buffer for resolved data with 2x resolution scale.
  static constexpr uint32_t kScaledResolveBufferSizeLog2 = 31;
  static constexpr uint32_t kScaledResolveBufferSize =
      1u << kScaledResolveBufferSizeLog2;
  ID3D12Resource* scaled_resolve_buffer_ = nullptr;
  D3D12_RESOURCE_STATES scaled_resolve_buffer_state_ =
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  // Not very big heaps (16 MB) because they are needed pretty sparsely. One
  // scaled 1280x720x32bpp texture is slighly bigger than 14 MB.
  static constexpr uint32_t kScaledResolveHeapSizeLog2 = 24;
  static constexpr uint32_t kScaledResolveHeapSize =
      1 << kScaledResolveHeapSizeLog2;
  static_assert(
      (kScaledResolveHeapSize % D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES) == 0,
      "Scaled resolve heap size must be a multiple of Direct3D tile size");
  // Resident portions of the tiled buffer.
  ID3D12Heap* scaled_resolve_heaps_[kScaledResolveBufferSize >>
                                    kScaledResolveHeapSizeLog2] = {};
  // Number of currently resident portions of the tiled buffer, for profiling.
  uint32_t scaled_resolve_heap_count_ = 0;
  // Bit vector storing whether each 4 KB physical memory page contains scaled
  // resolve data. uint32_t rather than uint64_t because parts of it are sent to
  // shaders.
  // PROTECTED BY THE SHARED MEMORY WATCH MUTEX!
  uint32_t* scaled_resolve_pages_ = nullptr;
  // Second level of the bit vector for faster rejection of non-scaled textures.
  // PROTECTED BY THE SHARED MEMORY WATCH MUTEX!
  uint64_t scaled_resolve_pages_l2_[(512 << 20) >> (12 + 5 + 6)];
  // Global watch for scaled resolve data invalidation.
  SharedMemory::GlobalWatchHandle scaled_resolve_global_watch_handle_ = nullptr;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_TEXTURE_CACHE_H_

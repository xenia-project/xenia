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
#include <unordered_map>

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
  TextureCache(D3D12CommandProcessor* command_processor,
               RegisterFile* register_file, SharedMemory* shared_memory);
  ~TextureCache();

  bool Initialize();
  void Shutdown();
  void ClearCache();

  void TextureFetchConstantWritten(uint32_t index);

  void BeginFrame();

  // Must be called within a frame - creates and untiles textures needed by
  // shaders and puts them in the SRV state. This may bind compute pipelines
  // (notifying the command processor about that), so this must be called before
  // binding the actual drawing pipeline.
  void RequestTextures(uint32_t used_vertex_texture_mask,
                       uint32_t used_pixel_texture_mask);

  void WriteTextureSRV(uint32_t fetch_constant,
                       TextureDimension shader_dimension,
                       D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void WriteSampler(uint32_t fetch_constant, TextureFilter mag_filter,
                    TextureFilter min_filter, TextureFilter mip_filter,
                    AnisoFilter aniso_filter,
                    D3D12_CPU_DESCRIPTOR_HANDLE handle);

  static DXGI_FORMAT GetResolveDXGIFormat(TextureFormat format);
  // The source buffer must be in the non-pixel-shader SRV state.
  bool TileResolvedTexture(TextureFormat format, uint32_t texture_base,
                           uint32_t texture_pitch, uint32_t texture_height,
                           uint32_t resolve_width, uint32_t resolve_height,
                           Endian128 endian, ID3D12Resource* buffer,
                           uint32_t buffer_size,
                           const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint);

  bool RequestSwapTexture(D3D12_CPU_DESCRIPTOR_HANDLE handle);

 private:
  enum class LoadMode {
    k8bpb,
    k16bpb,
    k32bpb,
    k64bpb,
    k128bpb,
    kDXT3A,
    kCTX1,
    kDepthUnorm,
    kDepthFloat,

    kCount,

    kUnknown = kCount
  };

  struct LoadModeInfo {
    const void* shader;
    size_t shader_size;
  };

  // Tiling modes for storing textures after resolving - needed only for the
  // formats that can be resolved to.
  enum class TileMode {
    k32bpp,
    k64bpp,

    kCount,

    kUnknown = kCount
  };

  struct TileModeInfo {
    const void* shader;
    size_t shader_size;
  };

  struct HostFormat {
    DXGI_FORMAT dxgi_format;
    LoadMode load_mode;
    TileMode tile_mode;
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
    inline bool IsInvalid() {
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
    // Byte size of one array slice of the top guest mip level.
    uint32_t base_slice_size;
    // Byte size of the top guest mip level.
    uint32_t base_size;
    // Byte size of one array slice of mips between 1 and key.mip_max_level.
    uint32_t mip_slice_size;
    // Byte size of mips between 1 and key.mip_max_level.
    uint32_t mip_size;
    // Byte offsets of each mipmap within one slice.
    uint32_t mip_offsets[14];
    // Byte pitches of each mipmap within one slice (for linear layout mainly).
    uint32_t mip_pitches[14];

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
    // Offset within the packed mip for small mips.
    uint32_t guest_mip_offset[3];

    static constexpr uint32_t kGuestPitchTiled = UINT32_MAX;
  };

  struct TileConstants {
    // Either from the start of the shared memory or from the start of the typed
    // UAV, in bytes.
    uint32_t guest_base;
    // 0:2 - endianness (up to Xin128).
    // 3:31 - actual guest texture width.
    uint32_t endian_guest_pitch;
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
    Texture* texture;
  };

  // Converts a texture fetch constant to a texture key, normalizing and
  // validating the values, or creating an invalid key.
  static void TextureKeyFromFetchConstant(
      const xenos::xe_gpu_texture_fetch_t& fetch, TextureKey& key_out,
      uint32_t& swizzle_out);

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
  static void WatchCallbackThunk(void* context, void* data, uint64_t argument);
  void WatchCallback(Texture* texture, bool is_mip);

  // Makes all bindings invalid. Also requesting textures after calling this
  // will cause another attempt to create a texture or to untile it if there was
  // an error.
  void ClearBindings();

  static const HostFormat host_formats_[64];

  static const char* const dimension_names_[4];

  D3D12CommandProcessor* command_processor_;
  RegisterFile* register_file_;
  SharedMemory* shared_memory_;

  static const LoadModeInfo load_mode_info_[];
  ID3D12RootSignature* load_root_signature_ = nullptr;
  ID3D12PipelineState* load_pipelines_[size_t(LoadMode::kCount)] = {};
  static const TileModeInfo tile_mode_info_[];
  ID3D12RootSignature* tile_root_signature_ = nullptr;
  ID3D12PipelineState* tile_pipelines_[size_t(TileMode::kCount)] = {};

  std::unordered_multimap<uint64_t, Texture*> textures_;

  TextureBinding texture_bindings_[32] = {};
  // Bit vector with bits reset on fetch constant writes to avoid getting
  // texture keys from the fetch constants again and again.
  uint32_t texture_keys_in_sync_ = 0;

  // Whether a texture has been invalidated (a watch has been triggered), so
  // need to try to reload textures, disregarding whether fetch constants have
  // been changed. A simple notification (texture validity is protected by a
  // mutex), so memory_order_relaxed is enough.
  std::atomic<bool> texture_invalidated_ = false;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_TEXTURE_CACHE_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TEXTURE_CACHE_H_
#define XENIA_GPU_TEXTURE_CACHE_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "xenia/base/assert.h"
#include "xenia/base/hash.h"
#include "xenia/base/math.h"
#include "xenia/base/mutex.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shared_memory.h"
#include "xenia/gpu/texture_util.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {

// Manages host copies of guest textures, performing untiling, format and endian
// conversion of textures stored in the shared memory, and also handling
// invalidation.
//
// Mipmaps are treated the following way, according to the GPU hang message
// found in game executables explaining the valid usage of BaseAddress when
// streaming the largest LOD (it says games should not use 0 as the base address
// when the largest LOD isn't loaded, but rather, either allocate a valid
// address for it or make it the same as mip_address):
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
// TODO(Triang3l): Attach the largest LOD to existing textures with a valid
// mip_address but no base ever used yet (no base_address) to save memory
// because textures are streamed this way anyway.
class TextureCache {
 public:
  // Hard limit, originating from the half-pixel offset filling hack in the
  // resolve shaders only filling up to 3 pixels, due to the bit counts used for
  // passing the scale to shaders, and because the full 490 MB EDRAM buffer is
  // within the minimum Direct3D 12 requirement of 128 * 2^20 texels in a single
  // buffer binding (counted as R32 for a byte address buffer).
  static constexpr uint32_t kMaxDrawResolutionScaleAlongAxis = 7;

  TextureCache(const TextureCache& texture_cache) = delete;
  TextureCache& operator=(const TextureCache& texture_cache) = delete;
  virtual ~TextureCache();

  // Returns whether the actual scale is not smaller than the requested one.
  static bool GetConfigDrawResolutionScale(uint32_t& x_out, uint32_t& y_out);
  uint32_t draw_resolution_scale_x() const { return draw_resolution_scale_x_; }
  uint32_t draw_resolution_scale_y() const { return draw_resolution_scale_y_; }

  divisors::MagicDiv draw_resolution_scale_x_divisor() const {
    return draw_resolution_scale_x_divisor_;
  }
  divisors::MagicDiv draw_resolution_scale_y_divisor() const {
    return draw_resolution_scale_y_divisor_;
  }

  bool IsDrawResolutionScaled() const {
    return draw_resolution_scale_x_ > 1 || draw_resolution_scale_y_ > 1;
  }

  virtual void ClearCache();

  virtual void CompletedSubmissionUpdated(uint64_t completed_submission_index);
  virtual void BeginSubmission(uint64_t new_submission_index);
  virtual void BeginFrame();

  void MarkRangeAsResolved(uint32_t start_unscaled, uint32_t length_unscaled);
  // Ensures the memory backing the range in the scaled resolve address space is
  // allocated and returns whether it is.
  virtual bool EnsureScaledResolveMemoryCommitted(
      uint32_t start_unscaled, uint32_t length_unscaled,
      uint32_t length_scaled_alignment_log2 = 0) {
    return false;
  }

  static uint32_t GuestToHostSwizzle(uint32_t guest_swizzle,
                                     uint32_t host_format_swizzle);

  void TextureFetchConstantWritten(uint32_t index) {
    texture_bindings_in_sync_ &= ~(UINT32_C(1) << index);
  }
  void TextureFetchConstantsWritten(uint32_t first_index, uint32_t last_index) {
    // generate a mask of all bits from before the first index, and xor it with
    // all bits before the last index this produces a mask covering only the
    // bits between first and last
    uint32_t res = ((1U << first_index) - 1) ^ static_cast<uint32_t>((1ULL << (last_index + 1)) - 1ULL);
    // todo: check that this is right

    texture_bindings_in_sync_ &= ~res;
  }

  virtual void RequestTextures(uint32_t used_texture_mask);

  // "ActiveTexture" means as of the latest RequestTextures call.

  uint32_t GetActiveTextureHostSwizzle(uint32_t fetch_constant_index) const {
    const TextureBinding* binding =
        GetValidTextureBinding(fetch_constant_index);
    return binding ? binding->host_swizzle : xenos::XE_GPU_TEXTURE_SWIZZLE_0000;
  }
  uint8_t GetActiveTextureSwizzledSigns(uint32_t fetch_constant_index) const {
    const TextureBinding* binding =
        GetValidTextureBinding(fetch_constant_index);
    return binding ? binding->swizzled_signs : kSwizzledSignsUnsigned;
  }
  bool IsActiveTextureResolved(uint32_t fetch_constant_index) const {
    const TextureBinding* binding =
        GetValidTextureBinding(fetch_constant_index);
    if (!binding) {
      return false;
    }
    return (binding->texture && binding->texture->IsResolved()) ||
           (binding->texture_signed && binding->texture_signed->IsResolved());
  }
  template <swcache::PrefetchTag tag>
  void PrefetchTextureBinding(uint32_t fetch_constant_index) const {
    swcache::Prefetch<tag>(&texture_bindings_[fetch_constant_index]);
    swcache::Prefetch<tag>(
        &texture_bindings_[fetch_constant_index +
                           1]);  // we may cross a cache line boundary :( size
                                 // of the structure is 0x28
  }

 protected:
  struct TextureKey {
    // Dimensions minus 1 are stored similarly to how they're stored in fetch
    // constants so fewer bits can be used, while the maximum size (8192 for 2D)
    // can still be encoded (a 8192x sky texture is used in 4D530910).

    // Physical 4 KB page with the base mip level, disregarding A/C/E address
    // range prefix.
    uint32_t base_page : 17;             // 17 total
    xenos::DataDimension dimension : 2;  // 19
    uint32_t width_minus_1 : 13;         // 32

    uint32_t height_minus_1 : 13;  // 45
    uint32_t tiled : 1;            // 46
    uint32_t packed_mips : 1;      // 47
    // Physical 4 KB page with mip 1 and smaller.
    uint32_t mip_page : 17;  // 64

    // (Layers for stacked and 3D, 6 for cube, 1 for other dimensions) - 1.
    uint32_t depth_or_array_size_minus_1 : 10;  // 74
    uint32_t pitch : 9;                         // 83
    uint32_t mip_max_level : 4;                 // 87
    xenos::TextureFormat format : 6;            // 93
    xenos::Endian endianness : 2;               // 95
    // Whether this texture is signed and has a different host representation
    // than an unsigned view of the same guest texture.
    uint32_t signed_separate : 1;  // 96

    // Whether this texture is a resolution-scaled resolve target.
    uint32_t scaled_resolve : 1;  // 97
    // Least important in ==, so placed last.
    uint32_t is_valid : 1;  // 98

    TextureKey() { MakeInvalid(); }
    TextureKey(const TextureKey& key) {
      std::memcpy(this, &key, sizeof(*this));
    }
    TextureKey& operator=(const TextureKey& key) {
      std::memcpy(this, &key, sizeof(*this));
      return *this;
    }
    void MakeInvalid() {
      // Zero everything, including the padding, for a stable hash.
      std::memset(this, 0, sizeof(*this));
    }

    using Hasher = xe::hash::XXHasher<TextureKey>;
    bool operator==(const TextureKey& key) const {
      return !std::memcmp(this, &key, sizeof(*this));
    }
    bool operator!=(const TextureKey& key) const { return !(*this == key); }

    uint32_t GetWidth() const { return width_minus_1 + 1; }
    uint32_t GetHeight() const { return height_minus_1 + 1; }
    uint32_t GetDepthOrArraySize() const {
      return depth_or_array_size_minus_1 + 1;
    }

    texture_util::TextureGuestLayout GetGuestLayout() const {
      return texture_util::GetGuestTextureLayout(
          dimension, pitch, GetWidth(), GetHeight(), GetDepthOrArraySize(),
          tiled, format, packed_mips, base_page != 0, mip_max_level);
    }

    static const char* GetLogDimensionName(xenos::DataDimension dimension);
    const char* GetLogDimensionName() const {
      return GetLogDimensionName(dimension);
    }
    void LogAction(const char* action) const;
  };

  class Texture {
   public:
    Texture(const Texture& texture) = delete;
    Texture& operator=(const Texture& texture) = delete;
    virtual ~Texture();

    TextureCache& texture_cache() const { return texture_cache_; }

    const TextureKey& key() const { return key_; }

    const texture_util::TextureGuestLayout& guest_layout() const {
      return guest_layout_;
    }
    uint32_t GetGuestBaseSize() const {
      return guest_layout().base.level_data_extent_bytes;
    }
    uint32_t GetGuestMipsSize() const {
      return guest_layout().mips_total_extent_bytes;
    }

    uint64_t GetHostMemoryUsage() const { return host_memory_usage_; }

    uint64_t last_usage_submission_index() const {
      return last_usage_submission_index_;
    }
    uint64_t last_usage_time() const { return last_usage_time_; }

    bool GetBaseResolved() const { return base_resolved_; }
    void SetBaseResolved(bool base_resolved) {
      assert_false(!base_resolved && key().scaled_resolve);
      base_resolved_ = base_resolved;
    }
    bool GetMipsResolved() const { return mips_resolved_; }
    void SetMipsResolved(bool mips_resolved) {
      assert_false(!mips_resolved && key().scaled_resolve);
      mips_resolved_ = mips_resolved;
    }
    bool IsResolved() const { return base_resolved_ || mips_resolved_; }

    bool base_outdated(const global_unique_lock_type& global_lock) const {
      return base_outdated_;
    }
    bool mips_outdated(const global_unique_lock_type& global_lock) const {
      return mips_outdated_;
    }
    void MakeUpToDateAndWatch(const global_unique_lock_type& global_lock);

    void WatchCallback(const global_unique_lock_type& global_lock, bool is_mip);

    // For LRU caching - updates the last usage frame and moves the texture to
    // the end of the usage queue. Must be called any time the texture is
    // referenced by any GPU work in the implementation to make sure it's not
    // destroyed while still in use.
    void MarkAsUsed();

    void LogAction(const char* action) const;

   protected:
    explicit Texture(TextureCache& texture_cache, const TextureKey& key);

    void SetHostMemoryUsage(uint64_t new_host_memory_usage) {
      texture_cache_.UpdateTexturesTotalHostMemoryUsage(new_host_memory_usage,
                                                        host_memory_usage_);
      host_memory_usage_ = new_host_memory_usage;
    }

   private:
    TextureCache& texture_cache_;

    TextureKey key_;

    texture_util::TextureGuestLayout guest_layout_;

    uint64_t host_memory_usage_ = 0;

    uint64_t last_usage_submission_index_;
    uint64_t last_usage_time_;
    Texture* used_previous_;
    Texture* used_next_;

    // Whether the most up-to-date base / mips contain pages with data from a
    // resolve operation (rather than from the CPU or memexport), primarily for
    // choosing between piecewise linear gamma and sRGB when the former is
    // emulated with the latter.
    bool base_resolved_;
    bool mips_resolved_;

    // These are to be accessed within the global critical region to synchronize
    // with shared memory.
    // Whether the recent base level data needs reloading from the memory.
    bool base_outdated_ = false;
    // Whether the recent mip data needs reloading from the memory.
    bool mips_outdated_ = false;
    // Watch handles for the memory ranges.
    SharedMemory::WatchHandle base_watch_handle_ = nullptr;
    SharedMemory::WatchHandle mips_watch_handle_ = nullptr;
  };

  // Rules of data access in load shaders:
  // - Source reading (from the shared memory or the scaled resolve buffer):
  //   - Guest data may be stored in a sparsely-allocated buffer, or, in
  //     Direct3D 12 terms, a tiled buffer. This means that some regions of the
  //     buffer may not be mapped. On tiled resources tier 1 hardware, accessing
  //     unmapped tiles results in undefined behavior, including a GPU page
  //     fault and device removal. So, shaders must not try to access
  //     potentially unmapped regions (that are outside the texture memory
  //     extents calculated on the CPU, taking into account that Xenia can't
  //     overestimate texture sizes freely since it must not try to upload
  //     unallocated pages on the CPU).
  //   - Buffer tiles have 64 KB size on Direct3D 12. Vulkan has its own
  //     alignment requirements for sparse binding. But overall, we're
  //     allocating pretty large regions.
  //   - Resolution scaling disabled:
  //     - Shared memory allocates regions of power of two sizes that map
  //       directly to the same portions of the 512 MB of the console's
  //       physical memory. So, a 64 KB-aligned host buffer region is also 64
  //       KB-aligned in the guest address space.
  //     - Tiled textures: 32x32x4-block tiles are always resident each as a
  //       whole. If the width is bigger than the pitch, the overflowing 32x32x4
  //       tiles are also loaded as entire tiles. We do not have separate
  //       shaders for 2D and 3D. So, for tiled textures, it's safe to consider
  //       that if any location within a 32x32-aligned portion is within the
  //       texture bounds, the entire 32x32 portion also can be read.
  //     - Linear textures: Pitch is aligned to 256 bytes. Row count, however,
  //       is not aligned to anything (unless the mip tail is being loaded). The
  //       overflowing last row in case `width > pitch`, however, is made
  //       resident up to the last texel in it. But row start alignment is 256,
  //       which is a power of two, and is smaller than the Direct3D 12 tile
  //       size of 64 KB. So, if any block within a 256-aligned region is within
  //       the texture bounds, without resolution scaling, reading from any
  //       location in that 256-aligned region is safe.
  //     - Since we use the same shaders for tiled and linear textures (as well
  //       as 1D textures), this means that without resolution scaling, it's
  //       safe to access a min(256 bytes, 32 blocks)-aligned portion along X,
  //       but only within the same row of blocks, with bounds checking only for
  //       such portion as a whole, but without additional bounds checking
  //       inside of it.
  //     - Therefore, it's recommended that shaders read power-of-two amounts of
  //       blocks (so there will naturally be some alignment to some power of
  //       two), and this way, each thread may read at most 16 16bpb blocks or
  //       at most 32 8bpb or smaller blocks with in a single `if (x < width)`
  //       for the whole aligned range of the same length.
  //   - Resolution scaling enabled:
  //     - For simplicity, unlike in the shared memory, buffer tile boundaries
  //       are not aligned to powers of 2 the same way as guest addresses are.
  //       While for 2x2 resolution scaling it still happens to be the case
  //       because `host scaling unit address = guest scaling unit address << 2`
  //       (similarly for 2x1 and 1x2), for 3x or x3, it's not - a 64 KB host
  //       tile would represent 7281.777 guest bytes with 3x3 (disregarding that
  //       sequences of texels that are adjacent in memory alongside the
  //       horizontal axis, not individual bytes, are scaled, but even in that
  //       case it's not scaling by 2^n still).
  //     - The above would affect the `width > pitch` case for linear textures,
  //       requiring overestimating the width in calculation of the range of the
  //       tiles to map, while not doing this overestimation on the guest memory
  //       extent calculation side (otherwise it may result in attempting to
  //       upload unallocated memory on the CPU). For example, let's take look
  //       at an extreme case of a 369x28 k_8 texture with a pitch of 256 bytes.
  //       The last row, in guest memory, would be loaded from the [7168, 7281)
  //       range, or, with 3x3 resolution scaling, from bytes [64512, 65529).
  //       However, if we try to unconditionally load 2 pixels, like the texture
  //       is 370x28, we will be accessing the bytes [64512, 65538). But bytes
  //       65536 and 65537 will be in another 64 KB tile, which may be not
  //       mapped yet. However, none of this is an issue for one simple reason -
  //       resolving is only possible to tiled textures, so linear textures will
  //       never be resolution-scaled.
  //     - Tiled textures have potentially referenced guest 32x32-block tiles
  //       loaded in their entirety. So, just like for unscaled textures, if any
  //       block within a tile is available, the entire tile is as well.
  // - Destination writing (to the linear buffer):
  //   - host_x_blocks_per_thread specifies how many pixels can be written
  //     without bounds checking within increments of that amount - the pitch of
  //     the destination buffer is manually overaligned if needed.

  // In textures, resolution scaling is done for 8-byte portions of memory for
  // 8bpp textures, and for 16-byte portions for textures of higher bit depths
  // (these are the sizes of regions where contiguous texels in memory are also
  // contiguous in the texture along the horizontal axis, so 64-bit and 128-bit
  // loads / stores, for 8bpp and 16bpp+ respectively, can be used for untiling
  // regardless of the resolution scale).

  struct LoadConstants {
    uint32_t is_tiled_3d_endian_scale;
    // Base offset in bytes, resolution-scaled.
    uint32_t guest_offset;
    // For tiled textures - row pitch in blocks, aligned to 32, unscaled.
    // For linear textures - row pitch in bytes.
    uint32_t guest_pitch_aligned;
    // For 3D textures only (ignored otherwise) - aligned to 32, unscaled.
    uint32_t guest_z_stride_block_rows_aligned;

    // - std140 vector boundary -

    // If this is a packed mip tail, this is aligned to tile dimensions.
    // Resolution-scaled.
    uint32_t size_blocks[3];
    // Base offset in bytes.
    uint32_t host_offset;

    // - std140 vector boundary -

    uint32_t host_pitch;
    uint32_t height_texels;
  };

  static constexpr uint32_t kLoadGuestXThreadsPerGroupLog2 = 2;
  static constexpr uint32_t kLoadGuestYBlocksPerGroupLog2 = 5;

  enum LoadShaderIndex {
    kLoadShaderIndex8bpb,
    kLoadShaderIndex16bpb,
    kLoadShaderIndex32bpb,
    kLoadShaderIndex64bpb,
    kLoadShaderIndex128bpb,
    kLoadShaderIndexR5G5B5A1ToB5G5R5A1,
    kLoadShaderIndexR5G6B5ToB5G6R5,
    kLoadShaderIndexR5G5B6ToB5G6R5WithRBGASwizzle,
    kLoadShaderIndexRGBA4ToBGRA4,
    kLoadShaderIndexRGBA4ToARGB4,
    kLoadShaderIndexGBGR8ToGRGB8,
    kLoadShaderIndexGBGR8ToRGB8,
    kLoadShaderIndexBGRG8ToRGBG8,
    kLoadShaderIndexBGRG8ToRGB8,
    kLoadShaderIndexR10G11B11ToRGBA16,
    kLoadShaderIndexR10G11B11ToRGBA16SNorm,
    kLoadShaderIndexR11G11B10ToRGBA16,
    kLoadShaderIndexR11G11B10ToRGBA16SNorm,
    kLoadShaderIndexR16UNormToFloat,
    kLoadShaderIndexR16SNormToFloat,
    kLoadShaderIndexRG16UNormToFloat,
    kLoadShaderIndexRG16SNormToFloat,
    kLoadShaderIndexRGBA16UNormToFloat,
    kLoadShaderIndexRGBA16SNormToFloat,
    kLoadShaderIndexDXT1ToRGBA8,
    kLoadShaderIndexDXT3ToRGBA8,
    kLoadShaderIndexDXT5ToRGBA8,
    kLoadShaderIndexDXNToRG8,
    kLoadShaderIndexDXT3A,
    kLoadShaderIndexDXT3AAs1111ToBGRA4,
    kLoadShaderIndexDXT3AAs1111ToARGB4,
    kLoadShaderIndexDXT5AToR8,
    kLoadShaderIndexCTX1,
    kLoadShaderIndexDepthUnorm,
    kLoadShaderIndexDepthFloat,

    kLoadShaderCount,
    kLoadShaderIndexUnknown = kLoadShaderCount,
  };

  struct LoadShaderInfo {
    // Log2 of the sizes, in bytes, of the elements in the source (guest) and
    // the destination (host) buffer bindings accessed by the copying shader,
    // since the shader may copy multiple blocks per one invocation.
    uint32_t source_bpe_log2;
    uint32_t dest_bpe_log2;
    // Number of bytes in a host resolution-scaled block (corresponding to a
    // guest block if not decompressing, or a host texel if decompressing)
    // written by the shader.
    uint32_t bytes_per_host_block;
    // Log2 of the number of guest resolution-scaled blocks along the X axis
    // loaded by a single thread shader group.
    uint32_t guest_x_blocks_per_thread_log2;

    uint32_t GetGuestXBlocksPerGroupLog2() const {
      return kLoadGuestXThreadsPerGroupLog2 + guest_x_blocks_per_thread_log2;
    }
  };

  static constexpr uint8_t kSwizzledSignsUnsigned =
      uint8_t(xenos::TextureSign::kUnsigned) * uint8_t(0b01010101);

  struct TextureBinding {
    TextureKey key;
    // Destination swizzle merged with guest to host format swizzle.
    uint32_t host_swizzle;
    // Packed TextureSign values, 2 bit per each component, with guest-side
    // destination swizzle from the fetch constant applied to them.
    uint8_t swizzled_signs;
    // Unsigned version of the texture (or signed if they have the same data).
    Texture* texture;
    // Signed version of the texture if the data in the signed version is
    // different on the host.
    Texture* texture_signed;

    TextureBinding() { Reset(); }

    void Reset() {
      std::memset(this, 0, sizeof(*this));
      host_swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_0000;
      swizzled_signs = kSwizzledSignsUnsigned;
    }
  };

  explicit TextureCache(const RegisterFile& register_file,
                        SharedMemory& shared_memory,
                        uint32_t draw_resolution_scale_x,
                        uint32_t draw_resolution_scale_y);

  const RegisterFile& register_file() const { return register_file_; }
  SharedMemory& shared_memory() const { return shared_memory_; }

  // May be called for purposes like clearing the cache, as well as in the
  // destructor of the implementation if textures, for instance, have references
  // to the implementation that are used in their destructor, and will become
  // invalid if the implementation is destroyed before the texture.
  void DestroyAllTextures(bool from_destructor = false);

  // Whether the signed version of the texture has a different representation on
  // the host than its unsigned version (for example, if it's a fixed-point
  // texture emulated with a larger host pixel format).
  virtual bool IsSignedVersionSeparateForFormat(TextureKey key) const {
    return false;
  }
  // Parameters like whether the texture is tiled and its dimensions are checked
  // externally, the implementation should take only format-related parameters
  // such as the format itself and the signedness into account.
  virtual bool IsScaledResolveSupportedForFormat(TextureKey key) const {
    return false;
  }
  // For formats with less than 4 components, implementations normally should
  // replicate the last component into the non-existent ones, similar to what is
  // done for unused components of operands in shaders by Microsoft's Xbox 360
  // shader compiler (.xxxx, .xyyy, .xyzz, .xyzw).
  // For DXT3A and DXT5A, RRRR swizzle is specified in:
  // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
  // 4D5307E6 also expects replicated components in k_8 sprites.
  // DXN is read as RG in 4D5307E6, but as RA in 415607E6.
  // TODO(Triang3l): Find out the correct contents of unused texture components.
  virtual uint32_t GetHostFormatSwizzle(TextureKey key) const = 0;

  virtual uint32_t GetMaxHostTextureWidthHeight(
      xenos::DataDimension dimension) const = 0;
  virtual uint32_t GetMaxHostTextureDepthOrArraySize(
      xenos::DataDimension dimension) const = 0;

  // The texture must be created exactly with this key (if the implementation
  // supports the texture with this key, otherwise, or in case of a runtime
  // failure, it should return nullptr), modifying it is not allowed.
  virtual std::unique_ptr<Texture> CreateTexture(TextureKey key) = 0;

  // Returns nullptr not only if the key is not supported, but also if couldn't
  // create the texture - if it's nullptr, occasionally a recreation attempt
  // should be made.
  Texture* FindOrCreateTexture(TextureKey key);

  static const LoadShaderInfo& GetLoadShaderInfo(
      LoadShaderIndex load_shader_index) {
    assert_true(load_shader_index < kLoadShaderCount);
    return load_shader_info_[load_shader_index];
  }
  bool LoadTextureData(Texture& texture);
  void LoadTexturesData(Texture** textures, uint32_t n_textures);
  // Writes the texture data (for base, mips or both - but not neither) from the
  // shared memory or the scaled resolve memory. The shared memory management is
  // done outside this function, the implementation just needs to load the data
  // into the texture object.
  virtual bool LoadTextureDataFromResidentMemoryImpl(Texture& texture,
                                                     bool load_base,
                                                     bool load_mips) = 0;

  // Converts a texture fetch constant to a texture key, normalizing and
  // validating the values, or creating an invalid key, and also gets the
  // post-guest-swizzle signedness.
  static void BindingInfoFromFetchConstant(
      const xenos::xe_gpu_texture_fetch_t& fetch, TextureKey& key_out,
      uint8_t* swizzled_signs_out);

  // Makes all texture bindings invalid. Also requesting textures after calling
  // this will cause another attempt to create a texture or to untile it if
  // there was an error.
  void ResetTextureBindings(bool from_destructor = false);

  const TextureBinding* GetValidTextureBinding(
      uint32_t fetch_constant_index) const {
    const TextureBinding& binding = texture_bindings_[fetch_constant_index];
    return binding.key.is_valid ? &binding : nullptr;
  }
  // Called when something in a texture binding is changed for the
  // implementation to update the internal dependencies of the binding.
  virtual void UpdateTextureBindingsImpl(uint32_t fetch_constant_mask) {}

 private:
  void UpdateTexturesTotalHostMemoryUsage(uint64_t add, uint64_t subtract);

  // Shared memory callback for texture data invalidation.
  static void WatchCallback(const global_unique_lock_type& global_lock,
                            void* context, void* data, uint64_t argument,
                            bool invalidated_by_gpu);

  // Checks if there are any pages that contain scaled resolve data within the
  // range.
  bool IsRangeScaledResolved(uint32_t start_unscaled, uint32_t length_unscaled);
  // Global shared memory invalidation callback for invalidating scaled resolved
  // texture data.
  static void ScaledResolveGlobalWatchCallbackThunk(
      const global_unique_lock_type& global_lock, void* context,
      uint32_t address_first, uint32_t address_last, bool invalidated_by_gpu);
  void ScaledResolveGlobalWatchCallback(
      const global_unique_lock_type& global_lock, uint32_t address_first,
      uint32_t address_last, bool invalidated_by_gpu);

  const RegisterFile& register_file_;
  SharedMemory& shared_memory_;
  uint32_t draw_resolution_scale_x_;
  uint32_t draw_resolution_scale_y_;
  divisors::MagicDiv draw_resolution_scale_x_divisor_;
  divisors::MagicDiv draw_resolution_scale_y_divisor_;
  static const LoadShaderInfo load_shader_info_[kLoadShaderCount];

  xe::global_critical_region global_critical_region_;
  // Bit vector storing whether each 4 KB physical memory page contains scaled
  // resolve data. uint32_t rather than uint64_t because parts of it can be sent
  // to shaders.
  std::unique_ptr<uint32_t[]> scaled_resolve_pages_;
  // Second level of the bit vector for faster rejection of non-scaled textures.
  // >> 12 for 4 KB pages, >> 5 for uint32_t level 1 bits, >> 6 for uint64_t
  // level 2 bits.
  uint64_t scaled_resolve_pages_l2_[SharedMemory::kBufferSize >> (12 + 5 + 6)];

  // Global watch for scaled resolve data invalidation.
  SharedMemory::GlobalWatchHandle scaled_resolve_global_watch_handle_ = nullptr;

  uint64_t current_submission_index_ = 0;
  uint64_t current_submission_time_ = 0;

  std::unordered_map<TextureKey, std::unique_ptr<Texture>, TextureKey::Hasher>
      textures_;

  uint64_t textures_total_host_memory_usage_ = 0;

  Texture* texture_used_first_ = nullptr;
  Texture* texture_used_last_ = nullptr;

  // Whether a texture has become outdated (a memory watch has been triggered),
  // so need to recheck if textures aren't outdated, disregarding whether fetch
  // constants have been changed.
  std::atomic<bool> texture_became_outdated_{false};

  std::array<TextureBinding, xenos::kTextureFetchConstantCount>
      texture_bindings_;
  // Bit vector with bits reset on fetch constant writes to avoid parsing fetch
  // constants again and again.
  uint32_t texture_bindings_in_sync_ = 0;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TEXTURE_CACHE_H_

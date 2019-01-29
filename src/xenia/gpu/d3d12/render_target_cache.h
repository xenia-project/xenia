/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_RENDER_TARGET_CACHE_H_
#define XENIA_GPU_D3D12_RENDER_TARGET_CACHE_H_

#include <gflags/gflags.h>

#include <unordered_map>

#include "xenia/gpu/d3d12/d3d12_shader.h"
#include "xenia/gpu/d3d12/shared_memory.h"
#include "xenia/gpu/d3d12/texture_cache.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"
#include "xenia/ui/d3d12/d3d12_api.h"

DECLARE_bool(d3d12_16bit_rtv_full_range);

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor;

// =============================================================================
// How EDRAM is used by Xenos:
// (Copied from the old version of the render target cache, so implementation
//  info may differ from the way EDRAM is emulated now.)
// =============================================================================
//
// On the 360 the render target is an opaque block of memory in EDRAM that's
// only accessible via resolves. We use this to our advantage to simulate
// something like it as best we can by having a shared backing memory with
// a multitude of views for each tile location in EDRAM.
//
// This allows us to have the same base address write to the same memory
// regardless of framebuffer format. Resolving then uses whatever format the
// resolve requests straight from the backing memory.
//
// EDRAM is a beast and we only approximate it as best we can. Basically,
// the 10MiB of EDRAM is composed of 2048 5120b tiles. Each tile is 80x16px.
// +-----+-----+-----+---
// |tile0|tile1|tile2|...  2048 times
// +-----+-----+-----+---
// Operations dealing with EDRAM deal in tile offsets, so base 0x100 is tile
// offset 256, 256*5120=1310720b into the buffer. All rendering operations are
// aligned to tiles so trying to draw at 256px wide will have a real width of
// 320px by rounding up to the next tile.
//
// MSAA and other settings will modify the exact pixel sizes, like 4X makes
// each tile effectively 40x8px / 2X makes each tile 80x8px, but they are still
// all 5120b. As we try to emulate this we adjust our viewport when rendering to
// stretch pixels as needed.
//
// It appears that games also take advantage of MSAA stretching tiles when doing
// clears. Games will clear a view with 1/2X pitch/height and 4X MSAA and then
// later draw to that view with 1X pitch/height and 1X MSAA.
//
// The good news is that games cannot read EDRAM directly but must use a copy
// operation to get the data out. That gives us a chance to do whatever we
// need to (re-tile, etc) only when requested.
//
// To approximate the tiled EDRAM layout we use a single large chunk of memory.
// From this memory we create many VkImages (and VkImageViews) of various
// formats and dimensions as requested by the game. These are used as
// attachments during rendering and as sources during copies. They are also
// heavily aliased - lots of images will reference the same locations in the
// underlying EDRAM buffer. The only requirement is that there are no hazards
// with specific tiles (reading/writing the same tile through different images)
// and otherwise it should be ok *fingers crossed*.
//
// One complication is the copy/resolve process itself: we need to give back
// the data asked for in the format desired and where it goes is arbitrary
// (any address in physical memory). If the game is good we get resolves of
// EDRAM into fixed base addresses with scissored regions. If the game is bad
// we are broken.
//
// Resolves from EDRAM result in tiled textures - that's texture tiles, not
// EDRAM tiles. If we wanted to ensure byte-for-byte correctness we'd need to
// then tile the images as we wrote them out. For now, we just attempt to
// get the (X, Y) in linear space and do that. This really comes into play
// when multiple resolves write to the same texture or memory aliased by
// multiple textures - which is common due to predicated tiling. The examples
// below demonstrate what this looks like, but the important thing is that
// we are aware of partial textures and overlapping regions.
//
// Example with multiple render targets:
//   Two color targets of 256x256px tightly packed in EDRAM:
//     color target 0: base 0x0, pitch 320, scissor 0,0, 256x256
//       starts at tile 0, buffer offset 0
//       contains 64 tiles (320/80)*(256/16)
//     color target 1: base 0x40, pitch 320, scissor 256,0, 256x256
//       starts at tile 64 (after color target 0), buffer offset 327680b
//       contains 64 tiles
//   In EDRAM each set of 64 tiles is contiguous:
//     +------+------+   +------+------+------+
//     |ct0.0 |ct0.1 |...|ct0.63|ct1.0 |ct1.1 |...
//     +------+------+   +------+------+------+
//   To render into these, we setup two VkImages:
//     image 0: bound to buffer offset 0, 320x256x4=327680b
//     image 1: bound to buffer offset 327680b, 320x256x4=327680b
//   So when we render to them:
//     +------+-+ scissored to 256x256, actually 320x256
//     | .    | | <- . appears at some untiled offset in the buffer, but
//     |      | |      consistent if aliased with the same format
//     +------+-+
//   In theory, this gives us proper aliasing in most cases.
//
// Example with horizontal predicated tiling:
//   Trying to render 1024x576 @4X MSAA, splitting into two regions
//   horizontally:
//     +----------+
//     | 1024x288 |
//     +----------+
//     | 1024x288 |
//     +----------+
//   EDRAM configured for 1056x288px with tile size 2112x567px (4X MSAA):
//     color target 0: base 0x0, pitch 1080, 26x36 tiles
//   First render (top):
//     window offset 0,0
//     scissor 0,0, 1024x288
//   First resolve (top):
//     RB_COPY_DEST_BASE    0x1F45D000
//     RB_COPY_DEST_PITCH   pitch=1024, height=576
//     vertices: 0,0, 1024,0, 1024,288
//   Second render (bottom):
//     window offset 0,-288
//     scissor 0,288, 1024x288
//   Second resolve (bottom):
//     RB_COPY_DEST_BASE    0x1F57D000 (+1179648b)
//     RB_COPY_DEST_PITCH   pitch=1024, height=576
//       (exactly 1024x288*4b after first resolve)
//     vertices: 0,288, 1024,288, 1024,576
//   Resolving here is easy as the textures are contiguous in memory. We can
//   snoop in the first resolve with the dest height to know the total size,
//   and in the second resolve see that it overlaps and place it in the
//   existing target.
//
// Example with vertical predicated tiling:
//   Trying to render 1280x720 @2X MSAA, splitting into two regions
//   vertically:
//     +-----+-----+
//     | 640 | 640 |
//     |  x  |  x  |
//     | 720 | 720 |
//     +-----+-----+
//   EDRAM configured for 640x736px with tile size 640x1472px (2X MSAA):
//     color target 0: base 0x0, pitch 640, 8x92 tiles
//   First render (left):
//     window offset 0,0
//     scissor 0,0, 640x720
//   First resolve (left):
//     RB_COPY_DEST_BASE    0x1BC6D000
//     RB_COPY_DEST_PITCH   pitch=1280, height=720
//     vertices: 0,0, 640,0, 640,720
//   Second render (right):
//     window offset -640,0
//     scissor 640,0, 640x720
//   Second resolve (right):
//     RB_COPY_DEST_BASE    0x1BC81000 (+81920b)
//     RB_COPY_DEST_PITCH   pitch=1280, height=720
//     vertices: 640,0, 1280,0, 1280,720
//   Resolving here is much more difficult as resolves are tiled and the right
//   half of the texture is 81920b away:
//     81920/4bpp=20480px, /32 (texture tile size)=640px
//   We know the texture size with the first resolve and with the second we
//   must check for overlap then compute the offset (in both X and Y).
//
// =============================================================================
// Surface size:
// =============================================================================
//
// XGSurfaceSize code in game executables calculates the size in tiles in the
// following order:
// 1) If MSAA is >=2x, multiply the height by 2.
// 2) If MSAA is 4x, multiply the width by 2.
// 3) 80x16-align multisampled width and height.
// 4) Multiply width*height by 4 or 8 depending on the pixel format.
// 5) Divide the byte size by 5120.
// This means that when working with EDRAM surface sizes we should assume that a
// multisampled surface is the same as a single-sampled surface with 2x height
// and width - however, format size doesn't effect the dimensions. Surface pitch
// in the surface info register is single-sampled.
//
// =============================================================================
// Rasterizer-ordered view usage:
// =============================================================================
//
// There is a separate output merger emulation path currently in development,
// using rasterizer-ordered views for writing directly to the 10 MB EDRAM buffer
// instead of the host output merger for render target output.
//
// The convential method of implementing Xenos render targets via host render
// targets has various flaws that may be impossible to fix:
// - k_16_16 and k_16_16_16_16 have -32...32 range on Xenos, but there's no
//   equivalent format on PC APIs. They may be emulated using snorm16 (by
//   dividing shader color output by 32) or float32, however, blending behaves
//   incorrectly for both. In the former case, multiplicative blending may not
//   work correctly - 1 becomes 1/32, and instead of 1 * 1 = 1, you get
//   1/32 * 1/32 = 1/1024. For 32-bit floats, additive blending result may go up
//   to infinity.
// - k_2_10_10_10_FLOAT has similar blending issues, though less prominent, when
//   emulated via float16 render targets. In addition to a greater range for
//   RGB (values can go up to 65504 and infinity rather than 31.875), alpha is
//   represented totally differently - in k_2_10_10_10_FLOAT, it may have only
//   4 values, and adding, for example, 0.1 to 0.333 will still result in 0.333,
//   while with float16, it will be increasing, and the limit is infinity.
// - Due to simultaneously bound host render targets being independent from each
//   other, and because the height is unknown (and the viewport and scissor are
//   not always present - D3DPT_RECTLIST is used very commonly, especially for
//   clearing (Direct3D 9 Clear is implemented this way on the Xbox 360) and
//   copying, and it's usually drawn without a viewport and with 8192x8192
//   scissor), there may be cases of simultaneously bound render targets
//   overlapping each other in the EDRAM in a way that is difficult to resolve,
//   and stores/loads may destroy data.
//
// =============================================================================
// 2x width and height scaling implementation:
// =============================================================================
//
// For ease of mapping EDRAM addresses, host pixels (top-left, top-right,
// bottom-left, bottom-right) within EACH GUEST SAMPLE are stored consecutively,
// this means that the address of each sample with 4x resolution enabled is 4x
// the address of it without increased resolution - and you only need to add
// (uint(SV_Position.y) * 2u + uint(SV_Position.x)) to the dword/qword index to
// get each of the 4 host pixels for each sample.
class RenderTargetCache {
 public:
  // Direct3D 12 debug layer does some kaschenit-style trolling by giving errors
  // that contradict each other when you use null RTV descriptors - if you set
  // a valid format in RTVFormats in the pipeline state, it says that null
  // descriptors can only be used if the format in the pipeline state is
  // DXGI_FORMAT_UNKNOWN, however, if DXGI_FORMAT_UNKNOWN is set, it complains
  // that the format in the pipeline doesn't match the RTV format. So we have to
  // make render target bindings consecutive and remap the output indices in
  // pixel shaders.
  struct PipelineRenderTarget {
    uint32_t guest_render_target;
    DXGI_FORMAT format;
  };

  RenderTargetCache(D3D12CommandProcessor* command_processor,
                    RegisterFile* register_file);
  ~RenderTargetCache();

  bool Initialize(const TextureCache* texture_cache);
  void Shutdown();
  void ClearCache();

  void BeginFrame();
  // Called in the beginning of a draw call - may bind pipelines.
  bool UpdateRenderTargets(const D3D12Shader* pixel_shader);
  // Returns the host-to-guest mappings and host formats of currently bound
  // render targets for pipeline creation and remapping in shaders. They are
  // consecutive, and format DXGI_FORMAT_UNKNOWN terminates the list. Depth
  // format is in the 5th render target.
  const PipelineRenderTarget* GetCurrentPipelineRenderTargets() const {
    return current_pipeline_render_targets_;
  }
  // Performs the resolve to a shared memory area according to the current
  // register values, and also clears the EDRAM buffer if needed. Must be in a
  // frame for calling.
  bool Resolve(SharedMemory* shared_memory, TextureCache* texture_cache,
               Memory* memory);
  // Flushes the render targets to EDRAM and unbinds them, for instance, when
  // the command processor takes over framebuffer bindings to draw something
  // special.
  void UnbindRenderTargets();
  // Transitions the EDRAM buffer to a UAV - for use with ROV rendering.
  void UseEDRAMAsUAV();
  void WriteEDRAMUint32UAVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void EndFrame();

  // Totally necessary to rely on the base format - Too Human switches between
  // 2_10_10_10_FLOAT and 2_10_10_10_FLOAT_AS_16_16_16_16 every draw.
  static ColorRenderTargetFormat GetBaseColorFormat(
      ColorRenderTargetFormat format);
  static inline bool IsColorFormat64bpp(ColorRenderTargetFormat format) {
    return format == ColorRenderTargetFormat::k_16_16_16_16 ||
           format == ColorRenderTargetFormat::k_16_16_16_16_FLOAT ||
           format == ColorRenderTargetFormat::k_32_32_FLOAT;
  }
  static DXGI_FORMAT GetColorDXGIFormat(ColorRenderTargetFormat format);
  // Nvidia may have higher performance with 24-bit depth, AMD should have no
  // performance difference, but with EDRAM loads/stores less conversion should
  // be performed by the shaders if D24S8 is emulated as D24_UNORM_S8_UINT, and
  // it's probably more accurate.
  static inline DXGI_FORMAT GetDepthDXGIFormat(DepthRenderTargetFormat format) {
    return format == DepthRenderTargetFormat::kD24FS8
               ? DXGI_FORMAT_D32_FLOAT_S8X24_UINT
               : DXGI_FORMAT_D24_UNORM_S8_UINT;
  }

 private:
  enum class EDRAMLoadStoreMode {
    kColor32bpp,
    kColor64bpp,
    kColor7e3,
    kDepthUnorm,
    kDepthFloat,

    kCount
  };

  struct EDRAMLoadStoreModeInfo {
    const void* load_shader;
    size_t load_shader_size;
    const WCHAR* load_pipeline_name;

    const void* store_shader;
    size_t store_shader_size;
    const WCHAR* store_pipeline_name;

    const void* load_2x_resolve_shader;
    size_t load_2x_resolve_shader_size;
    const WCHAR* load_2x_resolve_pipeline_name;
  };

  union RenderTargetKey {
    struct {
      // Supersampled (_ss - scaled 2x if needed) dimensions, divided by 80x16.
      // The limit is 2560x2560 without AA, 2560x5120 with 2x AA, and 5120x5120
      // with 4x AA, and twice as much (up to 10240x10240) with 2x resolution
      // scaling.
      uint32_t width_ss_div_80 : 8;    // 8
      uint32_t height_ss_div_16 : 10;  // 18
      uint32_t is_depth : 1;           // 19
      uint32_t format : 4;             // 23
    };
    uint32_t value;

    // Clearing the unused bits.
    RenderTargetKey() : value(0) {}
    RenderTargetKey(const RenderTargetKey& key) : value(key.value) {}
    RenderTargetKey& operator=(const RenderTargetKey& key) {
      value = key.value;
      return *this;
    }
    bool operator==(const RenderTargetKey& key) const {
      return value == key.value;
    }
    bool operator!=(const RenderTargetKey& key) const {
      return value != key.value;
    }
  };

  struct RenderTarget {
    ID3D12Resource* resource;
    D3D12_RESOURCE_STATES state;
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    RenderTargetKey key;
#if 0
    // The first 4 MB page in the heaps.
    uint32_t heap_page_first;
    // The number of 4 MB pages this render target uses.
    uint32_t heap_page_count;
#else
    // Index of the render target when multiple render targets with the same key
    // are bound simultaneously.
    uint32_t instance;
#endif
    // Color/depth and stencil layouts.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2];
    // Buffer size needed to copy the render target to the EDRAM buffer.
    uint32_t copy_buffer_size;
  };

  struct RenderTargetBinding {
    // Whether this render target has been used since the last full update.
    bool is_bound;
    uint32_t edram_base;
    // How many 16-pixel rows has already been drawn to the render target since
    // the last full update.
    uint32_t edram_dirty_rows;
    union {
      uint32_t format;
      ColorRenderTargetFormat color_format;
      DepthRenderTargetFormat depth_format;
    };
    RenderTarget* render_target;
  };

  // Converting resolve pipeline.
  struct ResolvePipeline {
    ID3D12PipelineState* pipeline;
    DXGI_FORMAT dest_format;
  };

  union ResolveTargetKey {
    struct {
      // 2560 / 32 = 80 (7 bits), * 2 for 2x resolution scale = 160 (8 bits).
      uint32_t width_div_32 : 8;
      uint32_t height_div_32 : 8;
      DXGI_FORMAT format : 16;
    };
    uint32_t value;
  };

  // Target for converting resolves.
  struct ResolveTarget {
    ID3D12Resource* resource;
    D3D12_RESOURCE_STATES state;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
    ResolveTargetKey key;
#if 0
    // The first 4 MB page in the heaps.
    uint32_t heap_page_first;
#endif
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    // Buffer size needed to copy the resolve target to a linear buffer.
    uint32_t copy_buffer_size;
  };

  uint32_t GetEDRAMBufferSize() const;

  void TransitionEDRAMBuffer(D3D12_RESOURCE_STATES new_state);

  void WriteEDRAMRawSRVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void WriteEDRAMRawUAVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);

  void ClearBindings();

#if 0
  // Checks if the heap for the render target exists and tries to create it if
  // it's not.
  bool MakeHeapResident(uint32_t heap_index);
#endif

  // Creates a new RTV/DSV descriptor heap if needed to be able to allocate one
  // descriptor in it.
  bool EnsureRTVHeapAvailable(bool is_depth);

  // Returns true if a render target with such key can be created.
  static bool GetResourceDesc(RenderTargetKey key, D3D12_RESOURCE_DESC& desc);

#if 0
  RenderTarget* FindOrCreateRenderTarget(RenderTargetKey key,
                                         uint32_t heap_page_first);
#else
  RenderTarget* FindOrCreateRenderTarget(RenderTargetKey key,
                                         uint32_t instance);
#endif

  // Calculates the tile layout for a rectangle on a render target of the given
  // configuration. The base is adjusted so it points to the tile containing the
  // top-left pixel of the rectangle, the rectangle is also adjusted so it's
  // relative to that tile (because its coordinates don't have to be multiples
  // of the tile size) and so it's not larger than the pitch and the available
  // memory space. EDRAM row pitch in tiles (for memory access) and actual width
  // and height of the region containing the rectangle in tiles (for thread
  // group count) are also written. This function returns true if the requested
  // rectangle is within the bounds of EDRAM and is not empty, but if it returns
  // false, the output values may not be written, so the return value must be
  // checked.
  static bool GetEDRAMLayout(uint32_t pitch_pixels, MsaaSamples msaa_samples,
                             bool is_64bpp, uint32_t& base_in_out,
                             D3D12_RECT& rect_in_out, uint32_t& pitch_tiles_out,
                             uint32_t& row_width_ss_div_80_out,
                             uint32_t& rows_out);

  static EDRAMLoadStoreMode GetLoadStoreMode(bool is_depth, uint32_t format);

  // Must be in a frame to call. Stores the dirty areas of the currently bound
  // render targets and marks them as clean.
  void StoreRenderTargetsToEDRAM();

  // Must be in a frame to call. Loads the render targets from the EDRAM buffer,
  // filling all the rows the render target can hold.
  void LoadRenderTargetsFromEDRAM(uint32_t render_target_count,
                                  RenderTarget* const* render_targets,
                                  const uint32_t* edram_bases);

  // Performs the copying part of a resolve.
  bool ResolveCopy(SharedMemory* shared_memory, TextureCache* texture_cache,
                   uint32_t edram_base, uint32_t surface_pitch,
                   MsaaSamples msaa_samples, bool is_depth, uint32_t src_format,
                   const D3D12_RECT& rect);
  // Performs the clearing part of a resolve.
  bool ResolveClear(uint32_t edram_base, uint32_t surface_pitch,
                    MsaaSamples msaa_samples, bool is_depth, uint32_t format,
                    const D3D12_RECT& rect);

  ID3D12PipelineState* GetResolvePipeline(DXGI_FORMAT dest_format);
  // Returns any available resolve target placed at least at
  // min_heap_first_page, or tries to place it at the specified position (if not
  // possible, will place it in the next heap).
#if 0
  ResolveTarget* FindOrCreateResolveTarget(uint32_t width, uint32_t height,
                                           DXGI_FORMAT format,
                                           uint32_t min_heap_first_page);
#else
  ResolveTarget* FindOrCreateResolveTarget(uint32_t width, uint32_t height,
                                           DXGI_FORMAT format);
#endif

  D3D12CommandProcessor* command_processor_;
  RegisterFile* register_file_;

  // Whether 1 guest pixel is rendered as 2x2 host pixels (currently only
  // supported with ROV).
  bool resolution_scale_2x_ = false;

  // The EDRAM buffer allowing color and depth data to be reinterpreted.
  ID3D12Resource* edram_buffer_ = nullptr;
  D3D12_RESOURCE_STATES edram_buffer_state_;
  bool edram_buffer_cleared_;

  // Non-shader-visible descriptor heap containing pre-created SRV and UAV
  // descriptors of the EDRAM buffer, for faster binding (via copying rather
  // than creation).
  enum class EDRAMBufferDescriptorIndex : uint32_t {
    kRawSRV,
    kRawUAV,
    // For ROV access primarily.
    kUint32UAV,

    kCount,
  };
  ID3D12DescriptorHeap* edram_buffer_descriptor_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE edram_buffer_descriptor_heap_start_;

  // EDRAM root signatures.
  ID3D12RootSignature* edram_load_store_root_signature_ = nullptr;
  ID3D12RootSignature* edram_clear_root_signature_ = nullptr;
  struct EDRAMLoadStoreRootConstants {
    union {
      struct {
        uint32_t rt_color_depth_offset;
        uint32_t rt_color_depth_pitch;
        uint32_t rt_stencil_offset;
        uint32_t rt_stencil_pitch;
      };
      struct {
        // 0:11 - resolve area width/height in pixels.
        // 12:16 - offset in the destination texture (only up to 31 - assuming
        //         32*n is pre-applied to the base pointer).
        // 17: - left/top of the copied region (relative to EDRAM base).
        uint32_t tile_sample_dimensions[2];
        uint32_t tile_sample_dest_base;
        // 0:13 - destination pitch.
        // 14:15 - sample to load (14 - vertical index, 15 - horizontal index).
        // 16:18 - destination endianness.
        // 19:31 - BPP-specific info for swapping red/blue, 0 if not swapping.
        //   For 32 bits per pixel:
        //     19:23 - red/blue bit depth.
        //     24:28 - blue offset.
        //   For 64 bits per pixel, it's 1 if need to swap 0:15 and 32:47.
        uint32_t tile_sample_dest_info;
      };
      struct {
        // 16 bits for X, 16 bits for Y.
        uint32_t clear_rect_lt;
        uint32_t clear_rect_rb;
        union {
          struct {
            uint32_t clear_color_high;
            uint32_t clear_color_low;
          };
          struct {
            uint32_t clear_depth24;
            uint32_t clear_depth32;
          };
        };
      };
    };
    // 0:10 - EDRAM base in tiles.
    // 11 - log2(vertical sample count), 0 for 1x AA, 1 for 2x/4x AA.
    // 12 - log2(horizontal sample count), 0 for 1x/2x AA, 1 for 4x AA.
    // 13 - whether 2x resolution scale is used.
    // 14 - whether to apply the hack and duplicate the top/left
    //      half-row/half-column to reduce the impact of half-pixel offset with
    //      2x resolution scale.
    // 15 - whether it's a depth render target.
    // 16: - EDRAM pitch in tiles.
    uint32_t base_samples_2x_depth_pitch;
  };
  // EDRAM pipelines.
  static const EDRAMLoadStoreModeInfo
      edram_load_store_mode_info_[size_t(EDRAMLoadStoreMode::kCount)];
  ID3D12PipelineState*
      edram_load_pipelines_[size_t(EDRAMLoadStoreMode::kCount)] = {};
  ID3D12PipelineState*
      edram_store_pipelines_[size_t(EDRAMLoadStoreMode::kCount)] = {};
  ID3D12PipelineState*
      edram_load_2x_resolve_pipelines_[size_t(EDRAMLoadStoreMode::kCount)] = {};
  ID3D12PipelineState* edram_tile_sample_32bpp_pipeline_ = nullptr;
  ID3D12PipelineState* edram_tile_sample_64bpp_pipeline_ = nullptr;
  ID3D12PipelineState* edram_clear_32bpp_pipeline_ = nullptr;
  ID3D12PipelineState* edram_clear_64bpp_pipeline_ = nullptr;
  ID3D12PipelineState* edram_clear_depth_float_pipeline_ = nullptr;

  // FIXME(Triang3l): Investigate what's wrong with placed RTV/DSV aliasing on
  // Nvidia Maxwell 1st generation and older.
#if 0
  // 48 MB heaps backing used render targets resources, created when needed.
  // 24 MB proved to be not enough to store a single render target occupying the
  // entire EDRAM - a 32-bit depth/stencil one - at some resolution.
  // But we also need more than 32 MB to be able to resolve the entire EDRAM
  // into a k_32_32_32_32_FLOAT texture.
  // TODO(Triang3l): With 2x resolution scale, render targets can take 4x more
  // memory - won't fit in this heap size. Resolution scale support was added
  // when placed resources already have been disabled, however.
  ID3D12Heap* heaps_[5] = {};
  static constexpr uint32_t kHeap4MBPages = 12;
#endif

  static constexpr uint32_t kRenderTargetDescriptorHeapSize = 2048;
  // Descriptor heap, for linear allocation of heaps and descriptors.
  struct RenderTargetDescriptorHeap {
    ID3D12DescriptorHeap* heap;
    D3D12_CPU_DESCRIPTOR_HANDLE start_handle;
    // When descriptors_used is >= kRenderTargetDescriptorHeapSize, a new heap
    // must be allocated and linked to the one that became full now.
    uint32_t descriptors_used;
    RenderTargetDescriptorHeap* previous;
  };
  RenderTargetDescriptorHeap* descriptor_heaps_color_ = nullptr;
  RenderTargetDescriptorHeap* descriptor_heaps_depth_ = nullptr;

  std::unordered_multimap<uint32_t, RenderTarget*> render_targets_;

  uint32_t current_surface_pitch_ = 0;
  MsaaSamples current_msaa_samples_ = MsaaSamples::k1X;
  uint32_t current_edram_max_rows_ = 0;
  RenderTargetBinding current_bindings_[5] = {};

  PipelineRenderTarget current_pipeline_render_targets_[5];

  ID3D12RootSignature* resolve_root_signature_ = nullptr;
  struct ResolveRootConstants {
    // In samples.
    // Left and top in the lower 16 bits, width and height in the upper.
    uint32_t rect_samples_lw;
    uint32_t rect_samples_th;
    // In samples. Width in the lower 16 bits, height in the upper.
    uint32_t source_size;
    // 0 - log2(vertical sample count), 0 for 1x AA, 1 for 2x/4x AA.
    // 1 - log2(horizontal sample count), 0 for 1x/2x AA, 1 for 4x AA.
    // 2:3 - vertical sample position:
    //       0 for the upper samples with 2x/4x AA.
    //       1 for 1x AA or to mix samples with 2x/4x AA.
    //       2 for the lower samples with 2x/4x AA.
    // 4:5 - horizontal sample position:
    //       0 for the left samples with 4x AA.
    //       1 for 1x/2x AA or to mix samples with 4x AA.
    //       2 for the right samples with 4x AA.
    // 6 - whether to apply the hack and duplicate the top/left
    //     half-row/half-column to reduce the impact of half-pixel offset with
    //     2x resolution scale.
    // 7:12 - exponent bias.
    uint32_t resolve_info;
  };
  std::vector<ResolvePipeline> resolve_pipelines_;
#if 0
  std::unordered_multimap<uint32_t, ResolveTarget*> resolve_targets_;
#else
  std::unordered_map<uint32_t, ResolveTarget*> resolve_targets_;
#endif
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_RENDER_TARGET_CACHE_H_

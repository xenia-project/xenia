/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_RENDER_CACHE_H_
#define XENIA_GPU_VULKAN_RENDER_CACHE_H_

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace gpu {
namespace vulkan {

// TODO(benvanik): make public API?
class CachedTileView;
class CachedFramebuffer;
class CachedRenderPass;

// Uniquely identifies EDRAM tiles.
struct TileViewKey {
  // Offset into EDRAM in 5120b tiles.
  uint16_t tile_offset;
  // Tile width of the view in base 80x16 tiles.
  uint16_t tile_width;
  // Tile height of the view in base 80x16 tiles.
  uint16_t tile_height;
  // 1 if format is ColorRenderTargetFormat, else DepthRenderTargetFormat.
  uint16_t color_or_depth : 1;
  // Surface MSAA samples
  uint16_t msaa_samples : 2;
  // Either ColorRenderTargetFormat or DepthRenderTargetFormat.
  uint16_t edram_format : 13;
};
static_assert(sizeof(TileViewKey) == 8, "Key must be tightly packed");

// Cached view representing EDRAM memory.
// TODO(benvanik): reuse VkImage's with multiple VkViews for compatible
//     formats?
class CachedTileView {
 public:
  // Key identifying the view in the cache.
  TileViewKey key;
  // Image
  VkImage image = nullptr;
  // Simple view on the image matching the format.
  VkImageView image_view = nullptr;
  // Image layout
  VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  // Memory buffer
  VkDeviceMemory memory = nullptr;
  // Image sample count
  VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT;

  // (if a depth view) Image view of depth aspect
  VkImageView image_view_depth = nullptr;
  // (if a depth view) Image view of stencil aspect
  VkImageView image_view_stencil = nullptr;

  CachedTileView(ui::vulkan::VulkanDevice* device,
                 VkCommandBuffer command_buffer, VkDeviceMemory edram_memory,
                 TileViewKey view_key);
  ~CachedTileView();

  bool IsEqual(const TileViewKey& other_key) const {
    auto a = reinterpret_cast<const uint64_t*>(&key);
    auto b = reinterpret_cast<const uint64_t*>(&other_key);
    return *a == *b;
  }

  bool operator<(const CachedTileView& other) const {
    return key.tile_offset < other.key.tile_offset;
  }

  VkExtent2D GetSize() const {
    return {key.tile_width * 80u, key.tile_height * 16u};
  }

 private:
  VkDevice device_ = nullptr;
};

// Parsed render configuration from the current render state.
struct RenderConfiguration {
  // Render mode (color+depth, depth-only, etc).
  xenos::ModeControl mode_control;
  // Target surface pitch multiplied by MSAA, in pixels.
  uint32_t surface_pitch_px;
  // ESTIMATED target surface height multiplied by MSAA, in pixels.
  uint32_t surface_height_px;
  // Surface MSAA setting.
  MsaaSamples surface_msaa;
  // Color attachments for the 4 render targets.
  struct {
    bool used;
    uint32_t edram_base;
    ColorRenderTargetFormat format;
  } color[4];
  // Depth/stencil attachment.
  struct {
    bool used;
    uint32_t edram_base;
    DepthRenderTargetFormat format;
  } depth_stencil;
};

// Current render state based on the register-specified configuration.
struct RenderState {
  // Parsed configuration.
  RenderConfiguration config;
  // Render pass (to be used with pipelines/etc).
  CachedRenderPass* render_pass = nullptr;
  VkRenderPass render_pass_handle = nullptr;
  // Target framebuffer bound to the render pass.
  CachedFramebuffer* framebuffer = nullptr;
  VkFramebuffer framebuffer_handle = nullptr;

  bool color_attachment_written[4] = {false};
  bool depth_attachment_written = false;
};

// Manages the virtualized EDRAM and the render target cache.
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
// TODO(benvanik): what, if any, barriers do we need? any transitions?
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
class RenderCache {
 public:
  RenderCache(RegisterFile* register_file, ui::vulkan::VulkanDevice* device);
  ~RenderCache();

  VkResult Initialize();
  void Shutdown();

  // Call this to determine if you should start a new render pass or continue
  // with an already open pass.
  bool dirty() const;

  CachedTileView* FindTileView(uint32_t base, uint32_t pitch,
                               MsaaSamples samples, bool color_or_depth,
                               uint32_t format);

  // Begins a render pass targeting the state-specified framebuffer formats.
  // The command buffer will be transitioned into the render pass phase.
  const RenderState* BeginRenderPass(VkCommandBuffer command_buffer,
                                     VulkanShader* vertex_shader,
                                     VulkanShader* pixel_shader);

  // Ends the current render pass.
  // The command buffer will be transitioned out of the render pass phase.
  void EndRenderPass();

  // Clears all cached content.
  void ClearCache();

  // Queues commands to copy EDRAM contents into an image.
  // The command buffer must not be inside of a render pass when calling this.
  void RawCopyToImage(VkCommandBuffer command_buffer, uint32_t edram_base,
                      VkImage image, VkImageLayout image_layout,
                      bool color_or_depth, VkOffset3D offset,
                      VkExtent3D extents);

  // Queues commands to blit EDRAM contents into an image.
  // The command buffer must not be inside of a render pass when calling this.
  void BlitToImage(VkCommandBuffer command_buffer, uint32_t edram_base,
                   uint32_t pitch, uint32_t height, MsaaSamples num_samples,
                   VkImage image, VkImageLayout image_layout,
                   bool color_or_depth, uint32_t format, VkFilter filter,
                   VkOffset3D offset, VkExtent3D extents);

  // Queues commands to clear EDRAM contents with a solid color.
  // The command buffer must not be inside of a render pass when calling this.
  void ClearEDRAMColor(VkCommandBuffer command_buffer, uint32_t edram_base,
                       ColorRenderTargetFormat format, uint32_t pitch,
                       uint32_t height, MsaaSamples num_samples, float* color);
  // Queues commands to clear EDRAM contents with depth/stencil values.
  // The command buffer must not be inside of a render pass when calling this.
  void ClearEDRAMDepthStencil(VkCommandBuffer command_buffer,
                              uint32_t edram_base,
                              DepthRenderTargetFormat format, uint32_t pitch,
                              uint32_t height, MsaaSamples num_samples,
                              float depth, uint32_t stencil);
  // Queues commands to fill EDRAM contents with a constant value.
  // The command buffer must not be inside of a render pass when calling this.
  void FillEDRAM(VkCommandBuffer command_buffer, uint32_t value);

 private:
  // Parses the current state into a configuration object.
  bool ParseConfiguration(RenderConfiguration* config);

  // Finds a tile view. Returns nullptr if none found matching the key.
  CachedTileView* FindTileView(const TileViewKey& view_key) const;

  // Gets or creates a tile view with the given parameters.
  CachedTileView* FindOrCreateTileView(VkCommandBuffer command_buffer,
                                       const TileViewKey& view_key);

  void UpdateTileView(VkCommandBuffer command_buffer, CachedTileView* view,
                      bool load, bool insert_barrier = true);

  // Gets or creates a render pass and frame buffer for the given configuration.
  // This attempts to reuse as much as possible across render passes and
  // framebuffers.
  bool ConfigureRenderPass(VkCommandBuffer command_buffer,
                           RenderConfiguration* config,
                           CachedRenderPass** out_render_pass,
                           CachedFramebuffer** out_framebuffer);

  RegisterFile* register_file_ = nullptr;
  ui::vulkan::VulkanDevice* device_ = nullptr;

  // Entire 10MiB of EDRAM.
  VkDeviceMemory edram_memory_ = nullptr;
  // Buffer overlayed 1:1 with edram_memory_ to allow raw access.
  VkBuffer edram_buffer_ = nullptr;

  // Cache of VkImage and VkImageView's for all of our EDRAM tilings.
  // TODO(benvanik): non-linear lookup? Should only be a small number of these.
  std::vector<CachedTileView*> cached_tile_views_;

  // Cache of render passes based on formats.
  std::vector<CachedRenderPass*> cached_render_passes_;

  // Shadows of the registers that impact the render pass we choose.
  // If the registers don't change between passes we can quickly reuse the
  // previous one.
  struct ShadowRegisters {
    reg::RB_MODECONTROL rb_modecontrol;
    reg::RB_SURFACE_INFO rb_surface_info;
    reg::RB_COLOR_INFO rb_color_info;
    reg::RB_COLOR_INFO rb_color1_info;
    reg::RB_COLOR_INFO rb_color2_info;
    reg::RB_COLOR_INFO rb_color3_info;
    reg::RB_DEPTH_INFO rb_depth_info;
    uint32_t rb_color_mask;
    uint32_t pa_sc_window_scissor_tl;
    uint32_t pa_sc_window_scissor_br;

    ShadowRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } shadow_registers_;
  bool SetShadowRegister(uint32_t* dest, uint32_t register_name);

  // Configuration used for the current/previous Begin/End, representing the
  // current shadow register state.
  RenderState current_state_;

  // Only valid during a BeginRenderPass/EndRenderPass block.
  VkCommandBuffer current_command_buffer_ = nullptr;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_RENDER_CACHE_H_

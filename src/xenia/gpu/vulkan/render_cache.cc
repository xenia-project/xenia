/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/render_cache.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"

namespace xe {
namespace gpu {
namespace vulkan {

using namespace xe::gpu::xenos;
using xe::ui::vulkan::CheckResult;

constexpr uint32_t kEdramBufferCapacity = 10 * 1024 * 1024;

VkFormat ColorRenderTargetFormatToVkFormat(ColorRenderTargetFormat format) {
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8:
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case ColorRenderTargetFormat::k_2_10_10_10:
    case ColorRenderTargetFormat::k_2_10_10_10_unknown:
      return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_unknown:
      // WARNING: this is wrong, most likely - no float form in vulkan?
      XELOGW("Unsupported EDRAM format k_2_10_10_10_FLOAT used");
      return VK_FORMAT_A2R10G10B10_SSCALED_PACK32;
    case ColorRenderTargetFormat::k_16_16:
      return VK_FORMAT_R16G16_UNORM;
    case ColorRenderTargetFormat::k_16_16_16_16:
      return VK_FORMAT_R16G16B16A16_UNORM;
    case ColorRenderTargetFormat::k_16_16_FLOAT:
      return VK_FORMAT_R16G16_SFLOAT;
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case ColorRenderTargetFormat::k_32_FLOAT:
      return VK_FORMAT_R32_SFLOAT;
    case ColorRenderTargetFormat::k_32_32_FLOAT:
      return VK_FORMAT_R32G32_SFLOAT;
    default:
      assert_unhandled_case(key.edram_format);
      return VK_FORMAT_UNDEFINED;
  }
}

VkFormat DepthRenderTargetFormatToVkFormat(DepthRenderTargetFormat format) {
  switch (format) {
    case DepthRenderTargetFormat::kD24S8:
      return VK_FORMAT_D24_UNORM_S8_UINT;
    case DepthRenderTargetFormat::kD24FS8:
      // TODO(benvanik): some way to emulate? resolve-time flag?
      XELOGW("Unsupported EDRAM format kD24FS8 used");
      return VK_FORMAT_D24_UNORM_S8_UINT;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

// Cached framebuffer referencing tile attachments.
// Each framebuffer is specific to a render pass. Ugh.
class CachedFramebuffer {
 public:
  // TODO(benvanik): optimized key? tile base + format for each?

  // Framebuffer with the attachments ready for use in the parent render pass.
  VkFramebuffer handle = nullptr;
  // Width of the framebuffer in pixels.
  uint32_t width = 0;
  // Height of the framebuffer in pixels.
  uint32_t height = 0;
  // References to color attachments, if used.
  CachedTileView* color_attachments[4] = {nullptr};
  // Reference to depth/stencil attachment, if used.
  CachedTileView* depth_stencil_attachment = nullptr;

  CachedFramebuffer(VkDevice device, VkRenderPass render_pass,
                    uint32_t surface_width, uint32_t surface_height,
                    CachedTileView* target_color_attachments[4],
                    CachedTileView* target_depth_stencil_attachment);
  ~CachedFramebuffer();

  bool IsCompatible(const RenderConfiguration& desired_config) const;

 private:
  VkDevice device_ = nullptr;
};

// Cached render passes based on register states.
// Each render pass is dependent on the format, dimensions, and use of
// all attachments. The same render pass can be reused for multiple
// framebuffers pointing at various tile views, though those cached
// framebuffers are specific to the render pass.
class CachedRenderPass {
 public:
  // Configuration this pass was created with.
  RenderConfiguration config;
  // Initialized render pass for the register state.
  VkRenderPass handle = nullptr;
  // Cache of framebuffers for the various tile attachments.
  std::vector<CachedFramebuffer*> cached_framebuffers;

  CachedRenderPass(VkDevice device, const RenderConfiguration& desired_config);
  ~CachedRenderPass();

  bool IsCompatible(const RenderConfiguration& desired_config) const;

 private:
  VkDevice device_ = nullptr;
};

CachedTileView::CachedTileView(ui::vulkan::VulkanDevice* device,
                               VkCommandBuffer command_buffer,
                               VkDeviceMemory edram_memory,
                               TileViewKey view_key)
    : device_(*device), key(std::move(view_key)) {
  // Map format to Vulkan.
  VkFormat vulkan_format = VK_FORMAT_UNDEFINED;
  uint32_t bpp = 4;
  if (key.color_or_depth) {
    auto edram_format = static_cast<ColorRenderTargetFormat>(key.edram_format);
    vulkan_format = ColorRenderTargetFormatToVkFormat(edram_format);
    switch (edram_format) {
      case ColorRenderTargetFormat::k_16_16_16_16:
      case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      case ColorRenderTargetFormat::k_32_32_FLOAT:
        bpp = 8;
        break;
      default:
        bpp = 4;
        break;
    }
  } else {
    auto edram_format = static_cast<DepthRenderTargetFormat>(key.edram_format);
    vulkan_format = DepthRenderTargetFormatToVkFormat(edram_format);
  }
  assert_true(vulkan_format != VK_FORMAT_UNDEFINED);
  assert_true(bpp == 4);

  // Create the image with the desired properties.
  VkImageCreateInfo image_info;
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.pNext = nullptr;
  // TODO(benvanik): exploit VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT so we can have
  //     multiple views.
  image_info.flags = 0;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = vulkan_format;
  image_info.extent.width = key.tile_width * 80;
  image_info.extent.height = key.tile_height * 16;
  image_info.extent.depth = 1;
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples =
      static_cast<VkSampleCountFlagBits>(VK_SAMPLE_COUNT_1_BIT);
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.usage |= key.color_or_depth
                          ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                          : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.queueFamilyIndexCount = 0;
  image_info.pQueueFamilyIndices = nullptr;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  auto err = vkCreateImage(device_, &image_info, nullptr, &image);
  CheckResult(err, "vkCreateImage");

  VkMemoryRequirements memory_requirements;
  vkGetImageMemoryRequirements(*device, image, &memory_requirements);

  // Bind to a newly allocated chunk.
  // TODO: Alias from a really big buffer?
  memory = device->AllocateMemory(memory_requirements, 0);
  err = vkBindImageMemory(device_, image, memory, 0);
  CheckResult(err, "vkBindImageMemory");

  // Create the image view we'll use to attach it to a framebuffer.
  VkImageViewCreateInfo image_view_info;
  image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image_view_info.pNext = nullptr;
  image_view_info.flags = 0;
  image_view_info.image = image;
  image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image_view_info.format = image_info.format;
  // TODO(benvanik): manipulate? may not be able to when attached.
  image_view_info.components = {
      VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
      VK_COMPONENT_SWIZZLE_A,
  };
  image_view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (key.color_or_depth) {
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  } else {
    image_view_info.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  err = vkCreateImageView(device_, &image_view_info, nullptr, &image_view);
  CheckResult(err, "vkCreateImageView");

  // TODO(benvanik): transition to general layout?
  VkImageMemoryBarrier image_barrier;
  image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_barrier.pNext = nullptr;
  image_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
  image_barrier.dstAccessMask =
      key.color_or_depth ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                         : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  image_barrier.dstAccessMask |=
      VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.image = image;
  image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_barrier.subresourceRange.baseMipLevel = 0;
  image_barrier.subresourceRange.levelCount = 1;
  image_barrier.subresourceRange.baseArrayLayer = 0;
  image_barrier.subresourceRange.layerCount = 1;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &image_barrier);
}

CachedTileView::~CachedTileView() {
  vkDestroyImageView(device_, image_view, nullptr);
  vkDestroyImage(device_, image, nullptr);
  vkFreeMemory(device_, memory, nullptr);
}

CachedFramebuffer::CachedFramebuffer(
    VkDevice device, VkRenderPass render_pass, uint32_t surface_width,
    uint32_t surface_height, CachedTileView* target_color_attachments[4],
    CachedTileView* target_depth_stencil_attachment)
    : device_(device),
      width(surface_width),
      height(surface_height),
      depth_stencil_attachment(target_depth_stencil_attachment) {
  for (int i = 0; i < 4; ++i) {
    color_attachments[i] = target_color_attachments[i];
  }

  // Create framebuffer.
  VkImageView image_views[5] = {nullptr};
  int image_view_count = 0;
  for (int i = 0; i < 4; ++i) {
    if (color_attachments[i]) {
      image_views[image_view_count++] = color_attachments[i]->image_view;
    }
  }
  if (depth_stencil_attachment) {
    image_views[image_view_count++] = depth_stencil_attachment->image_view;
  }
  VkFramebufferCreateInfo framebuffer_info;
  framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_info.pNext = nullptr;
  framebuffer_info.renderPass = render_pass;
  framebuffer_info.attachmentCount = image_view_count;
  framebuffer_info.pAttachments = image_views;
  framebuffer_info.width = width;
  framebuffer_info.height = height;
  framebuffer_info.layers = 1;
  auto err = vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &handle);
  CheckResult(err, "vkCreateFramebuffer");
}

CachedFramebuffer::~CachedFramebuffer() {
  vkDestroyFramebuffer(device_, handle, nullptr);
}

bool CachedFramebuffer::IsCompatible(
    const RenderConfiguration& desired_config) const {
  // We already know all render pass things line up, so let's verify dimensions,
  // edram offsets, etc. We need an exact match.
  if (desired_config.surface_pitch_px != width ||
      desired_config.surface_height_px != height) {
    return false;
  }
  // TODO(benvanik): separate image views from images in tiles and store in fb?
  for (int i = 0; i < 4; ++i) {
    // Ensure the the attachment points to the same tile.
    if (!color_attachments[i]) {
      continue;
    }
    auto& color_info = color_attachments[i]->key;
    auto& desired_color_info = desired_config.color[i];
    if (color_info.tile_offset != desired_color_info.edram_base ||
        color_info.edram_format !=
            static_cast<uint16_t>(desired_color_info.format)) {
      return false;
    }
  }
  // Ensure depth attachment is correct.
  if (depth_stencil_attachment &&
      (depth_stencil_attachment->key.tile_offset !=
           desired_config.depth_stencil.edram_base ||
       depth_stencil_attachment->key.edram_format !=
           static_cast<uint16_t>(desired_config.depth_stencil.format))) {
    return false;
  }
  return true;
}

CachedRenderPass::CachedRenderPass(VkDevice device,
                                   const RenderConfiguration& desired_config)
    : device_(device) {
  std::memcpy(&config, &desired_config, sizeof(config));

  // Initialize all attachments to default unused.
  // As we set layout(location=RT) in shaders we must always provide 4.
  VkAttachmentDescription attachments[5];
  for (int i = 0; i < 4; ++i) {
    attachments[i].flags = 0;
    attachments[i].format = VK_FORMAT_UNDEFINED;
    attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[i].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    attachments[i].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
  }
  auto& depth_stencil_attachment = attachments[4];
  depth_stencil_attachment.flags = 0;
  depth_stencil_attachment.format = VK_FORMAT_UNDEFINED;
  depth_stencil_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depth_stencil_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  depth_stencil_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_stencil_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  depth_stencil_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_stencil_attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
  depth_stencil_attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkAttachmentReference depth_stencil_attachment_ref;
  depth_stencil_attachment_ref.attachment = VK_ATTACHMENT_UNUSED;
  depth_stencil_attachment_ref.layout = VK_IMAGE_LAYOUT_GENERAL;

  // Configure attachments based on what's enabled.
  VkAttachmentReference color_attachment_refs[4];
  for (int i = 0; i < 4; ++i) {
    auto& color_config = config.color[i];
    // TODO(benvanik): see how loose we can be with these.
    attachments[i].format =
        ColorRenderTargetFormatToVkFormat(color_config.format);
    auto& color_attachment_ref = color_attachment_refs[i];
    color_attachment_ref.attachment = i;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_GENERAL;
  }
  auto& depth_config = config.depth_stencil;
  depth_stencil_attachment_ref.attachment = 4;
  depth_stencil_attachment.format =
      DepthRenderTargetFormatToVkFormat(depth_config.format);

  // Single subpass that writes to our attachments.
  VkSubpassDescription subpass_info;
  subpass_info.flags = 0;
  subpass_info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass_info.inputAttachmentCount = 0;
  subpass_info.pInputAttachments = nullptr;
  subpass_info.colorAttachmentCount = 4;
  subpass_info.pColorAttachments = color_attachment_refs;
  subpass_info.pResolveAttachments = nullptr;
  subpass_info.pDepthStencilAttachment = &depth_stencil_attachment_ref;
  subpass_info.preserveAttachmentCount = 0;
  subpass_info.pPreserveAttachments = nullptr;

  // Create the render pass.
  VkRenderPassCreateInfo render_pass_info;
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.pNext = nullptr;
  render_pass_info.attachmentCount = 5;
  render_pass_info.pAttachments = attachments;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass_info;
  render_pass_info.dependencyCount = 0;
  render_pass_info.pDependencies = nullptr;
  auto err = vkCreateRenderPass(device_, &render_pass_info, nullptr, &handle);
  CheckResult(err, "vkCreateRenderPass");
}

CachedRenderPass::~CachedRenderPass() {
  for (auto framebuffer : cached_framebuffers) {
    delete framebuffer;
  }
  cached_framebuffers.clear();

  vkDestroyRenderPass(device_, handle, nullptr);
}

bool CachedRenderPass::IsCompatible(
    const RenderConfiguration& desired_config) const {
  for (int i = 0; i < 4; ++i) {
    // TODO(benvanik): allow compatible vulkan formats.
    if (config.color[i].format != desired_config.color[i].format) {
      return false;
    }
  }
  if (config.depth_stencil.format != desired_config.depth_stencil.format) {
    return false;
  }
  return true;
}

RenderCache::RenderCache(RegisterFile* register_file,
                         ui::vulkan::VulkanDevice* device)
    : register_file_(register_file), device_(device) {
  VkResult status = VK_SUCCESS;

  // Create the buffer we'll bind to our memory.
  VkBufferCreateInfo buffer_info;
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.pNext = nullptr;
  buffer_info.flags = 0;
  buffer_info.size = kEdramBufferCapacity;
  buffer_info.usage =
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  buffer_info.queueFamilyIndexCount = 0;
  buffer_info.pQueueFamilyIndices = nullptr;
  status = vkCreateBuffer(*device, &buffer_info, nullptr, &edram_buffer_);
  CheckResult(status, "vkCreateBuffer");

  // Query requirements for the buffer.
  // It should be 1:1.
  VkMemoryRequirements buffer_requirements;
  vkGetBufferMemoryRequirements(*device_, edram_buffer_, &buffer_requirements);
  assert_true(buffer_requirements.size == kEdramBufferCapacity);

  // Allocate EDRAM memory.
  // TODO(benvanik): do we need it host visible?
  edram_memory_ = device->AllocateMemory(buffer_requirements);
  assert_not_null(edram_memory_);

  // Bind buffer to map our entire memory.
  status = vkBindBufferMemory(*device_, edram_buffer_, edram_memory_, 0);
  CheckResult(status, "vkBindBufferMemory");

  if (status == VK_SUCCESS) {
    status = vkBindBufferMemory(*device_, edram_buffer_, edram_memory_, 0);
    CheckResult(status, "vkBindBufferMemory");

    // Upload a grid into the EDRAM buffer.
    uint32_t* gpu_data = nullptr;
    status = vkMapMemory(*device_, edram_memory_, 0, buffer_requirements.size,
                         0, reinterpret_cast<void**>(&gpu_data));
    CheckResult(status, "vkMapMemory");

    if (status == VK_SUCCESS) {
      for (int i = 0; i < kEdramBufferCapacity / 4; i++) {
        gpu_data[i] = (i % 8) >= 4 ? 0xFF0000FF : 0xFFFFFFFF;
      }

      vkUnmapMemory(*device_, edram_memory_);
    }
  }
}

RenderCache::~RenderCache() {
  // TODO(benvanik): wait for idle.

  // Dispose all render passes (and their framebuffers).
  for (auto render_pass : cached_render_passes_) {
    delete render_pass;
  }
  cached_render_passes_.clear();

  // Dispose all of our cached tile views.
  for (auto tile_view : cached_tile_views_) {
    delete tile_view;
  }
  cached_tile_views_.clear();

  // Release underlying EDRAM memory.
  vkDestroyBuffer(*device_, edram_buffer_, nullptr);
  vkFreeMemory(*device_, edram_memory_, nullptr);
}

const RenderState* RenderCache::BeginRenderPass(VkCommandBuffer command_buffer,
                                                VulkanShader* vertex_shader,
                                                VulkanShader* pixel_shader) {
  assert_null(current_command_buffer_);
  current_command_buffer_ = command_buffer;

  // Lookup or construct a render pass compatible with our current state.
  auto config = &current_state_.config;
  CachedRenderPass* render_pass = nullptr;
  CachedFramebuffer* framebuffer = nullptr;
  auto& regs = shadow_registers_;
  bool dirty = false;
  dirty |= SetShadowRegister(&regs.rb_modecontrol, XE_GPU_REG_RB_MODECONTROL);
  dirty |= SetShadowRegister(&regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  dirty |= SetShadowRegister(&regs.rb_color_info, XE_GPU_REG_RB_COLOR_INFO);
  dirty |= SetShadowRegister(&regs.rb_color1_info, XE_GPU_REG_RB_COLOR1_INFO);
  dirty |= SetShadowRegister(&regs.rb_color2_info, XE_GPU_REG_RB_COLOR2_INFO);
  dirty |= SetShadowRegister(&regs.rb_color3_info, XE_GPU_REG_RB_COLOR3_INFO);
  dirty |= SetShadowRegister(&regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |= SetShadowRegister(&regs.rb_depth_info, XE_GPU_REG_RB_DEPTH_INFO);
  dirty |= SetShadowRegister(&regs.pa_sc_window_scissor_tl,
                             XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL);
  dirty |= SetShadowRegister(&regs.pa_sc_window_scissor_br,
                             XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR);
  if (!dirty && current_state_.render_pass) {
    // No registers have changed so we can reuse the previous render pass -
    // just begin with what we had.
    render_pass = current_state_.render_pass;
    framebuffer = current_state_.framebuffer;
  } else {
    // Re-parse configuration.
    if (!ParseConfiguration(config)) {
      return nullptr;
    }

    // Lookup or generate a new render pass and framebuffer for the new state.
    if (!ConfigureRenderPass(command_buffer, config, &render_pass,
                             &framebuffer)) {
      return nullptr;
    }

    // Speculatively see if targets are actually used so we can skip copies
    for (int i = 0; i < 4; i++) {
      config->color[i].used = pixel_shader->writes_color_target(i);
    }
    config->depth_stencil.used = !!(regs.rb_depthcontrol & (0x4 | 0x2));

    current_state_.render_pass = render_pass;
    current_state_.render_pass_handle = render_pass->handle;
    current_state_.framebuffer = framebuffer;
    current_state_.framebuffer_handle = framebuffer->handle;

    VkBufferMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = edram_buffer_;
    barrier.offset = 0;
    barrier.size = 0;

    // Copy EDRAM buffer into render targets with tight packing.
    VkBufferImageCopy region;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageOffset = {0, 0, 0};

    // Depth
    auto depth_target = current_state_.framebuffer->depth_stencil_attachment;
    if (depth_target && current_state_.config.depth_stencil.used) {
      region.imageSubresource = {
          VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1};
      region.bufferOffset = depth_target->key.tile_offset * 5120;

      // Wait for any potential copies to finish.
      barrier.offset = region.bufferOffset;
      barrier.size = depth_target->key.tile_width * 80 *
                     depth_target->key.tile_height * 16 * 4;
      vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 1,
                           &barrier, 0, nullptr);

      region.imageExtent = {depth_target->key.tile_width * 80u,
                            depth_target->key.tile_height * 16u, 1};
      vkCmdCopyBufferToImage(command_buffer, edram_buffer_, depth_target->image,
                             VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    }

    // Color
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    for (int i = 0; i < 4; i++) {
      auto target = current_state_.framebuffer->color_attachments[i];
      if (!target || !current_state_.config.color[i].used) {
        continue;
      }

      region.bufferOffset = target->key.tile_offset * 5120;

      // Wait for any potential copies to finish.
      barrier.offset = region.bufferOffset;
      barrier.size =
          target->key.tile_width * 80 * target->key.tile_height * 16 * 4;
      vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 1,
                           &barrier, 0, nullptr);

      region.imageExtent = {target->key.tile_width * 80u,
                            target->key.tile_height * 16u, 1};
      vkCmdCopyBufferToImage(command_buffer, edram_buffer_, target->image,
                             VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    }
  }
  if (!render_pass) {
    return nullptr;
  }

  // Setup render pass in command buffer.
  // This is meant to preserve previous contents as we may be called
  // repeatedly.
  VkRenderPassBeginInfo render_pass_begin_info;
  render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin_info.pNext = nullptr;
  render_pass_begin_info.renderPass = render_pass->handle;
  render_pass_begin_info.framebuffer = framebuffer->handle;

  // Render into the entire buffer (or at least tell the API we are doing
  // this). In theory it'd be better to clip this to the scissor region, but
  // the docs warn anything but the full framebuffer may be slow.
  render_pass_begin_info.renderArea.offset.x = 0;
  render_pass_begin_info.renderArea.offset.y = 0;
  render_pass_begin_info.renderArea.extent.width = config->surface_pitch_px;
  render_pass_begin_info.renderArea.extent.height = config->surface_height_px;

  // Configure clear color, if clearing.
  // TODO(benvanik): enable clearing here during resolve?
  render_pass_begin_info.clearValueCount = 0;
  render_pass_begin_info.pClearValues = nullptr;

  // Begin the render pass.
  vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info,
                       VK_SUBPASS_CONTENTS_INLINE);

  return &current_state_;
}

bool RenderCache::ParseConfiguration(RenderConfiguration* config) {
  auto& regs = shadow_registers_;

  // RB_MODECONTROL
  // Rough mode control (color, color+depth, etc).
  config->mode_control = static_cast<ModeControl>(regs.rb_modecontrol & 0x7);

  // RB_SURFACE_INFO
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  config->surface_pitch_px = regs.rb_surface_info & 0x3FFF;
  config->surface_msaa =
      static_cast<MsaaSamples>((regs.rb_surface_info >> 16) & 0x3);

  // TODO(benvanik): verify min/max so we don't go out of bounds.
  // TODO(benvanik): has to be a good way to get height.
  // Guess the height from the scissor height.
  // It's wildly inaccurate, but I've never seen it be bigger than the
  // EDRAM tiling.
  uint32_t ws_y = (regs.pa_sc_window_scissor_tl >> 16) & 0x7FFF;
  uint32_t ws_h = ((regs.pa_sc_window_scissor_br >> 16) & 0x7FFF) - ws_y;
  config->surface_height_px = std::min(2560u, xe::round_up(ws_h, 16));

  // Color attachment configuration.
  if (config->mode_control == ModeControl::kColorDepth) {
    uint32_t color_info[4] = {
        regs.rb_color_info, regs.rb_color1_info, regs.rb_color2_info,
        regs.rb_color3_info,
    };
    for (int i = 0; i < 4; ++i) {
      config->color[i].edram_base = color_info[i] & 0xFFF;
      config->color[i].format =
          static_cast<ColorRenderTargetFormat>((color_info[i] >> 16) & 0xF);
      // We don't support GAMMA formats, so switch them to what we do support.
      switch (config->color[i].format) {
        case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
          config->color[i].format = ColorRenderTargetFormat::k_8_8_8_8;
          break;
      }
    }
  } else {
    for (int i = 0; i < 4; ++i) {
      config->color[i].edram_base = 0;
      config->color[i].format = ColorRenderTargetFormat::k_8_8_8_8;
    }
  }

  // Depth/stencil attachment configuration.
  if (config->mode_control == ModeControl::kColorDepth ||
      config->mode_control == ModeControl::kDepth) {
    config->depth_stencil.edram_base = regs.rb_depth_info & 0xFFF;
    config->depth_stencil.format =
        static_cast<DepthRenderTargetFormat>((regs.rb_depth_info >> 16) & 0x1);
  } else {
    config->depth_stencil.edram_base = 0;
    config->depth_stencil.format = DepthRenderTargetFormat::kD24S8;
  }

  return true;
}

bool RenderCache::ConfigureRenderPass(VkCommandBuffer command_buffer,
                                      RenderConfiguration* config,
                                      CachedRenderPass** out_render_pass,
                                      CachedFramebuffer** out_framebuffer) {
  *out_render_pass = nullptr;
  *out_framebuffer = nullptr;

  // TODO(benvanik): better lookup.
  // Attempt to find the render pass in our cache.
  CachedRenderPass* render_pass = nullptr;
  for (auto cached_render_pass : cached_render_passes_) {
    if (cached_render_pass->IsCompatible(*config)) {
      // Found a match.
      render_pass = cached_render_pass;
      break;
    }
  }

  // If no render pass was found in the cache create a new one.
  if (!render_pass) {
    render_pass = new CachedRenderPass(*device_, *config);
    cached_render_passes_.push_back(render_pass);
  }

  // TODO(benvanik): better lookup.
  // Attempt to find the framebuffer in the render pass cache.
  CachedFramebuffer* framebuffer = nullptr;
  for (auto cached_framebuffer : render_pass->cached_framebuffers) {
    if (cached_framebuffer->IsCompatible(*config)) {
      // Found a match.
      framebuffer = cached_framebuffer;
      break;
    }
  }

  // If no framebuffer was found in the cache create a new one.
  if (!framebuffer) {
    CachedTileView* target_color_attachments[4] = {nullptr, nullptr, nullptr,
                                                   nullptr};
    for (int i = 0; i < 4; ++i) {
      TileViewKey color_key;
      color_key.tile_offset = config->color[i].edram_base;
      color_key.tile_width = config->surface_pitch_px / 80;
      color_key.tile_height = config->surface_height_px / 16;
      color_key.color_or_depth = 1;
      color_key.edram_format = static_cast<uint16_t>(config->color[i].format);
      target_color_attachments[i] =
          FindOrCreateTileView(command_buffer, color_key);
      if (!target_color_attachments) {
        XELOGE("Failed to get tile view for color attachment");
        return false;
      }
    }

    TileViewKey depth_stencil_key;
    depth_stencil_key.tile_offset = config->depth_stencil.edram_base;
    depth_stencil_key.tile_width = config->surface_pitch_px / 80;
    depth_stencil_key.tile_height = config->surface_height_px / 16;
    depth_stencil_key.color_or_depth = 0;
    depth_stencil_key.edram_format =
        static_cast<uint16_t>(config->depth_stencil.format);
    auto target_depth_stencil_attachment =
        FindOrCreateTileView(command_buffer, depth_stencil_key);
    if (!target_depth_stencil_attachment) {
      XELOGE("Failed to get tile view for depth/stencil attachment");
      return false;
    }

    framebuffer = new CachedFramebuffer(
        *device_, render_pass->handle, config->surface_pitch_px,
        config->surface_height_px, target_color_attachments,
        target_depth_stencil_attachment);
    render_pass->cached_framebuffers.push_back(framebuffer);
  }

  *out_render_pass = render_pass;
  *out_framebuffer = framebuffer;
  return true;
}

CachedTileView* RenderCache::FindOrCreateTileView(
    VkCommandBuffer command_buffer, const TileViewKey& view_key) {
  auto tile_view = FindTileView(view_key);
  if (tile_view) {
    return tile_view;
  }

  // Create a new tile and add to the cache.
  tile_view =
      new CachedTileView(device_, command_buffer, edram_memory_, view_key);
  cached_tile_views_.push_back(tile_view);

  return tile_view;
}

CachedTileView* RenderCache::FindTileView(const TileViewKey& view_key) const {
  // Check the cache.
  // TODO(benvanik): better lookup.
  for (auto tile_view : cached_tile_views_) {
    if (tile_view->IsEqual(view_key)) {
      return tile_view;
    }
  }

  return nullptr;
}

void RenderCache::EndRenderPass() {
  assert_not_null(current_command_buffer_);

  // End the render pass.
  vkCmdEndRenderPass(current_command_buffer_);

  // Copy all render targets back into our EDRAM buffer.
  // Don't bother waiting on this command to complete, as next render pass may
  // reuse previous framebuffer attachments. If they need this, they will wait.
  // TODO: Should we bother re-tiling the images on copy back?
  //
  // FIXME: There's a case where we may have a really big render target (as we
  // can't get the correct height atm) and we may end up overwriting the valid
  // contents of another render target by mistake! Need to reorder copy commands
  // to avoid this.
  VkBufferImageCopy region;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageOffset = {0, 0, 0};
  // Depth/stencil
  auto depth_target = current_state_.framebuffer->depth_stencil_attachment;
  if (depth_target && current_state_.config.depth_stencil.used) {
    region.imageSubresource = {
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1};
    region.bufferOffset = depth_target->key.tile_offset * 5120;
    region.imageExtent = {depth_target->key.tile_width * 80u,
                          depth_target->key.tile_height * 16u, 1};
    vkCmdCopyImageToBuffer(current_command_buffer_, depth_target->image,
                           VK_IMAGE_LAYOUT_GENERAL, edram_buffer_, 1, &region);
  }

  // Color
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  for (int i = 0; i < 4; i++) {
    auto target = current_state_.framebuffer->color_attachments[i];
    if (!target || !current_state_.config.color[i].used) {
      continue;
    }

    region.bufferOffset = target->key.tile_offset * 5120;
    region.imageExtent = {target->key.tile_width * 80u,
                          target->key.tile_height * 16u, 1};
    vkCmdCopyImageToBuffer(current_command_buffer_, target->image,
                           VK_IMAGE_LAYOUT_GENERAL, edram_buffer_, 1, &region);
  }

  current_command_buffer_ = nullptr;
}

void RenderCache::ClearCache() {
  // TODO(benvanik): caching.
}

void RenderCache::RawCopyToImage(VkCommandBuffer command_buffer,
                                 uint32_t edram_base, VkImage image,
                                 VkImageLayout image_layout,
                                 bool color_or_depth, VkOffset3D offset,
                                 VkExtent3D extents) {
  // Transition the texture into a transfer destination layout.
  VkImageMemoryBarrier image_barrier;
  image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_barrier.pNext = nullptr;
  image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  if (image_layout != VK_IMAGE_LAYOUT_GENERAL &&
      image_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    image_barrier.srcAccessMask = 0;
    image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier.oldLayout = image_layout;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier.image = image;
    image_barrier.subresourceRange = {0, 0, 1, 0, 1};
    image_barrier.subresourceRange.aspectMask =
        color_or_depth
            ? VK_IMAGE_ASPECT_COLOR_BIT
            : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  VkBufferMemoryBarrier buffer_barrier;
  buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  buffer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer_barrier.buffer = edram_buffer_;
  buffer_barrier.offset = edram_base * 5120;
  // TODO: Calculate this accurately (need texel size)
  buffer_barrier.size = extents.width * extents.height * 4;

  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 1,
                       &buffer_barrier, 1, &image_barrier);

  // Issue the copy command.
  VkBufferImageCopy region;
  region.bufferOffset = edram_base * 5120;
  region.bufferImageHeight = 0;
  region.bufferRowLength = 0;
  region.imageOffset = offset;
  region.imageExtent = extents;
  region.imageSubresource = {0, 0, 0, 1};
  region.imageSubresource.aspectMask =
      color_or_depth ? VK_IMAGE_ASPECT_COLOR_BIT
                     : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  vkCmdCopyBufferToImage(command_buffer, edram_buffer_, image, image_layout, 1,
                         &region);

  // Transition the image back into its previous layout.
  if (image_layout != VK_IMAGE_LAYOUT_GENERAL &&
      image_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    image_barrier.srcAccessMask = image_barrier.dstAccessMask;
    image_barrier.dstAccessMask = 0;
    std::swap(image_barrier.oldLayout, image_barrier.newLayout);
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &image_barrier);
  }
}

void RenderCache::ClearEDRAMColor(VkCommandBuffer command_buffer,
                                  uint32_t edram_base,
                                  ColorRenderTargetFormat format,
                                  uint32_t pitch, uint32_t height,
                                  float* color) {
  // Grab a tile view (as we need to clear an image first)
  TileViewKey key;
  key.color_or_depth = 1;
  key.edram_format = static_cast<uint16_t>(format);
  key.tile_offset = edram_base;
  key.tile_width = pitch / 80;
  key.tile_height = height / 16;
  auto tile_view = FindOrCreateTileView(command_buffer, key);
  assert_not_null(tile_view);

  VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkClearColorValue clear_value;
  std::memcpy(clear_value.float32, color, sizeof(float) * 4);

  // Issue a clear command
  vkCmdClearColorImage(command_buffer, tile_view->image,
                       VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1, &range);

  // Copy image back into EDRAM buffer
  VkBufferImageCopy copy_range;
  copy_range.bufferOffset = edram_base * 5120;
  copy_range.bufferImageHeight = 0;
  copy_range.bufferRowLength = 0;
  copy_range.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy_range.imageExtent = {key.tile_width * 80u, key.tile_height * 16u, 1u};
  copy_range.imageOffset = {0, 0, 0};
  vkCmdCopyImageToBuffer(command_buffer, tile_view->image,
                         VK_IMAGE_LAYOUT_GENERAL, edram_buffer_, 1,
                         &copy_range);
}

void RenderCache::ClearEDRAMDepthStencil(VkCommandBuffer command_buffer,
                                         uint32_t edram_base,
                                         DepthRenderTargetFormat format,
                                         uint32_t pitch, uint32_t height,
                                         float depth, uint32_t stencil) {
  // Grab a tile view (as we need to clear an image first)
  TileViewKey key;
  key.color_or_depth = 0;
  key.edram_format = static_cast<uint16_t>(format);
  key.tile_offset = edram_base;
  key.tile_width = pitch / 80;
  key.tile_height = height / 16;
  auto tile_view = FindOrCreateTileView(command_buffer, key);
  assert_not_null(tile_view);

  VkImageSubresourceRange range = {
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1,
  };
  VkClearDepthStencilValue clear_value;
  clear_value.depth = depth;
  clear_value.stencil = stencil;

  // Issue a clear command
  vkCmdClearDepthStencilImage(command_buffer, tile_view->image,
                              VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1, &range);

  // Copy image back into EDRAM buffer
  VkBufferImageCopy copy_range;
  copy_range.bufferOffset = edram_base * 5120;
  copy_range.bufferImageHeight = 0;
  copy_range.bufferRowLength = 0;
  copy_range.imageSubresource = {
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1,
  };
  copy_range.imageExtent = {key.tile_width * 80u, key.tile_height * 16u, 1u};
  copy_range.imageOffset = {0, 0, 0};
  vkCmdCopyImageToBuffer(command_buffer, tile_view->image,
                         VK_IMAGE_LAYOUT_GENERAL, edram_buffer_, 1,
                         &copy_range);
}

bool RenderCache::SetShadowRegister(uint32_t* dest, uint32_t register_name) {
  uint32_t value = register_file_->values[register_name].u32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

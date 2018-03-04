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
#include "xenia/gpu/registers.h"
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
      return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
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
      // Vulkan doesn't support 24-bit floats, so just promote it to 32-bit
      return VK_FORMAT_D32_SFLOAT_S8_UINT;
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
  // FIXME(DrChat): Was this check necessary?
  // assert_true(bpp == 4);

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
  if (FLAGS_vulkan_native_msaa) {
    auto msaa_samples = static_cast<MsaaSamples>(key.msaa_samples);
    switch (msaa_samples) {
      case MsaaSamples::k1X:
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        break;
      case MsaaSamples::k2X:
        image_info.samples = VK_SAMPLE_COUNT_2_BIT;
        break;
      case MsaaSamples::k4X:
        image_info.samples = VK_SAMPLE_COUNT_4_BIT;
        break;
      default:
        assert_unhandled_case(msaa_samples);
    }
  } else {
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  }
  sample_count = image_info.samples;
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

  device->DbgSetObjectName(
      reinterpret_cast<uint64_t>(image), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT,
      xe::format_string("%.8X pitch %.8X(%d)", key.tile_offset, key.tile_width,
                        key.tile_width));

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
      VK_COMPONENT_SWIZZLE_R,
      VK_COMPONENT_SWIZZLE_G,
      VK_COMPONENT_SWIZZLE_B,
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

  // Create separate depth/stencil views.
  if (key.color_or_depth == 0) {
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    err = vkCreateImageView(device_, &image_view_info, nullptr,
                            &image_view_depth);
    CheckResult(err, "vkCreateImageView");

    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    err = vkCreateImageView(device_, &image_view_info, nullptr,
                            &image_view_stencil);
    CheckResult(err, "vkCreateImageView");
  }

  // TODO(benvanik): transition to general layout?
  VkImageMemoryBarrier image_barrier;
  image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_barrier.pNext = nullptr;
  image_barrier.srcAccessMask = 0;
  image_barrier.dstAccessMask =
      key.color_or_depth ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                         : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.image = image;
  image_barrier.subresourceRange.aspectMask =
      key.color_or_depth
          ? VK_IMAGE_ASPECT_COLOR_BIT
          : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  image_barrier.subresourceRange.baseMipLevel = 0;
  image_barrier.subresourceRange.levelCount = 1;
  image_barrier.subresourceRange.baseArrayLayer = 0;
  image_barrier.subresourceRange.layerCount = 1;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       key.color_or_depth
                           ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                           : VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &image_barrier);

  image_layout = image_barrier.newLayout;
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
  framebuffer_info.flags = 0;
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
  uint32_t surface_pitch_px = desired_config.surface_msaa != MsaaSamples::k4X
                                  ? desired_config.surface_pitch_px
                                  : desired_config.surface_pitch_px * 2;
  uint32_t surface_height_px = desired_config.surface_msaa == MsaaSamples::k1X
                                   ? desired_config.surface_height_px
                                   : desired_config.surface_height_px * 2;
  surface_pitch_px = std::min(surface_pitch_px, 2560u);
  surface_height_px = std::min(surface_height_px, 2560u);
  if (surface_pitch_px != width || surface_height_px != height) {
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

  VkSampleCountFlagBits sample_count;
  if (FLAGS_vulkan_native_msaa) {
    switch (desired_config.surface_msaa) {
      case MsaaSamples::k1X:
        sample_count = VK_SAMPLE_COUNT_1_BIT;
        break;
      case MsaaSamples::k2X:
        sample_count = VK_SAMPLE_COUNT_2_BIT;
        break;
      case MsaaSamples::k4X:
        sample_count = VK_SAMPLE_COUNT_4_BIT;
        break;
      default:
        assert_unhandled_case(desired_config.surface_msaa);
        break;
    }
  } else {
    sample_count = VK_SAMPLE_COUNT_1_BIT;
  }

  // Initialize all attachments to default unused.
  // As we set layout(location=RT) in shaders we must always provide 4.
  VkAttachmentDescription attachments[5];
  for (int i = 0; i < 4; ++i) {
    attachments[i].flags = VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT;
    attachments[i].format = VK_FORMAT_UNDEFINED;
    attachments[i].samples = sample_count;
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
  depth_stencil_attachment.samples = sample_count;
  depth_stencil_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  depth_stencil_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_stencil_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  depth_stencil_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_stencil_attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
  depth_stencil_attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

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

  // Configure depth.
  VkAttachmentReference depth_stencil_attachment_ref;
  depth_stencil_attachment_ref.layout = VK_IMAGE_LAYOUT_GENERAL;

  auto& depth_config = config.depth_stencil;
  depth_stencil_attachment_ref.attachment = 4;
  depth_stencil_attachment.format =
      DepthRenderTargetFormatToVkFormat(depth_config.format);

  // Single subpass that writes to our attachments.
  // FIXME: "Multiple attachments that alias the same memory must not be used in
  // a single subpass"
  // TODO: Input attachment for depth/stencil reads?
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
  std::memset(&render_pass_info, 0, sizeof(render_pass_info));
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.pNext = nullptr;
  render_pass_info.flags = 0;
  render_pass_info.attachmentCount = 5;
  render_pass_info.pAttachments = attachments;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass_info;

  // Add a dependency on external render passes -> us (MAY_ALIAS bit)
  VkSubpassDependency dependencies[1];
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[0].dependencyFlags = 0;

  render_pass_info.dependencyCount = 1;
  render_pass_info.pDependencies = dependencies;
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
  if (config.surface_msaa != desired_config.surface_msaa &&
      FLAGS_vulkan_native_msaa) {
    return false;
  }

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
    : register_file_(register_file), device_(device) {}

RenderCache::~RenderCache() { Shutdown(); }

VkResult RenderCache::Initialize() {
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
  status = vkCreateBuffer(*device_, &buffer_info, nullptr, &edram_buffer_);
  CheckResult(status, "vkCreateBuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Query requirements for the buffer.
  // It should be 1:1.
  VkMemoryRequirements buffer_requirements;
  vkGetBufferMemoryRequirements(*device_, edram_buffer_, &buffer_requirements);
  assert_true(buffer_requirements.size == kEdramBufferCapacity);

  // Allocate EDRAM memory.
  // TODO(benvanik): do we need it host visible?
  edram_memory_ = device_->AllocateMemory(buffer_requirements);
  assert_not_null(edram_memory_);
  if (!edram_memory_) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  // Bind buffer to map our entire memory.
  status = vkBindBufferMemory(*device_, edram_buffer_, edram_memory_, 0);
  CheckResult(status, "vkBindBufferMemory");
  if (status != VK_SUCCESS) {
    return status;
  }

  if (status == VK_SUCCESS) {
    // For debugging, upload a grid into the EDRAM buffer.
    uint32_t* gpu_data = nullptr;
    status = vkMapMemory(*device_, edram_memory_, 0, buffer_requirements.size,
                         0, reinterpret_cast<void**>(&gpu_data));

    if (status == VK_SUCCESS) {
      for (int i = 0; i < kEdramBufferCapacity / 4; i++) {
        gpu_data[i] = (i % 8) >= 4 ? 0xFF0000FF : 0xFFFFFFFF;
      }

      vkUnmapMemory(*device_, edram_memory_);
    }
  }

  return VK_SUCCESS;
}

void RenderCache::Shutdown() {
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
  if (edram_buffer_) {
    vkDestroyBuffer(*device_, edram_buffer_, nullptr);
    edram_buffer_ = nullptr;
  }
  if (edram_memory_) {
    vkFreeMemory(*device_, edram_memory_, nullptr);
    edram_memory_ = nullptr;
  }
}

bool RenderCache::dirty() const {
  auto& regs = *register_file_;
  auto& cur_regs = shadow_registers_;

  bool dirty = false;
  dirty |= cur_regs.rb_modecontrol.value != regs[XE_GPU_REG_RB_MODECONTROL].u32;
  dirty |=
      cur_regs.rb_surface_info.value != regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  dirty |= cur_regs.rb_color_info.value != regs[XE_GPU_REG_RB_COLOR_INFO].u32;
  dirty |= cur_regs.rb_color1_info.value != regs[XE_GPU_REG_RB_COLOR1_INFO].u32;
  dirty |= cur_regs.rb_color2_info.value != regs[XE_GPU_REG_RB_COLOR2_INFO].u32;
  dirty |= cur_regs.rb_color3_info.value != regs[XE_GPU_REG_RB_COLOR3_INFO].u32;
  dirty |= cur_regs.rb_depth_info.value != regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
  dirty |= cur_regs.rb_color_mask != regs[XE_GPU_REG_RB_COLOR_MASK].u32;
  dirty |= cur_regs.pa_sc_window_scissor_tl !=
           regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL].u32;
  dirty |= cur_regs.pa_sc_window_scissor_br !=
           regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR].u32;
  return dirty;
}

const RenderState* RenderCache::BeginRenderPass(VkCommandBuffer command_buffer,
                                                VulkanShader* vertex_shader,
                                                VulkanShader* pixel_shader) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  assert_null(current_command_buffer_);
  current_command_buffer_ = command_buffer;

  // Lookup or construct a render pass compatible with our current state.
  auto previous_render_pass = current_state_.render_pass;
  auto config = &current_state_.config;
  CachedRenderPass* render_pass = nullptr;
  CachedFramebuffer* framebuffer = nullptr;
  auto& regs = shadow_registers_;
  bool dirty = false;
  dirty |=
      SetShadowRegister(&regs.rb_modecontrol.value, XE_GPU_REG_RB_MODECONTROL);
  dirty |= SetShadowRegister(&regs.rb_surface_info.value,
                             XE_GPU_REG_RB_SURFACE_INFO);
  dirty |=
      SetShadowRegister(&regs.rb_color_info.value, XE_GPU_REG_RB_COLOR_INFO);
  dirty |=
      SetShadowRegister(&regs.rb_color1_info.value, XE_GPU_REG_RB_COLOR1_INFO);
  dirty |=
      SetShadowRegister(&regs.rb_color2_info.value, XE_GPU_REG_RB_COLOR2_INFO);
  dirty |=
      SetShadowRegister(&regs.rb_color3_info.value, XE_GPU_REG_RB_COLOR3_INFO);
  dirty |=
      SetShadowRegister(&regs.rb_depth_info.value, XE_GPU_REG_RB_DEPTH_INFO);
  dirty |= SetShadowRegister(&regs.rb_color_mask, XE_GPU_REG_RB_COLOR_MASK);
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

    current_state_.render_pass = render_pass;
    current_state_.render_pass_handle = render_pass->handle;
    current_state_.framebuffer = framebuffer;
    current_state_.framebuffer_handle = framebuffer->handle;

    // TODO(DrChat): Determine if we actually need an EDRAM buffer.
    /*
    // Depth
    auto depth_target = current_state_.framebuffer->depth_stencil_attachment;
    if (depth_target && current_state_.config.depth_stencil.used) {
      UpdateTileView(command_buffer, depth_target, true);
    }

    // Color
    for (int i = 0; i < 4; i++) {
      auto target = current_state_.framebuffer->color_attachments[i];
      if (!target || !current_state_.config.color[i].used) {
        continue;
      }

      UpdateTileView(command_buffer, target, true);
    }
    */
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

  if (config->surface_msaa == MsaaSamples::k2X) {
    render_pass_begin_info.renderArea.extent.height =
        std::min(config->surface_height_px * 2, 2560u);
  } else if (config->surface_msaa == MsaaSamples::k4X) {
    render_pass_begin_info.renderArea.extent.width *= 2;
    render_pass_begin_info.renderArea.extent.height =
        std::min(config->surface_height_px * 2, 2560u);
  }

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
  config->mode_control = regs.rb_modecontrol.edram_mode;

  // RB_SURFACE_INFO
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  config->surface_pitch_px = regs.rb_surface_info.surface_pitch;
  config->surface_msaa = regs.rb_surface_info.msaa_samples;

  // TODO(benvanik): verify min/max so we don't go out of bounds.
  // TODO(benvanik): has to be a good way to get height.
  // Guess the height from the scissor height.
  // It's wildly inaccurate, but I've never seen it be bigger than the
  // EDRAM tiling.
  /*
  uint32_t ws_y = (regs.pa_sc_window_scissor_tl >> 16) & 0x7FFF;
  uint32_t ws_h = ((regs.pa_sc_window_scissor_br >> 16) & 0x7FFF) - ws_y;
  config->surface_height_px = std::min(2560u, xe::round_up(ws_h, 16));
  */

  // TODO(DrChat): Find an accurate way to get the surface height. Until we do,
  // we're going to hardcode it to 2560, as that's the absolute maximum.
  config->surface_height_px = 2560;

  // Color attachment configuration.
  if (config->mode_control == ModeControl::kColorDepth) {
    reg::RB_COLOR_INFO color_info[4] = {
        regs.rb_color_info,
        regs.rb_color1_info,
        regs.rb_color2_info,
        regs.rb_color3_info,
    };
    for (int i = 0; i < 4; ++i) {
      config->color[i].edram_base = color_info[i].color_base;
      config->color[i].format = color_info[i].color_format;
      config->color[i].used = ((regs.rb_color_mask >> (i * 4)) & 0xf) != 0;
      // We don't support GAMMA formats, so switch them to what we do support.
      switch (config->color[i].format) {
        case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
          config->color[i].format = ColorRenderTargetFormat::k_8_8_8_8;
          break;
        case ColorRenderTargetFormat::k_2_10_10_10_unknown:
          config->color[i].format = ColorRenderTargetFormat::k_2_10_10_10;
          break;
        case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_unknown:
          config->color[i].format = ColorRenderTargetFormat::k_2_10_10_10_FLOAT;
          break;
        default:
          // The rest are good
          break;
      }
    }
  } else {
    for (int i = 0; i < 4; ++i) {
      config->color[i].edram_base = 0;
      config->color[i].format = ColorRenderTargetFormat::k_8_8_8_8;
      config->color[i].used = false;
    }
  }

  // Depth/stencil attachment configuration.
  if (config->mode_control == ModeControl::kColorDepth ||
      config->mode_control == ModeControl::kDepth) {
    config->depth_stencil.edram_base = regs.rb_depth_info.depth_base;
    config->depth_stencil.format = regs.rb_depth_info.depth_format;
    config->depth_stencil.used = true;
  } else {
    config->depth_stencil.edram_base = 0;
    config->depth_stencil.format = DepthRenderTargetFormat::kD24S8;
    config->depth_stencil.used = false;
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
    uint32_t tile_width = config->surface_msaa == MsaaSamples::k4X ? 40 : 80;
    uint32_t tile_height = config->surface_msaa != MsaaSamples::k1X ? 8 : 16;

    CachedTileView* target_color_attachments[4] = {nullptr, nullptr, nullptr,
                                                   nullptr};
    for (int i = 0; i < 4; ++i) {
      TileViewKey color_key;
      color_key.tile_offset = config->color[i].edram_base;
      color_key.tile_width =
          xe::round_up(config->surface_pitch_px, tile_width) / tile_width;
      // color_key.tile_height =
      //     xe::round_up(config->surface_height_px, tile_height) / tile_height;
      color_key.tile_height = 160;
      color_key.color_or_depth = 1;
      color_key.msaa_samples =
          0;  // static_cast<uint16_t>(config->surface_msaa);
      color_key.edram_format = static_cast<uint16_t>(config->color[i].format);
      target_color_attachments[i] =
          FindOrCreateTileView(command_buffer, color_key);
      if (!target_color_attachments[i]) {
        XELOGE("Failed to get tile view for color attachment");
        return false;
      }
    }

    TileViewKey depth_stencil_key;
    depth_stencil_key.tile_offset = config->depth_stencil.edram_base;
    depth_stencil_key.tile_width =
        xe::round_up(config->surface_pitch_px, tile_width) / tile_width;
    // depth_stencil_key.tile_height =
    //     xe::round_up(config->surface_height_px, tile_height) / tile_height;
    depth_stencil_key.tile_height = 160;
    depth_stencil_key.color_or_depth = 0;
    depth_stencil_key.msaa_samples =
        0;  // static_cast<uint16_t>(config->surface_msaa);
    depth_stencil_key.edram_format =
        static_cast<uint16_t>(config->depth_stencil.format);
    auto target_depth_stencil_attachment =
        FindOrCreateTileView(command_buffer, depth_stencil_key);
    if (!target_depth_stencil_attachment) {
      XELOGE("Failed to get tile view for depth/stencil attachment");
      return false;
    }

    uint32_t surface_pitch_px = config->surface_msaa != MsaaSamples::k4X
                                    ? config->surface_pitch_px
                                    : config->surface_pitch_px * 2;
    uint32_t surface_height_px = config->surface_msaa == MsaaSamples::k1X
                                     ? config->surface_height_px
                                     : config->surface_height_px * 2;
    surface_pitch_px = std::min(surface_pitch_px, 2560u);
    surface_height_px = std::min(surface_height_px, 2560u);
    framebuffer = new CachedFramebuffer(
        *device_, render_pass->handle, surface_pitch_px, surface_height_px,
        target_color_attachments, target_depth_stencil_attachment);
    render_pass->cached_framebuffers.push_back(framebuffer);
  }

  *out_render_pass = render_pass;
  *out_framebuffer = framebuffer;
  return true;
}

CachedTileView* RenderCache::FindTileView(uint32_t base, uint32_t pitch,
                                          MsaaSamples samples,
                                          bool color_or_depth,
                                          uint32_t format) {
  uint32_t tile_width = samples == MsaaSamples::k4X ? 40 : 80;
  uint32_t tile_height = samples != MsaaSamples::k1X ? 8 : 16;

  if (color_or_depth) {
    // Adjust similar formats for easier matching.
    switch (static_cast<ColorRenderTargetFormat>(format)) {
      case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
        format = uint32_t(ColorRenderTargetFormat::k_8_8_8_8);
        break;
      case ColorRenderTargetFormat::k_2_10_10_10_unknown:
        format = uint32_t(ColorRenderTargetFormat::k_2_10_10_10);
        break;
      case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_unknown:
        format = uint32_t(ColorRenderTargetFormat::k_2_10_10_10_FLOAT);
        break;
      default:
        // Other types as-is.
        break;
    }
  }

  TileViewKey key;
  key.tile_offset = base;
  key.tile_width = xe::round_up(pitch, tile_width) / tile_width;
  key.tile_height = 160;
  key.color_or_depth = color_or_depth ? 1 : 0;
  key.msaa_samples = 0;
  key.edram_format = static_cast<uint16_t>(format);
  auto view = FindTileView(key);
  if (view) {
    return view;
  }

  return nullptr;
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

void RenderCache::UpdateTileView(VkCommandBuffer command_buffer,
                                 CachedTileView* view, bool load,
                                 bool insert_barrier) {
  uint32_t tile_width =
      view->key.msaa_samples == uint16_t(MsaaSamples::k4X) ? 40 : 80;
  uint32_t tile_height =
      view->key.msaa_samples != uint16_t(MsaaSamples::k1X) ? 8 : 16;

  if (insert_barrier) {
    VkBufferMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    if (load) {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    } else {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = edram_buffer_;
    barrier.offset = view->key.tile_offset * 5120;
    barrier.size = view->key.tile_width * tile_width * view->key.tile_height *
                           tile_height * view->key.color_or_depth
                       ? 4
                       : 1;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 1,
                         &barrier, 0, nullptr);
  }

  // TODO(DrChat): Stencil copies.
  VkBufferImageCopy region;
  region.bufferOffset = view->key.tile_offset * 5120;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {0, 0, 0, 1};
  region.imageSubresource.aspectMask = view->key.color_or_depth
                                           ? VK_IMAGE_ASPECT_COLOR_BIT
                                           : VK_IMAGE_ASPECT_DEPTH_BIT;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {view->key.tile_width * tile_width,
                        view->key.tile_height * tile_height, 1};
  if (load) {
    vkCmdCopyBufferToImage(command_buffer, edram_buffer_, view->image,
                           VK_IMAGE_LAYOUT_GENERAL, 1, &region);
  } else {
    vkCmdCopyImageToBuffer(command_buffer, view->image, VK_IMAGE_LAYOUT_GENERAL,
                           edram_buffer_, 1, &region);
  }
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

  // TODO(DrChat): Determine if we actually need an EDRAM buffer.
  /*
  std::vector<CachedTileView*> cached_views;

  // Depth
  auto depth_target = current_state_.framebuffer->depth_stencil_attachment;
  if (depth_target && current_state_.config.depth_stencil.used) {
    cached_views.push_back(depth_target);
  }

  // Color
  for (int i = 0; i < 4; i++) {
    auto target = current_state_.framebuffer->color_attachments[i];
    if (!target || !current_state_.config.color[i].used) {
      continue;
    }

    cached_views.push_back(target);
  }

  std::sort(
      cached_views.begin(), cached_views.end(),
      [](CachedTileView const* a, CachedTileView const* b) { return *a < *b; });

  for (auto view : cached_views) {
    UpdateTileView(current_command_buffer_, view, false, false);
  }
  */

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

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &image_barrier);
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
                       &buffer_barrier, 0, nullptr);

  // Issue the copy command.
  // TODO(DrChat): Stencil copies.
  VkBufferImageCopy region;
  region.bufferOffset = edram_base * 5120;
  region.bufferImageHeight = 0;
  region.bufferRowLength = 0;
  region.imageOffset = offset;
  region.imageExtent = extents;
  region.imageSubresource = {0, 0, 0, 1};
  region.imageSubresource.aspectMask =
      color_or_depth ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
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

void RenderCache::BlitToImage(VkCommandBuffer command_buffer,
                              uint32_t edram_base, uint32_t pitch,
                              uint32_t height, MsaaSamples num_samples,
                              VkImage image, VkImageLayout image_layout,
                              bool color_or_depth, uint32_t format,
                              VkFilter filter, VkOffset3D offset,
                              VkExtent3D extents) {
  if (color_or_depth) {
    // Adjust similar formats for easier matching.
    switch (static_cast<ColorRenderTargetFormat>(format)) {
      case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
        format = uint32_t(ColorRenderTargetFormat::k_8_8_8_8);
        break;
      case ColorRenderTargetFormat::k_2_10_10_10_unknown:
        format = uint32_t(ColorRenderTargetFormat::k_2_10_10_10);
        break;
      case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_unknown:
        format = uint32_t(ColorRenderTargetFormat::k_2_10_10_10_FLOAT);
        break;
      default:
        // Rest are OK
        break;
    }
  }

  uint32_t tile_width = num_samples == MsaaSamples::k4X ? 40 : 80;
  uint32_t tile_height = num_samples != MsaaSamples::k1X ? 8 : 16;

  // Grab a tile view that represents the source image.
  TileViewKey key;
  key.color_or_depth = color_or_depth ? 1 : 0;
  key.msaa_samples = 0;  // static_cast<uint16_t>(num_samples);
  key.edram_format = format;
  key.tile_offset = edram_base;
  key.tile_width = xe::round_up(pitch, tile_width) / tile_width;
  // key.tile_height = xe::round_up(height, tile_height) / tile_height;
  key.tile_height = 160;
  auto tile_view = FindOrCreateTileView(command_buffer, key);
  assert_not_null(tile_view);

  // Update the view with the latest contents.
  // UpdateTileView(command_buffer, tile_view, true, true);

  // Put a barrier on the tile view.
  VkImageMemoryBarrier image_barrier;
  image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_barrier.pNext = nullptr;
  image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.srcAccessMask =
      color_or_depth ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                     : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  image_barrier.image = tile_view->image;
  image_barrier.subresourceRange = {0, 0, 1, 0, 1};
  image_barrier.subresourceRange.aspectMask =
      color_or_depth ? VK_IMAGE_ASPECT_COLOR_BIT
                     : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &image_barrier);

  // If we overflow we'll lose the device here.
  // assert_true(extents.width <= key.tile_width * tile_width);
  // assert_true(extents.height <= key.tile_height * tile_height);

  // Now issue the blit to the destination.
  if (tile_view->sample_count == VK_SAMPLE_COUNT_1_BIT) {
    VkImageBlit image_blit;
    image_blit.srcSubresource = {0, 0, 0, 1};
    image_blit.srcSubresource.aspectMask =
        color_or_depth ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
    image_blit.srcOffsets[0] = {0, 0, offset.z};
    image_blit.srcOffsets[1] = {int32_t(extents.width), int32_t(extents.height),
                                int32_t(extents.depth)};

    image_blit.dstSubresource = {0, 0, 0, 1};
    image_blit.dstSubresource.aspectMask =
        color_or_depth ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
    image_blit.dstOffsets[0] = offset;
    image_blit.dstOffsets[1] = {offset.x + int32_t(extents.width),
                                offset.y + int32_t(extents.height),
                                offset.z + int32_t(extents.depth)};
    vkCmdBlitImage(command_buffer, tile_view->image, VK_IMAGE_LAYOUT_GENERAL,
                   image, image_layout, 1, &image_blit, filter);
  } else {
    VkImageResolve image_resolve;
    image_resolve.srcSubresource = {0, 0, 0, 1};
    image_resolve.srcSubresource.aspectMask =
        color_or_depth ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
    image_resolve.srcOffset = {0, 0, 0};

    image_resolve.dstSubresource = {0, 0, 0, 1};
    image_resolve.dstSubresource.aspectMask =
        color_or_depth ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
    image_resolve.dstOffset = offset;

    image_resolve.extent = extents;
    vkCmdResolveImage(command_buffer, tile_view->image, VK_IMAGE_LAYOUT_GENERAL,
                      image, image_layout, 1, &image_resolve);
  }

  // Add another barrier on the tile view.
  image_barrier.srcAccessMask = image_barrier.dstAccessMask;
  image_barrier.dstAccessMask =
      color_or_depth ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                     : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  std::swap(image_barrier.oldLayout, image_barrier.newLayout);
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &image_barrier);
}

void RenderCache::ClearEDRAMColor(VkCommandBuffer command_buffer,
                                  uint32_t edram_base,
                                  ColorRenderTargetFormat format,
                                  uint32_t pitch, uint32_t height,
                                  MsaaSamples num_samples, float* color) {
  // TODO: For formats <= 4 bpp, we can directly fill the EDRAM buffer. Just
  // need to detect this and calculate a value.

  // Adjust similar formats for easier matching.
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      format = ColorRenderTargetFormat::k_8_8_8_8;
      break;
    case ColorRenderTargetFormat::k_2_10_10_10_unknown:
      format = ColorRenderTargetFormat::k_2_10_10_10;
      break;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_unknown:
      format = ColorRenderTargetFormat::k_2_10_10_10_FLOAT;
      break;
    default:
      // Rest are OK
      break;
  }

  uint32_t tile_width = num_samples == MsaaSamples::k4X ? 40 : 80;
  uint32_t tile_height = num_samples != MsaaSamples::k1X ? 8 : 16;

  // Grab a tile view (as we need to clear an image first)
  TileViewKey key;
  key.color_or_depth = 1;
  key.msaa_samples = 0;  // static_cast<uint16_t>(num_samples);
  key.edram_format = static_cast<uint16_t>(format);
  key.tile_offset = edram_base;
  key.tile_width = xe::round_up(pitch, tile_width) / tile_width;
  // key.tile_height = xe::round_up(height, tile_height) / tile_height;
  key.tile_height = 160;
  auto tile_view = FindOrCreateTileView(command_buffer, key);
  assert_not_null(tile_view);

  VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkClearColorValue clear_value;
  std::memcpy(clear_value.float32, color, sizeof(float) * 4);

  // Issue a clear command
  vkCmdClearColorImage(command_buffer, tile_view->image,
                       VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1, &range);

  // Copy image back into EDRAM buffer
  // UpdateTileView(command_buffer, tile_view, false, false);
}

void RenderCache::ClearEDRAMDepthStencil(VkCommandBuffer command_buffer,
                                         uint32_t edram_base,
                                         DepthRenderTargetFormat format,
                                         uint32_t pitch, uint32_t height,
                                         MsaaSamples num_samples, float depth,
                                         uint32_t stencil) {
  // TODO: For formats <= 4 bpp, we can directly fill the EDRAM buffer. Just
  // need to detect this and calculate a value.

  uint32_t tile_width = num_samples == MsaaSamples::k4X ? 40 : 80;
  uint32_t tile_height = num_samples != MsaaSamples::k1X ? 8 : 16;

  // Grab a tile view (as we need to clear an image first)
  TileViewKey key;
  key.color_or_depth = 0;
  key.msaa_samples = 0;  // static_cast<uint16_t>(num_samples);
  key.edram_format = static_cast<uint16_t>(format);
  key.tile_offset = edram_base;
  key.tile_width = xe::round_up(pitch, tile_width) / tile_width;
  // key.tile_height = xe::round_up(height, tile_height) / tile_height;
  key.tile_height = 160;
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
  // UpdateTileView(command_buffer, tile_view, false, false);
}

void RenderCache::FillEDRAM(VkCommandBuffer command_buffer, uint32_t value) {
  vkCmdFillBuffer(command_buffer, edram_buffer_, 0, kEdramBufferCapacity,
                  value);
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

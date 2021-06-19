/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_render_target_cache.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

VulkanRenderTargetCache::VulkanRenderTargetCache(
    VulkanCommandProcessor& command_processor,
    const RegisterFile& register_file)
    : RenderTargetCache(register_file), command_processor_(command_processor) {}

VulkanRenderTargetCache::~VulkanRenderTargetCache() { Shutdown(true); }

bool VulkanRenderTargetCache::Initialize() {
  InitializeCommon();
  return true;
}

void VulkanRenderTargetCache::Shutdown(bool from_destructor) {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  last_update_framebuffer_ = VK_NULL_HANDLE;
  for (const auto& framebuffer_pair : framebuffers_) {
    dfn.vkDestroyFramebuffer(device, framebuffer_pair.second.framebuffer,
                             nullptr);
  }
  framebuffers_.clear();

  last_update_render_pass_ = VK_NULL_HANDLE;
  for (const auto& render_pass_pair : render_passes_) {
    dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
  }
  render_passes_.clear();

  if (!from_destructor) {
    ShutdownCommon();
  }
}

void VulkanRenderTargetCache::ClearCache() {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // Framebuffer objects must be destroyed because they reference views of
  // attachment images, which may be removed by the common ClearCache.
  last_update_framebuffer_ = VK_NULL_HANDLE;
  for (const auto& framebuffer_pair : framebuffers_) {
    dfn.vkDestroyFramebuffer(device, framebuffer_pair.second.framebuffer,
                             nullptr);
  }
  framebuffers_.clear();

  last_update_render_pass_ = VK_NULL_HANDLE;
  for (const auto& render_pass_pair : render_passes_) {
    dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
  }
  render_passes_.clear();

  RenderTargetCache::ClearCache();
}

bool VulkanRenderTargetCache::Update(bool is_rasterization_done,
                                     uint32_t shader_writes_color_targets) {
  if (!RenderTargetCache::Update(is_rasterization_done,
                                 shader_writes_color_targets)) {
    return false;
  }

  auto rb_surface_info = register_file().Get<reg::RB_SURFACE_INFO>();
  RenderTarget* const* depth_and_color_render_targets =
      last_update_accumulated_render_targets();
  uint32_t render_targets_are_srgb =
      gamma_render_target_as_srgb_
          ? last_update_accumulated_color_targets_are_gamma()
          : 0;

  RenderPassKey render_pass_key;
  render_pass_key.msaa_samples = rb_surface_info.msaa_samples;
  // TODO(Triang3l): 2x MSAA as 4x.
  if (depth_and_color_render_targets[0]) {
    render_pass_key.depth_and_color_used |= 1 << 0;
    render_pass_key.depth_format =
        depth_and_color_render_targets[0]->key().GetDepthFormat();
  }
  if (depth_and_color_render_targets[1]) {
    render_pass_key.depth_and_color_used |= 1 << 1;
    render_pass_key.color_0_view_format =
        (render_targets_are_srgb & (1 << 0))
            ? xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA
            : depth_and_color_render_targets[1]->key().GetColorFormat();
  }
  if (depth_and_color_render_targets[2]) {
    render_pass_key.depth_and_color_used |= 1 << 2;
    render_pass_key.color_1_view_format =
        (render_targets_are_srgb & (1 << 1))
            ? xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA
            : depth_and_color_render_targets[2]->key().GetColorFormat();
  }
  if (depth_and_color_render_targets[3]) {
    render_pass_key.depth_and_color_used |= 1 << 3;
    render_pass_key.color_2_view_format =
        (render_targets_are_srgb & (1 << 2))
            ? xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA
            : depth_and_color_render_targets[3]->key().GetColorFormat();
  }
  if (depth_and_color_render_targets[4]) {
    render_pass_key.depth_and_color_used |= 1 << 4;
    render_pass_key.color_3_view_format =
        (render_targets_are_srgb & (1 << 3))
            ? xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA
            : depth_and_color_render_targets[4]->key().GetColorFormat();
  }

  const Framebuffer* framebuffer = last_update_framebuffer_;
  VkRenderPass render_pass = last_update_render_pass_key_ == render_pass_key
                                 ? last_update_render_pass_
                                 : VK_NULL_HANDLE;
  if (render_pass == VK_NULL_HANDLE) {
    render_pass = GetRenderPass(render_pass_key);
    if (render_pass == VK_NULL_HANDLE) {
      return false;
    }
    // Framebuffer for a different render pass needed now.
    framebuffer = nullptr;
  }

  uint32_t pitch_tiles_at_32bpp =
      ((rb_surface_info.surface_pitch
        << uint32_t(rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X)) +
       (xenos::kEdramTileWidthSamples - 1)) /
      xenos::kEdramTileWidthSamples;
  if (framebuffer) {
    if (last_update_framebuffer_pitch_tiles_at_32bpp_ != pitch_tiles_at_32bpp ||
        std::memcmp(last_update_framebuffer_attachments_,
                    depth_and_color_render_targets,
                    sizeof(last_update_framebuffer_attachments_))) {
      framebuffer = nullptr;
    }
  }
  if (!framebuffer) {
    framebuffer = GetFramebuffer(render_pass_key, pitch_tiles_at_32bpp,
                                 depth_and_color_render_targets);
    if (!framebuffer) {
      return false;
    }
  }

  // Successful update - write the new configuration.
  last_update_render_pass_key_ = render_pass_key;
  last_update_render_pass_ = render_pass;
  last_update_framebuffer_pitch_tiles_at_32bpp_ = pitch_tiles_at_32bpp;
  std::memcpy(last_update_framebuffer_attachments_,
              depth_and_color_render_targets,
              sizeof(last_update_framebuffer_attachments_));
  last_update_framebuffer_ = framebuffer;

  // Transition the used render targets.
  VkPipelineStageFlags barrier_src_stage_mask = 0;
  VkPipelineStageFlags barrier_dst_stage_mask = 0;
  VkImageMemoryBarrier barrier_image_memory[1 + xenos::kMaxColorRenderTargets];
  uint32_t barrier_image_memory_count = 0;
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    RenderTarget* rt = depth_and_color_render_targets[i];
    if (!rt) {
      continue;
    }
    auto& vulkan_rt = *static_cast<VulkanRenderTarget*>(rt);
    VkPipelineStageFlags rt_src_stage_mask = vulkan_rt.current_stage_mask();
    VkAccessFlags rt_src_access_mask = vulkan_rt.current_access_mask();
    VkImageLayout rt_old_layout = vulkan_rt.current_layout();
    VkPipelineStageFlags rt_dst_stage_mask;
    VkAccessFlags rt_dst_access_mask;
    VkImageLayout rt_new_layout;
    if (i) {
      rt_dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      rt_dst_access_mask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      rt_new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else {
      rt_dst_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      rt_dst_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      rt_new_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    bool rt_image_memory_barrier_needed =
        rt_src_access_mask != rt_dst_access_mask ||
        rt_old_layout != rt_new_layout;
    if (rt_image_memory_barrier_needed ||
        rt_src_stage_mask != rt_dst_stage_mask) {
      barrier_src_stage_mask |= rt_src_stage_mask;
      barrier_dst_stage_mask |= rt_dst_stage_mask;
      if (rt_image_memory_barrier_needed) {
        VkImageMemoryBarrier& rt_image_memory_barrier =
            barrier_image_memory[barrier_image_memory_count++];
        rt_image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        rt_image_memory_barrier.pNext = nullptr;
        rt_image_memory_barrier.srcAccessMask = rt_src_access_mask;
        rt_image_memory_barrier.dstAccessMask = rt_dst_access_mask;
        rt_image_memory_barrier.oldLayout = rt_old_layout;
        rt_image_memory_barrier.newLayout = rt_new_layout;
        rt_image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rt_image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rt_image_memory_barrier.image = vulkan_rt.image();
        ui::vulkan::util::InitializeSubresourceRange(
            rt_image_memory_barrier.subresourceRange,
            i ? VK_IMAGE_ASPECT_COLOR_BIT
              : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
      }
      vulkan_rt.SetUsage(rt_dst_stage_mask, rt_dst_access_mask, rt_new_layout);
    }
  }
  if (barrier_src_stage_mask || barrier_dst_stage_mask ||
      barrier_image_memory_count) {
    if (!barrier_src_stage_mask) {
      barrier_src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    if (!barrier_dst_stage_mask) {
      barrier_dst_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    command_processor_.EndRenderPass();
    command_processor_.deferred_command_buffer().CmdVkPipelineBarrier(
        barrier_src_stage_mask, barrier_dst_stage_mask, 0, 0, nullptr, 0,
        nullptr, barrier_image_memory_count, barrier_image_memory);
  }

  return true;
}

VkRenderPass VulkanRenderTargetCache::GetRenderPass(RenderPassKey key) {
  auto it = render_passes_.find(key.key);
  if (it != render_passes_.end()) {
    return it->second;
  }

  VkSampleCountFlagBits samples;
  switch (key.msaa_samples) {
    case xenos::MsaaSamples::k1X:
      samples = VK_SAMPLE_COUNT_1_BIT;
      break;
    case xenos::MsaaSamples::k2X:
      // Using unconditionally because if 2x is emulated as 4x, the key will
      // also contain 4x.
      samples = VK_SAMPLE_COUNT_2_BIT;
      break;
    case xenos::MsaaSamples::k4X:
      samples = VK_SAMPLE_COUNT_4_BIT;
      break;
    default:
      return VK_NULL_HANDLE;
  }

  VkAttachmentDescription attachments[1 + xenos::kMaxColorRenderTargets];
  if (key.depth_and_color_used & 0b1) {
    VkAttachmentDescription& attachment = attachments[0];
    attachment.flags = 0;
    attachment.format = GetDepthVulkanFormat(key.depth_format);
    attachment.samples = samples;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }
  VkAttachmentReference color_attachments[xenos::kMaxColorRenderTargets];
  xenos::ColorRenderTargetFormat color_formats[] = {
      key.color_0_view_format,
      key.color_1_view_format,
      key.color_2_view_format,
      key.color_3_view_format,
  };
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    VkAttachmentReference& color_attachment = color_attachments[i];
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    uint32_t attachment_bit = uint32_t(1) << (1 + i);
    if (!(key.depth_and_color_used & attachment_bit)) {
      color_attachment.attachment = VK_ATTACHMENT_UNUSED;
      continue;
    }
    uint32_t attachment_index =
        xe::bit_count(key.depth_and_color_used & (attachment_bit - 1));
    color_attachment.attachment = attachment_index;
    VkAttachmentDescription& attachment = attachments[attachment_index];
    attachment.flags = 0;
    attachment.format = GetColorVulkanFormat(color_formats[i]);
    attachment.samples = samples;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }

  VkAttachmentReference depth_stencil_attachment;
  depth_stencil_attachment.attachment =
      (key.depth_and_color_used & 0b1) ? 0 : VK_ATTACHMENT_UNUSED;
  depth_stencil_attachment.layout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass;
  subpass.flags = 0;
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.inputAttachmentCount = 0;
  subpass.pInputAttachments = nullptr;
  subpass.colorAttachmentCount =
      32 - xe::lzcnt(uint32_t(key.depth_and_color_used >> 1));
  subpass.pColorAttachments = color_attachments;
  subpass.pResolveAttachments = nullptr;
  subpass.pDepthStencilAttachment =
      (key.depth_and_color_used & 0b1) ? &depth_stencil_attachment : nullptr;
  subpass.preserveAttachmentCount = 0;
  subpass.pPreserveAttachments = nullptr;

  VkPipelineStageFlags dependency_stage_mask = 0;
  VkAccessFlags dependency_access_mask = 0;
  if (key.depth_and_color_used & 0b1) {
    dependency_stage_mask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency_access_mask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  }
  if (key.depth_and_color_used >> 1) {
    dependency_stage_mask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency_access_mask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  }
  VkSubpassDependency subpass_dependencies[2];
  subpass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  subpass_dependencies[0].dstSubpass = 0;
  subpass_dependencies[0].srcStageMask = dependency_stage_mask;
  subpass_dependencies[0].dstStageMask = dependency_stage_mask;
  subpass_dependencies[0].srcAccessMask = dependency_access_mask;
  subpass_dependencies[0].dstAccessMask = dependency_access_mask;
  subpass_dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  subpass_dependencies[1].srcSubpass = 0;
  subpass_dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  subpass_dependencies[1].srcStageMask = dependency_stage_mask;
  subpass_dependencies[1].dstStageMask = dependency_stage_mask;
  subpass_dependencies[1].srcAccessMask = dependency_access_mask;
  subpass_dependencies[1].dstAccessMask = dependency_access_mask;
  subpass_dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  VkRenderPassCreateInfo render_pass_create_info;
  render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_create_info.pNext = nullptr;
  render_pass_create_info.flags = 0;
  render_pass_create_info.attachmentCount =
      xe::bit_count(key.depth_and_color_used);
  render_pass_create_info.pAttachments = attachments;
  render_pass_create_info.subpassCount = 1;
  render_pass_create_info.pSubpasses = &subpass;
  render_pass_create_info.dependencyCount =
      key.depth_and_color_used ? uint32_t(xe::countof(subpass_dependencies))
                               : 0;
  render_pass_create_info.pDependencies = subpass_dependencies;

  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  VkRenderPass render_pass;
  if (dfn.vkCreateRenderPass(device, &render_pass_create_info, nullptr,
                             &render_pass) != VK_SUCCESS) {
    XELOGE("Failed to create a Vulkan render pass");
    return VK_NULL_HANDLE;
  }
  render_passes_.emplace(key.key, render_pass);
  return render_pass;
}

VkFormat VulkanRenderTargetCache::GetDepthVulkanFormat(
    xenos::DepthRenderTargetFormat format) const {
  // TODO(Triang3l): Conditional 24-bit depth.
  return VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkFormat VulkanRenderTargetCache::GetColorVulkanFormat(
    xenos::ColorRenderTargetFormat format) const {
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return gamma_render_target_as_srgb_ ? VK_FORMAT_R8G8B8A8_SRGB
                                          : VK_FORMAT_R8G8B8A8_UNORM;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case xenos::ColorRenderTargetFormat::k_16_16:
      // TODO(Triang3l): Fallback to float16 (disregarding clearing correctness
      // likely) - possibly on render target gathering, treating them entirely
      // as float16.
      return VK_FORMAT_R16G16_SNORM;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      // TODO(Triang3l): Fallback to float16 (disregarding clearing correctness
      // likely) - possibly on render target gathering, treating them entirely
      // as float16.
      return VK_FORMAT_R16G16B16A16_SNORM;
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return VK_FORMAT_R16G16_SFLOAT;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return VK_FORMAT_R32_SFLOAT;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return VK_FORMAT_R32G32_SFLOAT;
    default:
      assert_unhandled_case(format);
      return VK_FORMAT_UNDEFINED;
  }
}

VkFormat VulkanRenderTargetCache::GetColorOwnershipTransferVulkanFormat(
    xenos::ColorRenderTargetFormat format, bool* is_integer_out) const {
  if (is_integer_out) {
    *is_integer_out = true;
  }
  // Floating-point numbers have NaNs that need to be propagated without
  // modifications to the bit representation, and SNORM has two representations
  // of -1.
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_16_16:
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return VK_FORMAT_R16G16_UINT;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return VK_FORMAT_R16G16B16A16_UINT;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return VK_FORMAT_R32_UINT;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return VK_FORMAT_R32G32_UINT;
    default:
      if (is_integer_out) {
        *is_integer_out = false;
      }
      return GetColorVulkanFormat(format);
  }
}

VulkanRenderTargetCache::VulkanRenderTarget::~VulkanRenderTarget() {
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  if (view_color_transfer_separate_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, view_color_transfer_separate_, nullptr);
  }
  if (view_srgb_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, view_srgb_, nullptr);
  }
  if (view_stencil_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, view_stencil_, nullptr);
  }
  if (view_depth_stencil_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, view_depth_stencil_, nullptr);
  }
  dfn.vkDestroyImageView(device, view_depth_color_, nullptr);
  dfn.vkDestroyImage(device, image_, nullptr);
  dfn.vkFreeMemory(device, memory_, nullptr);
}

uint32_t VulkanRenderTargetCache::GetMaxRenderTargetWidth() const {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  return provider.device_properties().limits.maxFramebufferWidth;
}

uint32_t VulkanRenderTargetCache::GetMaxRenderTargetHeight() const {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  return provider.device_properties().limits.maxFramebufferHeight;
}

RenderTargetCache::RenderTarget* VulkanRenderTargetCache::CreateRenderTarget(
    RenderTargetKey key) {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // Create the image.

  VkImageCreateInfo image_create_info;
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.pNext = nullptr;
  image_create_info.flags = 0;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  // TODO(Triang3l): Resolution scaling.
  image_create_info.extent.width = key.GetWidth();
  image_create_info.extent.height =
      GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples);
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  // TODO(Triang3l): 2x MSAA as 4x.
  image_create_info.samples =
      VkSampleCountFlagBits(uint32_t(1) << uint32_t(key.msaa_samples));
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = nullptr;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkFormat transfer_format;
  bool is_srgb_view_needed = false;
  if (key.is_depth) {
    image_create_info.format = GetDepthVulkanFormat(key.GetDepthFormat());
    transfer_format = image_create_info.format;
    image_create_info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  } else {
    xenos::ColorRenderTargetFormat color_format = key.GetColorFormat();
    image_create_info.format = GetColorVulkanFormat(color_format);
    transfer_format = GetColorOwnershipTransferVulkanFormat(color_format);
    is_srgb_view_needed =
        gamma_render_target_as_srgb_ &&
        (color_format == xenos::ColorRenderTargetFormat::k_8_8_8_8 ||
         color_format == xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA);
    if (image_create_info.format != transfer_format || is_srgb_view_needed) {
      image_create_info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    }
    image_create_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (image_create_info.format == VK_FORMAT_UNDEFINED) {
    XELOGE("VulkanRenderTargetCache: Unknown {} render target format {}",
           key.is_depth ? "depth" : "color", key.resource_format);
    return nullptr;
  }
  VkImage image;
  if (dfn.vkCreateImage(device, &image_create_info, nullptr, &image) !=
      VK_SUCCESS) {
    // TODO(Triang3l): Error message.
    return nullptr;
  }

  // Allocate and bind the memory.

  VkMemoryAllocateInfo memory_allocate_info;
  VkMemoryRequirements memory_requirements;
  dfn.vkGetImageMemoryRequirements(device, image, &memory_requirements);
  if (!xe::bit_scan_forward(memory_requirements.memoryTypeBits &
                                provider.memory_types_device_local(),
                            &memory_allocate_info.memoryTypeIndex)) {
    dfn.vkDestroyImage(device, image, nullptr);
    return nullptr;
  }
  memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  VkMemoryDedicatedAllocateInfoKHR memory_dedicated_allocate_info;
  if (provider.device_extensions().khr_dedicated_allocation) {
    memory_dedicated_allocate_info.sType =
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
    memory_dedicated_allocate_info.pNext = nullptr;
    memory_dedicated_allocate_info.image = image;
    memory_dedicated_allocate_info.buffer = VK_NULL_HANDLE;
    memory_allocate_info.pNext = &memory_dedicated_allocate_info;
  } else {
    memory_allocate_info.pNext = nullptr;
  }
  memory_allocate_info.allocationSize = memory_requirements.size;
  VkDeviceMemory memory;
  if (dfn.vkAllocateMemory(device, &memory_allocate_info, nullptr, &memory) !=
      VK_SUCCESS) {
    // TODO(Triang3l): Error message.
    dfn.vkDestroyImage(device, image, nullptr);
    return nullptr;
  }
  if (dfn.vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
    // TODO(Triang3l): Error message.
    dfn.vkDestroyImage(device, image, nullptr);
    dfn.vkFreeMemory(device, memory, nullptr);
    return nullptr;
  }

  // Create the image views.

  VkImageViewCreateInfo view_create_info;
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.pNext = nullptr;
  view_create_info.flags = 0;
  view_create_info.image = image;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_create_info.format = image_create_info.format;
  view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  ui::vulkan::util::InitializeSubresourceRange(
      view_create_info.subresourceRange,
      key.is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
  VkImageView view_depth_color;
  if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                            &view_depth_color) != VK_SUCCESS) {
    // TODO(Triang3l): Error message.
    dfn.vkDestroyImage(device, image, nullptr);
    dfn.vkFreeMemory(device, memory, nullptr);
    return nullptr;
  }
  VkImageView view_depth_stencil = VK_NULL_HANDLE;
  VkImageView view_stencil = VK_NULL_HANDLE;
  VkImageView view_srgb = VK_NULL_HANDLE;
  VkImageView view_color_transfer_separate = VK_NULL_HANDLE;
  if (key.is_depth) {
    view_create_info.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                              &view_depth_stencil) != VK_SUCCESS) {
      // TODO(Triang3l): Error message.
      dfn.vkDestroyImageView(device, view_depth_color, nullptr);
      dfn.vkDestroyImage(device, image, nullptr);
      dfn.vkFreeMemory(device, memory, nullptr);
      return nullptr;
    }
    view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                              &view_stencil) != VK_SUCCESS) {
      // TODO(Triang3l): Error message.
      dfn.vkDestroyImageView(device, view_depth_stencil, nullptr);
      dfn.vkDestroyImageView(device, view_depth_color, nullptr);
      dfn.vkDestroyImage(device, image, nullptr);
      dfn.vkFreeMemory(device, memory, nullptr);
      return nullptr;
    }
  } else {
    if (is_srgb_view_needed) {
      view_create_info.format = VK_FORMAT_R8G8B8A8_SRGB;
      if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                                &view_srgb) != VK_SUCCESS) {
        // TODO(Triang3l): Error message.
        dfn.vkDestroyImageView(device, view_depth_color, nullptr);
        dfn.vkDestroyImage(device, image, nullptr);
        dfn.vkFreeMemory(device, memory, nullptr);
        return nullptr;
      }
    }
    if (transfer_format != image_create_info.format) {
      view_create_info.format = transfer_format;
      if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                                &view_color_transfer_separate) != VK_SUCCESS) {
        // TODO(Triang3l): Error message.
        if (view_srgb != VK_NULL_HANDLE) {
          dfn.vkDestroyImageView(device, view_srgb, nullptr);
        }
        dfn.vkDestroyImageView(device, view_depth_color, nullptr);
        dfn.vkDestroyImage(device, image, nullptr);
        dfn.vkFreeMemory(device, memory, nullptr);
        return nullptr;
      }
    }
  }

  VkImageView view_transfer_separate = VK_NULL_HANDLE;

  return new VulkanRenderTarget(key, provider, image, memory, view_depth_color,
                                view_depth_stencil, view_stencil, view_srgb,
                                view_color_transfer_separate);
}

const VulkanRenderTargetCache::Framebuffer*
VulkanRenderTargetCache::GetFramebuffer(
    RenderPassKey render_pass_key, uint32_t pitch_tiles_at_32bpp,
    const RenderTarget* const* depth_and_color_render_targets) {
  FramebufferKey key;
  key.render_pass_key = render_pass_key;
  key.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
  if (render_pass_key.depth_and_color_used & (1 << 0)) {
    key.depth_base_tiles = depth_and_color_render_targets[0]->key().base_tiles;
  }
  if (render_pass_key.depth_and_color_used & (1 << 1)) {
    key.color_0_base_tiles =
        depth_and_color_render_targets[1]->key().base_tiles;
  }
  if (render_pass_key.depth_and_color_used & (1 << 2)) {
    key.color_1_base_tiles =
        depth_and_color_render_targets[2]->key().base_tiles;
  }
  if (render_pass_key.depth_and_color_used & (1 << 3)) {
    key.color_2_base_tiles =
        depth_and_color_render_targets[3]->key().base_tiles;
  }
  if (render_pass_key.depth_and_color_used & (1 << 4)) {
    key.color_3_base_tiles =
        depth_and_color_render_targets[4]->key().base_tiles;
  }
  auto it = framebuffers_.find(key);
  if (it != framebuffers_.end()) {
    return &it->second;
  }

  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  VkRenderPass render_pass = GetRenderPass(render_pass_key);
  if (render_pass == VK_NULL_HANDLE) {
    return nullptr;
  }

  VkImageView attachments[1 + xenos::kMaxColorRenderTargets];
  uint32_t attachment_count = 0;
  uint32_t depth_and_color_rts_remaining = render_pass_key.depth_and_color_used;
  uint32_t rt_index;
  while (xe::bit_scan_forward(depth_and_color_rts_remaining, &rt_index)) {
    depth_and_color_rts_remaining &= ~(uint32_t(1) << rt_index);
    const auto& vulkan_rt = *static_cast<const VulkanRenderTarget*>(
        depth_and_color_render_targets[rt_index]);
    attachments[attachment_count++] = rt_index ? vulkan_rt.view_depth_color()
                                               : vulkan_rt.view_depth_stencil();
  }

  VkFramebufferCreateInfo framebuffer_create_info;
  framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_create_info.pNext = nullptr;
  framebuffer_create_info.flags = 0;
  framebuffer_create_info.renderPass = render_pass;
  framebuffer_create_info.attachmentCount = attachment_count;
  framebuffer_create_info.pAttachments = attachments;
  VkExtent2D host_extent;
  if (pitch_tiles_at_32bpp) {
    host_extent.width = RenderTargetKey::GetWidth(pitch_tiles_at_32bpp,
                                                  render_pass_key.msaa_samples);
    host_extent.height = GetRenderTargetHeight(pitch_tiles_at_32bpp,
                                               render_pass_key.msaa_samples);
  } else {
    assert_zero(render_pass_key.depth_and_color_used);
    host_extent.width = 0;
    host_extent.height = 0;
  }
  // Vulkan requires width and height greater than 0.
  framebuffer_create_info.width = std::max(host_extent.width, uint32_t(1));
  framebuffer_create_info.height = std::max(host_extent.height, uint32_t(1));
  framebuffer_create_info.layers = 1;
  VkFramebuffer framebuffer;
  if (dfn.vkCreateFramebuffer(device, &framebuffer_create_info, nullptr,
                              &framebuffer) != VK_SUCCESS) {
    return nullptr;
  }
  // Creates at a persistent location - safe to use pointers.
  return &framebuffers_
              .emplace(std::piecewise_construct, std::forward_as_tuple(key),
                       std::forward_as_tuple(framebuffer, host_extent))
              .first->second;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

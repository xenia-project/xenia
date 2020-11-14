/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_render_target_cache.h"

#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"

namespace xe {
namespace gpu {
namespace vulkan {

VulkanRenderTargetCache::VulkanRenderTargetCache(
    VulkanCommandProcessor& command_processor,
    const RegisterFile& register_file)
    : command_processor_(command_processor), register_file_(register_file) {}

VulkanRenderTargetCache::~VulkanRenderTargetCache() { Shutdown(); }

bool VulkanRenderTargetCache::Initialize() { return true; }

void VulkanRenderTargetCache::Shutdown() { ClearCache(); }

void VulkanRenderTargetCache::ClearCache() {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  for (const auto& framebuffer_pair : framebuffers_) {
    dfn.vkDestroyFramebuffer(device, framebuffer_pair.second, nullptr);
  }
  framebuffers_.clear();

  for (const auto& render_pass_pair : render_passes_) {
    dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
  }
  render_passes_.clear();
}

VkRenderPass VulkanRenderTargetCache::GetRenderPass(RenderPassKey key) {
  auto it = render_passes_.find(key.key);
  if (it != render_passes_.end()) {
    return it->second;
  }

  // TODO(Triang3l): Attachments and dependencies.

  VkSubpassDescription subpass_description;
  subpass_description.flags = 0;
  subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass_description.inputAttachmentCount = 0;
  subpass_description.pInputAttachments = nullptr;
  subpass_description.colorAttachmentCount = 0;
  subpass_description.pColorAttachments = nullptr;
  subpass_description.pResolveAttachments = nullptr;
  subpass_description.pDepthStencilAttachment = nullptr;
  subpass_description.preserveAttachmentCount = 0;
  subpass_description.pPreserveAttachments = nullptr;

  VkRenderPassCreateInfo render_pass_create_info;
  render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_create_info.pNext = nullptr;
  render_pass_create_info.flags = 0;
  render_pass_create_info.attachmentCount = 0;
  render_pass_create_info.pAttachments = nullptr;
  render_pass_create_info.subpassCount = 1;
  render_pass_create_info.pSubpasses = &subpass_description;
  render_pass_create_info.dependencyCount = 0;
  render_pass_create_info.pDependencies = nullptr;

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

VkFramebuffer VulkanRenderTargetCache::GetFramebuffer(FramebufferKey key) {
  auto it = framebuffers_.find(key);
  if (it != framebuffers_.end()) {
    return it->second;
  }

  VkRenderPass render_pass = GetRenderPass(key.render_pass_key);
  if (render_pass == VK_NULL_HANDLE) {
    return VK_NULL_HANDLE;
  }

  VkFramebufferCreateInfo framebuffer_create_info;
  framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_create_info.pNext = nullptr;
  framebuffer_create_info.flags = 0;
  framebuffer_create_info.renderPass = render_pass;
  framebuffer_create_info.attachmentCount = 0;
  framebuffer_create_info.pAttachments = nullptr;
  framebuffer_create_info.width = 1280;
  framebuffer_create_info.height = 720;
  framebuffer_create_info.layers = 1;

  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  VkFramebuffer framebuffer;
  if (dfn.vkCreateFramebuffer(device, &framebuffer_create_info, nullptr,
                              &framebuffer) != VK_SUCCESS) {
    XELOGE("Failed to create a Vulkan framebuffer");
    return VK_NULL_HANDLE;
  }
  framebuffers_.emplace(key, framebuffer);
  return framebuffer;
}

bool VulkanRenderTargetCache::UpdateRenderTargets(
    FramebufferKey& framebuffer_key_out) {
  framebuffer_key_out = FramebufferKey();
  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

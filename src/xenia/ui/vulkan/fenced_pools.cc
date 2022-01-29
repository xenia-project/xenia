/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/fenced_pools.h"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace ui {
namespace vulkan {

using util::CheckResult;

CommandBufferPool::CommandBufferPool(const VulkanProvider& provider,
                                     uint32_t queue_family_index)
    : BaseFencedPool(provider) {
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();

  // Create the pool used for allocating buffers.
  // They are marked as transient (short-lived) and cycled frequently.
  VkCommandPoolCreateInfo cmd_pool_info;
  cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmd_pool_info.pNext = nullptr;
  cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cmd_pool_info.queueFamilyIndex = queue_family_index;
  auto err =
      dfn.vkCreateCommandPool(device, &cmd_pool_info, nullptr, &command_pool_);
  CheckResult(err, "vkCreateCommandPool");

  // Allocate a bunch of command buffers to start.
  constexpr uint32_t kDefaultCount = 32;
  VkCommandBufferAllocateInfo command_buffer_info;
  command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_info.pNext = nullptr;
  command_buffer_info.commandPool = command_pool_;
  command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_info.commandBufferCount = kDefaultCount;
  VkCommandBuffer command_buffers[kDefaultCount];
  err = dfn.vkAllocateCommandBuffers(device, &command_buffer_info,
                                     command_buffers);
  CheckResult(err, "vkCreateCommandBuffer");
  for (size_t i = 0; i < xe::countof(command_buffers); ++i) {
    PushEntry(command_buffers[i], nullptr);
  }
}

CommandBufferPool::~CommandBufferPool() {
  FreeAllEntries();
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  dfn.vkDestroyCommandPool(device, command_pool_, nullptr);
  command_pool_ = nullptr;
}

VkCommandBuffer CommandBufferPool::AllocateEntry(void* data) {
  // TODO(benvanik): allocate a bunch at once?
  VkCommandBufferAllocateInfo command_buffer_info;
  command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_info.pNext = nullptr;
  command_buffer_info.commandPool = command_pool_;
  command_buffer_info.level =
      VkCommandBufferLevel(reinterpret_cast<uintptr_t>(data));
  command_buffer_info.commandBufferCount = 1;
  VkCommandBuffer command_buffer;
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  auto err = dfn.vkAllocateCommandBuffers(device, &command_buffer_info,
                                          &command_buffer);
  CheckResult(err, "vkCreateCommandBuffer");
  return command_buffer;
}

void CommandBufferPool::FreeEntry(VkCommandBuffer handle) {
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  dfn.vkFreeCommandBuffers(device, command_pool_, 1, &handle);
}

DescriptorPool::DescriptorPool(const VulkanProvider& provider,
                               uint32_t max_count,
                               std::vector<VkDescriptorPoolSize> pool_sizes)
    : BaseFencedPool(provider) {
  VkDescriptorPoolCreateInfo descriptor_pool_info;
  descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_info.pNext = nullptr;
  descriptor_pool_info.flags =
      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  descriptor_pool_info.maxSets = max_count;
  descriptor_pool_info.poolSizeCount = uint32_t(pool_sizes.size());
  descriptor_pool_info.pPoolSizes = pool_sizes.data();
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  auto err = dfn.vkCreateDescriptorPool(device, &descriptor_pool_info, nullptr,
                                        &descriptor_pool_);
  CheckResult(err, "vkCreateDescriptorPool");
}
DescriptorPool::~DescriptorPool() {
  FreeAllEntries();
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  dfn.vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
  descriptor_pool_ = nullptr;
}

VkDescriptorSet DescriptorPool::AllocateEntry(void* data) {
  VkDescriptorSetLayout layout = reinterpret_cast<VkDescriptorSetLayout>(data);

  VkDescriptorSet descriptor_set = nullptr;
  VkDescriptorSetAllocateInfo set_alloc_info;
  set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_alloc_info.pNext = nullptr;
  set_alloc_info.descriptorPool = descriptor_pool_;
  set_alloc_info.descriptorSetCount = 1;
  set_alloc_info.pSetLayouts = &layout;
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  auto err =
      dfn.vkAllocateDescriptorSets(device, &set_alloc_info, &descriptor_set);
  CheckResult(err, "vkAllocateDescriptorSets");

  return descriptor_set;
}

void DescriptorPool::FreeEntry(VkDescriptorSet handle) {
  const VulkanProvider::DeviceFunctions& dfn = provider_.dfn();
  VkDevice device = provider_.device();
  dfn.vkFreeDescriptorSets(device, descriptor_pool_, 1, &handle);
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

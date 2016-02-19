/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/buffer_cache.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

BufferCache::BufferCache(RegisterFile* register_file,
                         ui::vulkan::VulkanDevice* device, size_t capacity)
    : register_file_(register_file),
      device_(*device),
      transient_capacity_(capacity) {
  // Uniform buffer.
  VkBufferCreateInfo uniform_buffer_info;
  uniform_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  uniform_buffer_info.pNext = nullptr;
  uniform_buffer_info.flags = 0;
  uniform_buffer_info.size = transient_capacity_;
  uniform_buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  uniform_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  uniform_buffer_info.queueFamilyIndexCount = 0;
  uniform_buffer_info.pQueueFamilyIndices = nullptr;
  auto err = vkCreateBuffer(device_, &uniform_buffer_info, nullptr,
                            &transient_uniform_buffer_);
  CheckResult(err, "vkCreateBuffer");

  // Index buffer.
  VkBufferCreateInfo index_buffer_info;
  index_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  index_buffer_info.pNext = nullptr;
  index_buffer_info.flags = 0;
  index_buffer_info.size = transient_capacity_;
  index_buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  index_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  index_buffer_info.queueFamilyIndexCount = 0;
  index_buffer_info.pQueueFamilyIndices = nullptr;
  err = vkCreateBuffer(device_, &index_buffer_info, nullptr,
                       &transient_index_buffer_);
  CheckResult(err, "vkCreateBuffer");

  // Vertex buffer.
  VkBufferCreateInfo vertex_buffer_info;
  vertex_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  vertex_buffer_info.pNext = nullptr;
  vertex_buffer_info.flags = 0;
  vertex_buffer_info.size = transient_capacity_;
  vertex_buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  vertex_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  vertex_buffer_info.queueFamilyIndexCount = 0;
  vertex_buffer_info.pQueueFamilyIndices = nullptr;
  err = vkCreateBuffer(*device, &vertex_buffer_info, nullptr,
                       &transient_vertex_buffer_);
  CheckResult(err, "vkCreateBuffer");

  // Allocate the underlying buffer we use for all storage.
  // We query all types and take the max alignment.
  VkMemoryRequirements uniform_buffer_requirements;
  VkMemoryRequirements index_buffer_requirements;
  VkMemoryRequirements vertex_buffer_requirements;
  vkGetBufferMemoryRequirements(device_, transient_uniform_buffer_,
                                &uniform_buffer_requirements);
  vkGetBufferMemoryRequirements(device_, transient_index_buffer_,
                                &index_buffer_requirements);
  vkGetBufferMemoryRequirements(device_, transient_vertex_buffer_,
                                &vertex_buffer_requirements);
  uniform_buffer_alignment_ = uniform_buffer_requirements.alignment;
  index_buffer_alignment_ = index_buffer_requirements.alignment;
  vertex_buffer_alignment_ = vertex_buffer_requirements.alignment;
  VkMemoryRequirements buffer_requirements;
  buffer_requirements.size = transient_capacity_;
  buffer_requirements.alignment =
      std::max(uniform_buffer_requirements.alignment,
               std::max(index_buffer_requirements.alignment,
                        vertex_buffer_requirements.alignment));
  buffer_requirements.memoryTypeBits =
      uniform_buffer_requirements.memoryTypeBits |
      index_buffer_requirements.memoryTypeBits |
      vertex_buffer_requirements.memoryTypeBits;
  transient_buffer_memory_ = device->AllocateMemory(
      buffer_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

  // Alias all buffers to our memory.
  vkBindBufferMemory(device_, transient_uniform_buffer_,
                     transient_buffer_memory_, 0);
  vkBindBufferMemory(device_, transient_index_buffer_, transient_buffer_memory_,
                     0);
  vkBindBufferMemory(device_, transient_vertex_buffer_,
                     transient_buffer_memory_, 0);

  // Map memory and keep it mapped while we use it.
  err = vkMapMemory(device_, transient_buffer_memory_, 0, VK_WHOLE_SIZE, 0,
                    &transient_buffer_data_);
  CheckResult(err, "vkMapMemory");

  // Descriptor pool used for all of our cached descriptors.
  // In the steady state we don't allocate anything, so these are all manually
  // managed.
  VkDescriptorPoolCreateInfo descriptor_pool_info;
  descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_info.pNext = nullptr;
  descriptor_pool_info.flags =
      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  descriptor_pool_info.maxSets = 1;
  VkDescriptorPoolSize pool_sizes[1];
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  pool_sizes[0].descriptorCount = 2;
  descriptor_pool_info.poolSizeCount = 1;
  descriptor_pool_info.pPoolSizes = pool_sizes;
  err = vkCreateDescriptorPool(device_, &descriptor_pool_info, nullptr,
                               &descriptor_pool_);
  CheckResult(err, "vkCreateDescriptorPool");

  // Create the descriptor set layout used for our uniform buffer.
  // As it is a static binding that uses dynamic offsets during draws we can
  // create this once and reuse it forever.
  VkDescriptorSetLayoutBinding vertex_uniform_binding;
  vertex_uniform_binding.binding = 0;
  vertex_uniform_binding.descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  vertex_uniform_binding.descriptorCount = 1;
  vertex_uniform_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  vertex_uniform_binding.pImmutableSamplers = nullptr;
  VkDescriptorSetLayoutBinding fragment_uniform_binding;
  fragment_uniform_binding.binding = 1;
  fragment_uniform_binding.descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  fragment_uniform_binding.descriptorCount = 1;
  fragment_uniform_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragment_uniform_binding.pImmutableSamplers = nullptr;
  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info;
  descriptor_set_layout_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_info.pNext = nullptr;
  descriptor_set_layout_info.flags = 0;
  VkDescriptorSetLayoutBinding uniform_bindings[] = {
      vertex_uniform_binding, fragment_uniform_binding,
  };
  descriptor_set_layout_info.bindingCount =
      static_cast<uint32_t>(xe::countof(uniform_bindings));
  descriptor_set_layout_info.pBindings = uniform_bindings;
  err = vkCreateDescriptorSetLayout(device_, &descriptor_set_layout_info,
                                    nullptr, &descriptor_set_layout_);
  CheckResult(err, "vkCreateDescriptorSetLayout");

  // Create the descriptor we'll use for the uniform buffer.
  // This is what we hand out to everyone (who then also needs to use our
  // offsets).
  VkDescriptorSetAllocateInfo set_alloc_info;
  set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_alloc_info.pNext = nullptr;
  set_alloc_info.descriptorPool = descriptor_pool_;
  set_alloc_info.descriptorSetCount = 1;
  set_alloc_info.pSetLayouts = &descriptor_set_layout_;
  err = vkAllocateDescriptorSets(device_, &set_alloc_info,
                                 &transient_descriptor_set_);
  CheckResult(err, "vkAllocateDescriptorSets");
}

BufferCache::~BufferCache() {
  vkFreeDescriptorSets(device_, descriptor_pool_, 1,
                       &transient_descriptor_set_);
  vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
  vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
  vkUnmapMemory(device_, transient_buffer_memory_);
  vkFreeMemory(device_, transient_buffer_memory_, nullptr);
  vkDestroyBuffer(device_, transient_uniform_buffer_, nullptr);
  vkDestroyBuffer(device_, transient_index_buffer_, nullptr);
  vkDestroyBuffer(device_, transient_vertex_buffer_, nullptr);
}

VkDeviceSize BufferCache::UploadConstantRegisters(
    const Shader::ConstantRegisterMap& constant_register_map) {
  // Allocate space in the buffer for our data.
  auto offset = AllocateTransientData(uniform_buffer_alignment_,
                                      constant_register_map.packed_byte_length);
  if (offset == VK_WHOLE_SIZE) {
    // OOM.
    return VK_WHOLE_SIZE;
  }

  // Run through registers and copy them into the buffer.
  // TODO(benvanik): optimize this - it's hit twice every call.
  const auto& values = register_file_->values;
  uint8_t* dest_ptr =
      reinterpret_cast<uint8_t*>(transient_buffer_data_) + offset;
  for (int i = 0; i < 4; ++i) {
    auto piece = constant_register_map.float_bitmap[i];
    if (!piece) {
      continue;
    }
    for (int j = 0, sh = 0; j < 64; ++j, sh << 1) {
      if (piece & sh) {
        xe::copy_128_aligned(
            dest_ptr,
            &values[XE_GPU_REG_SHADER_CONSTANT_000_X + i * 64 + j].f32, 1);
        dest_ptr += 16;
      }
    }
  }
  for (int i = 0; i < 32; ++i) {
    if (constant_register_map.int_bitmap & (1 << i)) {
      xe::store<uint32_t>(dest_ptr,
                          values[XE_GPU_REG_SHADER_CONSTANT_LOOP_00 + i].u32);
      dest_ptr += 4;
    }
  }
  for (int i = 0; i < 8; ++i) {
    if (constant_register_map.bool_bitmap[i]) {
      xe::store<uint32_t>(
          dest_ptr, values[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 + i].u32);
      dest_ptr += 4;
    }
  }

  return offset;
}

std::pair<VkBuffer, VkDeviceSize> BufferCache::UploadIndexBuffer(
    const void* source_ptr, size_t source_length, IndexFormat format) {
  // TODO(benvanik): check cache.

  // Allocate space in the buffer for our data.
  auto offset = AllocateTransientData(index_buffer_alignment_, source_length);
  if (offset == VK_WHOLE_SIZE) {
    // OOM.
    return {nullptr, VK_WHOLE_SIZE};
  }

  // Copy data into the buffer.
  // TODO(benvanik): get min/max indices and pass back?
  // TODO(benvanik): memcpy then use compute shaders to swap?
  if (format == IndexFormat::kInt16) {
    // Endian::k8in16, swap half-words.
    xe::copy_and_swap_16_aligned(
        reinterpret_cast<uint16_t*>(transient_buffer_data_) + offset,
        reinterpret_cast<const uint16_t*>(source_ptr), source_length / 2);
  } else if (format == IndexFormat::kInt32) {
    // Endian::k8in32, swap words.
    xe::copy_and_swap_32_aligned(
        reinterpret_cast<uint32_t*>(transient_buffer_data_) + offset,
        reinterpret_cast<const uint32_t*>(source_ptr), source_length / 4);
  }

  return {transient_index_buffer_, offset};
}

std::pair<VkBuffer, VkDeviceSize> BufferCache::UploadVertexBuffer(
    const void* source_ptr, size_t source_length) {
  // TODO(benvanik): check cache.

  // Allocate space in the buffer for our data.
  auto offset = AllocateTransientData(vertex_buffer_alignment_, source_length);
  if (offset == VK_WHOLE_SIZE) {
    // OOM.
    return {nullptr, VK_WHOLE_SIZE};
  }

  // Copy data into the buffer.
  // TODO(benvanik): memcpy then use compute shaders to swap?
  // Endian::k8in32, swap words.
  xe::copy_and_swap_32_aligned(
      reinterpret_cast<uint32_t*>(transient_buffer_data_) + offset,
      reinterpret_cast<const uint32_t*>(source_ptr), source_length / 4);

  return {transient_vertex_buffer_, offset};
}

VkDeviceSize BufferCache::AllocateTransientData(size_t alignment,
                                                size_t length) {
  // Try to add to end, wrapping if required.

  // Check to ensure there is space.
  if (false) {
    // Consume all fences.
  }

  // Slice off our bit.

  return VK_WHOLE_SIZE;
}

void BufferCache::Flush(VkCommandBuffer command_buffer) {
  // If we are flushing a big enough chunk queue up an event.
  // We don't want to do this for everything but often enough so that we won't
  // run out of space.
  if (true) {
    // VkEvent finish_event;
    // vkCmdSetEvent(cmd_buffer, finish_event,
    //              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  }

  // Flush memory.
  // TODO(benvanik): subrange.
  VkMappedMemoryRange dirty_range;
  dirty_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  dirty_range.pNext = nullptr;
  dirty_range.memory = transient_buffer_memory_;
  dirty_range.offset = 0;
  dirty_range.size = transient_capacity_;
  vkFlushMappedMemoryRanges(device_, 1, &dirty_range);
}

void BufferCache::InvalidateCache() {
  // TODO(benvanik): caching.
}

void BufferCache::ClearCache() {
  // TODO(benvanik): caching.
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

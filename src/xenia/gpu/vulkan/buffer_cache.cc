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

constexpr VkDeviceSize kConstantRegisterUniformRange =
    512 * 4 * 4 + 8 * 4 + 32 * 4;

BufferCache::BufferCache(RegisterFile* register_file, Memory* memory,
                         ui::vulkan::VulkanDevice* device, size_t capacity)
    : register_file_(register_file), memory_(memory), device_(device) {
  transient_buffer_ = std::make_unique<ui::vulkan::CircularBuffer>(
      device_,
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      capacity);
}

BufferCache::~BufferCache() { Shutdown(); }

VkResult BufferCache::Initialize() {
  VkMemoryRequirements pool_reqs;
  transient_buffer_->GetBufferMemoryRequirements(&pool_reqs);
  gpu_memory_pool_ = device_->AllocateMemory(pool_reqs);

  VkResult status = transient_buffer_->Initialize(gpu_memory_pool_, 0);
  if (status != VK_SUCCESS) {
    return status;
  }

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
  status = vkCreateDescriptorPool(*device_, &descriptor_pool_info, nullptr,
                                  &descriptor_pool_);
  if (status != VK_SUCCESS) {
    return status;
  }

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
      vertex_uniform_binding,
      fragment_uniform_binding,
  };
  descriptor_set_layout_info.bindingCount =
      static_cast<uint32_t>(xe::countof(uniform_bindings));
  descriptor_set_layout_info.pBindings = uniform_bindings;
  status = vkCreateDescriptorSetLayout(*device_, &descriptor_set_layout_info,
                                       nullptr, &descriptor_set_layout_);
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create the descriptor we'll use for the uniform buffer.
  // This is what we hand out to everyone (who then also needs to use our
  // offsets).
  VkDescriptorSetAllocateInfo set_alloc_info;
  set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_alloc_info.pNext = nullptr;
  set_alloc_info.descriptorPool = descriptor_pool_;
  set_alloc_info.descriptorSetCount = 1;
  set_alloc_info.pSetLayouts = &descriptor_set_layout_;
  status = vkAllocateDescriptorSets(*device_, &set_alloc_info,
                                    &transient_descriptor_set_);
  if (status != VK_SUCCESS) {
    return status;
  }

  // Initialize descriptor set with our buffers.
  VkDescriptorBufferInfo buffer_info;
  buffer_info.buffer = transient_buffer_->gpu_buffer();
  buffer_info.offset = 0;
  buffer_info.range = kConstantRegisterUniformRange;

  VkWriteDescriptorSet descriptor_writes[2];
  auto& vertex_uniform_binding_write = descriptor_writes[0];
  vertex_uniform_binding_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  vertex_uniform_binding_write.pNext = nullptr;
  vertex_uniform_binding_write.dstSet = transient_descriptor_set_;
  vertex_uniform_binding_write.dstBinding = 0;
  vertex_uniform_binding_write.dstArrayElement = 0;
  vertex_uniform_binding_write.descriptorCount = 1;
  vertex_uniform_binding_write.descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  vertex_uniform_binding_write.pBufferInfo = &buffer_info;
  auto& fragment_uniform_binding_write = descriptor_writes[1];
  fragment_uniform_binding_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  fragment_uniform_binding_write.pNext = nullptr;
  fragment_uniform_binding_write.dstSet = transient_descriptor_set_;
  fragment_uniform_binding_write.dstBinding = 1;
  fragment_uniform_binding_write.dstArrayElement = 0;
  fragment_uniform_binding_write.descriptorCount = 1;
  fragment_uniform_binding_write.descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  fragment_uniform_binding_write.pBufferInfo = &buffer_info;
  vkUpdateDescriptorSets(*device_, 2, descriptor_writes, 0, nullptr);

  return VK_SUCCESS;
}

void BufferCache::Shutdown() {
  if (transient_descriptor_set_) {
    vkFreeDescriptorSets(*device_, descriptor_pool_, 1,
                         &transient_descriptor_set_);
    transient_descriptor_set_ = nullptr;
  }

  if (descriptor_set_layout_) {
    vkDestroyDescriptorSetLayout(*device_, descriptor_set_layout_, nullptr);
    descriptor_set_layout_ = nullptr;
  }

  if (descriptor_pool_) {
    vkDestroyDescriptorPool(*device_, descriptor_pool_, nullptr);
    descriptor_pool_ = nullptr;
  }

  transient_buffer_->Shutdown();

  if (gpu_memory_pool_) {
    vkFreeMemory(*device_, gpu_memory_pool_, nullptr);
    gpu_memory_pool_ = nullptr;
  }
}

std::pair<VkDeviceSize, VkDeviceSize> BufferCache::UploadConstantRegisters(
    VkCommandBuffer command_buffer,
    const Shader::ConstantRegisterMap& vertex_constant_register_map,
    const Shader::ConstantRegisterMap& pixel_constant_register_map,
    VkFence fence) {
  // Fat struct, including all registers:
  // struct {
  //   vec4 float[512];
  //   uint bool[8];
  //   uint loop[32];
  // };
  auto offset = AllocateTransientData(kConstantRegisterUniformRange, fence);
  if (offset == VK_WHOLE_SIZE) {
    // OOM.
    return {VK_WHOLE_SIZE, VK_WHOLE_SIZE};
  }

  // Copy over all the registers.
  const auto& values = register_file_->values;
  uint8_t* dest_ptr = transient_buffer_->host_base() + offset;
  std::memcpy(dest_ptr, &values[XE_GPU_REG_SHADER_CONSTANT_000_X].f32,
              (512 * 4 * 4));
  dest_ptr += 512 * 4 * 4;
  std::memcpy(dest_ptr, &values[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031].u32,
              8 * 4);
  dest_ptr += 8 * 4;
  std::memcpy(dest_ptr, &values[XE_GPU_REG_SHADER_CONSTANT_LOOP_00].u32,
              32 * 4);
  dest_ptr += 32 * 4;

  transient_buffer_->Flush(offset, kConstantRegisterUniformRange);

  // Append a barrier to the command buffer.
  VkBufferMemoryBarrier barrier = {
      VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      nullptr,
      VK_ACCESS_HOST_WRITE_BIT,
      VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      transient_buffer_->gpu_buffer(),
      offset,
      kConstantRegisterUniformRange,
  };
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1,
                       &barrier, 0, nullptr);

  return {offset, offset};

// Packed upload code.
// This is not currently supported by the shaders, but would be awesome.
// We should be able to use this for any shader that does not do dynamic
// constant indexing.
#if 0
  // Allocate space in the buffer for our data.
  auto offset =
      AllocateTransientData(constant_register_map.packed_byte_length, fence);
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
#endif  // 0
}

std::pair<VkBuffer, VkDeviceSize> BufferCache::UploadIndexBuffer(
    VkCommandBuffer command_buffer, uint32_t source_addr,
    uint32_t source_length, IndexFormat format, VkFence fence) {
  auto offset = FindCachedTransientData(source_addr, source_length);
  if (offset != VK_WHOLE_SIZE) {
    return {transient_buffer_->gpu_buffer(), offset};
  }

  // Allocate space in the buffer for our data.
  offset = AllocateTransientData(source_length, fence);
  if (offset == VK_WHOLE_SIZE) {
    // OOM.
    return {nullptr, VK_WHOLE_SIZE};
  }

  const void* source_ptr = memory_->TranslatePhysical(source_addr);

  // Copy data into the buffer.
  // TODO(benvanik): get min/max indices and pass back?
  // TODO(benvanik): memcpy then use compute shaders to swap?
  if (format == IndexFormat::kInt16) {
    // Endian::k8in16, swap half-words.
    xe::copy_and_swap_16_aligned(transient_buffer_->host_base() + offset,
                                 source_ptr, source_length / 2);
  } else if (format == IndexFormat::kInt32) {
    // Endian::k8in32, swap words.
    xe::copy_and_swap_32_aligned(transient_buffer_->host_base() + offset,
                                 source_ptr, source_length / 4);
  }

  transient_buffer_->Flush(offset, source_length);

  // Append a barrier to the command buffer.
  VkBufferMemoryBarrier barrier = {
      VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      nullptr,
      VK_ACCESS_HOST_WRITE_BIT,
      VK_ACCESS_INDEX_READ_BIT,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      transient_buffer_->gpu_buffer(),
      offset,
      source_length,
  };
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1,
                       &barrier, 0, nullptr);

  CacheTransientData(source_addr, source_length, offset);
  return {transient_buffer_->gpu_buffer(), offset};
}

std::pair<VkBuffer, VkDeviceSize> BufferCache::UploadVertexBuffer(
    VkCommandBuffer command_buffer, uint32_t source_addr,
    uint32_t source_length, Endian endian, VkFence fence) {
  auto offset = FindCachedTransientData(source_addr, source_length);
  if (offset != VK_WHOLE_SIZE) {
    return {transient_buffer_->gpu_buffer(), offset};
  }

  // Allocate space in the buffer for our data.
  offset = AllocateTransientData(source_length, fence);
  if (offset == VK_WHOLE_SIZE) {
    // OOM.
    return {nullptr, VK_WHOLE_SIZE};
  }

  const void* source_ptr = memory_->TranslatePhysical(source_addr);

  // Copy data into the buffer.
  // TODO(benvanik): memcpy then use compute shaders to swap?
  if (endian == Endian::k8in32) {
    // Endian::k8in32, swap words.
    xe::copy_and_swap_32_aligned(transient_buffer_->host_base() + offset,
                                 source_ptr, source_length / 4);
  } else if (endian == Endian::k16in32) {
    xe::copy_and_swap_16_in_32_aligned(transient_buffer_->host_base() + offset,
                                       source_ptr, source_length / 4);
  } else {
    assert_always();
  }

  transient_buffer_->Flush(offset, source_length);

  // Append a barrier to the command buffer.
  VkBufferMemoryBarrier barrier = {
      VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      nullptr,
      VK_ACCESS_HOST_WRITE_BIT,
      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      transient_buffer_->gpu_buffer(),
      offset,
      source_length,
  };
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1,
                       &barrier, 0, nullptr);

  CacheTransientData(source_addr, source_length, offset);
  return {transient_buffer_->gpu_buffer(), offset};
}

VkDeviceSize BufferCache::AllocateTransientData(VkDeviceSize length,
                                                VkFence fence) {
  // Try fast path (if we have space).
  VkDeviceSize offset = TryAllocateTransientData(length, fence);
  if (offset != VK_WHOLE_SIZE) {
    return offset;
  }

  // Ran out of easy allocations.
  // Try consuming fences before we panic.
  transient_buffer_->Scavenge();

  // Try again. It may still fail if we didn't get enough space back.
  offset = TryAllocateTransientData(length, fence);
  return offset;
}

VkDeviceSize BufferCache::TryAllocateTransientData(VkDeviceSize length,
                                                   VkFence fence) {
  auto alloc = transient_buffer_->Acquire(length, fence);
  if (alloc) {
    return alloc->offset;
  }

  // No more space.
  return VK_WHOLE_SIZE;
}

VkDeviceSize BufferCache::FindCachedTransientData(uint32_t guest_address,
                                                  uint32_t guest_length) {
  uint64_t key = uint64_t(guest_length) << 32 | uint64_t(guest_address);
  auto it = transient_cache_.find(key);
  if (it != transient_cache_.end()) {
    return it->second;
  }

  return VK_WHOLE_SIZE;
}

void BufferCache::CacheTransientData(uint32_t guest_address,
                                     uint32_t guest_length,
                                     VkDeviceSize offset) {
  uint64_t key = uint64_t(guest_length) << 32 | uint64_t(guest_address);
  transient_cache_[key] = offset;
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
  dirty_range.memory = transient_buffer_->gpu_memory();
  dirty_range.offset = 0;
  dirty_range.size = transient_buffer_->capacity();
  vkFlushMappedMemoryRanges(*device_, 1, &dirty_range);
}

void BufferCache::InvalidateCache() { transient_cache_.clear(); }
void BufferCache::ClearCache() { transient_cache_.clear(); }

void BufferCache::Scavenge() {
  transient_cache_.clear();
  transient_buffer_->Scavenge();
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

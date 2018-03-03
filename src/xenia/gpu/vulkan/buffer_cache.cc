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

#include "third_party/vulkan/vk_mem_alloc.h"

using namespace xe::gpu::xenos;

namespace xe {
namespace gpu {
namespace vulkan {

#if XE_ARCH_AMD64
void copy_cmp_swap_16_unaligned(void* dest_ptr, const void* src_ptr,
                                uint16_t cmp_value, size_t count) {
  auto dest = reinterpret_cast<uint16_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint16_t*>(src_ptr);
  __m128i shufmask =
      _mm_set_epi8(0x0E, 0x0F, 0x0C, 0x0D, 0x0A, 0x0B, 0x08, 0x09, 0x06, 0x07,
                   0x04, 0x05, 0x02, 0x03, 0x00, 0x01);
  __m128i cmpval = _mm_set1_epi16(cmp_value);

  size_t i;
  for (i = 0; i + 8 <= count; i += 8) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output = _mm_shuffle_epi8(input, shufmask);

    __m128i mask = _mm_cmpeq_epi16(output, cmpval);
    output = _mm_or_si128(output, mask);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}

void copy_cmp_swap_32_unaligned(void* dest_ptr, const void* src_ptr,
                                uint32_t cmp_value, size_t count) {
  auto dest = reinterpret_cast<uint32_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint32_t*>(src_ptr);
  __m128i shufmask =
      _mm_set_epi8(0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B, 0x04, 0x05,
                   0x06, 0x07, 0x00, 0x01, 0x02, 0x03);
  __m128i cmpval = _mm_set1_epi32(cmp_value);

  size_t i;
  for (i = 0; i + 4 <= count; i += 4) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output = _mm_shuffle_epi8(input, shufmask);

    __m128i mask = _mm_cmpeq_epi32(output, cmpval);
    output = _mm_or_si128(output, mask);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}
#else
void copy_and_swap_16_unaligned(void* dest_ptr, const void* src_ptr,
                                uint16_t cmp_value, size_t count) {
  auto dest = reinterpret_cast<uint16_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint16_t*>(src_ptr);
  for (size_t i = 0; i < count; ++i) {
    uint16_t value = byte_swap(src[i]);
    dest[i] = value == cmp_value ? 0xFFFF : value;
  }
}

void copy_and_swap_32_unaligned(void* dest_ptr, const void* src_ptr,
                                uint32_t cmp_value, size_t count) {
  auto dest = reinterpret_cast<uint32_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint32_t*>(src_ptr);
  for (size_t i = 0; i < count; ++i) {
    uint32_t value = byte_swap(src[i]);
    dest[i] = value == cmp_value ? 0xFFFFFFFF : value;
  }
}
#endif

using xe::ui::vulkan::CheckResult;

constexpr VkDeviceSize kConstantRegisterUniformRange =
    512 * 4 * 4 + 8 * 4 + 32 * 4;

BufferCache::BufferCache(RegisterFile* register_file, Memory* memory,
                         ui::vulkan::VulkanDevice* device, size_t capacity)
    : register_file_(register_file), memory_(memory), device_(device) {
  transient_buffer_ = std::make_unique<ui::vulkan::CircularBuffer>(
      device_,
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      capacity, 4096);
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

  // Create a memory allocator for textures.
  VmaAllocatorCreateInfo alloc_info = {
      0, *device_, *device_, 0, 0, nullptr, nullptr,
  };
  status = vmaCreateAllocator(&alloc_info, &mem_allocator_);
  if (status != VK_SUCCESS) {
    return status;
  }

  status = CreateConstantDescriptorSet();
  if (status != VK_SUCCESS) {
    return status;
  }

  status = CreateVertexDescriptorPool();
  if (status != VK_SUCCESS) {
    return status;
  }

  return VK_SUCCESS;
}

VkResult xe::gpu::vulkan::BufferCache::CreateVertexDescriptorPool() {
  VkResult status;

  std::vector<VkDescriptorPoolSize> pool_sizes;
  pool_sizes.push_back({
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      65536,
  });
  vertex_descriptor_pool_ =
      std::make_unique<ui::vulkan::DescriptorPool>(*device_, 65536, pool_sizes);

  // 32 storage buffers available to vertex shader.
  // TODO(DrChat): In the future, this could hold memexport staging data.
  VkDescriptorSetLayoutBinding binding = {
      0,       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      32,      VK_SHADER_STAGE_VERTEX_BIT,
      nullptr,
  };

  VkDescriptorSetLayoutCreateInfo layout_info = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      nullptr,
      0,
      1,
      &binding,
  };
  status = vkCreateDescriptorSetLayout(*device_, &layout_info, nullptr,
                                       &vertex_descriptor_set_layout_);
  if (status != VK_SUCCESS) {
    return status;
  }

  return VK_SUCCESS;
}

void xe::gpu::vulkan::BufferCache::FreeVertexDescriptorPool() {
  vertex_descriptor_pool_.reset();

  VK_SAFE_DESTROY(vkDestroyDescriptorSetLayout, *device_,
                  vertex_descriptor_set_layout_, nullptr);
}

VkResult BufferCache::CreateConstantDescriptorSet() {
  VkResult status = VK_SUCCESS;

  // Descriptor pool used for all of our cached descriptors.
  // In the steady state we don't allocate anything, so these are all manually
  // managed.
  VkDescriptorPoolCreateInfo transient_descriptor_pool_info;
  transient_descriptor_pool_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  transient_descriptor_pool_info.pNext = nullptr;
  transient_descriptor_pool_info.flags =
      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  transient_descriptor_pool_info.maxSets = 1;
  VkDescriptorPoolSize pool_sizes[1];
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  pool_sizes[0].descriptorCount = 2;
  transient_descriptor_pool_info.poolSizeCount = 1;
  transient_descriptor_pool_info.pPoolSizes = pool_sizes;
  status = vkCreateDescriptorPool(*device_, &transient_descriptor_pool_info,
                                  nullptr, &constant_descriptor_pool_);
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create the descriptor set layout used for our uniform buffer.
  // As it is a static binding that uses dynamic offsets during draws we can
  // create this once and reuse it forever.
  VkDescriptorSetLayoutBinding bindings[2] = {};

  // Vertex constants
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  bindings[0].pImmutableSamplers = nullptr;

  // Fragment constants
  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings[1].pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info = {};
  descriptor_set_layout_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_info.pNext = nullptr;
  descriptor_set_layout_info.flags = 0;
  descriptor_set_layout_info.bindingCount =
      static_cast<uint32_t>(xe::countof(bindings));
  descriptor_set_layout_info.pBindings = bindings;
  status =
      vkCreateDescriptorSetLayout(*device_, &descriptor_set_layout_info,
                                  nullptr, &constant_descriptor_set_layout_);
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create the descriptor we'll use for the uniform buffer.
  // This is what we hand out to everyone (who then also needs to use our
  // offsets).
  VkDescriptorSetAllocateInfo set_alloc_info;
  set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_alloc_info.pNext = nullptr;
  set_alloc_info.descriptorPool = constant_descriptor_pool_;
  set_alloc_info.descriptorSetCount = 1;
  set_alloc_info.pSetLayouts = &constant_descriptor_set_layout_;
  status = vkAllocateDescriptorSets(*device_, &set_alloc_info,
                                    &constant_descriptor_set_);
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
  vertex_uniform_binding_write.dstSet = constant_descriptor_set_;
  vertex_uniform_binding_write.dstBinding = 0;
  vertex_uniform_binding_write.dstArrayElement = 0;
  vertex_uniform_binding_write.descriptorCount = 1;
  vertex_uniform_binding_write.descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  vertex_uniform_binding_write.pBufferInfo = &buffer_info;
  auto& fragment_uniform_binding_write = descriptor_writes[1];
  fragment_uniform_binding_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  fragment_uniform_binding_write.pNext = nullptr;
  fragment_uniform_binding_write.dstSet = constant_descriptor_set_;
  fragment_uniform_binding_write.dstBinding = 1;
  fragment_uniform_binding_write.dstArrayElement = 0;
  fragment_uniform_binding_write.descriptorCount = 1;
  fragment_uniform_binding_write.descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  fragment_uniform_binding_write.pBufferInfo = &buffer_info;
  vkUpdateDescriptorSets(*device_, 2, descriptor_writes, 0, nullptr);

  return VK_SUCCESS;
}
void xe::gpu::vulkan::BufferCache::FreeConstantDescriptorSet() {
  if (constant_descriptor_set_) {
    vkFreeDescriptorSets(*device_, constant_descriptor_pool_, 1,
                         &constant_descriptor_set_);
    constant_descriptor_set_ = nullptr;
  }

  VK_SAFE_DESTROY(vkDestroyDescriptorSetLayout, *device_,
                  constant_descriptor_set_layout_, nullptr);
  VK_SAFE_DESTROY(vkDestroyDescriptorPool, *device_, constant_descriptor_pool_,
                  nullptr);
}

void BufferCache::Shutdown() {
  if (mem_allocator_) {
    vmaDestroyAllocator(mem_allocator_);
    mem_allocator_ = nullptr;
  }

  FreeConstantDescriptorSet();
  FreeVertexDescriptorPool();

  transient_buffer_->Shutdown();
  VK_SAFE_DESTROY(vkFreeMemory, *device_, gpu_memory_pool_, nullptr);
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
  // Allocate space in the buffer for our data.
  auto offset = AllocateTransientData(source_length, fence);
  if (offset == VK_WHOLE_SIZE) {
    // OOM.
    return {nullptr, VK_WHOLE_SIZE};
  }

  const void* source_ptr = memory_->TranslatePhysical(source_addr);

  uint32_t prim_reset_index =
      register_file_->values[XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX].u32;
  bool prim_reset_enabled =
      !!(register_file_->values[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32 & (1 << 21));

  // Copy data into the buffer. If primitive reset is enabled, translate any
  // primitive reset indices to something Vulkan understands.
  // TODO(benvanik): memcpy then use compute shaders to swap?
  if (prim_reset_enabled) {
    if (format == IndexFormat::kInt16) {
      // Endian::k8in16, swap half-words.
      copy_cmp_swap_16_unaligned(
          transient_buffer_->host_base() + offset, source_ptr,
          static_cast<uint16_t>(prim_reset_index), source_length / 2);
    } else if (format == IndexFormat::kInt32) {
      // Endian::k8in32, swap words.
      copy_cmp_swap_32_unaligned(transient_buffer_->host_base() + offset,
                                 source_ptr, prim_reset_index,
                                 source_length / 4);
    }
  } else {
    if (format == IndexFormat::kInt16) {
      // Endian::k8in16, swap half-words.
      xe::copy_and_swap_16_unaligned(transient_buffer_->host_base() + offset,
                                     source_ptr, source_length / 2);
    } else if (format == IndexFormat::kInt32) {
      // Endian::k8in32, swap words.
      xe::copy_and_swap_32_unaligned(transient_buffer_->host_base() + offset,
                                     source_ptr, source_length / 4);
    }
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

  return {transient_buffer_->gpu_buffer(), offset};
}

std::pair<VkBuffer, VkDeviceSize> BufferCache::UploadVertexBuffer(
    VkCommandBuffer command_buffer, uint32_t source_addr,
    uint32_t source_length, Endian endian, VkFence fence) {
  auto offset = FindCachedTransientData(source_addr, source_length);
  if (offset != VK_WHOLE_SIZE) {
    return {transient_buffer_->gpu_buffer(), offset};
  }

  // Slow path :)
  // Expand the region up to the allocation boundary
  auto physical_heap = memory_->GetPhysicalHeap();
  uint32_t upload_base = source_addr;
  uint32_t upload_size = source_length;

  // Ping the memory subsystem for allocation size.
  // TODO(DrChat): Artifacting occurring in GripShift with this enabled.
  // physical_heap->QueryBaseAndSize(&upload_base, &upload_size);
  assert(upload_base <= source_addr);
  uint32_t source_offset = source_addr - upload_base;

  // Allocate space in the buffer for our data.
  offset = AllocateTransientData(upload_size, fence);
  if (offset == VK_WHOLE_SIZE) {
    // OOM.
    return {nullptr, VK_WHOLE_SIZE};
  }

  const void* upload_ptr = memory_->TranslatePhysical(upload_base);

  // Copy data into the buffer.
  // TODO(benvanik): memcpy then use compute shaders to swap?
  if (endian == Endian::k8in32) {
    // Endian::k8in32, swap words.
    xe::copy_and_swap_32_unaligned(transient_buffer_->host_base() + offset,
                                   upload_ptr, source_length / 4);
  } else if (endian == Endian::k16in32) {
    xe::copy_and_swap_16_in_32_unaligned(
        transient_buffer_->host_base() + offset, upload_ptr, source_length / 4);
  } else {
    assert_always();
  }

  transient_buffer_->Flush(offset, upload_size);

  // Append a barrier to the command buffer.
  VkBufferMemoryBarrier barrier = {
      VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      nullptr,
      VK_ACCESS_HOST_WRITE_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      transient_buffer_->gpu_buffer(),
      offset,
      upload_size,
  };
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1,
                       &barrier, 0, nullptr);

  CacheTransientData(upload_base, upload_size, offset);
  return {transient_buffer_->gpu_buffer(), offset + source_offset};
}

void BufferCache::HashVertexBindings(
    XXH64_state_t* hash_state,
    const std::vector<Shader::VertexBinding>& vertex_bindings) {
  auto& regs = *register_file_;
  for (const auto& vertex_binding : vertex_bindings) {
    int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
            (vertex_binding.fetch_constant / 3) * 6;
    const auto group = reinterpret_cast<xe_gpu_fetch_group_t*>(&regs.values[r]);
    const xe_gpu_vertex_fetch_t* fetch = nullptr;
    switch (vertex_binding.fetch_constant % 3) {
      case 0:
        fetch = &group->vertex_fetch_0;
        break;
      case 1:
        fetch = &group->vertex_fetch_1;
        break;
      case 2:
        fetch = &group->vertex_fetch_2;
        break;
    }

    XXH64_update(hash_state, fetch, sizeof(fetch));
  }
}

VkDescriptorSet BufferCache::PrepareVertexSet(
    VkCommandBuffer command_buffer, VkFence fence,
    const std::vector<Shader::VertexBinding>& vertex_bindings) {
  // (quickly) Generate a hash.
  XXH64_state_t hash_state;
  XXH64_reset(&hash_state, 0);

  // (quickly) Generate a hash.
  HashVertexBindings(&hash_state, vertex_bindings);
  uint64_t hash = XXH64_digest(&hash_state);
  for (auto it = vertex_sets_.find(hash); it != vertex_sets_.end(); ++it) {
    // TODO(DrChat): We need to compare the bindings and ensure they're equal.
    return it->second;
  }

  if (!vertex_descriptor_pool_->has_open_batch()) {
    vertex_descriptor_pool_->BeginBatch(fence);
  }

  VkDescriptorSet set =
      vertex_descriptor_pool_->AcquireEntry(vertex_descriptor_set_layout_);
  if (!set) {
    return nullptr;
  }

  // TODO(DrChat): Define magic number 32 as a constant somewhere.
  VkDescriptorBufferInfo buffer_infos[32] = {};
  VkWriteDescriptorSet descriptor_write = {
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      nullptr,
      set,
      0,
      0,
      0,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      nullptr,
      buffer_infos,
      nullptr,
  };

  auto& regs = *register_file_;
  for (const auto& vertex_binding : vertex_bindings) {
    int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
            (vertex_binding.fetch_constant / 3) * 6;
    const auto group = reinterpret_cast<xe_gpu_fetch_group_t*>(&regs.values[r]);
    const xe_gpu_vertex_fetch_t* fetch = nullptr;
    switch (vertex_binding.fetch_constant % 3) {
      case 0:
        fetch = &group->vertex_fetch_0;
        break;
      case 1:
        fetch = &group->vertex_fetch_1;
        break;
      case 2:
        fetch = &group->vertex_fetch_2;
        break;
    }

    if (fetch->type != 0x3) {
      // TODO(DrChat): Some games use type 0x0 (with no data).
      return nullptr;
    }

    // TODO(benvanik): compute based on indices or vertex count.
    //     THIS CAN BE MASSIVELY INCORRECT (too large).
    // This may not be possible (with indexed vfetch).
    uint32_t source_length = fetch->size * 4;
    uint32_t physical_address = fetch->address << 2;

    // TODO(DrChat): This needs to be put in gpu::CommandProcessor
    // trace_writer_.WriteMemoryRead(physical_address, source_length);

    // Upload (or get a cached copy of) the buffer.
    auto buffer_ref =
        UploadVertexBuffer(command_buffer, physical_address, source_length,
                           static_cast<Endian>(fetch->endian), fence);
    if (buffer_ref.second == VK_WHOLE_SIZE) {
      // Failed to upload buffer.
      return nullptr;
    }

    // Stash the buffer reference for our bulk bind at the end.
    buffer_infos[descriptor_write.descriptorCount++] = {
        buffer_ref.first,
        buffer_ref.second,
        source_length,
    };
  }

  vkUpdateDescriptorSets(*device_, 1, &descriptor_write, 0, nullptr);
  vertex_sets_[hash] = set;
  return set;
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
  if (transient_cache_.empty()) {
    // Short-circuit exit.
    return VK_WHOLE_SIZE;
  }

  // Find the first element > guest_address
  auto it = transient_cache_.upper_bound(guest_address);
  if (it != transient_cache_.begin()) {
    // it = first element <= guest_address
    --it;

    if ((it->first + it->second.first) >= (guest_address + guest_length)) {
      // This data is contained within some existing transient data.
      auto source_offset = static_cast<VkDeviceSize>(guest_address - it->first);
      return it->second.second + source_offset;
    }
  }

  return VK_WHOLE_SIZE;
}

void BufferCache::CacheTransientData(uint32_t guest_address,
                                     uint32_t guest_length,
                                     VkDeviceSize offset) {
  transient_cache_[guest_address] = {guest_length, offset};

  // Erase any entries contained within
  auto it = transient_cache_.upper_bound(guest_address);
  while (it != transient_cache_.end()) {
    if ((guest_address + guest_length) >= (it->first + it->second.first)) {
      it = transient_cache_.erase(it);
    } else {
      break;
    }
  }
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
  SCOPE_profile_cpu_f("gpu");

  transient_cache_.clear();
  transient_buffer_->Scavenge();

  // TODO(DrChat): These could persist across frames, we just need a smart way
  // to delete unused ones.
  vertex_sets_.clear();
  if (vertex_descriptor_pool_->has_open_batch()) {
    vertex_descriptor_pool_->EndBatch();
  }

  vertex_descriptor_pool_->Scavenge();
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

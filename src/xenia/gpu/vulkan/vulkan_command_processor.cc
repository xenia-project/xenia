/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_command_processor.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/vulkan/vulkan_pipeline_cache.h"
#include "xenia/gpu/vulkan/vulkan_render_target_cache.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/vulkan/vulkan_shared_memory.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_context.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

VulkanCommandProcessor::VulkanCommandProcessor(
    VulkanGraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state),
      deferred_command_buffer_(*this) {}

VulkanCommandProcessor::~VulkanCommandProcessor() = default;

void VulkanCommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                      uint32_t length) {
  shared_memory_->MemoryInvalidationCallback(base_ptr, length, true);
  primitive_processor_->MemoryInvalidationCallback(base_ptr, length, true);
}

void VulkanCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {}

bool VulkanCommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Failed to initialize base command processor context");
    return false;
  }

  const ui::vulkan::VulkanProvider& provider =
      GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // No specific reason for 32768, just the "too much" amount from Direct3D 12
  // PIX warnings.
  transient_descriptor_pool_uniform_buffers_ =
      std::make_unique<ui::vulkan::TransientDescriptorPool>(
          provider, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32768, 32768);
  // 16384 is bigger than any single uniform buffer that Xenia needs, but is the
  // minimum maxUniformBufferRange, thus the safe minimum amount.
  VkDeviceSize uniform_buffer_alignment = std::max(
      provider.device_properties().limits.minUniformBufferOffsetAlignment,
      VkDeviceSize(1));
  uniform_buffer_pool_ = std::make_unique<ui::vulkan::VulkanUploadBufferPool>(
      provider, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      xe::align(std::max(ui::GraphicsUploadBufferPool::kDefaultPageSize,
                         size_t(16384)),
                size_t(uniform_buffer_alignment)));

  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
  descriptor_set_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_create_info.pNext = nullptr;
  descriptor_set_layout_create_info.flags = 0;
  descriptor_set_layout_create_info.bindingCount = 0;
  descriptor_set_layout_create_info.pBindings = nullptr;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_empty_) != VK_SUCCESS) {
    XELOGE("Failed to create an empty Vulkan descriptor set layout");
    return false;
  }
  VkShaderStageFlags shader_stages_guest_vertex =
      GetGuestVertexShaderStageFlags();
  VkDescriptorSetLayoutBinding descriptor_set_layout_binding_uniform_buffer;
  descriptor_set_layout_binding_uniform_buffer.binding = 0;
  descriptor_set_layout_binding_uniform_buffer.descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptor_set_layout_binding_uniform_buffer.descriptorCount = 1;
  descriptor_set_layout_binding_uniform_buffer.stageFlags =
      shader_stages_guest_vertex | VK_SHADER_STAGE_FRAGMENT_BIT;
  descriptor_set_layout_binding_uniform_buffer.pImmutableSamplers = nullptr;
  descriptor_set_layout_create_info.bindingCount = 1;
  descriptor_set_layout_create_info.pBindings =
      &descriptor_set_layout_binding_uniform_buffer;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_fetch_bool_loop_constants_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan descriptor set layout for the fetch, bool "
        "and loop constants uniform buffer");
    return false;
  }
  descriptor_set_layout_binding_uniform_buffer.stageFlags =
      shader_stages_guest_vertex;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_float_constants_vertex_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan descriptor set layout for the vertex shader "
        "float constants uniform buffer");
    return false;
  }
  descriptor_set_layout_binding_uniform_buffer.stageFlags =
      VK_SHADER_STAGE_FRAGMENT_BIT;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_float_constants_pixel_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan descriptor set layout for the pixel shader "
        "float constants uniform buffer");
    return false;
  }
  descriptor_set_layout_binding_uniform_buffer.stageFlags =
      shader_stages_guest_vertex | VK_SHADER_STAGE_FRAGMENT_BIT;
  if (provider.device_features().tessellationShader) {
    descriptor_set_layout_binding_uniform_buffer.stageFlags |=
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
  }
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_system_constants_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan descriptor set layout for the system "
        "constants uniform buffer");
    return false;
  }
  uint32_t shared_memory_binding_count_log2 =
      SpirvShaderTranslator::GetSharedMemoryStorageBufferCountLog2(
          provider.device_properties().limits.maxStorageBufferRange);
  uint32_t shared_memory_binding_count = uint32_t(1)
                                         << shared_memory_binding_count_log2;
  VkDescriptorSetLayoutBinding
      descriptor_set_layout_bindings_shared_memory_and_edram[1];
  descriptor_set_layout_bindings_shared_memory_and_edram[0].binding = 0;
  descriptor_set_layout_bindings_shared_memory_and_edram[0].descriptorType =
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  descriptor_set_layout_bindings_shared_memory_and_edram[0].descriptorCount =
      shared_memory_binding_count;
  // TODO(Triang3l): When fullDrawIndexUint32 fallback is added, force host
  // vertex shader access to the shared memory for the tessellation vertex
  // shader (to retrieve tessellation factors).
  descriptor_set_layout_bindings_shared_memory_and_edram[0].stageFlags =
      shader_stages_guest_vertex | VK_SHADER_STAGE_FRAGMENT_BIT;
  descriptor_set_layout_bindings_shared_memory_and_edram[0].pImmutableSamplers =
      nullptr;
  // TODO(Triang3l): EDRAM storage image binding for the fragment shader
  // interlocks case.
  descriptor_set_layout_create_info.pBindings =
      descriptor_set_layout_bindings_shared_memory_and_edram;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_shared_memory_and_edram_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan descriptor set layout for the shared memory "
        "and the EDRAM");
    return false;
  }

  shared_memory_ =
      std::make_unique<VulkanSharedMemory>(*this, *memory_, trace_writer_);
  if (!shared_memory_->Initialize()) {
    XELOGE("Failed to initialize shared memory");
    return false;
  }

  primitive_processor_ = std::make_unique<VulkanPrimitiveProcessor>(
      *register_file_, *memory_, trace_writer_, *shared_memory_, *this);
  if (!primitive_processor_->Initialize()) {
    XELOGE("Failed to initialize the geometric primitive processor");
    return false;
  }

  render_target_cache_ =
      std::make_unique<VulkanRenderTargetCache>(*this, *register_file_);
  if (!render_target_cache_->Initialize()) {
    XELOGE("Failed to initialize the render target cache");
    return false;
  }

  pipeline_cache_ = std::make_unique<VulkanPipelineCache>(
      *this, *register_file_, *render_target_cache_);
  if (!pipeline_cache_->Initialize()) {
    XELOGE("Failed to initialize the graphics pipeline cache");
    return false;
  }

  // Shared memory and EDRAM common bindings.
  VkDescriptorPoolSize descriptor_pool_sizes[1];
  descriptor_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  descriptor_pool_sizes[0].descriptorCount = shared_memory_binding_count;
  // TODO(Triang3l): EDRAM storage image binding for the fragment shader
  // interlocks case.
  VkDescriptorPoolCreateInfo descriptor_pool_create_info;
  descriptor_pool_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_create_info.pNext = nullptr;
  descriptor_pool_create_info.flags = 0;
  descriptor_pool_create_info.maxSets = 1;
  descriptor_pool_create_info.poolSizeCount = 1;
  descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes;
  if (dfn.vkCreateDescriptorPool(device, &descriptor_pool_create_info, nullptr,
                                 &shared_memory_and_edram_descriptor_pool_) !=
      VK_SUCCESS) {
    XELOGE(
        "Failed to create the Vulkan descriptor pool for shared memory and "
        "EDRAM");
    return false;
  }
  VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
  descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_set_allocate_info.pNext = nullptr;
  descriptor_set_allocate_info.descriptorPool =
      shared_memory_and_edram_descriptor_pool_;
  descriptor_set_allocate_info.descriptorSetCount = 1;
  descriptor_set_allocate_info.pSetLayouts =
      &descriptor_set_layout_shared_memory_and_edram_;
  if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                   &shared_memory_and_edram_descriptor_set_) !=
      VK_SUCCESS) {
    XELOGE(
        "Failed to allocate the Vulkan descriptor set for shared memory and "
        "EDRAM");
    return false;
  }
  VkDescriptorBufferInfo
      shared_memory_descriptor_buffers_info[SharedMemory::kBufferSize /
                                            (128 << 20)];
  uint32_t shared_memory_binding_range =
      SharedMemory::kBufferSize >> shared_memory_binding_count_log2;
  for (uint32_t i = 0; i < shared_memory_binding_count; ++i) {
    VkDescriptorBufferInfo& shared_memory_descriptor_buffer_info =
        shared_memory_descriptor_buffers_info[i];
    shared_memory_descriptor_buffer_info.buffer = shared_memory_->buffer();
    shared_memory_descriptor_buffer_info.offset =
        shared_memory_binding_range * i;
    shared_memory_descriptor_buffer_info.range = shared_memory_binding_range;
  }
  VkWriteDescriptorSet write_descriptor_sets[1];
  write_descriptor_sets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write_descriptor_sets[0].pNext = nullptr;
  write_descriptor_sets[0].dstSet = shared_memory_and_edram_descriptor_set_;
  write_descriptor_sets[0].dstBinding = 0;
  write_descriptor_sets[0].dstArrayElement = 0;
  write_descriptor_sets[0].descriptorCount = shared_memory_binding_count;
  write_descriptor_sets[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write_descriptor_sets[0].pImageInfo = nullptr;
  write_descriptor_sets[0].pBufferInfo = shared_memory_descriptor_buffers_info;
  write_descriptor_sets[0].pTexelBufferView = nullptr;
  // TODO(Triang3l): EDRAM storage image binding for the fragment shader
  // interlocks case.
  dfn.vkUpdateDescriptorSets(device, 1, write_descriptor_sets, 0, nullptr);

  // Just not to expose uninitialized memory.
  std::memset(&system_constants_, 0, sizeof(system_constants_));

  return true;
}

void VulkanCommandProcessor::ShutdownContext() {
  AwaitAllQueueOperationsCompletion();

  const ui::vulkan::VulkanProvider& provider =
      GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorPool, device,
      shared_memory_and_edram_descriptor_pool_);

  pipeline_cache_.reset();

  render_target_cache_.reset();

  primitive_processor_.reset();

  shared_memory_.reset();

  for (const auto& pipeline_layout_pair : pipeline_layouts_) {
    dfn.vkDestroyPipelineLayout(
        device, pipeline_layout_pair.second.pipeline_layout, nullptr);
  }
  pipeline_layouts_.clear();
  for (const auto& descriptor_set_layout_pair :
       descriptor_set_layouts_textures_) {
    dfn.vkDestroyDescriptorSetLayout(device, descriptor_set_layout_pair.second,
                                     nullptr);
  }
  descriptor_set_layouts_textures_.clear();

  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorSetLayout, device,
      descriptor_set_layout_shared_memory_and_edram_);
  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorSetLayout, device,
      descriptor_set_layout_system_constants_);
  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorSetLayout, device,
      descriptor_set_layout_float_constants_pixel_);
  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorSetLayout, device,
      descriptor_set_layout_float_constants_vertex_);
  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorSetLayout, device,
      descriptor_set_layout_fetch_bool_loop_constants_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout,
                                         device, descriptor_set_layout_empty_);

  uniform_buffer_pool_.reset();
  transient_descriptor_pool_uniform_buffers_.reset();

  sparse_bind_wait_stage_mask_ = 0;
  sparse_buffer_binds_.clear();
  sparse_memory_binds_.clear();

  deferred_command_buffer_.Reset();
  for (const auto& command_buffer_pair : command_buffers_submitted_) {
    dfn.vkDestroyCommandPool(device, command_buffer_pair.first.pool, nullptr);
  }
  command_buffers_submitted_.clear();
  for (const CommandBuffer& command_buffer : command_buffers_writable_) {
    dfn.vkDestroyCommandPool(device, command_buffer.pool, nullptr);
  }
  command_buffers_writable_.clear();

  std::memset(closed_frame_submissions_, 0, sizeof(closed_frame_submissions_));
  frame_completed_ = 0;
  frame_current_ = 1;
  frame_open_ = false;

  for (const auto& semaphore : submissions_in_flight_semaphores_) {
    dfn.vkDestroySemaphore(device, semaphore.first, nullptr);
  }
  submissions_in_flight_semaphores_.clear();
  for (VkFence& fence : submissions_in_flight_fences_) {
    dfn.vkDestroyFence(device, fence, nullptr);
  }
  submissions_in_flight_fences_.clear();
  current_submission_wait_stage_masks_.clear();
  for (VkSemaphore semaphore : current_submission_wait_semaphores_) {
    dfn.vkDestroySemaphore(device, semaphore, nullptr);
  }
  current_submission_wait_semaphores_.clear();
  submission_completed_ = 0;
  submission_open_ = false;

  for (VkSemaphore semaphore : semaphores_free_) {
    dfn.vkDestroySemaphore(device, semaphore, nullptr);
  }
  semaphores_free_.clear();
  for (VkFence fence : fences_free_) {
    dfn.vkDestroyFence(device, fence, nullptr);
  }
  fences_free_.clear();

  CommandProcessor::ShutdownContext();
}

void VulkanCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  CommandProcessor::WriteRegister(index, value);

  if (index >= XE_GPU_REG_SHADER_CONSTANT_000_X &&
      index <= XE_GPU_REG_SHADER_CONSTANT_511_W) {
    if (frame_open_) {
      uint32_t float_constant_index =
          (index - XE_GPU_REG_SHADER_CONSTANT_000_X) >> 2;
      if (float_constant_index >= 256) {
        float_constant_index -= 256;
        if (current_float_constant_map_pixel_[float_constant_index >> 6] &
            (1ull << (float_constant_index & 63))) {
          current_graphics_descriptor_set_values_up_to_date_ &=
              ~(uint32_t(1)
                << SpirvShaderTranslator::kDescriptorSetFloatConstantsPixel);
        }
      } else {
        if (current_float_constant_map_vertex_[float_constant_index >> 6] &
            (1ull << (float_constant_index & 63))) {
          current_graphics_descriptor_set_values_up_to_date_ &=
              ~(uint32_t(1)
                << SpirvShaderTranslator::kDescriptorSetFloatConstantsVertex);
        }
      }
    }
  } else if (index >= XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 &&
             index <= XE_GPU_REG_SHADER_CONSTANT_LOOP_31) {
    current_graphics_descriptor_set_values_up_to_date_ &= ~(
        uint32_t(1) << SpirvShaderTranslator::kDescriptorSetBoolLoopConstants);
  } else if (index >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 &&
             index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {
    current_graphics_descriptor_set_values_up_to_date_ &=
        ~(uint32_t(1) << SpirvShaderTranslator::kDescriptorSetFetchConstants);
  }
}

void VulkanCommandProcessor::SparseBindBuffer(
    VkBuffer buffer, uint32_t bind_count, const VkSparseMemoryBind* binds,
    VkPipelineStageFlags wait_stage_mask) {
  if (!bind_count) {
    return;
  }
  SparseBufferBind& buffer_bind = sparse_buffer_binds_.emplace_back();
  buffer_bind.buffer = buffer;
  buffer_bind.bind_offset = sparse_memory_binds_.size();
  buffer_bind.bind_count = bind_count;
  sparse_memory_binds_.reserve(sparse_memory_binds_.size() + bind_count);
  sparse_memory_binds_.insert(sparse_memory_binds_.end(), binds,
                              binds + bind_count);
  sparse_bind_wait_stage_mask_ |= wait_stage_mask;
}

void VulkanCommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                         uint32_t frontbuffer_width,
                                         uint32_t frontbuffer_height) {
  // FIXME(Triang3l): frontbuffer_ptr is currently unreliable, in the trace
  // player it's set to 0, but it's not needed anyway since the fetch constant
  // contains the address.

  SCOPE_profile_cpu_f("gpu");

  // In case the swap command is the only one in the frame.
  BeginSubmission(true);

  EndSubmission(true);
}

void VulkanCommandProcessor::EndRenderPass() {
  assert_true(submission_open_);
  if (current_render_pass_ == VK_NULL_HANDLE) {
    return;
  }
  deferred_command_buffer_.CmdVkEndRenderPass();
  current_render_pass_ = VK_NULL_HANDLE;
}

const VulkanPipelineCache::PipelineLayoutProvider*
VulkanCommandProcessor::GetPipelineLayout(uint32_t texture_count_pixel,
                                          uint32_t texture_count_vertex) {
  PipelineLayoutKey pipeline_layout_key;
  pipeline_layout_key.texture_count_pixel = texture_count_pixel;
  pipeline_layout_key.texture_count_vertex = texture_count_vertex;
  {
    auto it = pipeline_layouts_.find(pipeline_layout_key.key);
    if (it != pipeline_layouts_.end()) {
      return &it->second;
    }
  }

  const ui::vulkan::VulkanProvider& provider =
      GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  VkDescriptorSetLayout descriptor_set_layout_textures_pixel;
  if (texture_count_pixel) {
    TextureDescriptorSetLayoutKey texture_descriptor_set_layout_key;
    texture_descriptor_set_layout_key.is_vertex = 0;
    texture_descriptor_set_layout_key.texture_count = texture_count_pixel;
    auto it = descriptor_set_layouts_textures_.find(
        texture_descriptor_set_layout_key.key);
    if (it != descriptor_set_layouts_textures_.end()) {
      descriptor_set_layout_textures_pixel = it->second;
    } else {
      VkDescriptorSetLayoutBinding descriptor_set_layout_binding;
      descriptor_set_layout_binding.binding = 0;
      descriptor_set_layout_binding.descriptorType =
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptor_set_layout_binding.descriptorCount = texture_count_pixel;
      descriptor_set_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      descriptor_set_layout_binding.pImmutableSamplers = nullptr;
      VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
      descriptor_set_layout_create_info.sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      descriptor_set_layout_create_info.pNext = nullptr;
      descriptor_set_layout_create_info.flags = 0;
      descriptor_set_layout_create_info.bindingCount = 1;
      descriptor_set_layout_create_info.pBindings =
          &descriptor_set_layout_binding;
      if (dfn.vkCreateDescriptorSetLayout(
              device, &descriptor_set_layout_create_info, nullptr,
              &descriptor_set_layout_textures_pixel) != VK_SUCCESS) {
        XELOGE(
            "Failed to create a Vulkan descriptor set layout for {} combined "
            "images and samplers for guest pixel shaders",
            texture_count_pixel);
        return nullptr;
      }
      descriptor_set_layouts_textures_.emplace(
          texture_descriptor_set_layout_key.key,
          descriptor_set_layout_textures_pixel);
    }
  } else {
    descriptor_set_layout_textures_pixel = descriptor_set_layout_empty_;
  }

  VkDescriptorSetLayout descriptor_set_layout_textures_vertex;
  if (texture_count_vertex) {
    TextureDescriptorSetLayoutKey texture_descriptor_set_layout_key;
    texture_descriptor_set_layout_key.is_vertex = 0;
    texture_descriptor_set_layout_key.texture_count = texture_count_vertex;
    auto it = descriptor_set_layouts_textures_.find(
        texture_descriptor_set_layout_key.key);
    if (it != descriptor_set_layouts_textures_.end()) {
      descriptor_set_layout_textures_vertex = it->second;
    } else {
      VkDescriptorSetLayoutBinding descriptor_set_layout_binding;
      descriptor_set_layout_binding.binding = 0;
      descriptor_set_layout_binding.descriptorType =
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptor_set_layout_binding.descriptorCount = texture_count_vertex;
      descriptor_set_layout_binding.stageFlags =
          GetGuestVertexShaderStageFlags();
      descriptor_set_layout_binding.pImmutableSamplers = nullptr;
      VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
      descriptor_set_layout_create_info.sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      descriptor_set_layout_create_info.pNext = nullptr;
      descriptor_set_layout_create_info.flags = 0;
      descriptor_set_layout_create_info.bindingCount = 1;
      descriptor_set_layout_create_info.pBindings =
          &descriptor_set_layout_binding;
      if (dfn.vkCreateDescriptorSetLayout(
              device, &descriptor_set_layout_create_info, nullptr,
              &descriptor_set_layout_textures_vertex) != VK_SUCCESS) {
        XELOGE(
            "Failed to create a Vulkan descriptor set layout for {} combined "
            "images and samplers for guest vertex shaders",
            texture_count_vertex);
        return nullptr;
      }
      descriptor_set_layouts_textures_.emplace(
          texture_descriptor_set_layout_key.key,
          descriptor_set_layout_textures_vertex);
    }
  } else {
    descriptor_set_layout_textures_vertex = descriptor_set_layout_empty_;
  }

  VkDescriptorSetLayout
      descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetCount];
  // Immutable layouts.
  descriptor_set_layouts
      [SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram] =
          descriptor_set_layout_shared_memory_and_edram_;
  descriptor_set_layouts
      [SpirvShaderTranslator::kDescriptorSetBoolLoopConstants] =
          descriptor_set_layout_fetch_bool_loop_constants_;
  descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetSystemConstants] =
      descriptor_set_layout_system_constants_;
  descriptor_set_layouts
      [SpirvShaderTranslator::kDescriptorSetFloatConstantsPixel] =
          descriptor_set_layout_float_constants_pixel_;
  descriptor_set_layouts
      [SpirvShaderTranslator::kDescriptorSetFloatConstantsVertex] =
          descriptor_set_layout_float_constants_vertex_;
  descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetFetchConstants] =
      descriptor_set_layout_fetch_bool_loop_constants_;
  // Mutable layouts.
  descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetTexturesVertex] =
      descriptor_set_layout_textures_vertex;
  descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetTexturesPixel] =
      descriptor_set_layout_textures_pixel;

  VkPipelineLayoutCreateInfo pipeline_layout_create_info;
  pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.pNext = nullptr;
  pipeline_layout_create_info.flags = 0;
  pipeline_layout_create_info.setLayoutCount =
      uint32_t(xe::countof(descriptor_set_layouts));
  pipeline_layout_create_info.pSetLayouts = descriptor_set_layouts;
  pipeline_layout_create_info.pushConstantRangeCount = 0;
  pipeline_layout_create_info.pPushConstantRanges = nullptr;
  VkPipelineLayout pipeline_layout;
  if (dfn.vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr,
                                 &pipeline_layout) != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan pipeline layout for guest drawing with {} "
        "pixel shader and {} vertex shader textures",
        texture_count_pixel, texture_count_vertex);
    return nullptr;
  }
  PipelineLayout pipeline_layout_entry;
  pipeline_layout_entry.pipeline_layout = pipeline_layout;
  pipeline_layout_entry.descriptor_set_layout_textures_pixel_ref =
      descriptor_set_layout_textures_pixel;
  pipeline_layout_entry.descriptor_set_layout_textures_vertex_ref =
      descriptor_set_layout_textures_vertex;
  auto emplaced_pair =
      pipeline_layouts_.emplace(pipeline_layout_key.key, pipeline_layout_entry);
  // unordered_map insertion doesn't invalidate element references.
  return &emplaced_pair.first->second;
}

Shader* VulkanCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                           uint32_t guest_address,
                                           const uint32_t* host_address,
                                           uint32_t dword_count) {
  return pipeline_cache_->LoadShader(shader_type, host_address, dword_count);
}

bool VulkanCommandProcessor::IssueDraw(xenos::PrimitiveType prim_type,
                                       uint32_t index_count,
                                       IndexBufferInfo* index_buffer_info,
                                       bool major_mode_explicit) {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  const RegisterFile& regs = *register_file_;

  xenos::ModeControl edram_mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
  if (edram_mode == xenos::ModeControl::kCopy) {
    // Special copy handling.
    return IssueCopy();
  }

  // Vertex shader analysis.
  auto vertex_shader = static_cast<VulkanShader*>(active_vertex_shader());
  if (!vertex_shader) {
    // Always need a vertex shader.
    return false;
  }
  pipeline_cache_->AnalyzeShaderUcode(*vertex_shader);
  bool memexport_used_vertex = vertex_shader->is_valid_memexport_used();

  // Pixel shader analysis.
  bool primitive_polygonal = draw_util::IsPrimitivePolygonal(regs);
  bool is_rasterization_done =
      draw_util::IsRasterizationPotentiallyDone(regs, primitive_polygonal);
  VulkanShader* pixel_shader = nullptr;
  if (is_rasterization_done) {
    // See xenos::ModeControl for explanation why the pixel shader is only used
    // when it's kColorDepth here.
    if (edram_mode == xenos::ModeControl::kColorDepth) {
      pixel_shader = static_cast<VulkanShader*>(active_pixel_shader());
      if (pixel_shader) {
        pipeline_cache_->AnalyzeShaderUcode(*pixel_shader);
        if (!draw_util::IsPixelShaderNeededWithRasterization(*pixel_shader,
                                                             regs)) {
          pixel_shader = nullptr;
        }
      }
    }
  } else {
    // Disabling pixel shader for this case is also required by the pipeline
    // cache.
    if (!memexport_used_vertex) {
      // This draw has no effect.
      return true;
    }
  }
  // TODO(Triang3l): Memory export.

  BeginSubmission(true);

  // Process primitives.
  PrimitiveProcessor::ProcessingResult primitive_processing_result;
  if (!primitive_processor_->Process(primitive_processing_result)) {
    return false;
  }
  if (!primitive_processing_result.host_draw_vertex_count) {
    // Nothing to draw.
    return true;
  }
  // TODO(Triang3l): Tessellation.
  if (primitive_processing_result.host_vertex_shader_type !=
      Shader::HostVertexShaderType::kVertex) {
    return false;
  }

  // Shader modifications.
  SpirvShaderTranslator::Modification vertex_shader_modification =
      pipeline_cache_->GetCurrentVertexShaderModification(
          *vertex_shader, primitive_processing_result.host_vertex_shader_type);
  SpirvShaderTranslator::Modification pixel_shader_modification =
      pixel_shader
          ? pipeline_cache_->GetCurrentPixelShaderModification(*pixel_shader)
          : SpirvShaderTranslator::Modification(0);

  // Set up the render targets - this may perform dispatches and draws.
  uint32_t pixel_shader_writes_color_targets =
      pixel_shader ? pixel_shader->writes_color_targets() : 0;
  if (!render_target_cache_->Update(is_rasterization_done,
                                    pixel_shader_writes_color_targets)) {
    return false;
  }

  // Translate the shaders.
  VulkanShader::VulkanTranslation* vertex_shader_translation =
      static_cast<VulkanShader::VulkanTranslation*>(
          vertex_shader->GetOrCreateTranslation(
              vertex_shader_modification.value));
  VulkanShader::VulkanTranslation* pixel_shader_translation =
      pixel_shader ? static_cast<VulkanShader::VulkanTranslation*>(
                         pixel_shader->GetOrCreateTranslation(
                             pixel_shader_modification.value))
                   : nullptr;

  // Update the graphics pipeline, and if the new graphics pipeline has a
  // different layout, invalidate incompatible descriptor sets before updating
  // current_graphics_pipeline_layout_.
  VkPipeline pipeline;
  const VulkanPipelineCache::PipelineLayoutProvider* pipeline_layout_provider;
  if (!pipeline_cache_->ConfigurePipeline(
          vertex_shader_translation, pixel_shader_translation,
          primitive_processing_result,
          render_target_cache_->last_update_render_pass_key(), pipeline,
          pipeline_layout_provider)) {
    return false;
  }
  deferred_command_buffer_.CmdVkBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             pipeline);
  auto pipeline_layout =
      static_cast<const PipelineLayout*>(pipeline_layout_provider);
  if (current_graphics_pipeline_layout_ != pipeline_layout) {
    if (current_graphics_pipeline_layout_) {
      // Keep descriptor set layouts for which the new pipeline layout is
      // compatible with the previous one (pipeline layouts are compatible for
      // set N if set layouts 0 through N are compatible).
      uint32_t descriptor_sets_kept =
          uint32_t(SpirvShaderTranslator::kDescriptorSetCount);
      if (current_graphics_pipeline_layout_
              ->descriptor_set_layout_textures_vertex_ref !=
          pipeline_layout->descriptor_set_layout_textures_vertex_ref) {
        descriptor_sets_kept = std::min(
            descriptor_sets_kept,
            uint32_t(SpirvShaderTranslator::kDescriptorSetTexturesVertex));
      }
      if (current_graphics_pipeline_layout_
              ->descriptor_set_layout_textures_pixel_ref !=
          pipeline_layout->descriptor_set_layout_textures_pixel_ref) {
        descriptor_sets_kept = std::min(
            descriptor_sets_kept,
            uint32_t(SpirvShaderTranslator::kDescriptorSetTexturesPixel));
      }
    } else {
      // No or unknown pipeline layout previously bound - all bindings are in an
      // indeterminate state.
      current_graphics_descriptor_sets_bound_up_to_date_ = 0;
    }
    current_graphics_pipeline_layout_ = pipeline_layout;
  }

  const ui::vulkan::VulkanProvider& provider =
      GetVulkanContext().GetVulkanProvider();
  const VkPhysicalDeviceProperties& device_properties =
      provider.device_properties();

  // Get dynamic rasterizer state.
  draw_util::ViewportInfo viewport_info;
  // Just handling maxViewportDimensions is enough - viewportBoundsRange[1] must
  // be at least 2 * max(maxViewportDimensions[0...1]) - 1, and
  // maxViewportDimensions must be greater than or equal to the size of the
  // largest possible framebuffer attachment (if the viewport has positive
  // offset and is between maxViewportDimensions and viewportBoundsRange[1],
  // GetHostViewportInfo will adjust ndc_scale/ndc_offset to clamp it, and the
  // clamped range will be outside the largest possible framebuffer anyway.
  // FIXME(Triang3l): Possibly handle maxViewportDimensions and
  // viewportBoundsRange separately because when using fragment shader
  // interlocks, framebuffers are not used, while the range may be wider than
  // dimensions? Though viewport bigger than 4096 - the smallest possible
  // maximum dimension (which is below the 8192 texture size limit on the Xbox
  // 360) - and with offset, is probably a situation that never happens in real
  // life. Or even disregard the viewport bounds range in the fragment shader
  // interlocks case completely - apply the viewport and the scissor offset
  // directly to pixel address and to things like ps_param_gen.
  draw_util::GetHostViewportInfo(
      regs, 1, 1, false, device_properties.limits.maxViewportDimensions[0],
      device_properties.limits.maxViewportDimensions[1], true, false, false,
      false, viewport_info);

  // Update fixed-function dynamic state.
  UpdateFixedFunctionState(viewport_info);

  // Update system constants before uploading them.
  UpdateSystemConstantValues(primitive_processing_result.host_index_endian,
                             viewport_info);

  // Update uniform buffers and descriptor sets after binding the pipeline with
  // the new layout.
  if (!UpdateBindings(vertex_shader, pixel_shader)) {
    return false;
  }

  // Ensure vertex buffers are resident.
  // TODO(Triang3l): Cache residency for ranges in a way similar to how texture
  // validity is tracked.
  uint64_t vertex_buffers_resident[2] = {};
  for (const Shader::VertexBinding& vertex_binding :
       vertex_shader->vertex_bindings()) {
    uint32_t vfetch_index = vertex_binding.fetch_constant;
    if (vertex_buffers_resident[vfetch_index >> 6] &
        (uint64_t(1) << (vfetch_index & 63))) {
      continue;
    }
    const auto& vfetch_constant = regs.Get<xenos::xe_gpu_vertex_fetch_t>(
        XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + vfetch_index * 2);
    switch (vfetch_constant.type) {
      case xenos::FetchConstantType::kVertex:
        break;
      case xenos::FetchConstantType::kInvalidVertex:
        if (cvars::gpu_allow_invalid_fetch_constants) {
          break;
        }
        XELOGW(
            "Vertex fetch constant {} ({:08X} {:08X}) has \"invalid\" type! "
            "This "
            "is incorrect behavior, but you can try bypassing this by "
            "launching Xenia with --gpu_allow_invalid_fetch_constants=true.",
            vfetch_index, vfetch_constant.dword_0, vfetch_constant.dword_1);
        return false;
      default:
        XELOGW(
            "Vertex fetch constant {} ({:08X} {:08X}) is completely invalid!",
            vfetch_index, vfetch_constant.dword_0, vfetch_constant.dword_1);
        return false;
    }
    if (!shared_memory_->RequestRange(vfetch_constant.address << 2,
                                      vfetch_constant.size << 2)) {
      XELOGE(
          "Failed to request vertex buffer at 0x{:08X} (size {}) in the shared "
          "memory",
          vfetch_constant.address << 2, vfetch_constant.size << 2);
      return false;
    }
    vertex_buffers_resident[vfetch_index >> 6] |= uint64_t(1)
                                                  << (vfetch_index & 63);
  }

  // Insert the shared memory barrier if needed.
  // TODO(Triang3l): Memory export.
  shared_memory_->Use(VulkanSharedMemory::Usage::kRead);

  // After all commands that may dispatch, copy or insert barriers, enter the
  // render pass before drawing.
  VkRenderPass render_pass = render_target_cache_->last_update_render_pass();
  const VulkanRenderTargetCache::Framebuffer* framebuffer =
      render_target_cache_->last_update_framebuffer();
  if (current_render_pass_ != render_pass ||
      current_framebuffer_ != framebuffer->framebuffer) {
    if (current_render_pass_ != VK_NULL_HANDLE) {
      deferred_command_buffer_.CmdVkEndRenderPass();
    }
    current_render_pass_ = render_pass;
    current_framebuffer_ = framebuffer->framebuffer;
    VkRenderPassBeginInfo render_pass_begin_info;
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.pNext = nullptr;
    render_pass_begin_info.renderPass = render_pass;
    render_pass_begin_info.framebuffer = framebuffer->framebuffer;
    render_pass_begin_info.renderArea.offset.x = 0;
    render_pass_begin_info.renderArea.offset.y = 0;
    // TODO(Triang3l): Actual dirty width / height in the deferred command
    // buffer.
    render_pass_begin_info.renderArea.extent = framebuffer->host_extent;
    render_pass_begin_info.clearValueCount = 0;
    render_pass_begin_info.pClearValues = nullptr;
    deferred_command_buffer_.CmdVkBeginRenderPass(&render_pass_begin_info,
                                                  VK_SUBPASS_CONTENTS_INLINE);
  }

  // Draw.
  if (primitive_processing_result.index_buffer_type ==
      PrimitiveProcessor::ProcessedIndexBufferType::kNone) {
    deferred_command_buffer_.CmdVkDraw(
        primitive_processing_result.host_draw_vertex_count, 1, 0, 0);
  } else {
    std::pair<VkBuffer, VkDeviceSize> index_buffer;
    switch (primitive_processing_result.index_buffer_type) {
      case PrimitiveProcessor::ProcessedIndexBufferType::kGuest:
        index_buffer.first = shared_memory_->buffer();
        index_buffer.second = primitive_processing_result.guest_index_base;
        break;
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostConverted:
        index_buffer = primitive_processor_->GetConvertedIndexBuffer(
            primitive_processing_result.host_index_buffer_handle);
        break;
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostBuiltin:
        index_buffer = primitive_processor_->GetBuiltinIndexBuffer(
            primitive_processing_result.host_index_buffer_handle);
        break;
      default:
        assert_unhandled_case(primitive_processing_result.index_buffer_type);
        return false;
    }
    deferred_command_buffer_.CmdVkBindIndexBuffer(
        index_buffer.first, index_buffer.second,
        primitive_processing_result.host_index_format ==
                xenos::IndexFormat::kInt16
            ? VK_INDEX_TYPE_UINT16
            : VK_INDEX_TYPE_UINT32);
    deferred_command_buffer_.CmdVkDrawIndexed(
        primitive_processing_result.host_draw_vertex_count, 1, 0, 0, 0);
  }

  return true;
}

bool VulkanCommandProcessor::IssueCopy() {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  BeginSubmission(true);

  return true;
}

void VulkanCommandProcessor::InitializeTrace() {
  BeginSubmission(false);
  bool shared_memory_submitted =
      shared_memory_->InitializeTraceSubmitDownloads();
  if (!shared_memory_submitted) {
    return;
  }
  AwaitAllQueueOperationsCompletion();
  if (shared_memory_submitted) {
    shared_memory_->InitializeTraceCompleteDownloads();
  }
}

void VulkanCommandProcessor::CheckSubmissionFence(uint64_t await_submission) {
  if (await_submission >= GetCurrentSubmission()) {
    if (submission_open_) {
      EndSubmission(false);
    }
    // A submission won't be ended if it hasn't been started, or if ending
    // has failed - clamp the index.
    await_submission = GetCurrentSubmission() - 1;
  }

  const ui::vulkan::VulkanProvider& provider =
      GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  size_t fences_total = submissions_in_flight_fences_.size();
  size_t fences_awaited = 0;
  if (await_submission > submission_completed_) {
    // Await in a blocking way if requested.
    if (dfn.vkWaitForFences(device,
                            uint32_t(await_submission - submission_completed_),
                            submissions_in_flight_fences_.data(), VK_TRUE,
                            UINT64_MAX) == VK_SUCCESS) {
      fences_awaited += await_submission - submission_completed_;
    } else {
      XELOGE("Failed to await submission completion Vulkan fences");
    }
  }
  // Check how far into the submissions the GPU currently is, in order because
  // submission themselves can be executed out of order, but Xenia serializes
  // that for simplicity.
  while (fences_awaited < fences_total) {
    if (dfn.vkWaitForFences(device, 1,
                            &submissions_in_flight_fences_[fences_awaited],
                            VK_TRUE, 0) != VK_SUCCESS) {
      break;
    }
    ++fences_awaited;
  }
  if (!fences_awaited) {
    // Not updated - no need to reclaim or download things.
    return;
  }
  // Reclaim fences.
  fences_free_.reserve(fences_free_.size() + fences_awaited);
  auto submissions_in_flight_fences_awaited_end =
      submissions_in_flight_fences_.cbegin();
  std::advance(submissions_in_flight_fences_awaited_end, fences_awaited);
  fences_free_.insert(fences_free_.cend(),
                      submissions_in_flight_fences_.cbegin(),
                      submissions_in_flight_fences_awaited_end);
  submissions_in_flight_fences_.erase(submissions_in_flight_fences_.cbegin(),
                                      submissions_in_flight_fences_awaited_end);
  submission_completed_ += fences_awaited;

  // Reclaim semaphores.
  while (!submissions_in_flight_semaphores_.empty()) {
    const auto& semaphore_submission =
        submissions_in_flight_semaphores_.front();
    if (semaphore_submission.second > submission_completed_) {
      break;
    }
    semaphores_free_.push_back(semaphore_submission.first);
    submissions_in_flight_semaphores_.pop_front();
  }

  // Reclaim command pools.
  while (!command_buffers_submitted_.empty()) {
    const auto& command_buffer_pair = command_buffers_submitted_.front();
    if (command_buffer_pair.second > submission_completed_) {
      break;
    }
    command_buffers_writable_.push_back(command_buffer_pair.first);
    command_buffers_submitted_.pop_front();
  }

  shared_memory_->CompletedSubmissionUpdated();

  primitive_processor_->CompletedSubmissionUpdated();
}

void VulkanCommandProcessor::BeginSubmission(bool is_guest_command) {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  bool is_opening_frame = is_guest_command && !frame_open_;
  if (submission_open_ && !is_opening_frame) {
    return;
  }

  // Check the fence - needed for all kinds of submissions (to reclaim transient
  // resources early) and specifically for frames (not to queue too many), and
  // await the availability of the current frame.
  CheckSubmissionFence(
      is_opening_frame
          ? closed_frame_submissions_[frame_current_ % kMaxFramesInFlight]
          : 0);
  // TODO(Triang3l): If failed to await (completed submission < awaited frame
  // submission), do something like dropping the draw command that wanted to
  // open the frame.
  if (is_opening_frame) {
    // Update the completed frame index, also obtaining the actual completed
    // frame number (since the CPU may be actually less than 3 frames behind)
    // before reclaiming resources tracked with the frame number.
    frame_completed_ = std::max(frame_current_, uint64_t(kMaxFramesInFlight)) -
                       kMaxFramesInFlight;
    for (uint64_t frame = frame_completed_ + 1; frame < frame_current_;
         ++frame) {
      if (closed_frame_submissions_[frame % kMaxFramesInFlight] >
          submission_completed_) {
        break;
      }
      frame_completed_ = frame;
    }
  }

  if (!submission_open_) {
    submission_open_ = true;

    // Start a new deferred command buffer - will submit it to the real one in
    // the end of the submission (when async pipeline object creation requests
    // are fulfilled).
    deferred_command_buffer_.Reset();

    // Reset cached state of the command buffer.
    ff_viewport_update_needed_ = true;
    ff_scissor_update_needed_ = true;
    current_render_pass_ = VK_NULL_HANDLE;
    current_framebuffer_ = VK_NULL_HANDLE;
    current_graphics_pipeline_ = VK_NULL_HANDLE;
    current_graphics_pipeline_layout_ = nullptr;
    current_graphics_descriptor_sets_bound_up_to_date_ = 0;

    primitive_processor_->BeginSubmission();
  }

  if (is_opening_frame) {
    frame_open_ = true;

    // Reset bindings that depend on transient data.
    std::memset(current_float_constant_map_vertex_, 0,
                sizeof(current_float_constant_map_vertex_));
    std::memset(current_float_constant_map_pixel_, 0,
                sizeof(current_float_constant_map_pixel_));
    std::memset(current_graphics_descriptor_sets_, 0,
                sizeof(current_graphics_descriptor_sets_));
    current_graphics_descriptor_sets_
        [SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram] =
            shared_memory_and_edram_descriptor_set_;
    current_graphics_descriptor_set_values_up_to_date_ =
        uint32_t(1)
        << SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram;

    // Reclaim pool pages - no need to do this every small submission since some
    // may be reused.
    transient_descriptor_pool_uniform_buffers_->Reclaim(frame_completed_);
    uniform_buffer_pool_->Reclaim(frame_completed_);

    primitive_processor_->BeginFrame();
  }
}

bool VulkanCommandProcessor::EndSubmission(bool is_swap) {
  ui::vulkan::VulkanProvider& provider = GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // Make sure everything needed for submitting exist.
  if (submission_open_) {
    if (fences_free_.empty()) {
      VkFenceCreateInfo fence_create_info;
      fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      fence_create_info.pNext = nullptr;
      fence_create_info.flags = 0;
      VkFence fence;
      if (dfn.vkCreateFence(device, &fence_create_info, nullptr, &fence) !=
          VK_SUCCESS) {
        XELOGE("Failed to create a Vulkan fence");
        // Try to submit later. Completely dropping the submission is not
        // permitted because resources would be left in an undefined state.
        return false;
      }
      fences_free_.push_back(fence);
    }
    if (!sparse_memory_binds_.empty() && semaphores_free_.empty()) {
      VkSemaphoreCreateInfo semaphore_create_info;
      semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      semaphore_create_info.pNext = nullptr;
      semaphore_create_info.flags = 0;
      VkSemaphore semaphore;
      if (dfn.vkCreateSemaphore(device, &semaphore_create_info, nullptr,
                                &semaphore) != VK_SUCCESS) {
        XELOGE("Failed to create a Vulkan semaphore");
        return false;
      }
      semaphores_free_.push_back(semaphore);
    }
    if (command_buffers_writable_.empty()) {
      CommandBuffer command_buffer;
      VkCommandPoolCreateInfo command_pool_create_info;
      command_pool_create_info.sType =
          VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
      command_pool_create_info.pNext = nullptr;
      command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
      command_pool_create_info.queueFamilyIndex =
          provider.queue_family_graphics_compute();
      if (dfn.vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                                  &command_buffer.pool) != VK_SUCCESS) {
        XELOGE("Failed to create a Vulkan command pool");
        return false;
      }
      VkCommandBufferAllocateInfo command_buffer_allocate_info;
      command_buffer_allocate_info.sType =
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      command_buffer_allocate_info.pNext = nullptr;
      command_buffer_allocate_info.commandPool = command_buffer.pool;
      command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      command_buffer_allocate_info.commandBufferCount = 1;
      if (dfn.vkAllocateCommandBuffers(device, &command_buffer_allocate_info,
                                       &command_buffer.buffer) != VK_SUCCESS) {
        XELOGE("Failed to allocate a Vulkan command buffer");
        dfn.vkDestroyCommandPool(device, command_buffer.pool, nullptr);
        return false;
      }
      command_buffers_writable_.push_back(command_buffer);
    }
  }

  bool is_closing_frame = is_swap && frame_open_;

  if (is_closing_frame) {
    primitive_processor_->EndFrame();
  }

  if (submission_open_) {
    EndRenderPass();

    primitive_processor_->EndSubmission();

    shared_memory_->EndSubmission();

    uniform_buffer_pool_->FlushWrites();

    // Submit sparse binds earlier, before executing the deferred command
    // buffer, to reduce latency.
    if (!sparse_memory_binds_.empty()) {
      sparse_buffer_bind_infos_temp_.clear();
      sparse_buffer_bind_infos_temp_.reserve(sparse_buffer_binds_.size());
      for (const SparseBufferBind& sparse_buffer_bind : sparse_buffer_binds_) {
        VkSparseBufferMemoryBindInfo& sparse_buffer_bind_info =
            sparse_buffer_bind_infos_temp_.emplace_back();
        sparse_buffer_bind_info.buffer = sparse_buffer_bind.buffer;
        sparse_buffer_bind_info.bindCount = sparse_buffer_bind.bind_count;
        sparse_buffer_bind_info.pBinds =
            sparse_memory_binds_.data() + sparse_buffer_bind.bind_offset;
      }
      assert_false(semaphores_free_.empty());
      VkSemaphore bind_sparse_semaphore = semaphores_free_.back();
      VkBindSparseInfo bind_sparse_info;
      bind_sparse_info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
      bind_sparse_info.pNext = nullptr;
      bind_sparse_info.waitSemaphoreCount = 0;
      bind_sparse_info.pWaitSemaphores = nullptr;
      bind_sparse_info.bufferBindCount =
          uint32_t(sparse_buffer_bind_infos_temp_.size());
      bind_sparse_info.pBufferBinds =
          !sparse_buffer_bind_infos_temp_.empty()
              ? sparse_buffer_bind_infos_temp_.data()
              : nullptr;
      bind_sparse_info.imageOpaqueBindCount = 0;
      bind_sparse_info.pImageOpaqueBinds = nullptr;
      bind_sparse_info.imageBindCount = 0;
      bind_sparse_info.pImageBinds = 0;
      bind_sparse_info.signalSemaphoreCount = 1;
      bind_sparse_info.pSignalSemaphores = &bind_sparse_semaphore;
      if (provider.BindSparse(1, &bind_sparse_info, VK_NULL_HANDLE) !=
          VK_SUCCESS) {
        XELOGE("Failed to submit Vulkan sparse binds");
        return false;
      }
      current_submission_wait_semaphores_.push_back(bind_sparse_semaphore);
      semaphores_free_.pop_back();
      current_submission_wait_stage_masks_.push_back(
          sparse_bind_wait_stage_mask_);
      sparse_bind_wait_stage_mask_ = 0;
      sparse_buffer_binds_.clear();
      sparse_memory_binds_.clear();
    }

    assert_false(command_buffers_writable_.empty());
    CommandBuffer command_buffer = command_buffers_writable_.back();
    if (dfn.vkResetCommandPool(device, command_buffer.pool, 0) != VK_SUCCESS) {
      XELOGE("Failed to reset a Vulkan command pool");
      return false;
    }
    VkCommandBufferBeginInfo command_buffer_begin_info;
    command_buffer_begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.pNext = nullptr;
    command_buffer_begin_info.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    command_buffer_begin_info.pInheritanceInfo = nullptr;
    if (dfn.vkBeginCommandBuffer(command_buffer.buffer,
                                 &command_buffer_begin_info) != VK_SUCCESS) {
      XELOGE("Failed to begin a Vulkan command buffer");
      return false;
    }
    deferred_command_buffer_.Execute(command_buffer.buffer);
    if (dfn.vkEndCommandBuffer(command_buffer.buffer) != VK_SUCCESS) {
      XELOGE("Failed to end a Vulkan command buffer");
      return false;
    }

    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    if (!current_submission_wait_semaphores_.empty()) {
      submit_info.waitSemaphoreCount =
          uint32_t(current_submission_wait_semaphores_.size());
      submit_info.pWaitSemaphores = current_submission_wait_semaphores_.data();
      submit_info.pWaitDstStageMask =
          current_submission_wait_stage_masks_.data();
    } else {
      submit_info.waitSemaphoreCount = 0;
      submit_info.pWaitSemaphores = nullptr;
      submit_info.pWaitDstStageMask = nullptr;
    }
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer.buffer;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = nullptr;
    assert_false(fences_free_.empty());
    VkFence fence = fences_free_.back();
    if (dfn.vkResetFences(device, 1, &fence) != VK_SUCCESS) {
      XELOGE("Failed to reset a Vulkan submission fence");
      return false;
    }
    if (provider.SubmitToGraphicsComputeQueue(1, &submit_info, fence) !=
        VK_SUCCESS) {
      XELOGE("Failed to submit a Vulkan command buffer");
      return false;
    }
    uint64_t submission_current = GetCurrentSubmission();
    current_submission_wait_stage_masks_.clear();
    for (VkSemaphore semaphore : current_submission_wait_semaphores_) {
      submissions_in_flight_semaphores_.emplace_back(semaphore,
                                                     submission_current);
    }
    current_submission_wait_semaphores_.clear();
    command_buffers_submitted_.emplace_back(command_buffer, submission_current);
    command_buffers_writable_.pop_back();
    // Increments the current submission number, going to the next submission.
    submissions_in_flight_fences_.push_back(fence);
    fences_free_.pop_back();

    submission_open_ = false;
  }

  if (is_closing_frame) {
    frame_open_ = false;
    // Submission already closed now, so minus 1.
    closed_frame_submissions_[(frame_current_++) % kMaxFramesInFlight] =
        GetCurrentSubmission() - 1;

    if (cache_clear_requested_ && AwaitAllQueueOperationsCompletion()) {
      cache_clear_requested_ = false;

      assert_true(command_buffers_submitted_.empty());
      for (const CommandBuffer& command_buffer : command_buffers_writable_) {
        dfn.vkDestroyCommandPool(device, command_buffer.pool, nullptr);
      }
      command_buffers_writable_.clear();

      uniform_buffer_pool_->ClearCache();
      transient_descriptor_pool_uniform_buffers_->ClearCache();

      pipeline_cache_->ClearCache();

      render_target_cache_->ClearCache();

      for (const auto& pipeline_layout_pair : pipeline_layouts_) {
        dfn.vkDestroyPipelineLayout(
            device, pipeline_layout_pair.second.pipeline_layout, nullptr);
      }
      pipeline_layouts_.clear();
      for (const auto& descriptor_set_layout_pair :
           descriptor_set_layouts_textures_) {
        dfn.vkDestroyDescriptorSetLayout(
            device, descriptor_set_layout_pair.second, nullptr);
      }
      descriptor_set_layouts_textures_.clear();

      primitive_processor_->ClearCache();
    }
  }

  return true;
}

VkShaderStageFlags VulkanCommandProcessor::GetGuestVertexShaderStageFlags()
    const {
  VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT;
  const ui::vulkan::VulkanProvider& provider =
      GetVulkanContext().GetVulkanProvider();
  if (provider.device_features().tessellationShader) {
    stages |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
  }
  // TODO(Triang3l): Vertex to compute translation for rectangle and possibly
  // point emulation.
  return stages;
}

void VulkanCommandProcessor::UpdateFixedFunctionState(
    const draw_util::ViewportInfo& viewport_info) {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  const RegisterFile& regs = *register_file_;

  // Window parameters.
  // http://ftp.tku.edu.tw/NetBSD/NetBSD-current/xsrc/external/mit/xf86-video-ati/dist/src/r600_reg_auto_r6xx.h
  // See r200UpdateWindow:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  auto pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();

  // Viewport.
  VkViewport viewport;
  if (viewport_info.xy_extent[0] && viewport_info.xy_extent[1]) {
    viewport.x = float(viewport_info.xy_offset[0]);
    viewport.y = float(viewport_info.xy_offset[1]);
    viewport.width = float(viewport_info.xy_extent[0]);
    viewport.height = float(viewport_info.xy_extent[1]);
  } else {
    // Vulkan viewport width must be greater than 0.0f, but the Xenia  viewport
    // may be empty for various reasons - set the viewport to outside the
    // framebuffer.
    viewport.x = -1.0f;
    viewport.y = -1.0f;
    viewport.width = 1.0f;
    viewport.height = 1.0f;
  }
  viewport.minDepth = viewport_info.z_min;
  viewport.maxDepth = viewport_info.z_max;
  ff_viewport_update_needed_ |= ff_viewport_.x != viewport.x;
  ff_viewport_update_needed_ |= ff_viewport_.y != viewport.y;
  ff_viewport_update_needed_ |= ff_viewport_.width != viewport.width;
  ff_viewport_update_needed_ |= ff_viewport_.height != viewport.height;
  ff_viewport_update_needed_ |= ff_viewport_.minDepth != viewport.minDepth;
  ff_viewport_update_needed_ |= ff_viewport_.maxDepth != viewport.maxDepth;
  if (ff_viewport_update_needed_) {
    ff_viewport_ = viewport;
    deferred_command_buffer_.CmdVkSetViewport(0, 1, &viewport);
    ff_viewport_update_needed_ = false;
  }

  // Scissor.
  draw_util::Scissor scissor;
  draw_util::GetScissor(regs, scissor);
  VkRect2D scissor_rect;
  scissor_rect.offset.x = int32_t(scissor.offset[0]);
  scissor_rect.offset.y = int32_t(scissor.offset[1]);
  scissor_rect.extent.width = scissor.extent[0];
  scissor_rect.extent.height = scissor.extent[1];
  ff_scissor_update_needed_ |= ff_scissor_.offset.x != scissor_rect.offset.x;
  ff_scissor_update_needed_ |= ff_scissor_.offset.y != scissor_rect.offset.y;
  ff_scissor_update_needed_ |=
      ff_scissor_.extent.width != scissor_rect.extent.width;
  ff_scissor_update_needed_ |=
      ff_scissor_.extent.height != scissor_rect.extent.height;
  if (ff_scissor_update_needed_) {
    ff_scissor_ = scissor_rect;
    deferred_command_buffer_.CmdVkSetScissor(0, 1, &scissor_rect);
    ff_scissor_update_needed_ = false;
  }
}

void VulkanCommandProcessor::UpdateSystemConstantValues(
    xenos::Endian index_endian, const draw_util::ViewportInfo& viewport_info) {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  const RegisterFile& regs = *register_file_;
  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  int32_t vgt_indx_offset = int32_t(regs[XE_GPU_REG_VGT_INDX_OFFSET].u32);

  bool dirty = false;

  // Flags.
  uint32_t flags = 0;
  // W0 division control.
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  // 8: VTX_XY_FMT = true: the incoming XY have already been multiplied by 1/W0.
  //               = false: multiply the X, Y coordinates by 1/W0.
  // 9: VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
  //              = false: multiply the Z coordinate by 1/W0.
  // 10: VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal
  //                        to get 1/W0.
  if (pa_cl_vte_cntl.vtx_xy_fmt) {
    flags |= SpirvShaderTranslator::kSysFlag_XYDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_z_fmt) {
    flags |= SpirvShaderTranslator::kSysFlag_ZDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_w0_fmt) {
    flags |= SpirvShaderTranslator::kSysFlag_WNotReciprocal;
  }
  dirty |= system_constants_.flags != flags;
  system_constants_.flags = flags;

  // Index or tessellation edge factor buffer endianness.
  dirty |= system_constants_.vertex_index_endian != index_endian;
  system_constants_.vertex_index_endian = index_endian;

  // Vertex index offset.
  dirty |= system_constants_.vertex_base_index != vgt_indx_offset;
  system_constants_.vertex_base_index = vgt_indx_offset;

  // Conversion to host normalized device coordinates.
  for (uint32_t i = 0; i < 3; ++i) {
    dirty |= system_constants_.ndc_scale[i] != viewport_info.ndc_scale[i];
    dirty |= system_constants_.ndc_offset[i] != viewport_info.ndc_offset[i];
    system_constants_.ndc_scale[i] = viewport_info.ndc_scale[i];
    system_constants_.ndc_offset[i] = viewport_info.ndc_offset[i];
  }

  if (dirty) {
    current_graphics_descriptor_set_values_up_to_date_ &=
        ~(uint32_t(1) << SpirvShaderTranslator::kDescriptorSetSystemConstants);
  }
}

bool VulkanCommandProcessor::UpdateBindings(const VulkanShader* vertex_shader,
                                            const VulkanShader* pixel_shader) {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  const RegisterFile& regs = *register_file_;

  // Invalidate descriptors for changed data.
  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ on the Xbox
  // 360 (however, OpenGL ES on Adreno 200 on Android has different ranges).
  assert_true(regs[XE_GPU_REG_SQ_VS_CONST].u32 == 0x000FF000 ||
              regs[XE_GPU_REG_SQ_VS_CONST].u32 == 0x00000000);
  assert_true(regs[XE_GPU_REG_SQ_PS_CONST].u32 == 0x000FF100 ||
              regs[XE_GPU_REG_SQ_PS_CONST].u32 == 0x00000000);
  // Check if the float constant layout is still the same and get the counts.
  const Shader::ConstantRegisterMap& float_constant_map_vertex =
      vertex_shader->constant_register_map();
  uint32_t float_constant_count_vertex = float_constant_map_vertex.float_count;
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_float_constant_map_vertex_[i] !=
        float_constant_map_vertex.float_bitmap[i]) {
      current_float_constant_map_vertex_[i] =
          float_constant_map_vertex.float_bitmap[i];
      // If no float constants at all, any buffer can be reused for them, so not
      // invalidating.
      if (float_constant_count_vertex) {
        current_graphics_descriptor_set_values_up_to_date_ &=
            ~(
                uint32_t(1)
                << SpirvShaderTranslator::kDescriptorSetFloatConstantsVertex);
      }
    }
  }
  uint32_t float_constant_count_pixel = 0;
  if (pixel_shader != nullptr) {
    const Shader::ConstantRegisterMap& float_constant_map_pixel =
        pixel_shader->constant_register_map();
    float_constant_count_pixel = float_constant_map_pixel.float_count;
    for (uint32_t i = 0; i < 4; ++i) {
      if (current_float_constant_map_pixel_[i] !=
          float_constant_map_pixel.float_bitmap[i]) {
        current_float_constant_map_pixel_[i] =
            float_constant_map_pixel.float_bitmap[i];
        if (float_constant_count_pixel) {
          current_graphics_descriptor_set_values_up_to_date_ &=
              ~(uint32_t(1)
                << SpirvShaderTranslator::kDescriptorSetFloatConstantsPixel);
        }
      }
    }
  } else {
    std::memset(current_float_constant_map_pixel_, 0,
                sizeof(current_float_constant_map_pixel_));
  }

  // Make sure new descriptor sets are bound to the command buffer.
  current_graphics_descriptor_sets_bound_up_to_date_ &=
      current_graphics_descriptor_set_values_up_to_date_;

  // Write the new descriptor sets.
  VkWriteDescriptorSet
      write_descriptor_sets[SpirvShaderTranslator::kDescriptorSetCount];
  uint32_t write_descriptor_set_count = 0;
  uint32_t write_descriptor_set_bits = 0;
  assert_not_zero(
      current_graphics_descriptor_set_values_up_to_date_ &
      (uint32_t(1)
       << SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram));
  VkDescriptorBufferInfo buffer_info_bool_loop_constants;
  if (!(current_graphics_descriptor_set_values_up_to_date_ &
        (uint32_t(1)
         << SpirvShaderTranslator::kDescriptorSetBoolLoopConstants))) {
    VkWriteDescriptorSet& write_bool_loop_constants =
        write_descriptor_sets[write_descriptor_set_count++];
    constexpr size_t kBoolLoopConstantsSize = sizeof(uint32_t) * (8 + 32);
    uint8_t* mapping_bool_loop_constants = WriteUniformBufferBinding(
        kBoolLoopConstantsSize,
        descriptor_set_layout_fetch_bool_loop_constants_,
        buffer_info_bool_loop_constants, write_bool_loop_constants);
    if (!mapping_bool_loop_constants) {
      return false;
    }
    std::memcpy(mapping_bool_loop_constants,
                &regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031].u32,
                kBoolLoopConstantsSize);
    write_descriptor_set_bits |=
        uint32_t(1) << SpirvShaderTranslator::kDescriptorSetBoolLoopConstants;
    current_graphics_descriptor_sets_
        [SpirvShaderTranslator::kDescriptorSetBoolLoopConstants] =
            write_bool_loop_constants.dstSet;
  }
  VkDescriptorBufferInfo buffer_info_system_constants;
  if (!(current_graphics_descriptor_set_values_up_to_date_ &
        (uint32_t(1)
         << SpirvShaderTranslator::kDescriptorSetSystemConstants))) {
    VkWriteDescriptorSet& write_system_constants =
        write_descriptor_sets[write_descriptor_set_count++];
    uint8_t* mapping_system_constants = WriteUniformBufferBinding(
        sizeof(SpirvShaderTranslator::SystemConstants),
        descriptor_set_layout_system_constants_, buffer_info_system_constants,
        write_system_constants);
    if (!mapping_system_constants) {
      return false;
    }
    std::memcpy(mapping_system_constants, &system_constants_,
                sizeof(SpirvShaderTranslator::SystemConstants));
    write_descriptor_set_bits |=
        uint32_t(1) << SpirvShaderTranslator::kDescriptorSetSystemConstants;
    current_graphics_descriptor_sets_
        [SpirvShaderTranslator::kDescriptorSetSystemConstants] =
            write_system_constants.dstSet;
  }
  VkDescriptorBufferInfo buffer_info_float_constant_pixel;
  if (!(current_graphics_descriptor_set_values_up_to_date_ &
        (uint32_t(1)
         << SpirvShaderTranslator::kDescriptorSetFloatConstantsPixel))) {
    // Even if the shader doesn't need any float constants, a valid binding must
    // still be provided (the pipeline layout always has float constants, for
    // both the vertex shader and the pixel shader), so if the first draw in the
    // frame doesn't have float constants at all, still allocate an empty
    // buffer.
    VkWriteDescriptorSet& write_float_constants_pixel =
        write_descriptor_sets[write_descriptor_set_count++];
    uint8_t* mapping_float_constants_pixel = WriteUniformBufferBinding(
        sizeof(float) * 4 * std::max(float_constant_count_pixel, uint32_t(1)),
        descriptor_set_layout_float_constants_pixel_,
        buffer_info_float_constant_pixel, write_float_constants_pixel);
    if (!mapping_float_constants_pixel) {
      return false;
    }
    for (uint32_t i = 0; i < 4; ++i) {
      uint64_t float_constant_map_entry = current_float_constant_map_pixel_[i];
      uint32_t float_constant_index;
      while (xe::bit_scan_forward(float_constant_map_entry,
                                  &float_constant_index)) {
        float_constant_map_entry &= ~(1ull << float_constant_index);
        std::memcpy(mapping_float_constants_pixel,
                    &regs[XE_GPU_REG_SHADER_CONSTANT_256_X + (i << 8) +
                          (float_constant_index << 2)]
                         .f32,
                    sizeof(float) * 4);
        mapping_float_constants_pixel += sizeof(float) * 4;
      }
    }
    write_descriptor_set_bits |=
        uint32_t(1) << SpirvShaderTranslator::kDescriptorSetFloatConstantsPixel;
    current_graphics_descriptor_sets_
        [SpirvShaderTranslator::kDescriptorSetFloatConstantsPixel] =
            write_float_constants_pixel.dstSet;
  }
  VkDescriptorBufferInfo buffer_info_float_constant_vertex;
  if (!(current_graphics_descriptor_set_values_up_to_date_ &
        (uint32_t(1)
         << SpirvShaderTranslator::kDescriptorSetFloatConstantsVertex))) {
    VkWriteDescriptorSet& write_float_constants_vertex =
        write_descriptor_sets[write_descriptor_set_count++];
    uint8_t* mapping_float_constants_vertex = WriteUniformBufferBinding(
        sizeof(float) * 4 * std::max(float_constant_count_vertex, uint32_t(1)),
        descriptor_set_layout_float_constants_vertex_,
        buffer_info_float_constant_vertex, write_float_constants_vertex);
    if (!mapping_float_constants_vertex) {
      return false;
    }
    for (uint32_t i = 0; i < 4; ++i) {
      uint64_t float_constant_map_entry = current_float_constant_map_vertex_[i];
      uint32_t float_constant_index;
      while (xe::bit_scan_forward(float_constant_map_entry,
                                  &float_constant_index)) {
        float_constant_map_entry &= ~(1ull << float_constant_index);
        std::memcpy(mapping_float_constants_vertex,
                    &regs[XE_GPU_REG_SHADER_CONSTANT_000_X + (i << 8) +
                          (float_constant_index << 2)]
                         .f32,
                    sizeof(float) * 4);
        mapping_float_constants_vertex += sizeof(float) * 4;
      }
    }
    write_descriptor_set_bits |=
        uint32_t(1)
        << SpirvShaderTranslator::kDescriptorSetFloatConstantsVertex;
    current_graphics_descriptor_sets_
        [SpirvShaderTranslator::kDescriptorSetFloatConstantsVertex] =
            write_float_constants_vertex.dstSet;
  }
  VkDescriptorBufferInfo buffer_info_fetch_constants;
  if (!(current_graphics_descriptor_set_values_up_to_date_ &
        (uint32_t(1) << SpirvShaderTranslator::kDescriptorSetFetchConstants))) {
    VkWriteDescriptorSet& write_fetch_constants =
        write_descriptor_sets[write_descriptor_set_count++];
    constexpr size_t kFetchConstantsSize = sizeof(uint32_t) * 6 * 32;
    uint8_t* mapping_fetch_constants = WriteUniformBufferBinding(
        kFetchConstantsSize, descriptor_set_layout_fetch_bool_loop_constants_,
        buffer_info_fetch_constants, write_fetch_constants);
    if (!mapping_fetch_constants) {
      return false;
    }
    std::memcpy(mapping_fetch_constants,
                &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0].u32,
                kFetchConstantsSize);
    write_descriptor_set_bits |=
        uint32_t(1) << SpirvShaderTranslator::kDescriptorSetFetchConstants;
    current_graphics_descriptor_sets_
        [SpirvShaderTranslator::kDescriptorSetFetchConstants] =
            write_fetch_constants.dstSet;
  }
  if (write_descriptor_set_count) {
    const ui::vulkan::VulkanProvider& provider =
        GetVulkanContext().GetVulkanProvider();
    const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
    VkDevice device = provider.device();
    dfn.vkUpdateDescriptorSets(device, write_descriptor_set_count,
                               write_descriptor_sets, 0, nullptr);
  }
  // Only make valid if written successfully.
  current_graphics_descriptor_set_values_up_to_date_ |=
      write_descriptor_set_bits;

  // Bind the new descriptor sets.
  uint32_t descriptor_sets_needed =
      (uint32_t(1) << SpirvShaderTranslator::kDescriptorSetCount) - 1;
  if (current_graphics_pipeline_layout_
          ->descriptor_set_layout_textures_vertex_ref ==
      descriptor_set_layout_empty_) {
    descriptor_sets_needed &=
        ~(uint32_t(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex);
  }
  if (current_graphics_pipeline_layout_
          ->descriptor_set_layout_textures_pixel_ref ==
      descriptor_set_layout_empty_) {
    descriptor_sets_needed &=
        ~(uint32_t(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel);
  }
  uint32_t descriptor_sets_remaining =
      descriptor_sets_needed &
      ~current_graphics_descriptor_sets_bound_up_to_date_;
  uint32_t descriptor_set_index;
  while (
      xe::bit_scan_forward(descriptor_sets_remaining, &descriptor_set_index)) {
    uint32_t descriptor_set_mask_tzcnt =
        xe::tzcnt(~(descriptor_sets_remaining |
                    ((uint32_t(1) << descriptor_set_index) - 1)));
    // TODO(Triang3l): Bind to compute for rectangle list emulation without
    // geometry shaders.
    deferred_command_buffer_.CmdVkBindDescriptorSets(
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        current_graphics_pipeline_layout_->pipeline_layout,
        descriptor_set_index, descriptor_set_mask_tzcnt - descriptor_set_index,
        current_graphics_descriptor_sets_ + descriptor_set_index, 0, nullptr);
    if (descriptor_set_mask_tzcnt >= 32) {
      break;
    }
    descriptor_sets_remaining &=
        ~((uint32_t(1) << descriptor_set_mask_tzcnt) - 1);
  }
  current_graphics_descriptor_sets_bound_up_to_date_ |= descriptor_sets_needed;

  return true;
}

uint8_t* VulkanCommandProcessor::WriteUniformBufferBinding(
    size_t size, VkDescriptorSetLayout descriptor_set_layout,
    VkDescriptorBufferInfo& descriptor_buffer_info_out,
    VkWriteDescriptorSet& write_descriptor_set_out) {
  VkDescriptorSet descriptor_set =
      transient_descriptor_pool_uniform_buffers_->Request(
          frame_current_, descriptor_set_layout, 1);
  if (descriptor_set == VK_NULL_HANDLE) {
    return nullptr;
  }
  const ui::vulkan::VulkanProvider& provider =
      GetVulkanContext().GetVulkanProvider();
  uint8_t* mapping = uniform_buffer_pool_->Request(
      frame_current_, size,
      size_t(
          provider.device_properties().limits.minUniformBufferOffsetAlignment),
      descriptor_buffer_info_out.buffer, descriptor_buffer_info_out.offset);
  if (!mapping) {
    return nullptr;
  }
  descriptor_buffer_info_out.range = VkDeviceSize(size);
  write_descriptor_set_out.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write_descriptor_set_out.pNext = nullptr;
  write_descriptor_set_out.dstSet = descriptor_set;
  write_descriptor_set_out.dstBinding = 0;
  write_descriptor_set_out.dstArrayElement = 0;
  write_descriptor_set_out.descriptorCount = 1;
  write_descriptor_set_out.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  write_descriptor_set_out.pImageInfo = nullptr;
  write_descriptor_set_out.pBufferInfo = &descriptor_buffer_info_out;
  write_descriptor_set_out.pTexelBufferView = nullptr;
  return mapping;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

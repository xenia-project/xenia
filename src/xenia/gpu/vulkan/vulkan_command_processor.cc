/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_command_processor.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <tuple>
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
#include "xenia/ui/vulkan/vulkan_presenter.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/fullscreen_tc_vs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/uv_ps.h"
}  // namespace shaders

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

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
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

  // TODO(Triang3l): Get the actual draw resolution scale when the texture cache
  // supports resolution scaling.
  render_target_cache_ = std::make_unique<VulkanRenderTargetCache>(
      *register_file_, *memory_, &trace_writer_, 1, 1, *this);
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

  // Swap objects.

  // Swap render pass. Doesn't make assumptions about outer usage (explicit
  // barriers must be used instead) for simplicity of use in different scenarios
  // with different pipelines.
  VkAttachmentDescription swap_render_pass_attachment;
  swap_render_pass_attachment.flags = 0;
  swap_render_pass_attachment.format =
      ui::vulkan::VulkanPresenter::kGuestOutputFormat;
  swap_render_pass_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  swap_render_pass_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  swap_render_pass_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  swap_render_pass_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  swap_render_pass_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  swap_render_pass_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  swap_render_pass_attachment.finalLayout =
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference swap_render_pass_color_attachment;
  swap_render_pass_color_attachment.attachment = 0;
  swap_render_pass_color_attachment.layout =
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkSubpassDescription swap_render_pass_subpass = {};
  swap_render_pass_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  swap_render_pass_subpass.colorAttachmentCount = 1;
  swap_render_pass_subpass.pColorAttachments =
      &swap_render_pass_color_attachment;
  VkSubpassDependency swap_render_pass_dependencies[2];
  for (uint32_t i = 0; i < 2; ++i) {
    VkSubpassDependency& swap_render_pass_dependency =
        swap_render_pass_dependencies[i];
    swap_render_pass_dependency.srcSubpass = i ? 0 : VK_SUBPASS_EXTERNAL;
    swap_render_pass_dependency.dstSubpass = i ? VK_SUBPASS_EXTERNAL : 0;
    swap_render_pass_dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    swap_render_pass_dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    swap_render_pass_dependency.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    swap_render_pass_dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    swap_render_pass_dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  }
  VkRenderPassCreateInfo swap_render_pass_create_info;
  swap_render_pass_create_info.sType =
      VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  swap_render_pass_create_info.pNext = nullptr;
  swap_render_pass_create_info.flags = 0;
  swap_render_pass_create_info.attachmentCount = 1;
  swap_render_pass_create_info.pAttachments = &swap_render_pass_attachment;
  swap_render_pass_create_info.subpassCount = 1;
  swap_render_pass_create_info.pSubpasses = &swap_render_pass_subpass;
  swap_render_pass_create_info.dependencyCount =
      uint32_t(xe::countof(swap_render_pass_dependencies));
  swap_render_pass_create_info.pDependencies = swap_render_pass_dependencies;
  if (dfn.vkCreateRenderPass(device, &swap_render_pass_create_info, nullptr,
                             &swap_render_pass_) != VK_SUCCESS) {
    XELOGE("Failed to create the Vulkan render pass for presentation");
    return false;
  }

  // Swap pipeline layout.
  // TODO(Triang3l): Source binding, push constants, FXAA pipeline layout.
  VkPipelineLayoutCreateInfo swap_pipeline_layout_create_info;
  swap_pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  swap_pipeline_layout_create_info.pNext = nullptr;
  swap_pipeline_layout_create_info.flags = 0;
  swap_pipeline_layout_create_info.setLayoutCount = 0;
  swap_pipeline_layout_create_info.pSetLayouts = nullptr;
  swap_pipeline_layout_create_info.pushConstantRangeCount = 0;
  swap_pipeline_layout_create_info.pPushConstantRanges = nullptr;
  if (dfn.vkCreatePipelineLayout(device, &swap_pipeline_layout_create_info,
                                 nullptr,
                                 &swap_pipeline_layout_) != VK_SUCCESS) {
    XELOGE("Failed to create the Vulkan pipeline layout for presentation");
    return false;
  }

  // Swap pipeline.

  VkPipelineShaderStageCreateInfo swap_pipeline_stages[2];
  swap_pipeline_stages[0].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  swap_pipeline_stages[0].pNext = nullptr;
  swap_pipeline_stages[0].flags = 0;
  swap_pipeline_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  swap_pipeline_stages[0].module = ui::vulkan::util::CreateShaderModule(
      provider, shaders::fullscreen_tc_vs, sizeof(shaders::fullscreen_tc_vs));
  if (swap_pipeline_stages[0].module == VK_NULL_HANDLE) {
    XELOGE("Failed to create the Vulkan vertex shader module for presentation");
    return false;
  }
  swap_pipeline_stages[0].pName = "main";
  swap_pipeline_stages[0].pSpecializationInfo = nullptr;
  swap_pipeline_stages[1].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  swap_pipeline_stages[1].pNext = nullptr;
  swap_pipeline_stages[1].flags = 0;
  swap_pipeline_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  swap_pipeline_stages[1].module = ui::vulkan::util::CreateShaderModule(
      provider, shaders::uv_ps, sizeof(shaders::uv_ps));
  if (swap_pipeline_stages[1].module == VK_NULL_HANDLE) {
    XELOGE(
        "Failed to create the Vulkan fragment shader module for presentation");
    dfn.vkDestroyShaderModule(device, swap_pipeline_stages[0].module, nullptr);
    return false;
  }
  swap_pipeline_stages[1].pName = "main";
  swap_pipeline_stages[1].pSpecializationInfo = nullptr;

  VkPipelineVertexInputStateCreateInfo swap_pipeline_vertex_input_state = {};
  swap_pipeline_vertex_input_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo swap_pipeline_input_assembly_state;
  swap_pipeline_input_assembly_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  swap_pipeline_input_assembly_state.pNext = nullptr;
  swap_pipeline_input_assembly_state.flags = 0;
  swap_pipeline_input_assembly_state.topology =
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  swap_pipeline_input_assembly_state.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo swap_pipeline_viewport_state;
  swap_pipeline_viewport_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  swap_pipeline_viewport_state.pNext = nullptr;
  swap_pipeline_viewport_state.flags = 0;
  swap_pipeline_viewport_state.viewportCount = 1;
  swap_pipeline_viewport_state.pViewports = nullptr;
  swap_pipeline_viewport_state.scissorCount = 1;
  swap_pipeline_viewport_state.pScissors = nullptr;

  VkPipelineRasterizationStateCreateInfo swap_pipeline_rasterization_state = {};
  swap_pipeline_rasterization_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  swap_pipeline_rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
  swap_pipeline_rasterization_state.cullMode = VK_CULL_MODE_NONE;
  swap_pipeline_rasterization_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  swap_pipeline_rasterization_state.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo swap_pipeline_multisample_state = {};
  swap_pipeline_multisample_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  swap_pipeline_multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState
      swap_pipeline_color_blend_attachment_state = {};
  swap_pipeline_color_blend_attachment_state.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo swap_pipeline_color_blend_state = {};
  swap_pipeline_color_blend_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  swap_pipeline_color_blend_state.attachmentCount = 1;
  swap_pipeline_color_blend_state.pAttachments =
      &swap_pipeline_color_blend_attachment_state;

  static const VkDynamicState kSwapPipelineDynamicStates[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  VkPipelineDynamicStateCreateInfo swap_pipeline_dynamic_state;
  swap_pipeline_dynamic_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  swap_pipeline_dynamic_state.pNext = nullptr;
  swap_pipeline_dynamic_state.flags = 0;
  swap_pipeline_dynamic_state.dynamicStateCount =
      uint32_t(xe::countof(kSwapPipelineDynamicStates));
  swap_pipeline_dynamic_state.pDynamicStates = kSwapPipelineDynamicStates;

  VkGraphicsPipelineCreateInfo swap_pipeline_create_info;
  swap_pipeline_create_info.sType =
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  swap_pipeline_create_info.pNext = nullptr;
  swap_pipeline_create_info.flags = 0;
  swap_pipeline_create_info.stageCount =
      uint32_t(xe::countof(swap_pipeline_stages));
  swap_pipeline_create_info.pStages = swap_pipeline_stages;
  swap_pipeline_create_info.pVertexInputState =
      &swap_pipeline_vertex_input_state;
  swap_pipeline_create_info.pInputAssemblyState =
      &swap_pipeline_input_assembly_state;
  swap_pipeline_create_info.pTessellationState = nullptr;
  swap_pipeline_create_info.pViewportState = &swap_pipeline_viewport_state;
  swap_pipeline_create_info.pRasterizationState =
      &swap_pipeline_rasterization_state;
  swap_pipeline_create_info.pMultisampleState =
      &swap_pipeline_multisample_state;
  swap_pipeline_create_info.pDepthStencilState = nullptr;
  swap_pipeline_create_info.pColorBlendState = &swap_pipeline_color_blend_state;
  swap_pipeline_create_info.pDynamicState = &swap_pipeline_dynamic_state;
  swap_pipeline_create_info.layout = swap_pipeline_layout_;
  swap_pipeline_create_info.renderPass = swap_render_pass_;
  swap_pipeline_create_info.subpass = 0;
  swap_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
  swap_pipeline_create_info.basePipelineIndex = -1;
  VkResult swap_pipeline_create_result = dfn.vkCreateGraphicsPipelines(
      device, VK_NULL_HANDLE, 1, &swap_pipeline_create_info, nullptr,
      &swap_pipeline_);
  for (size_t i = 0; i < xe::countof(swap_pipeline_stages); ++i) {
    dfn.vkDestroyShaderModule(device, swap_pipeline_stages[i].module, nullptr);
  }
  if (swap_pipeline_create_result != VK_SUCCESS) {
    XELOGE("Failed to create the Vulkan pipeline for presentation");
    return false;
  }

  // Just not to expose uninitialized memory.
  std::memset(&system_constants_, 0, sizeof(system_constants_));

  return true;
}

void VulkanCommandProcessor::ShutdownContext() {
  AwaitAllQueueOperationsCompletion();

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  for (const auto& framebuffer_pair : swap_framebuffers_outdated_) {
    dfn.vkDestroyFramebuffer(device, framebuffer_pair.second, nullptr);
  }
  swap_framebuffers_outdated_.clear();
  for (SwapFramebuffer& swap_framebuffer : swap_framebuffers_) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device,
                                           swap_framebuffer.framebuffer);
  }

  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                         swap_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         swap_pipeline_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyRenderPass, device,
                                         swap_render_pass_);

  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorPool, device,
      shared_memory_and_edram_descriptor_pool_);

  pipeline_cache_.reset();

  render_target_cache_.reset();

  primitive_processor_.reset();

  shared_memory_.reset();

  for (const auto& pipeline_layout_pair : pipeline_layouts_) {
    dfn.vkDestroyPipelineLayout(
        device, pipeline_layout_pair.second.GetPipelineLayout(), nullptr);
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
    dfn.vkDestroyCommandPool(device, command_buffer_pair.second.pool, nullptr);
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
    dfn.vkDestroySemaphore(device, semaphore.second, nullptr);
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

  device_lost_ = false;

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

void VulkanCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                       uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height) {
  // FIXME(Triang3l): frontbuffer_ptr is currently unreliable, in the trace
  // player it's set to 0, but it's not needed anyway since the fetch constant
  // contains the address.

  SCOPE_profile_cpu_f("gpu");

  ui::Presenter* presenter = graphics_system_->presenter();
  if (!presenter) {
    return;
  }

  // TODO(Triang3l): Resolution scale.
  uint32_t resolution_scale = 1;
  uint32_t scaled_width = frontbuffer_width * resolution_scale;
  uint32_t scaled_height = frontbuffer_height * resolution_scale;
  presenter->RefreshGuestOutput(
      scaled_width, scaled_height, 1280 * resolution_scale,
      720 * resolution_scale,
      [this, scaled_width, scaled_height](
          ui::Presenter::GuestOutputRefreshContext& context) -> bool {
        // In case the swap command is the only one in the frame.
        if (!BeginSubmission(true)) {
          return false;
        }

        auto& vulkan_context = static_cast<
            ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext&>(
            context);
        uint64_t guest_output_image_version = vulkan_context.image_version();

        const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
        const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
        VkDevice device = provider.device();

        // Make sure a framebuffer is available for the current guest output
        // image version.
        size_t swap_framebuffer_index = SIZE_MAX;
        size_t swap_framebuffer_new_index = SIZE_MAX;
        // Try to find the existing framebuffer for the current guest output
        // image version, or an unused (without an existing framebuffer, or with
        // one, but that has never actually been used dynamically) slot.
        for (size_t i = 0; i < swap_framebuffers_.size(); ++i) {
          const SwapFramebuffer& existing_swap_framebuffer =
              swap_framebuffers_[i];
          if (existing_swap_framebuffer.framebuffer != VK_NULL_HANDLE &&
              existing_swap_framebuffer.version == guest_output_image_version) {
            swap_framebuffer_index = i;
            break;
          }
          if (existing_swap_framebuffer.framebuffer == VK_NULL_HANDLE ||
              !existing_swap_framebuffer.last_submission) {
            swap_framebuffer_new_index = i;
          }
        }
        if (swap_framebuffer_index == SIZE_MAX) {
          if (swap_framebuffer_new_index == SIZE_MAX) {
            // Replace the earliest used framebuffer.
            swap_framebuffer_new_index = 0;
            for (size_t i = 1; i < swap_framebuffers_.size(); ++i) {
              if (swap_framebuffers_[i].last_submission <
                  swap_framebuffers_[swap_framebuffer_new_index]
                      .last_submission) {
                swap_framebuffer_new_index = i;
              }
            }
          }
          swap_framebuffer_index = swap_framebuffer_new_index;
          SwapFramebuffer& new_swap_framebuffer =
              swap_framebuffers_[swap_framebuffer_new_index];
          if (new_swap_framebuffer.framebuffer != VK_NULL_HANDLE) {
            if (submission_completed_ >= new_swap_framebuffer.last_submission) {
              dfn.vkDestroyFramebuffer(device, new_swap_framebuffer.framebuffer,
                                       nullptr);
            } else {
              swap_framebuffers_outdated_.emplace_back(
                  new_swap_framebuffer.last_submission,
                  new_swap_framebuffer.framebuffer);
            }
            new_swap_framebuffer.framebuffer = VK_NULL_HANDLE;
          }
          VkImageView guest_output_image_view_srgb =
              vulkan_context.image_view();
          VkFramebufferCreateInfo swap_framebuffer_create_info;
          swap_framebuffer_create_info.sType =
              VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
          swap_framebuffer_create_info.pNext = nullptr;
          swap_framebuffer_create_info.flags = 0;
          swap_framebuffer_create_info.renderPass = swap_render_pass_;
          swap_framebuffer_create_info.attachmentCount = 1;
          swap_framebuffer_create_info.pAttachments =
              &guest_output_image_view_srgb;
          swap_framebuffer_create_info.width = scaled_width;
          swap_framebuffer_create_info.height = scaled_height;
          swap_framebuffer_create_info.layers = 1;
          if (dfn.vkCreateFramebuffer(
                  device, &swap_framebuffer_create_info, nullptr,
                  &new_swap_framebuffer.framebuffer) != VK_SUCCESS) {
            XELOGE("Failed to create the Vulkan framebuffer for presentation");
            return false;
          }
          new_swap_framebuffer.version = guest_output_image_version;
          // The actual submission index will be set if the framebuffer is
          // actually used, not dropped due to some error.
          new_swap_framebuffer.last_submission = 0;
        }


        if (vulkan_context.image_ever_written_previously()) {
          // Insert a barrier after the last presenter's usage of the guest
          // output image. Will be overwriting all the contents, so oldLayout
          // layout is UNDEFINED. The render pass will do the layout transition,
          // but newLayout must not be UNDEFINED.
          PushImageMemoryBarrier(
              vulkan_context.image(),
              ui::vulkan::util::InitializeSubresourceRange(),
              ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }

        // End the current render pass before inserting barriers and starting a
        // new one, and insert the barrier.
        SubmitBarriers(true);

        SwapFramebuffer& swap_framebuffer =
            swap_framebuffers_[swap_framebuffer_index];
        swap_framebuffer.last_submission = GetCurrentSubmission();

        VkRenderPassBeginInfo render_pass_begin_info;
        render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.pNext = nullptr;
        render_pass_begin_info.renderPass = swap_render_pass_;
        render_pass_begin_info.framebuffer = swap_framebuffer.framebuffer;
        render_pass_begin_info.renderArea.offset.x = 0;
        render_pass_begin_info.renderArea.offset.y = 0;
        render_pass_begin_info.renderArea.extent.width = scaled_width;
        render_pass_begin_info.renderArea.extent.height = scaled_height;
        render_pass_begin_info.clearValueCount = 0;
        render_pass_begin_info.pClearValues = nullptr;
        deferred_command_buffer_.CmdVkBeginRenderPass(
            &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = float(scaled_width);
        viewport.height = float(scaled_height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        SetViewport(viewport);
        VkRect2D scissor;
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = scaled_width;
        scissor.extent.height = scaled_height;
        SetScissor(scissor);

        BindExternalGraphicsPipeline(swap_pipeline_);

        deferred_command_buffer_.CmdVkDraw(3, 1, 0, 0);

        deferred_command_buffer_.CmdVkEndRenderPass();

        // Insert the release barrier.
        PushImageMemoryBarrier(
            vulkan_context.image(),
            ui::vulkan::util::InitializeSubresourceRange(),
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout);

        // Need to submit all the commands before giving the image back to the
        // presenter so it can submit its own commands for displaying it to the
        // queue, and also need to submit the release barrier.
        EndSubmission(true);
        return true;
      });

  // End the frame even if did not present for any reason (the image refresher
  // was not called), to prevent leaking per-frame resources.
  EndSubmission(true);
}

bool VulkanCommandProcessor::PushBufferMemoryBarrier(
    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
    VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
    VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
    uint32_t src_queue_family_index, uint32_t dst_queue_family_index,
    bool skip_if_equal) {
  if (skip_if_equal && src_stage_mask == dst_stage_mask &&
      src_access_mask == dst_access_mask &&
      src_queue_family_index == dst_queue_family_index) {
    return false;
  }

  // Separate different barriers for overlapping buffer ranges into different
  // pipeline barrier commands.
  for (const VkBufferMemoryBarrier& other_buffer_memory_barrier :
       pending_barriers_buffer_memory_barriers_) {
    if (other_buffer_memory_barrier.buffer != buffer ||
        (size != VK_WHOLE_SIZE &&
         offset + size <= other_buffer_memory_barrier.offset) ||
        (other_buffer_memory_barrier.size != VK_WHOLE_SIZE &&
         other_buffer_memory_barrier.offset +
                 other_buffer_memory_barrier.size <=
             offset)) {
      continue;
    }
    if (other_buffer_memory_barrier.offset == offset &&
        other_buffer_memory_barrier.size == size &&
        other_buffer_memory_barrier.srcAccessMask == src_access_mask &&
        other_buffer_memory_barrier.dstAccessMask == dst_access_mask &&
        other_buffer_memory_barrier.srcQueueFamilyIndex ==
            src_queue_family_index &&
        other_buffer_memory_barrier.dstQueueFamilyIndex ==
            dst_queue_family_index) {
      // The barrier is already pending.
      current_pending_barrier_.src_stage_mask |= src_stage_mask;
      current_pending_barrier_.dst_stage_mask |= dst_stage_mask;
      return true;
    }
    SplitPendingBarrier();
    break;
  }

  current_pending_barrier_.src_stage_mask |= src_stage_mask;
  current_pending_barrier_.dst_stage_mask |= dst_stage_mask;
  VkBufferMemoryBarrier& buffer_memory_barrier =
      pending_barriers_buffer_memory_barriers_.emplace_back();
  buffer_memory_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  buffer_memory_barrier.pNext = nullptr;
  buffer_memory_barrier.srcAccessMask = src_access_mask;
  buffer_memory_barrier.dstAccessMask = dst_access_mask;
  buffer_memory_barrier.srcQueueFamilyIndex = src_queue_family_index;
  buffer_memory_barrier.dstQueueFamilyIndex = dst_queue_family_index;
  buffer_memory_barrier.buffer = buffer;
  buffer_memory_barrier.offset = offset;
  buffer_memory_barrier.size = size;
  return true;
}

bool VulkanCommandProcessor::PushImageMemoryBarrier(
    VkImage image, const VkImageSubresourceRange& subresource_range,
    VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
    VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
    VkImageLayout old_layout, VkImageLayout new_layout,
    uint32_t src_queue_family_index, uint32_t dst_queue_family_index,
    bool skip_if_equal) {
  if (skip_if_equal && src_stage_mask == dst_stage_mask &&
      src_access_mask == dst_access_mask && old_layout == new_layout &&
      src_queue_family_index == dst_queue_family_index) {
    return false;
  }

  // Separate different barriers for overlapping image subresource ranges into
  // different pipeline barrier commands.
  for (const VkImageMemoryBarrier& other_image_memory_barrier :
       pending_barriers_image_memory_barriers_) {
    if (other_image_memory_barrier.image != image ||
        !(other_image_memory_barrier.subresourceRange.aspectMask &
          subresource_range.aspectMask) ||
        (subresource_range.levelCount != VK_REMAINING_MIP_LEVELS &&
         subresource_range.baseMipLevel + subresource_range.levelCount <=
             other_image_memory_barrier.subresourceRange.baseMipLevel) ||
        (other_image_memory_barrier.subresourceRange.levelCount !=
             VK_REMAINING_MIP_LEVELS &&
         other_image_memory_barrier.subresourceRange.baseMipLevel +
                 other_image_memory_barrier.subresourceRange.levelCount <=
             subresource_range.baseMipLevel) ||
        (subresource_range.layerCount != VK_REMAINING_ARRAY_LAYERS &&
         subresource_range.baseArrayLayer + subresource_range.layerCount <=
             other_image_memory_barrier.subresourceRange.baseArrayLayer) ||
        (other_image_memory_barrier.subresourceRange.layerCount !=
             VK_REMAINING_ARRAY_LAYERS &&
         other_image_memory_barrier.subresourceRange.baseArrayLayer +
                 other_image_memory_barrier.subresourceRange.layerCount <=
             subresource_range.baseArrayLayer)) {
      continue;
    }
    if (other_image_memory_barrier.subresourceRange.aspectMask ==
            subresource_range.aspectMask &&
        other_image_memory_barrier.subresourceRange.baseMipLevel ==
            subresource_range.baseMipLevel &&
        other_image_memory_barrier.subresourceRange.levelCount ==
            subresource_range.levelCount &&
        other_image_memory_barrier.subresourceRange.baseArrayLayer ==
            subresource_range.baseArrayLayer &&
        other_image_memory_barrier.subresourceRange.layerCount ==
            subresource_range.layerCount &&
        other_image_memory_barrier.srcAccessMask == src_access_mask &&
        other_image_memory_barrier.dstAccessMask == dst_access_mask &&
        other_image_memory_barrier.oldLayout == old_layout &&
        other_image_memory_barrier.newLayout == new_layout &&
        other_image_memory_barrier.srcQueueFamilyIndex ==
            src_queue_family_index &&
        other_image_memory_barrier.dstQueueFamilyIndex ==
            dst_queue_family_index) {
      // The barrier is already pending.
      current_pending_barrier_.src_stage_mask |= src_stage_mask;
      current_pending_barrier_.dst_stage_mask |= dst_stage_mask;
      return true;
    }
    SplitPendingBarrier();
    break;
  }

  current_pending_barrier_.src_stage_mask |= src_stage_mask;
  current_pending_barrier_.dst_stage_mask |= dst_stage_mask;
  VkImageMemoryBarrier& image_memory_barrier =
      pending_barriers_image_memory_barriers_.emplace_back();
  image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_memory_barrier.pNext = nullptr;
  image_memory_barrier.srcAccessMask = src_access_mask;
  image_memory_barrier.dstAccessMask = dst_access_mask;
  image_memory_barrier.oldLayout = old_layout;
  image_memory_barrier.newLayout = new_layout;
  image_memory_barrier.srcQueueFamilyIndex = src_queue_family_index;
  image_memory_barrier.dstQueueFamilyIndex = dst_queue_family_index;
  image_memory_barrier.image = image;
  image_memory_barrier.subresourceRange = subresource_range;
  return true;
}

bool VulkanCommandProcessor::SubmitBarriers(bool force_end_render_pass) {
  assert_true(submission_open_);
  SplitPendingBarrier();
  if (pending_barriers_.empty()) {
    if (force_end_render_pass) {
      EndRenderPass();
    }
    return false;
  }
  EndRenderPass();
  for (auto it = pending_barriers_.cbegin(); it != pending_barriers_.cend();
       ++it) {
    auto it_next = std::next(it);
    bool is_last = it_next == pending_barriers_.cend();
    // .data() + offset, not &[offset], for buffer and image barriers, because
    // if there are no buffer or image memory barriers in the last pipeline
    // barriers, the offsets may be equal to the sizes of the vectors.
    deferred_command_buffer_.CmdVkPipelineBarrier(
        it->src_stage_mask ? it->src_stage_mask
                           : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        it->dst_stage_mask ? it->dst_stage_mask
                           : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr,
        uint32_t((is_last ? pending_barriers_buffer_memory_barriers_.size()
                          : it_next->buffer_memory_barriers_offset) -
                 it->buffer_memory_barriers_offset),
        pending_barriers_buffer_memory_barriers_.data() +
            it->buffer_memory_barriers_offset,
        uint32_t((is_last ? pending_barriers_image_memory_barriers_.size()
                          : it_next->image_memory_barriers_offset) -
                 it->image_memory_barriers_offset),
        pending_barriers_image_memory_barriers_.data() +
            it->image_memory_barriers_offset);
  }
  pending_barriers_.clear();
  pending_barriers_buffer_memory_barriers_.clear();
  pending_barriers_image_memory_barriers_.clear();
  current_pending_barrier_.buffer_memory_barriers_offset = 0;
  current_pending_barrier_.image_memory_barriers_offset = 0;
  return true;
}

void VulkanCommandProcessor::SubmitBarriersAndEnterRenderTargetCacheRenderPass(
    VkRenderPass render_pass,
    const VulkanRenderTargetCache::Framebuffer* framebuffer) {
  SubmitBarriers(false);
  if (current_render_pass_ == render_pass &&
      current_framebuffer_ == framebuffer) {
    return;
  }
  if (current_render_pass_ != VK_NULL_HANDLE) {
    deferred_command_buffer_.CmdVkEndRenderPass();
  }
  current_render_pass_ = render_pass;
  current_framebuffer_ = framebuffer;
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

void VulkanCommandProcessor::EndRenderPass() {
  assert_true(submission_open_);
  if (current_render_pass_ == VK_NULL_HANDLE) {
    return;
  }
  deferred_command_buffer_.CmdVkEndRenderPass();
  current_render_pass_ = VK_NULL_HANDLE;
  current_framebuffer_ = nullptr;
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

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
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
  auto emplaced_pair = pipeline_layouts_.emplace(
      std::piecewise_construct, std::forward_as_tuple(pipeline_layout_key.key),
      std::forward_as_tuple(pipeline_layout,
                            descriptor_set_layout_textures_vertex,
                            descriptor_set_layout_textures_pixel));
  // unordered_map insertion doesn't invalidate element references.
  return &emplaced_pair.first->second;
}

void VulkanCommandProcessor::BindExternalGraphicsPipeline(
    VkPipeline pipeline, bool keep_dynamic_depth_bias,
    bool keep_dynamic_blend_constants, bool keep_dynamic_stencil_mask_ref) {
  if (!keep_dynamic_depth_bias) {
    dynamic_depth_bias_update_needed_ = true;
  }
  if (!keep_dynamic_blend_constants) {
    dynamic_blend_constants_update_needed_ = true;
  }
  if (!keep_dynamic_stencil_mask_ref) {
    dynamic_stencil_compare_mask_front_update_needed_ = true;
    dynamic_stencil_compare_mask_back_update_needed_ = true;
    dynamic_stencil_write_mask_front_update_needed_ = true;
    dynamic_stencil_write_mask_back_update_needed_ = true;
    dynamic_stencil_reference_front_update_needed_ = true;
    dynamic_stencil_reference_back_update_needed_ = true;
  }
  if (current_external_graphics_pipeline_ == pipeline) {
    return;
  }
  deferred_command_buffer_.CmdVkBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             pipeline);
  current_external_graphics_pipeline_ = pipeline;
  current_guest_graphics_pipeline_ = VK_NULL_HANDLE;
  current_guest_graphics_pipeline_layout_ = VK_NULL_HANDLE;
}

void VulkanCommandProcessor::BindExternalComputePipeline(VkPipeline pipeline) {
  if (current_external_compute_pipeline_ == pipeline) {
    return;
  }
  deferred_command_buffer_.CmdVkBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,
                                             pipeline);
  current_external_compute_pipeline_ = pipeline;
}

void VulkanCommandProcessor::SetViewport(const VkViewport& viewport) {
  if (!dynamic_viewport_update_needed_) {
    dynamic_viewport_update_needed_ |= dynamic_viewport_.x != viewport.x;
    dynamic_viewport_update_needed_ |= dynamic_viewport_.y != viewport.y;
    dynamic_viewport_update_needed_ |=
        dynamic_viewport_.width != viewport.width;
    dynamic_viewport_update_needed_ |=
        dynamic_viewport_.height != viewport.height;
    dynamic_viewport_update_needed_ |=
        dynamic_viewport_.minDepth != viewport.minDepth;
    dynamic_viewport_update_needed_ |=
        dynamic_viewport_.maxDepth != viewport.maxDepth;
  }
  if (dynamic_viewport_update_needed_) {
    dynamic_viewport_ = viewport;
    deferred_command_buffer_.CmdVkSetViewport(0, 1, &dynamic_viewport_);
    dynamic_viewport_update_needed_ = false;
  }
}

void VulkanCommandProcessor::SetScissor(const VkRect2D& scissor) {
  if (!dynamic_scissor_update_needed_) {
    dynamic_scissor_update_needed_ |=
        dynamic_scissor_.offset.x != scissor.offset.x;
    dynamic_scissor_update_needed_ |=
        dynamic_scissor_.offset.y != scissor.offset.y;
    dynamic_scissor_update_needed_ |=
        dynamic_scissor_.extent.width != scissor.extent.width;
    dynamic_scissor_update_needed_ |=
        dynamic_scissor_.extent.height != scissor.extent.height;
  }
  if (dynamic_scissor_update_needed_) {
    dynamic_scissor_ = scissor;
    deferred_command_buffer_.CmdVkSetScissor(0, 1, &dynamic_scissor_);
    dynamic_scissor_update_needed_ = false;
  }
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

  if (!BeginSubmission(true)) {
    return false;
  }

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

  reg::RB_DEPTHCONTROL normalized_depth_control =
      draw_util::GetNormalizedDepthControl(regs);
  uint32_t normalized_color_mask =
      pixel_shader ? draw_util::GetNormalizedColorMask(
                         regs, pixel_shader->writes_color_targets())
                   : 0;

  // Shader modifications.
  SpirvShaderTranslator::Modification vertex_shader_modification =
      pipeline_cache_->GetCurrentVertexShaderModification(
          *vertex_shader, primitive_processing_result.host_vertex_shader_type);
  SpirvShaderTranslator::Modification pixel_shader_modification =
      pixel_shader ? pipeline_cache_->GetCurrentPixelShaderModification(
                         *pixel_shader, normalized_color_mask)
                   : SpirvShaderTranslator::Modification(0);

  // Set up the render targets - this may perform dispatches and draws.
  if (!render_target_cache_->Update(is_rasterization_done,
                                    normalized_depth_control,
                                    normalized_color_mask, *vertex_shader)) {
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
  // current_guest_graphics_pipeline_layout_.
  VkPipeline pipeline;
  const VulkanPipelineCache::PipelineLayoutProvider* pipeline_layout_provider;
  if (!pipeline_cache_->ConfigurePipeline(
          vertex_shader_translation, pixel_shader_translation,
          primitive_processing_result, normalized_depth_control,
          normalized_color_mask,
          render_target_cache_->last_update_render_pass_key(), pipeline,
          pipeline_layout_provider)) {
    return false;
  }
  if (current_guest_graphics_pipeline_ != pipeline) {
    deferred_command_buffer_.CmdVkBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                               pipeline);
    current_guest_graphics_pipeline_ = pipeline;
    current_external_graphics_pipeline_ = VK_NULL_HANDLE;
  }
  auto pipeline_layout =
      static_cast<const PipelineLayout*>(pipeline_layout_provider);
  if (current_guest_graphics_pipeline_layout_ != pipeline_layout) {
    if (current_guest_graphics_pipeline_layout_) {
      // Keep descriptor set layouts for which the new pipeline layout is
      // compatible with the previous one (pipeline layouts are compatible for
      // set N if set layouts 0 through N are compatible).
      uint32_t descriptor_sets_kept =
          uint32_t(SpirvShaderTranslator::kDescriptorSetCount);
      if (current_guest_graphics_pipeline_layout_
              ->descriptor_set_layout_textures_vertex_ref() !=
          pipeline_layout->descriptor_set_layout_textures_vertex_ref()) {
        descriptor_sets_kept = std::min(
            descriptor_sets_kept,
            uint32_t(SpirvShaderTranslator::kDescriptorSetTexturesVertex));
      }
      if (current_guest_graphics_pipeline_layout_
              ->descriptor_set_layout_textures_pixel_ref() !=
          pipeline_layout->descriptor_set_layout_textures_pixel_ref()) {
        descriptor_sets_kept = std::min(
            descriptor_sets_kept,
            uint32_t(SpirvShaderTranslator::kDescriptorSetTexturesPixel));
      }
    } else {
      // No or unknown pipeline layout previously bound - all bindings are in an
      // indeterminate state.
      current_graphics_descriptor_sets_bound_up_to_date_ = 0;
    }
    current_guest_graphics_pipeline_layout_ = pipeline_layout;
  }

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const VkPhysicalDeviceLimits& device_limits =
      provider.device_properties().limits;

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
      regs, 1, 1, false, device_limits.maxViewportDimensions[0],
      device_limits.maxViewportDimensions[1], true, normalized_depth_control,
      false, false, false, viewport_info);

  // Update dynamic graphics pipeline state.
  UpdateDynamicState(viewport_info, primitive_polygonal,
                     normalized_depth_control);

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

  // After all commands that may dispatch, copy or insert barriers, submit the
  // barriers (may end the render pass), and (re)enter the render pass before
  // drawing.
  SubmitBarriersAndEnterRenderTargetCacheRenderPass(
      render_target_cache_->last_update_render_pass(),
      render_target_cache_->last_update_framebuffer());

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

  if (!BeginSubmission(true)) {
    return false;
  }

  return true;
}

void VulkanCommandProcessor::InitializeTrace() {
  CommandProcessor::InitializeTrace();

  if (!BeginSubmission(true)) {
    return;
  }
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

void VulkanCommandProcessor::CheckSubmissionFenceAndDeviceLoss(
    uint64_t await_submission) {
  // Only report once, no need to retry a wait that won't succeed anyway.
  if (device_lost_) {
    return;
  }

  if (await_submission >= GetCurrentSubmission()) {
    if (submission_open_) {
      EndSubmission(false);
    }
    // A submission won't be ended if it hasn't been started, or if ending
    // has failed - clamp the index.
    await_submission = GetCurrentSubmission() - 1;
  }

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  size_t fences_total = submissions_in_flight_fences_.size();
  size_t fences_awaited = 0;
  if (await_submission > submission_completed_) {
    // Await in a blocking way if requested.
    // TODO(Triang3l): Await only one fence. "Fence signal operations that are
    // defined by vkQueueSubmit additionally include in the first
    // synchronization scope all commands that occur earlier in submission
    // order."
    VkResult wait_result = dfn.vkWaitForFences(
        device, uint32_t(await_submission - submission_completed_),
        submissions_in_flight_fences_.data(), VK_TRUE, UINT64_MAX);
    if (wait_result == VK_SUCCESS) {
      fences_awaited += await_submission - submission_completed_;
    } else {
      XELOGE("Failed to await submission completion Vulkan fences");
      if (wait_result == VK_ERROR_DEVICE_LOST) {
        device_lost_ = true;
      }
    }
  }
  // Check how far into the submissions the GPU currently is, in order because
  // submission themselves can be executed out of order, but Xenia serializes
  // that for simplicity.
  while (fences_awaited < fences_total) {
    VkResult fence_status = dfn.vkWaitForFences(
        device, 1, &submissions_in_flight_fences_[fences_awaited], VK_TRUE, 0);
    if (fence_status != VK_SUCCESS) {
      if (fence_status == VK_ERROR_DEVICE_LOST) {
        device_lost_ = true;
      }
      break;
    }
    ++fences_awaited;
  }
  if (device_lost_) {
    graphics_system_->OnHostGpuLossFromAnyThread(true);
    return;
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
    if (semaphore_submission.first > submission_completed_) {
      break;
    }
    semaphores_free_.push_back(semaphore_submission.second);
    submissions_in_flight_semaphores_.pop_front();
  }

  // Reclaim command pools.
  while (!command_buffers_submitted_.empty()) {
    const auto& command_buffer_pair = command_buffers_submitted_.front();
    if (command_buffer_pair.first > submission_completed_) {
      break;
    }
    command_buffers_writable_.push_back(command_buffer_pair.second);
    command_buffers_submitted_.pop_front();
  }

  shared_memory_->CompletedSubmissionUpdated();

  primitive_processor_->CompletedSubmissionUpdated();

  render_target_cache_->CompletedSubmissionUpdated();

  // Destroy outdated swap objects.
  while (!swap_framebuffers_outdated_.empty()) {
    const auto& framebuffer_pair = swap_framebuffers_outdated_.front();
    if (framebuffer_pair.first > submission_completed_) {
      break;
    }
    dfn.vkDestroyFramebuffer(device, framebuffer_pair.second, nullptr);
    swap_framebuffers_outdated_.pop_front();
  }
}

bool VulkanCommandProcessor::BeginSubmission(bool is_guest_command) {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  if (device_lost_) {
    return false;
  }

  bool is_opening_frame = is_guest_command && !frame_open_;
  if (submission_open_ && !is_opening_frame) {
    return true;
  }

  // Check the fence - needed for all kinds of submissions (to reclaim transient
  // resources early) and specifically for frames (not to queue too many), and
  // await the availability of the current frame. Also check whether the device
  // is still available, and whether the await was successful.
  uint64_t await_submission =
      is_opening_frame
          ? closed_frame_submissions_[frame_current_ % kMaxFramesInFlight]
          : 0;
  CheckSubmissionFenceAndDeviceLoss(await_submission);
  if (device_lost_ || submission_completed_ < await_submission) {
    return false;
  }

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
    dynamic_viewport_update_needed_ = true;
    dynamic_scissor_update_needed_ = true;
    dynamic_depth_bias_update_needed_ = true;
    dynamic_blend_constants_update_needed_ = true;
    dynamic_stencil_compare_mask_front_update_needed_ = true;
    dynamic_stencil_compare_mask_back_update_needed_ = true;
    dynamic_stencil_write_mask_front_update_needed_ = true;
    dynamic_stencil_write_mask_back_update_needed_ = true;
    dynamic_stencil_reference_front_update_needed_ = true;
    dynamic_stencil_reference_back_update_needed_ = true;
    current_render_pass_ = VK_NULL_HANDLE;
    current_framebuffer_ = nullptr;
    current_guest_graphics_pipeline_ = VK_NULL_HANDLE;
    current_external_graphics_pipeline_ = VK_NULL_HANDLE;
    current_external_compute_pipeline_ = VK_NULL_HANDLE;
    current_guest_graphics_pipeline_layout_ = nullptr;
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

  return true;
}

bool VulkanCommandProcessor::EndSubmission(bool is_swap) {
  ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
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

    render_target_cache_->EndSubmission();

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
      VkResult bind_sparse_result;
      {
        ui::vulkan::VulkanProvider::QueueAcquisition queue_acquisition(
            provider.AcquireQueue(provider.queue_family_sparse_binding(), 0));
        bind_sparse_result = dfn.vkQueueBindSparse(
            queue_acquisition.queue, 1, &bind_sparse_info, VK_NULL_HANDLE);
      }
      if (bind_sparse_result != VK_SUCCESS) {
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

    SubmitBarriers(true);

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
    VkResult submit_result;
    {
      ui::vulkan::VulkanProvider::QueueAcquisition queue_acquisition(
          provider.AcquireQueue(provider.queue_family_graphics_compute(), 0));
      submit_result =
          dfn.vkQueueSubmit(queue_acquisition.queue, 1, &submit_info, fence);
    }
    if (submit_result != VK_SUCCESS) {
      XELOGE("Failed to submit a Vulkan command buffer");
      if (submit_result == VK_ERROR_DEVICE_LOST && !device_lost_) {
        device_lost_ = true;
        graphics_system_->OnHostGpuLossFromAnyThread(true);
      }
      return false;
    }
    uint64_t submission_current = GetCurrentSubmission();
    current_submission_wait_stage_masks_.clear();
    for (VkSemaphore semaphore : current_submission_wait_semaphores_) {
      submissions_in_flight_semaphores_.emplace_back(submission_current,
                                                     semaphore);
    }
    current_submission_wait_semaphores_.clear();
    command_buffers_submitted_.emplace_back(submission_current, command_buffer);
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
            device, pipeline_layout_pair.second.GetPipelineLayout(), nullptr);
      }
      pipeline_layouts_.clear();
      for (const auto& descriptor_set_layout_pair :
           descriptor_set_layouts_textures_) {
        dfn.vkDestroyDescriptorSetLayout(
            device, descriptor_set_layout_pair.second, nullptr);
      }
      descriptor_set_layouts_textures_.clear();

      primitive_processor_->ClearCache();

      for (SwapFramebuffer& swap_framebuffer : swap_framebuffers_) {
        ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device,
                                               swap_framebuffer.framebuffer);
      }
    }
  }

  return true;
}

void VulkanCommandProcessor::SplitPendingBarrier() {
  size_t pending_buffer_memory_barrier_count =
      pending_barriers_buffer_memory_barriers_.size();
  size_t pending_image_memory_barrier_count =
      pending_barriers_image_memory_barriers_.size();
  if (!current_pending_barrier_.src_stage_mask &&
      !current_pending_barrier_.dst_stage_mask &&
      current_pending_barrier_.buffer_memory_barriers_offset >=
          pending_buffer_memory_barrier_count &&
      current_pending_barrier_.image_memory_barriers_offset >=
          pending_image_memory_barrier_count) {
    return;
  }
  pending_barriers_.emplace_back(current_pending_barrier_);
  current_pending_barrier_.src_stage_mask = 0;
  current_pending_barrier_.dst_stage_mask = 0;
  current_pending_barrier_.buffer_memory_barriers_offset =
      pending_buffer_memory_barrier_count;
  current_pending_barrier_.image_memory_barriers_offset =
      pending_image_memory_barrier_count;
}

VkShaderStageFlags VulkanCommandProcessor::GetGuestVertexShaderStageFlags()
    const {
  VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT;
  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  if (provider.device_features().tessellationShader) {
    stages |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
  }
  // TODO(Triang3l): Vertex to compute translation for rectangle and possibly
  // point emulation.
  return stages;
}

void VulkanCommandProcessor::UpdateDynamicState(
    const draw_util::ViewportInfo& viewport_info, bool primitive_polygonal,
    reg::RB_DEPTHCONTROL normalized_depth_control) {
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
  SetViewport(viewport);

  // Scissor.
  draw_util::Scissor scissor;
  draw_util::GetScissor(regs, scissor);
  VkRect2D scissor_rect;
  scissor_rect.offset.x = int32_t(scissor.offset[0]);
  scissor_rect.offset.y = int32_t(scissor.offset[1]);
  scissor_rect.extent.width = scissor.extent[0];
  scissor_rect.extent.height = scissor.extent[1];
  SetScissor(scissor_rect);

  // Depth bias.
  // TODO(Triang3l): Disable the depth bias for the fragment shader interlock RB
  // implementation.
  float depth_bias_constant_factor, depth_bias_slope_factor;
  draw_util::GetPreferredFacePolygonOffset(regs, primitive_polygonal,
                                           depth_bias_slope_factor,
                                           depth_bias_constant_factor);
  depth_bias_constant_factor *= draw_util::GetD3D10PolygonOffsetFactor(
      regs.Get<reg::RB_DEPTH_INFO>().depth_format, true);
  // With non-square resolution scaling, make sure the worst-case impact is
  // reverted (slope only along the scaled axis), thus max. More bias is better
  // than less bias, because less bias means Z fighting with the background is
  // more likely.
  depth_bias_slope_factor *=
      xenos::kPolygonOffsetScaleSubpixelUnit *
      float(std::max(render_target_cache_->draw_resolution_scale_x(),
                     render_target_cache_->draw_resolution_scale_y()));
  // std::memcmp instead of != so in case of NaN, every draw won't be
  // invalidating it.
  dynamic_depth_bias_update_needed_ |=
      std::memcmp(&dynamic_depth_bias_constant_factor_,
                  &depth_bias_constant_factor, sizeof(float)) != 0;
  dynamic_depth_bias_update_needed_ |=
      std::memcmp(&dynamic_depth_bias_slope_factor_, &depth_bias_slope_factor,
                  sizeof(float)) != 0;
  if (dynamic_depth_bias_update_needed_) {
    dynamic_depth_bias_constant_factor_ = depth_bias_constant_factor;
    dynamic_depth_bias_slope_factor_ = depth_bias_slope_factor;
    deferred_command_buffer_.CmdVkSetDepthBias(
        dynamic_depth_bias_constant_factor_, 0.0f,
        dynamic_depth_bias_slope_factor_);
    dynamic_depth_bias_update_needed_ = false;
  }

  // Blend constants.
  float blend_constants[] = {
      regs[XE_GPU_REG_RB_BLEND_RED].f32,
      regs[XE_GPU_REG_RB_BLEND_GREEN].f32,
      regs[XE_GPU_REG_RB_BLEND_BLUE].f32,
      regs[XE_GPU_REG_RB_BLEND_ALPHA].f32,
  };
  dynamic_blend_constants_update_needed_ |=
      std::memcmp(dynamic_blend_constants_, blend_constants,
                  sizeof(float) * 4) != 0;
  if (dynamic_blend_constants_update_needed_) {
    std::memcpy(dynamic_blend_constants_, blend_constants, sizeof(float) * 4);
    deferred_command_buffer_.CmdVkSetBlendConstants(dynamic_blend_constants_);
    dynamic_blend_constants_update_needed_ = false;
  }

  // Stencil masks and references.
  // Due to pretty complex conditions involving registers not directly related
  // to stencil (primitive type, culling), changing the values only when stencil
  // is actually needed. However, due to the way dynamic state needs to be set
  // in Vulkan, which doesn't take into account whether the state actually has
  // effect on drawing, and because the masks and the references are always
  // dynamic in Xenia guest pipelines, they must be set in the command buffer
  // before any draw.
  if (normalized_depth_control.stencil_enable) {
    Register stencil_ref_mask_front_reg, stencil_ref_mask_back_reg;
    if (primitive_polygonal && normalized_depth_control.backface_enable) {
      const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
      const VkPhysicalDevicePortabilitySubsetFeaturesKHR*
          device_portability_subset_features =
              provider.device_portability_subset_features();
      if (!device_portability_subset_features ||
          device_portability_subset_features->separateStencilMaskRef) {
        // Choose the back face values only if drawing only back faces.
        stencil_ref_mask_front_reg =
            regs.Get<reg::PA_SU_SC_MODE_CNTL>().cull_front
                ? XE_GPU_REG_RB_STENCILREFMASK_BF
                : XE_GPU_REG_RB_STENCILREFMASK;
        stencil_ref_mask_back_reg = stencil_ref_mask_front_reg;
      } else {
        stencil_ref_mask_front_reg = XE_GPU_REG_RB_STENCILREFMASK;
        stencil_ref_mask_back_reg = XE_GPU_REG_RB_STENCILREFMASK_BF;
      }
    } else {
      stencil_ref_mask_front_reg = XE_GPU_REG_RB_STENCILREFMASK;
      stencil_ref_mask_back_reg = XE_GPU_REG_RB_STENCILREFMASK;
    }
    auto stencil_ref_mask_front =
        regs.Get<reg::RB_STENCILREFMASK>(stencil_ref_mask_front_reg);
    auto stencil_ref_mask_back =
        regs.Get<reg::RB_STENCILREFMASK>(stencil_ref_mask_back_reg);
    // Compare mask.
    dynamic_stencil_compare_mask_front_update_needed_ |=
        dynamic_stencil_compare_mask_front_ !=
        stencil_ref_mask_front.stencilmask;
    dynamic_stencil_compare_mask_front_ = stencil_ref_mask_front.stencilmask;
    dynamic_stencil_compare_mask_back_update_needed_ |=
        dynamic_stencil_compare_mask_back_ != stencil_ref_mask_back.stencilmask;
    dynamic_stencil_compare_mask_back_ = stencil_ref_mask_back.stencilmask;
    // Write mask.
    dynamic_stencil_write_mask_front_update_needed_ |=
        dynamic_stencil_write_mask_front_ !=
        stencil_ref_mask_front.stencilwritemask;
    dynamic_stencil_write_mask_front_ = stencil_ref_mask_front.stencilwritemask;
    dynamic_stencil_write_mask_back_update_needed_ |=
        dynamic_stencil_write_mask_back_ !=
        stencil_ref_mask_back.stencilwritemask;
    dynamic_stencil_write_mask_back_ = stencil_ref_mask_back.stencilwritemask;
    // Reference.
    dynamic_stencil_reference_front_update_needed_ |=
        dynamic_stencil_reference_front_ != stencil_ref_mask_front.stencilref;
    dynamic_stencil_reference_front_ = stencil_ref_mask_front.stencilref;
    dynamic_stencil_reference_back_update_needed_ |=
        dynamic_stencil_reference_back_ != stencil_ref_mask_back.stencilref;
    dynamic_stencil_reference_back_ = stencil_ref_mask_back.stencilref;
  }
  // Using VK_STENCIL_FACE_FRONT_AND_BACK for higher safety when running on the
  // Vulkan portability subset without separateStencilMaskRef.
  if (dynamic_stencil_compare_mask_front_update_needed_ ||
      dynamic_stencil_compare_mask_back_update_needed_) {
    if (dynamic_stencil_compare_mask_front_ ==
        dynamic_stencil_compare_mask_back_) {
      deferred_command_buffer_.CmdVkSetStencilCompareMask(
          VK_STENCIL_FACE_FRONT_AND_BACK, dynamic_stencil_compare_mask_front_);
    } else {
      if (dynamic_stencil_compare_mask_front_update_needed_) {
        deferred_command_buffer_.CmdVkSetStencilCompareMask(
            VK_STENCIL_FACE_FRONT_BIT, dynamic_stencil_compare_mask_front_);
      }
      if (dynamic_stencil_compare_mask_back_update_needed_) {
        deferred_command_buffer_.CmdVkSetStencilCompareMask(
            VK_STENCIL_FACE_BACK_BIT, dynamic_stencil_compare_mask_back_);
      }
    }
    dynamic_stencil_compare_mask_front_update_needed_ = false;
    dynamic_stencil_compare_mask_back_update_needed_ = false;
  }
  if (dynamic_stencil_write_mask_front_update_needed_ ||
      dynamic_stencil_write_mask_back_update_needed_) {
    if (dynamic_stencil_write_mask_front_ == dynamic_stencil_write_mask_back_) {
      deferred_command_buffer_.CmdVkSetStencilWriteMask(
          VK_STENCIL_FACE_FRONT_AND_BACK, dynamic_stencil_write_mask_front_);
    } else {
      if (dynamic_stencil_write_mask_front_update_needed_) {
        deferred_command_buffer_.CmdVkSetStencilWriteMask(
            VK_STENCIL_FACE_FRONT_BIT, dynamic_stencil_write_mask_front_);
      }
      if (dynamic_stencil_write_mask_back_update_needed_) {
        deferred_command_buffer_.CmdVkSetStencilWriteMask(
            VK_STENCIL_FACE_BACK_BIT, dynamic_stencil_write_mask_back_);
      }
    }
    dynamic_stencil_write_mask_front_update_needed_ = false;
    dynamic_stencil_write_mask_back_update_needed_ = false;
  }
  if (dynamic_stencil_reference_front_update_needed_ ||
      dynamic_stencil_reference_back_update_needed_) {
    if (dynamic_stencil_reference_front_ == dynamic_stencil_reference_back_) {
      deferred_command_buffer_.CmdVkSetStencilReference(
          VK_STENCIL_FACE_FRONT_AND_BACK, dynamic_stencil_reference_front_);
    } else {
      if (dynamic_stencil_reference_front_update_needed_) {
        deferred_command_buffer_.CmdVkSetStencilReference(
            VK_STENCIL_FACE_FRONT_BIT, dynamic_stencil_reference_front_);
      }
      if (dynamic_stencil_reference_back_update_needed_) {
        deferred_command_buffer_.CmdVkSetStencilReference(
            VK_STENCIL_FACE_BACK_BIT, dynamic_stencil_reference_back_);
      }
    }
    dynamic_stencil_reference_front_update_needed_ = false;
    dynamic_stencil_reference_back_update_needed_ = false;
  }

  // TODO(Triang3l): VK_EXT_extended_dynamic_state and
  // VK_EXT_extended_dynamic_state2.
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
    const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
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
  if (current_guest_graphics_pipeline_layout_
          ->descriptor_set_layout_textures_vertex_ref() ==
      descriptor_set_layout_empty_) {
    descriptor_sets_needed &=
        ~(uint32_t(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex);
  }
  if (current_guest_graphics_pipeline_layout_
          ->descriptor_set_layout_textures_pixel_ref() ==
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
        current_guest_graphics_pipeline_layout_->GetPipelineLayout(),
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
  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
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

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/ui/graphics_context.h"
#include "xenia/ui/vulkan/vulkan_context.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_swap_chain.h"

namespace xe {
namespace ui {
namespace vulkan {

// Generated with `xenia-build genspirv`.
#include "xenia/ui/vulkan/shaders/bin/immediate_frag.h"
#include "xenia/ui/vulkan/shaders/bin/immediate_vert.h"

constexpr uint32_t kCircularBufferCapacity = 2 * 1024 * 1024;

class LightweightCircularBuffer {
 public:
  LightweightCircularBuffer(VulkanDevice* device) : device_(*device) {
    buffer_capacity_ = xe::round_up(kCircularBufferCapacity, 4096);

    // Index buffer.
    VkBufferCreateInfo index_buffer_info;
    index_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    index_buffer_info.pNext = nullptr;
    index_buffer_info.flags = 0;
    index_buffer_info.size = buffer_capacity_;
    index_buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    index_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    index_buffer_info.queueFamilyIndexCount = 0;
    index_buffer_info.pQueueFamilyIndices = nullptr;
    auto status =
        vkCreateBuffer(device_, &index_buffer_info, nullptr, &index_buffer_);
    CheckResult(status, "vkCreateBuffer");

    // Vertex buffer.
    VkBufferCreateInfo vertex_buffer_info;
    vertex_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertex_buffer_info.pNext = nullptr;
    vertex_buffer_info.flags = 0;
    vertex_buffer_info.size = buffer_capacity_;
    vertex_buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertex_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vertex_buffer_info.queueFamilyIndexCount = 0;
    vertex_buffer_info.pQueueFamilyIndices = nullptr;
    status =
        vkCreateBuffer(*device, &vertex_buffer_info, nullptr, &vertex_buffer_);
    CheckResult(status, "vkCreateBuffer");

    // Allocate underlying buffer.
    // We alias it for both vertices and indices.
    VkMemoryRequirements buffer_requirements;
    vkGetBufferMemoryRequirements(device_, index_buffer_, &buffer_requirements);
    buffer_memory_ = device->AllocateMemory(
        buffer_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    vkBindBufferMemory(*device, index_buffer_, buffer_memory_, 0);
    vkBindBufferMemory(*device, vertex_buffer_, buffer_memory_, 0);

    // Persistent mapping.
    status = vkMapMemory(device_, buffer_memory_, 0, VK_WHOLE_SIZE, 0,
                         &buffer_data_);
    CheckResult(status, "vkMapMemory");
  }

  ~LightweightCircularBuffer() {
    if (buffer_memory_) {
      vkUnmapMemory(device_, buffer_memory_);
      buffer_memory_ = nullptr;
    }

    VK_SAFE_DESTROY(vkDestroyBuffer, device_, index_buffer_, nullptr);
    VK_SAFE_DESTROY(vkDestroyBuffer, device_, vertex_buffer_, nullptr);
    VK_SAFE_DESTROY(vkFreeMemory, device_, buffer_memory_, nullptr);
  }

  VkBuffer vertex_buffer() const { return vertex_buffer_; }
  VkBuffer index_buffer() const { return index_buffer_; }

  // Allocates space for data and copies it into the buffer.
  // Returns the offset in the buffer of the data or VK_WHOLE_SIZE if the buffer
  // is full.
  VkDeviceSize Emplace(const void* source_data, size_t source_length) {
    // TODO(benvanik): query actual alignment.
    source_length = xe::round_up(source_length, 256);

    // Run down old fences to free up space.

    // Check to see if we have space.
    // return VK_WHOLE_SIZE;

    // Compute new range and mark as in use.
    if (current_offset_ + source_length > buffer_capacity_) {
      // Wraps around.
      current_offset_ = 0;
    }
    VkDeviceSize offset = current_offset_;
    current_offset_ += source_length;

    // Copy data.
    auto dest_ptr = reinterpret_cast<uint8_t*>(buffer_data_) + offset;
    std::memcpy(dest_ptr, source_data, source_length);

    // Insert fence.
    // TODO(benvanik): coarse-grained fences, these may be too fine.

    // Flush memory.
    // TODO(benvanik): do only in large batches? can barrier it.
    VkMappedMemoryRange dirty_range;
    dirty_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    dirty_range.pNext = nullptr;
    dirty_range.memory = buffer_memory_;
    dirty_range.offset = offset;
    dirty_range.size = source_length;
    vkFlushMappedMemoryRanges(device_, 1, &dirty_range);
    return offset;
  }

 private:
  VkDevice device_ = nullptr;

  VkBuffer index_buffer_ = nullptr;
  VkBuffer vertex_buffer_ = nullptr;
  VkDeviceMemory buffer_memory_ = nullptr;
  void* buffer_data_ = nullptr;
  size_t buffer_capacity_ = 0;
  size_t current_offset_ = 0;
};

class VulkanImmediateTexture : public ImmediateTexture {
 public:
  VulkanImmediateTexture(VulkanDevice* device, VkDescriptorPool descriptor_pool,
                         VkSampler sampler, uint32_t width, uint32_t height)
      : ImmediateTexture(width, height),
        device_(device),
        descriptor_pool_(descriptor_pool),
        sampler_(sampler) {}

  ~VulkanImmediateTexture() override { Shutdown(); }

  VkResult Initialize(VkDescriptorSetLayout descriptor_set_layout,
                      VkImageView image_view) {
    handle = reinterpret_cast<uintptr_t>(this);
    image_view_ = image_view;
    VkResult status;

    // Create descriptor set used just for this texture.
    // It never changes, so we can reuse it and not worry with updates.
    VkDescriptorSetAllocateInfo set_alloc_info;
    set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_alloc_info.pNext = nullptr;
    set_alloc_info.descriptorPool = descriptor_pool_;
    set_alloc_info.descriptorSetCount = 1;
    set_alloc_info.pSetLayouts = &descriptor_set_layout;
    status =
        vkAllocateDescriptorSets(*device_, &set_alloc_info, &descriptor_set_);
    CheckResult(status, "vkAllocateDescriptorSets");
    if (status != VK_SUCCESS) {
      return status;
    }

    // Initialize descriptor with our texture.
    VkDescriptorImageInfo texture_info;
    texture_info.sampler = sampler_;
    texture_info.imageView = image_view_;
    texture_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet descriptor_write;
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.pNext = nullptr;
    descriptor_write.dstSet = descriptor_set_;
    descriptor_write.dstBinding = 0;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorCount = 1;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_write.pImageInfo = &texture_info;
    vkUpdateDescriptorSets(*device_, 1, &descriptor_write, 0, nullptr);

    return VK_SUCCESS;
  }

  VkResult Initialize(VkDescriptorSetLayout descriptor_set_layout) {
    handle = reinterpret_cast<uintptr_t>(this);
    VkResult status;

    // Create image object.
    VkImageCreateInfo image_info;
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = nullptr;
    image_info.flags = 0;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.queueFamilyIndexCount = 0;
    image_info.pQueueFamilyIndices = nullptr;
    image_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    status = vkCreateImage(*device_, &image_info, nullptr, &image_);
    CheckResult(status, "vkCreateImage");
    if (status != VK_SUCCESS) {
      return status;
    }

    // Allocate memory for the image.
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(*device_, image_, &memory_requirements);
    device_memory_ = device_->AllocateMemory(
        memory_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (!device_memory_) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Bind memory and the image together.
    status = vkBindImageMemory(*device_, image_, device_memory_, 0);
    CheckResult(status, "vkBindImageMemory");
    if (status != VK_SUCCESS) {
      return status;
    }

    // Create image view used by the shader.
    VkImageViewCreateInfo view_info;
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.pNext = nullptr;
    view_info.flags = 0;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.components = {
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A,
    };
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    status = vkCreateImageView(*device_, &view_info, nullptr, &image_view_);
    CheckResult(status, "vkCreateImageView");
    if (status != VK_SUCCESS) {
      return status;
    }

    // Create descriptor set used just for this texture.
    // It never changes, so we can reuse it and not worry with updates.
    VkDescriptorSetAllocateInfo set_alloc_info;
    set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_alloc_info.pNext = nullptr;
    set_alloc_info.descriptorPool = descriptor_pool_;
    set_alloc_info.descriptorSetCount = 1;
    set_alloc_info.pSetLayouts = &descriptor_set_layout;
    status =
        vkAllocateDescriptorSets(*device_, &set_alloc_info, &descriptor_set_);
    CheckResult(status, "vkAllocateDescriptorSets");
    if (status != VK_SUCCESS) {
      return status;
    }

    // Initialize descriptor with our texture.
    VkDescriptorImageInfo texture_info;
    texture_info.sampler = sampler_;
    texture_info.imageView = image_view_;
    texture_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet descriptor_write;
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.pNext = nullptr;
    descriptor_write.dstSet = descriptor_set_;
    descriptor_write.dstBinding = 0;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorCount = 1;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_write.pImageInfo = &texture_info;
    vkUpdateDescriptorSets(*device_, 1, &descriptor_write, 0, nullptr);

    return VK_SUCCESS;
  }

  void Shutdown() {
    if (descriptor_set_) {
      vkFreeDescriptorSets(*device_, descriptor_pool_, 1, &descriptor_set_);
      descriptor_set_ = nullptr;
    }

    VK_SAFE_DESTROY(vkDestroyImageView, *device_, image_view_, nullptr);
    VK_SAFE_DESTROY(vkDestroyImage, *device_, image_, nullptr);
    VK_SAFE_DESTROY(vkFreeMemory, *device_, device_memory_, nullptr);
  }

  VkResult Upload(const uint8_t* src_data) {
    // TODO(benvanik): assert not in use? textures aren't dynamic right now.

    // Get device image layout.
    VkImageSubresource subresource;
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.mipLevel = 0;
    subresource.arrayLayer = 0;
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(*device_, image_, &subresource, &layout);

    // Map memory for upload.
    uint8_t* gpu_data = nullptr;
    auto status = vkMapMemory(*device_, device_memory_, 0, layout.size, 0,
                              reinterpret_cast<void**>(&gpu_data));
    CheckResult(status, "vkMapMemory");

    if (status == VK_SUCCESS) {
      // Copy the entire texture, hoping its layout matches what we expect.
      std::memcpy(gpu_data + layout.offset, src_data, layout.size);

      vkUnmapMemory(*device_, device_memory_);
    }

    return status;
  }

  // Queues a command to transition this texture to a new layout. This assumes
  // the command buffer WILL be queued and executed by the device.
  void TransitionLayout(VkCommandBuffer command_buffer,
                        VkImageLayout new_layout) {
    VkImageMemoryBarrier image_barrier;
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.pNext = nullptr;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.srcAccessMask = 0;
    image_barrier.dstAccessMask = 0;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier.newLayout = new_layout;
    image_barrier.image = image_;
    image_barrier.subresourceRange = {0, 0, 1, 0, 1};
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_layout_ = new_layout;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &image_barrier);
  }

  VkDescriptorSet descriptor_set() const { return descriptor_set_; }
  VkImageLayout layout() const { return image_layout_; }

 private:
  ui::vulkan::VulkanDevice* device_ = nullptr;
  VkDescriptorPool descriptor_pool_ = nullptr;
  VkSampler sampler_ = nullptr;  // Not owned.
  VkImage image_ = nullptr;
  VkImageLayout image_layout_ = VK_IMAGE_LAYOUT_PREINITIALIZED;
  VkDeviceMemory device_memory_ = nullptr;
  VkImageView image_view_ = nullptr;
  VkDescriptorSet descriptor_set_ = nullptr;
};

VulkanImmediateDrawer::VulkanImmediateDrawer(VulkanContext* graphics_context)
    : ImmediateDrawer(graphics_context), context_(graphics_context) {}

VulkanImmediateDrawer::~VulkanImmediateDrawer() { Shutdown(); }

VkResult VulkanImmediateDrawer::Initialize() {
  auto device = context_->device();

  // NEAREST + CLAMP
  VkSamplerCreateInfo sampler_info;
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.pNext = nullptr;
  sampler_info.flags = 0;
  sampler_info.magFilter = VK_FILTER_NEAREST;
  sampler_info.minFilter = VK_FILTER_NEAREST;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.mipLodBias = 0.0f;
  sampler_info.anisotropyEnable = VK_FALSE;
  sampler_info.maxAnisotropy = 1.0f;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = VK_COMPARE_OP_NEVER;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = 0.0f;
  sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  sampler_info.unnormalizedCoordinates = VK_FALSE;
  auto status = vkCreateSampler(*device, &sampler_info, nullptr,
                                &samplers_.nearest_clamp);
  CheckResult(status, "vkCreateSampler");
  if (status != VK_SUCCESS) {
    return status;
  }

  // NEAREST + REPEAT
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  status = vkCreateSampler(*device, &sampler_info, nullptr,
                           &samplers_.nearest_repeat);
  CheckResult(status, "vkCreateSampler");
  if (status != VK_SUCCESS) {
    return status;
  }

  // LINEAR + CLAMP
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  status =
      vkCreateSampler(*device, &sampler_info, nullptr, &samplers_.linear_clamp);
  CheckResult(status, "vkCreateSampler");
  if (status != VK_SUCCESS) {
    return status;
  }

  // LINEAR + REPEAT
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  status = vkCreateSampler(*device, &sampler_info, nullptr,
                           &samplers_.linear_repeat);
  CheckResult(status, "vkCreateSampler");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create the descriptor set layout used for our texture sampler.
  // As it changes almost every draw we keep it separate from the uniform buffer
  // and cache it on the textures.
  VkDescriptorSetLayoutCreateInfo texture_set_layout_info;
  texture_set_layout_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  texture_set_layout_info.pNext = nullptr;
  texture_set_layout_info.flags = 0;
  texture_set_layout_info.bindingCount = 1;
  VkDescriptorSetLayoutBinding texture_binding;
  texture_binding.binding = 0;
  texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  texture_binding.descriptorCount = 1;
  texture_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  texture_binding.pImmutableSamplers = nullptr;
  texture_set_layout_info.pBindings = &texture_binding;
  status = vkCreateDescriptorSetLayout(*device, &texture_set_layout_info,
                                       nullptr, &texture_set_layout_);
  CheckResult(status, "vkCreateDescriptorSetLayout");
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
  descriptor_pool_info.maxSets = 128;
  VkDescriptorPoolSize pool_sizes[1];
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[0].descriptorCount = 128;
  descriptor_pool_info.poolSizeCount = 1;
  descriptor_pool_info.pPoolSizes = pool_sizes;
  status = vkCreateDescriptorPool(*device, &descriptor_pool_info, nullptr,
                                  &descriptor_pool_);
  CheckResult(status, "vkCreateDescriptorPool");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create the pipeline layout used for our pipeline.
  // If we had multiple pipelines they would share this.
  VkPipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.pNext = nullptr;
  pipeline_layout_info.flags = 0;
  VkDescriptorSetLayout set_layouts[] = {texture_set_layout_};
  pipeline_layout_info.setLayoutCount =
      static_cast<uint32_t>(xe::countof(set_layouts));
  pipeline_layout_info.pSetLayouts = set_layouts;
  VkPushConstantRange push_constant_ranges[2];
  push_constant_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_constant_ranges[0].offset = 0;
  push_constant_ranges[0].size = sizeof(float) * 16;
  push_constant_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push_constant_ranges[1].offset = sizeof(float) * 16;
  push_constant_ranges[1].size = sizeof(int);
  pipeline_layout_info.pushConstantRangeCount =
      static_cast<uint32_t>(xe::countof(push_constant_ranges));
  pipeline_layout_info.pPushConstantRanges = push_constant_ranges;
  status = vkCreatePipelineLayout(*device, &pipeline_layout_info, nullptr,
                                  &pipeline_layout_);
  CheckResult(status, "vkCreatePipelineLayout");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Vertex and fragment shaders.
  VkShaderModuleCreateInfo vertex_shader_info;
  vertex_shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  vertex_shader_info.pNext = nullptr;
  vertex_shader_info.flags = 0;
  vertex_shader_info.codeSize = sizeof(immediate_vert);
  vertex_shader_info.pCode = reinterpret_cast<const uint32_t*>(immediate_vert);
  VkShaderModule vertex_shader;
  status = vkCreateShaderModule(*device, &vertex_shader_info, nullptr,
                                &vertex_shader);
  CheckResult(status, "vkCreateShaderModule");
  VkShaderModuleCreateInfo fragment_shader_info;
  fragment_shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  fragment_shader_info.pNext = nullptr;
  fragment_shader_info.flags = 0;
  fragment_shader_info.codeSize = sizeof(immediate_frag);
  fragment_shader_info.pCode =
      reinterpret_cast<const uint32_t*>(immediate_frag);
  VkShaderModule fragment_shader;
  status = vkCreateShaderModule(*device, &fragment_shader_info, nullptr,
                                &fragment_shader);
  CheckResult(status, "vkCreateShaderModule");

  // Pipeline used when rendering triangles.
  VkGraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.pNext = nullptr;
  pipeline_info.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
  VkPipelineShaderStageCreateInfo pipeline_stages[2];
  pipeline_stages[0].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipeline_stages[0].pNext = nullptr;
  pipeline_stages[0].flags = 0;
  pipeline_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  pipeline_stages[0].module = vertex_shader;
  pipeline_stages[0].pName = "main";
  pipeline_stages[0].pSpecializationInfo = nullptr;
  pipeline_stages[1].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipeline_stages[1].pNext = nullptr;
  pipeline_stages[1].flags = 0;
  pipeline_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  pipeline_stages[1].module = fragment_shader;
  pipeline_stages[1].pName = "main";
  pipeline_stages[1].pSpecializationInfo = nullptr;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = pipeline_stages;
  VkPipelineVertexInputStateCreateInfo vertex_state_info;
  vertex_state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_state_info.pNext = nullptr;
  vertex_state_info.flags = 0;
  VkVertexInputBindingDescription vertex_binding_descrs[1];
  vertex_binding_descrs[0].binding = 0;
  vertex_binding_descrs[0].stride = sizeof(ImmediateVertex);
  vertex_binding_descrs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  vertex_state_info.vertexBindingDescriptionCount =
      static_cast<uint32_t>(xe::countof(vertex_binding_descrs));
  vertex_state_info.pVertexBindingDescriptions = vertex_binding_descrs;
  VkVertexInputAttributeDescription vertex_attrib_descrs[3];
  vertex_attrib_descrs[0].location = 0;
  vertex_attrib_descrs[0].binding = 0;
  vertex_attrib_descrs[0].format = VK_FORMAT_R32G32_SFLOAT;
  vertex_attrib_descrs[0].offset = offsetof(ImmediateVertex, x);
  vertex_attrib_descrs[1].location = 1;
  vertex_attrib_descrs[1].binding = 0;
  vertex_attrib_descrs[1].format = VK_FORMAT_R32G32_SFLOAT;
  vertex_attrib_descrs[1].offset = offsetof(ImmediateVertex, u);
  vertex_attrib_descrs[2].location = 2;
  vertex_attrib_descrs[2].binding = 0;
  vertex_attrib_descrs[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  vertex_attrib_descrs[2].offset = offsetof(ImmediateVertex, color);
  vertex_state_info.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(xe::countof(vertex_attrib_descrs));
  vertex_state_info.pVertexAttributeDescriptions = vertex_attrib_descrs;
  pipeline_info.pVertexInputState = &vertex_state_info;
  VkPipelineInputAssemblyStateCreateInfo input_info;
  input_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_info.pNext = nullptr;
  input_info.flags = 0;
  input_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_info.primitiveRestartEnable = VK_FALSE;
  pipeline_info.pInputAssemblyState = &input_info;
  pipeline_info.pTessellationState = nullptr;
  VkPipelineViewportStateCreateInfo viewport_state_info;
  viewport_state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state_info.pNext = nullptr;
  viewport_state_info.flags = 0;
  viewport_state_info.viewportCount = 1;
  viewport_state_info.pViewports = nullptr;
  viewport_state_info.scissorCount = 1;
  viewport_state_info.pScissors = nullptr;
  pipeline_info.pViewportState = &viewport_state_info;
  VkPipelineRasterizationStateCreateInfo rasterization_info;
  rasterization_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization_info.pNext = nullptr;
  rasterization_info.flags = 0;
  rasterization_info.depthClampEnable = VK_FALSE;
  rasterization_info.rasterizerDiscardEnable = VK_FALSE;
  rasterization_info.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization_info.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterization_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterization_info.depthBiasEnable = VK_FALSE;
  rasterization_info.depthBiasConstantFactor = 0;
  rasterization_info.depthBiasClamp = 0;
  rasterization_info.depthBiasSlopeFactor = 0;
  rasterization_info.lineWidth = 1.0f;
  pipeline_info.pRasterizationState = &rasterization_info;
  VkPipelineMultisampleStateCreateInfo multisample_info;
  multisample_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_info.pNext = nullptr;
  multisample_info.flags = 0;
  multisample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisample_info.sampleShadingEnable = VK_FALSE;
  multisample_info.minSampleShading = 0;
  multisample_info.pSampleMask = nullptr;
  multisample_info.alphaToCoverageEnable = VK_FALSE;
  multisample_info.alphaToOneEnable = VK_FALSE;
  pipeline_info.pMultisampleState = &multisample_info;
  pipeline_info.pDepthStencilState = nullptr;
  VkPipelineColorBlendStateCreateInfo blend_info;
  blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend_info.pNext = nullptr;
  blend_info.flags = 0;
  blend_info.logicOpEnable = VK_FALSE;
  blend_info.logicOp = VK_LOGIC_OP_NO_OP;
  VkPipelineColorBlendAttachmentState blend_attachments[1];
  blend_attachments[0].blendEnable = VK_TRUE;
  blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend_attachments[0].dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_attachments[0].colorBlendOp = VK_BLEND_OP_ADD;
  blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend_attachments[0].dstAlphaBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_attachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
  blend_attachments[0].colorWriteMask = 0xF;
  blend_info.attachmentCount =
      static_cast<uint32_t>(xe::countof(blend_attachments));
  blend_info.pAttachments = blend_attachments;
  std::memset(blend_info.blendConstants, 0, sizeof(blend_info.blendConstants));
  pipeline_info.pColorBlendState = &blend_info;
  VkPipelineDynamicStateCreateInfo dynamic_state_info;
  dynamic_state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state_info.pNext = nullptr;
  dynamic_state_info.flags = 0;
  VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  dynamic_state_info.dynamicStateCount =
      static_cast<uint32_t>(xe::countof(dynamic_states));
  dynamic_state_info.pDynamicStates = dynamic_states;
  pipeline_info.pDynamicState = &dynamic_state_info;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = context_->swap_chain()->render_pass();
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = nullptr;
  pipeline_info.basePipelineIndex = -1;
  if (status == VK_SUCCESS) {
    status = vkCreateGraphicsPipelines(*device, nullptr, 1, &pipeline_info,
                                       nullptr, &triangle_pipeline_);
    CheckResult(status, "vkCreateGraphicsPipelines");
  }

  // Silly, but let's make a pipeline just for drawing lines.
  pipeline_info.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
  input_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pipeline_info.basePipelineHandle = triangle_pipeline_;
  pipeline_info.basePipelineIndex = -1;
  if (status == VK_SUCCESS) {
    status = vkCreateGraphicsPipelines(*device, nullptr, 1, &pipeline_info,
                                       nullptr, &line_pipeline_);
    CheckResult(status, "vkCreateGraphicsPipelines");
  }

  VK_SAFE_DESTROY(vkDestroyShaderModule, *device, vertex_shader, nullptr);
  VK_SAFE_DESTROY(vkDestroyShaderModule, *device, fragment_shader, nullptr);

  // Allocate the buffer we'll use for our vertex and index data.
  circular_buffer_ = std::make_unique<LightweightCircularBuffer>(device);

  return status;
}

void VulkanImmediateDrawer::Shutdown() {
  auto device = context_->device();

  circular_buffer_.reset();

  VK_SAFE_DESTROY(vkDestroyPipeline, *device, line_pipeline_, nullptr);
  VK_SAFE_DESTROY(vkDestroyPipeline, *device, triangle_pipeline_, nullptr);
  VK_SAFE_DESTROY(vkDestroyPipelineLayout, *device, pipeline_layout_, nullptr);

  VK_SAFE_DESTROY(vkDestroyDescriptorPool, *device, descriptor_pool_, nullptr);
  VK_SAFE_DESTROY(vkDestroyDescriptorSetLayout, *device, texture_set_layout_,
                  nullptr);

  VK_SAFE_DESTROY(vkDestroySampler, *device, samplers_.nearest_clamp, nullptr);
  VK_SAFE_DESTROY(vkDestroySampler, *device, samplers_.nearest_repeat, nullptr);
  VK_SAFE_DESTROY(vkDestroySampler, *device, samplers_.linear_clamp, nullptr);
  VK_SAFE_DESTROY(vkDestroySampler, *device, samplers_.linear_repeat, nullptr);
}

std::unique_ptr<ImmediateTexture> VulkanImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter, bool repeat,
    const uint8_t* data) {
  auto device = context_->device();

  VkResult status;
  VkSampler sampler = GetSampler(filter, repeat);

  auto texture = std::make_unique<VulkanImmediateTexture>(
      device, descriptor_pool_, sampler, width, height);
  status = texture->Initialize(texture_set_layout_);
  if (status != VK_SUCCESS) {
    texture->Shutdown();
    return nullptr;
  }

  if (data) {
    UpdateTexture(texture.get(), data);
  }
  return std::unique_ptr<ImmediateTexture>(texture.release());
}

std::unique_ptr<ImmediateTexture> VulkanImmediateDrawer::WrapTexture(
    VkImageView image_view, VkSampler sampler, uint32_t width,
    uint32_t height) {
  VkResult status;

  auto texture = std::make_unique<VulkanImmediateTexture>(
      context_->device(), descriptor_pool_, sampler, width, height);
  status = texture->Initialize(texture_set_layout_, image_view);
  if (status != VK_SUCCESS) {
    texture->Shutdown();
    return nullptr;
  }

  return texture;
}

void VulkanImmediateDrawer::UpdateTexture(ImmediateTexture* texture,
                                          const uint8_t* data) {
  static_cast<VulkanImmediateTexture*>(texture)->Upload(data);
}

void VulkanImmediateDrawer::Begin(int render_target_width,
                                  int render_target_height) {
  auto device = context_->device();
  auto swap_chain = context_->swap_chain();
  assert_null(current_cmd_buffer_);
  current_cmd_buffer_ = swap_chain->render_cmd_buffer();
  current_render_target_width_ = render_target_width;
  current_render_target_height_ = render_target_height;

  // Viewport changes only once per batch.
  VkViewport viewport;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(render_target_width);
  viewport.height = static_cast<float>(render_target_height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(current_cmd_buffer_, 0, 1, &viewport);

  // Update projection matrix.
  const float ortho_projection[4][4] = {
      {2.0f / render_target_width, 0.0f, 0.0f, 0.0f},
      {0.0f, 2.0f / -render_target_height, 0.0f, 0.0f},
      {0.0f, 0.0f, -1.0f, 0.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
  };
  vkCmdPushConstants(current_cmd_buffer_, pipeline_layout_,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16,
                     ortho_projection);
}

void VulkanImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  auto device = context_->device();

  // Upload vertices.
  VkDeviceSize vertices_offset = circular_buffer_->Emplace(
      batch.vertices, batch.vertex_count * sizeof(ImmediateVertex));
  if (vertices_offset == VK_WHOLE_SIZE) {
    // TODO(benvanik): die?
    return;
  }
  auto vertex_buffer = circular_buffer_->vertex_buffer();
  vkCmdBindVertexBuffers(current_cmd_buffer_, 0, 1, &vertex_buffer,
                         &vertices_offset);

  // Upload indices.
  if (batch.indices) {
    VkDeviceSize indices_offset = circular_buffer_->Emplace(
        batch.indices, batch.index_count * sizeof(uint16_t));
    if (indices_offset == VK_WHOLE_SIZE) {
      // TODO(benvanik): die?
      return;
    }
    vkCmdBindIndexBuffer(current_cmd_buffer_, circular_buffer_->index_buffer(),
                         indices_offset, VK_INDEX_TYPE_UINT16);
  }

  batch_has_index_buffer_ = !!batch.indices;
}

void VulkanImmediateDrawer::Draw(const ImmediateDraw& draw) {
  switch (draw.primitive_type) {
    case ImmediatePrimitiveType::kLines:
      vkCmdBindPipeline(current_cmd_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        line_pipeline_);
      break;
    case ImmediatePrimitiveType::kTriangles:
      vkCmdBindPipeline(current_cmd_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        triangle_pipeline_);
      break;
  }

  // Setup texture binding.
  auto texture = reinterpret_cast<VulkanImmediateTexture*>(draw.texture_handle);
  if (texture) {
    if (texture->layout() != VK_IMAGE_LAYOUT_GENERAL) {
      texture->TransitionLayout(current_cmd_buffer_, VK_IMAGE_LAYOUT_GENERAL);
    }

    auto texture_set = texture->descriptor_set();
    vkCmdBindDescriptorSets(current_cmd_buffer_,
                            VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                            0, 1, &texture_set, 0, nullptr);
  }

  // Use push constants for our per-draw changes.
  // Here, the restrict_texture_samples uniform.
  int restrict_texture_samples = draw.restrict_texture_samples ? 1 : 0;
  vkCmdPushConstants(current_cmd_buffer_, pipeline_layout_,
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(float) * 16,
                     sizeof(int), &restrict_texture_samples);

  // Scissor, if enabled.
  // Scissor can be disabled by making it the full screen.
  VkRect2D scissor;
  if (draw.scissor) {
    scissor.offset.x = draw.scissor_rect[0];
    scissor.offset.y = current_render_target_height_ -
                       (draw.scissor_rect[1] + draw.scissor_rect[3]);
    scissor.extent.width = draw.scissor_rect[2];
    scissor.extent.height = draw.scissor_rect[3];
  } else {
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = current_render_target_width_;
    scissor.extent.height = current_render_target_height_;
  }
  vkCmdSetScissor(current_cmd_buffer_, 0, 1, &scissor);

  // Issue draw.
  if (batch_has_index_buffer_) {
    vkCmdDrawIndexed(current_cmd_buffer_, draw.count, 1, draw.index_offset,
                     draw.base_vertex, 0);
  } else {
    vkCmdDraw(current_cmd_buffer_, draw.count, 1, draw.base_vertex, 0);
  }
}

void VulkanImmediateDrawer::EndDrawBatch() {}

void VulkanImmediateDrawer::End() { current_cmd_buffer_ = nullptr; }

VkSampler VulkanImmediateDrawer::GetSampler(ImmediateTextureFilter filter,
                                            bool repeat) {
  VkSampler sampler = nullptr;
  switch (filter) {
    case ImmediateTextureFilter::kNearest:
      sampler = repeat ? samplers_.nearest_repeat : samplers_.nearest_clamp;
      break;
    case ImmediateTextureFilter::kLinear:
      sampler = repeat ? samplers_.linear_repeat : samplers_.linear_clamp;
      break;
    default:
      assert_unhandled_case(filter);
      sampler = samplers_.nearest_clamp;
      break;
  }

  return sampler;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_context.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace ui {
namespace vulkan {

// Generated with `xb genspirv`.
#include "xenia/ui/shaders/bytecode/vulkan_spirv/immediate_frag.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/immediate_vert.h"

VulkanImmediateDrawer::VulkanImmediateTexture::~VulkanImmediateTexture() {
  if (immediate_drawer_) {
    immediate_drawer_->OnImmediateTextureDestroyed(*this);
  }
}

VulkanImmediateDrawer::VulkanImmediateDrawer(VulkanContext& graphics_context)
    : ImmediateDrawer(&graphics_context), context_(graphics_context) {}

VulkanImmediateDrawer::~VulkanImmediateDrawer() { Shutdown(); }

bool VulkanImmediateDrawer::Initialize() {
  const VulkanProvider& provider = context_.GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  VkDescriptorSetLayoutBinding texture_descriptor_set_layout_binding;
  texture_descriptor_set_layout_binding.binding = 0;
  texture_descriptor_set_layout_binding.descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  texture_descriptor_set_layout_binding.descriptorCount = 1;
  texture_descriptor_set_layout_binding.stageFlags =
      VK_SHADER_STAGE_FRAGMENT_BIT;
  texture_descriptor_set_layout_binding.pImmutableSamplers = nullptr;
  VkDescriptorSetLayoutCreateInfo texture_descriptor_set_layout_create_info;
  texture_descriptor_set_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  texture_descriptor_set_layout_create_info.pNext = nullptr;
  texture_descriptor_set_layout_create_info.flags = 0;
  texture_descriptor_set_layout_create_info.bindingCount = 1;
  texture_descriptor_set_layout_create_info.pBindings =
      &texture_descriptor_set_layout_binding;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &texture_descriptor_set_layout_create_info, nullptr,
          &texture_descriptor_set_layout_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create the immediate drawer Vulkan combined image sampler "
        "descriptor set layout");
    Shutdown();
    return false;
  }

  // Create the (1, 1, 1, 1) texture as a replacement when drawing without a
  // real texture.
  size_t white_texture_pending_upload_index;
  if (!CreateTextureResource(1, 1, ImmediateTextureFilter::kNearest, false,
                             nullptr, white_texture_,
                             white_texture_pending_upload_index)) {
    XELOGE("Failed to create a blank texture for the Vulkan immediate drawer");
    Shutdown();
    return false;
  }

  VkPushConstantRange push_constant_ranges[1];
  push_constant_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_constant_ranges[0].offset = offsetof(PushConstants, vertex);
  push_constant_ranges[0].size = sizeof(PushConstants::Vertex);
  VkPipelineLayoutCreateInfo pipeline_layout_create_info;
  pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.pNext = nullptr;
  pipeline_layout_create_info.flags = 0;
  pipeline_layout_create_info.setLayoutCount = 1;
  pipeline_layout_create_info.pSetLayouts = &texture_descriptor_set_layout_;
  pipeline_layout_create_info.pushConstantRangeCount =
      uint32_t(xe::countof(push_constant_ranges));
  pipeline_layout_create_info.pPushConstantRanges = push_constant_ranges;
  if (dfn.vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr,
                                 &pipeline_layout_) != VK_SUCCESS) {
    XELOGE("Failed to create the immediate drawer Vulkan pipeline layout");
    Shutdown();
    return false;
  }

  vertex_buffer_pool_ = std::make_unique<VulkanUploadBufferPool>(
      provider,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  // Reset the current state.
  current_command_buffer_ = VK_NULL_HANDLE;
  batch_open_ = false;

  return true;
}

void VulkanImmediateDrawer::Shutdown() {
  const VulkanProvider& provider = context_.GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, pipeline_line_);
  util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, pipeline_triangle_);

  vertex_buffer_pool_.reset();

  util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                             pipeline_layout_);

  for (auto& deleted_texture : textures_deleted_) {
    DestroyTextureResource(deleted_texture.first);
  }
  textures_deleted_.clear();
  for (SubmittedTextureUploadBuffer& submitted_texture_upload_buffer :
       texture_upload_buffers_submitted_) {
    dfn.vkDestroyBuffer(device, submitted_texture_upload_buffer.buffer,
                        nullptr);
    dfn.vkFreeMemory(device, submitted_texture_upload_buffer.buffer_memory,
                     nullptr);
  }
  texture_upload_buffers_submitted_.clear();
  for (PendingTextureUpload& pending_texture_upload :
       texture_uploads_pending_) {
    dfn.vkDestroyBuffer(device, pending_texture_upload.buffer, nullptr);
    dfn.vkFreeMemory(device, pending_texture_upload.buffer_memory, nullptr);
  }
  texture_uploads_pending_.clear();
  for (VulkanImmediateTexture* texture : textures_) {
    if (texture->immediate_drawer_ != this) {
      continue;
    }
    texture->immediate_drawer_ = nullptr;
    DestroyTextureResource(texture->resource_);
  }
  textures_.clear();
  if (white_texture_.image != VK_NULL_HANDLE) {
    DestroyTextureResource(white_texture_);
    white_texture_.image = VK_NULL_HANDLE;
  }

  texture_descriptor_pool_recycled_first_ = nullptr;
  texture_descriptor_pool_unallocated_first_ = nullptr;
  for (TextureDescriptorPool* pool : texture_descriptor_pools_) {
    dfn.vkDestroyDescriptorPool(device, pool->pool, nullptr);
    delete pool;
  }
  texture_descriptor_pools_.clear();
  util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout, device,
                             texture_descriptor_set_layout_);
}

std::unique_ptr<ImmediateTexture> VulkanImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter,
    bool is_repeated, const uint8_t* data) {
  assert_not_null(data);
  auto texture = std::make_unique<VulkanImmediateTexture>(width, height);
  size_t pending_upload_index;
  if (CreateTextureResource(width, height, filter, is_repeated, data,
                            texture->resource_, pending_upload_index)) {
    // Manage by this immediate drawer.
    texture->immediate_drawer_ = this;
    texture->immediate_drawer_index_ = textures_.size();
    textures_.push_back(texture.get());
    texture->pending_upload_index_ = pending_upload_index;
    texture_uploads_pending_[texture->pending_upload_index_].texture =
        texture.get();
  }
  return std::move(texture);
}

void VulkanImmediateDrawer::Begin(int render_target_width,
                                  int render_target_height) {
  assert_true(current_command_buffer_ == VK_NULL_HANDLE);
  assert_false(batch_open_);

  if (!EnsurePipelinesCreated()) {
    return;
  }

  current_command_buffer_ = context_.GetSwapCommandBuffer();

  uint64_t submission_completed = context_.swap_submission_completed();

  const VulkanProvider& provider = context_.GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // Destroy deleted textures.
  for (auto it = textures_deleted_.begin(); it != textures_deleted_.end();) {
    if (it->second > submission_completed) {
      ++it;
      continue;
    }
    DestroyTextureResource(it->first);
    if (std::next(it) != textures_deleted_.end()) {
      *it = textures_deleted_.back();
    }
    textures_deleted_.pop_back();
  }

  // Release upload buffers for completed texture uploads.
  auto erase_texture_uploads_end = texture_upload_buffers_submitted_.begin();
  while (erase_texture_uploads_end != texture_upload_buffers_submitted_.end()) {
    if (erase_texture_uploads_end->submission_index > submission_completed) {
      break;
    }
    dfn.vkDestroyBuffer(device, erase_texture_uploads_end->buffer, nullptr);
    dfn.vkFreeMemory(device, erase_texture_uploads_end->buffer_memory, nullptr);
    ++erase_texture_uploads_end;
  }
  texture_upload_buffers_submitted_.erase(
      texture_upload_buffers_submitted_.begin(), erase_texture_uploads_end);

  vertex_buffer_pool_->Reclaim(submission_completed);

  current_render_target_extent_.width = uint32_t(render_target_width);
  current_render_target_extent_.height = uint32_t(render_target_height);
  VkViewport viewport;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = float(render_target_width);
  viewport.height = float(render_target_height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  dfn.vkCmdSetViewport(current_command_buffer_, 0, 1, &viewport);
  PushConstants::Vertex push_constants_vertex;
  push_constants_vertex.viewport_size_inv[0] = 1.0f / viewport.width;
  push_constants_vertex.viewport_size_inv[1] = 1.0f / viewport.height;
  dfn.vkCmdPushConstants(current_command_buffer_, pipeline_layout_,
                         VK_SHADER_STAGE_VERTEX_BIT,
                         offsetof(PushConstants, vertex),
                         sizeof(PushConstants::Vertex), &push_constants_vertex);
  current_scissor_.offset.x = 0;
  current_scissor_.offset.y = 0;
  current_scissor_.extent.width = 0;
  current_scissor_.extent.height = 0;

  current_pipeline_ = VK_NULL_HANDLE;
  current_texture_descriptor_index_ = UINT32_MAX;
}

void VulkanImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  assert_false(batch_open_);
  if (current_command_buffer_ == VK_NULL_HANDLE) {
    // No surface, or failed to create the pipelines.
    return;
  }

  uint64_t submission_current = context_.swap_submission_current();
  const VulkanProvider::DeviceFunctions& dfn =
      context_.GetVulkanProvider().dfn();

  // Bind the vertices.
  size_t vertex_buffer_size = sizeof(ImmediateVertex) * batch.vertex_count;
  VkBuffer vertex_buffer;
  VkDeviceSize vertex_buffer_offset;
  void* vertex_buffer_mapping = vertex_buffer_pool_->Request(
      submission_current, vertex_buffer_size, sizeof(float), vertex_buffer,
      vertex_buffer_offset);
  if (!vertex_buffer_mapping) {
    XELOGE("Failed to get a buffer for {} vertices in the immediate drawer",
           batch.vertex_count);
    return;
  }
  std::memcpy(vertex_buffer_mapping, batch.vertices, vertex_buffer_size);
  dfn.vkCmdBindVertexBuffers(current_command_buffer_, 0, 1, &vertex_buffer,
                             &vertex_buffer_offset);

  // Bind the indices.
  batch_has_index_buffer_ = batch.indices != nullptr;
  if (batch_has_index_buffer_) {
    size_t index_buffer_size = sizeof(uint16_t) * batch.index_count;
    VkBuffer index_buffer;
    VkDeviceSize index_buffer_offset;
    void* index_buffer_mapping = vertex_buffer_pool_->Request(
        submission_current, index_buffer_size, sizeof(uint16_t), index_buffer,
        index_buffer_offset);
    if (!index_buffer_mapping) {
      XELOGE("Failed to get a buffer for {} indices in the immediate drawer",
             batch.index_count);
      return;
    }
    std::memcpy(index_buffer_mapping, batch.indices, index_buffer_size);
    dfn.vkCmdBindIndexBuffer(current_command_buffer_, index_buffer,
                             index_buffer_offset, VK_INDEX_TYPE_UINT16);
  }

  batch_open_ = true;
}

void VulkanImmediateDrawer::Draw(const ImmediateDraw& draw) {
  if (!batch_open_) {
    // No surface, or failed to create the pipelines, or could be an error while
    // obtaining the vertex and index buffers.
    return;
  }

  const VulkanProvider::DeviceFunctions& dfn =
      context_.GetVulkanProvider().dfn();

  // Set the scissor rectangle if enabled.
  VkRect2D scissor;
  if (draw.scissor) {
    scissor.offset.x = draw.scissor_rect[0];
    scissor.offset.y = current_render_target_extent_.height -
                       (draw.scissor_rect[1] + draw.scissor_rect[3]);
    scissor.extent.width = std::max(draw.scissor_rect[2], 0);
    scissor.extent.height = std::max(draw.scissor_rect[3], 0);
  } else {
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = current_render_target_extent_;
  }
  if (!scissor.extent.width || !scissor.extent.height) {
    // Nothing is visible (used as the default current_scissor_ value also).
    return;
  }
  if (current_scissor_.offset.x != scissor.offset.x ||
      current_scissor_.offset.y != scissor.offset.y ||
      current_scissor_.extent.width != scissor.extent.width ||
      current_scissor_.extent.height != scissor.extent.height) {
    current_scissor_ = scissor;
    dfn.vkCmdSetScissor(current_command_buffer_, 0, 1, &scissor);
  }

  // Bind the pipeline for the current primitive type.
  VkPipeline pipeline;
  switch (draw.primitive_type) {
    case ImmediatePrimitiveType::kLines:
      pipeline = pipeline_line_;
      break;
    case ImmediatePrimitiveType::kTriangles:
      pipeline = pipeline_triangle_;
      break;
    default:
      assert_unhandled_case(draw.primitive_type);
      return;
  }
  if (current_pipeline_ != pipeline) {
    current_pipeline_ = pipeline;
    dfn.vkCmdBindPipeline(current_command_buffer_,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  }

  // Bind the texture.
  uint32_t texture_descriptor_index;
  VulkanImmediateTexture* texture =
      static_cast<VulkanImmediateTexture*>(draw.texture);
  if (texture && texture->immediate_drawer_ == this) {
    texture_descriptor_index = texture->resource_.descriptor_index;
    texture->last_usage_submission_ = context_.swap_submission_current();
  } else {
    texture_descriptor_index = white_texture_.descriptor_index;
  }
  if (current_texture_descriptor_index_ != texture_descriptor_index) {
    current_texture_descriptor_index_ = texture_descriptor_index;
    VkDescriptorSet texture_descriptor_set =
        GetTextureDescriptor(texture_descriptor_index);
    dfn.vkCmdBindDescriptorSets(
        current_command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout_, 0, 1, &texture_descriptor_set, 0, nullptr);
  }

  // Draw.
  if (batch_has_index_buffer_) {
    dfn.vkCmdDrawIndexed(current_command_buffer_, draw.count, 1,
                         draw.index_offset, draw.base_vertex, 0);
  } else {
    dfn.vkCmdDraw(current_command_buffer_, draw.count, 1, draw.base_vertex, 0);
  }
}

void VulkanImmediateDrawer::EndDrawBatch() { batch_open_ = false; }

void VulkanImmediateDrawer::End() {
  assert_false(batch_open_);
  if (current_command_buffer_ == VK_NULL_HANDLE) {
    // Didn't draw anything because the of some issue or surface not being
    // available.
    return;
  }

  // Copy textures.
  if (!texture_uploads_pending_.empty()) {
    VkCommandBuffer setup_command_buffer =
        context_.AcquireSwapSetupCommandBuffer();
    if (setup_command_buffer != VK_NULL_HANDLE) {
      const VulkanProvider::DeviceFunctions& dfn =
          context_.GetVulkanProvider().dfn();
      size_t texture_uploads_pending_count = texture_uploads_pending_.size();
      uint64_t submission_current = context_.swap_submission_current();

      // Transition to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
      std::vector<VkImageMemoryBarrier> image_memory_barriers;
      image_memory_barriers.reserve(texture_uploads_pending_count);
      VkImageMemoryBarrier image_memory_barrier;
      image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      image_memory_barrier.pNext = nullptr;
      image_memory_barrier.srcAccessMask = 0;
      image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      util::InitializeSubresourceRange(image_memory_barrier.subresourceRange);
      for (const PendingTextureUpload& pending_texture_upload :
           texture_uploads_pending_) {
        image_memory_barriers.emplace_back(image_memory_barrier).image =
            pending_texture_upload.image;
      }
      dfn.vkCmdPipelineBarrier(
          setup_command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
          uint32_t(image_memory_barriers.size()), image_memory_barriers.data());

      // Do transfer operations and transition to
      // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, and also mark as used.
      for (size_t i = 0; i < texture_uploads_pending_count; ++i) {
        const PendingTextureUpload& pending_texture_upload =
            texture_uploads_pending_[i];
        if (pending_texture_upload.buffer != VK_NULL_HANDLE) {
          // Copying.
          VkBufferImageCopy copy_region;
          copy_region.bufferOffset = 0;
          copy_region.bufferRowLength = pending_texture_upload.width;
          copy_region.bufferImageHeight = pending_texture_upload.height;
          copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          copy_region.imageSubresource.mipLevel = 0;
          copy_region.imageSubresource.baseArrayLayer = 0;
          copy_region.imageSubresource.layerCount = 1;
          copy_region.imageOffset.x = 0;
          copy_region.imageOffset.y = 0;
          copy_region.imageOffset.z = 0;
          copy_region.imageExtent.width = pending_texture_upload.width;
          copy_region.imageExtent.height = pending_texture_upload.height;
          copy_region.imageExtent.depth = 1;
          dfn.vkCmdCopyBufferToImage(
              setup_command_buffer, pending_texture_upload.buffer,
              pending_texture_upload.image,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

          SubmittedTextureUploadBuffer& submitted_texture_upload_buffer =
              texture_upload_buffers_submitted_.emplace_back();
          submitted_texture_upload_buffer.buffer =
              pending_texture_upload.buffer;
          submitted_texture_upload_buffer.buffer_memory =
              pending_texture_upload.buffer_memory;
          submitted_texture_upload_buffer.submission_index = submission_current;
        } else {
          // Clearing (initializing the empty image).
          VkClearColorValue white_clear_value;
          white_clear_value.float32[0] = 1.0f;
          white_clear_value.float32[1] = 1.0f;
          white_clear_value.float32[2] = 1.0f;
          white_clear_value.float32[3] = 1.0f;
          dfn.vkCmdClearColorImage(
              setup_command_buffer, pending_texture_upload.image,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white_clear_value, 1,
              &image_memory_barrier.subresourceRange);
        }

        VkImageMemoryBarrier& image_memory_barrier_current =
            image_memory_barriers[i];
        image_memory_barrier_current.srcAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT;
        image_memory_barrier_current.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        image_memory_barrier_current.oldLayout =
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        image_memory_barrier_current.newLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        if (pending_texture_upload.texture) {
          pending_texture_upload.texture->last_usage_submission_ =
              submission_current;
          pending_texture_upload.texture->pending_upload_index_ = SIZE_MAX;
        }
      }
      dfn.vkCmdPipelineBarrier(
          setup_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
          uint32_t(image_memory_barriers.size()), image_memory_barriers.data());

      texture_uploads_pending_.clear();
    }
  }

  vertex_buffer_pool_->FlushWrites();
  current_command_buffer_ = VK_NULL_HANDLE;
}

bool VulkanImmediateDrawer::EnsurePipelinesCreated() {
  VkFormat swap_surface_format = context_.swap_surface_format().format;
  if (swap_surface_format == pipeline_framebuffer_format_) {
    // Either created, or failed to create once (don't try to create every
    // frame).
    return pipeline_triangle_ != VK_NULL_HANDLE &&
           pipeline_line_ != VK_NULL_HANDLE;
  }
  VkRenderPass swap_render_pass = context_.swap_render_pass();
  if (swap_surface_format == VK_FORMAT_UNDEFINED ||
      swap_render_pass == VK_NULL_HANDLE) {
    // Not ready yet.
    return false;
  }

  const VulkanProvider& provider = context_.GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // Safe to destroy the pipelines now - if the render pass was recreated,
  // completion of its usage has already been awaited.
  util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, pipeline_line_);
  util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, pipeline_triangle_);
  // If creation fails now, don't try to create every frame.
  pipeline_framebuffer_format_ = swap_surface_format;

  // Triangle pipeline.

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = util::CreateShaderModule(provider, immediate_vert,
                                              sizeof(immediate_vert));
  if (stages[0].module == VK_NULL_HANDLE) {
    XELOGE("Failed to create the immediate drawer Vulkan vertex shader module");
    return false;
  }
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = util::CreateShaderModule(provider, immediate_frag,
                                              sizeof(immediate_frag));
  if (stages[1].module == VK_NULL_HANDLE) {
    XELOGE(
        "Failed to create the immediate drawer Vulkan fragment shader module");
    dfn.vkDestroyShaderModule(device, stages[0].module, nullptr);
    return false;
  }
  stages[1].pName = "main";

  VkVertexInputBindingDescription vertex_input_binding;
  vertex_input_binding.binding = 0;
  vertex_input_binding.stride = sizeof(ImmediateVertex);
  vertex_input_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription vertex_input_attributes[3];
  vertex_input_attributes[0].location = 0;
  vertex_input_attributes[0].binding = 0;
  vertex_input_attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
  vertex_input_attributes[0].offset = offsetof(ImmediateVertex, x);
  vertex_input_attributes[1].location = 1;
  vertex_input_attributes[1].binding = 0;
  vertex_input_attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
  vertex_input_attributes[1].offset = offsetof(ImmediateVertex, u);
  vertex_input_attributes[2].location = 2;
  vertex_input_attributes[2].binding = 0;
  vertex_input_attributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  vertex_input_attributes[2].offset = offsetof(ImmediateVertex, color);
  VkPipelineVertexInputStateCreateInfo vertex_input_state;
  vertex_input_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_state.pNext = nullptr;
  vertex_input_state.flags = 0;
  vertex_input_state.vertexBindingDescriptionCount = 1;
  vertex_input_state.pVertexBindingDescriptions = &vertex_input_binding;
  vertex_input_state.vertexAttributeDescriptionCount =
      uint32_t(xe::countof(vertex_input_attributes));
  vertex_input_state.pVertexAttributeDescriptions = vertex_input_attributes;

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
  input_assembly_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly_state.pNext = nullptr;
  input_assembly_state.flags = 0;
  input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly_state.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewport_state;
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.pNext = nullptr;
  viewport_state.flags = 0;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = nullptr;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = nullptr;

  VkPipelineRasterizationStateCreateInfo rasterization_state = {};
  rasterization_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization_state.cullMode = VK_CULL_MODE_NONE;
  rasterization_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization_state.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample_state = {};
  multisample_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachment_state;
  color_blend_attachment_state.blendEnable = VK_TRUE;
  color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_blend_attachment_state.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
  // Don't change alpha (always 1).
  color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                VK_COLOR_COMPONENT_G_BIT |
                                                VK_COLOR_COMPONENT_B_BIT;
  VkPipelineColorBlendStateCreateInfo color_blend_state;
  color_blend_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend_state.pNext = nullptr;
  color_blend_state.flags = 0;
  color_blend_state.logicOpEnable = VK_FALSE;
  color_blend_state.logicOp = VK_LOGIC_OP_NO_OP;
  color_blend_state.attachmentCount = 1;
  color_blend_state.pAttachments = &color_blend_attachment_state;
  color_blend_state.blendConstants[0] = 1.0f;
  color_blend_state.blendConstants[1] = 1.0f;
  color_blend_state.blendConstants[2] = 1.0f;
  color_blend_state.blendConstants[3] = 1.0f;

  static const VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  VkPipelineDynamicStateCreateInfo dynamic_state;
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.pNext = nullptr;
  dynamic_state.flags = 0;
  dynamic_state.dynamicStateCount = uint32_t(xe::countof(dynamic_states));
  dynamic_state.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo pipeline_create_info;
  pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_create_info.pNext = nullptr;
  pipeline_create_info.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
  pipeline_create_info.stageCount = uint32_t(xe::countof(stages));
  pipeline_create_info.pStages = stages;
  pipeline_create_info.pVertexInputState = &vertex_input_state;
  pipeline_create_info.pInputAssemblyState = &input_assembly_state;
  pipeline_create_info.pTessellationState = nullptr;
  pipeline_create_info.pViewportState = &viewport_state;
  pipeline_create_info.pRasterizationState = &rasterization_state;
  pipeline_create_info.pMultisampleState = &multisample_state;
  pipeline_create_info.pDepthStencilState = nullptr;
  pipeline_create_info.pColorBlendState = &color_blend_state;
  pipeline_create_info.pDynamicState = &dynamic_state;
  pipeline_create_info.layout = pipeline_layout_;
  pipeline_create_info.renderPass = swap_render_pass;
  pipeline_create_info.subpass = 0;
  pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_create_info.basePipelineIndex = -1;
  if (dfn.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                    &pipeline_create_info, nullptr,
                                    &pipeline_triangle_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create the immediate drawer triangle list Vulkan pipeline");
    dfn.vkDestroyShaderModule(device, stages[1].module, nullptr);
    dfn.vkDestroyShaderModule(device, stages[0].module, nullptr);
    return false;
  }

  // Line pipeline.

  input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pipeline_create_info.flags =
      (pipeline_create_info.flags & ~VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT) |
      VK_PIPELINE_CREATE_DERIVATIVE_BIT;
  pipeline_create_info.basePipelineHandle = pipeline_triangle_;
  VkResult pipeline_line_create_result = dfn.vkCreateGraphicsPipelines(
      device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr,
      &pipeline_line_);
  dfn.vkDestroyShaderModule(device, stages[1].module, nullptr);
  dfn.vkDestroyShaderModule(device, stages[0].module, nullptr);
  if (pipeline_line_create_result != VK_SUCCESS) {
    XELOGE("Failed to create the immediate drawer line list Vulkan pipeline");
    dfn.vkDestroyPipeline(device, pipeline_triangle_, nullptr);
    pipeline_triangle_ = VK_NULL_HANDLE;
    return false;
  }

  return true;
}

uint32_t VulkanImmediateDrawer::AllocateTextureDescriptor() {
  // Try to reuse a recycled descriptor first.
  if (texture_descriptor_pool_recycled_first_) {
    TextureDescriptorPool* pool = texture_descriptor_pool_recycled_first_;
    assert_not_zero(pool->recycled_bits);
    uint32_t local_index;
    xe::bit_scan_forward(pool->recycled_bits, &local_index);
    pool->recycled_bits &= ~(uint64_t(1) << local_index);
    if (!pool->recycled_bits) {
      texture_descriptor_pool_recycled_first_ = pool->recycled_next;
    }
    return (pool->index << 6) | local_index;
  }

  const VulkanProvider& provider = context_.GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  VkDescriptorSetAllocateInfo allocate_info;
  allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocate_info.pNext = nullptr;
  allocate_info.descriptorSetCount = 1;
  allocate_info.pSetLayouts = &texture_descriptor_set_layout_;

  // If no recycled, try to create a new allocation within an existing pool with
  // unallocated descriptors left.
  while (texture_descriptor_pool_unallocated_first_) {
    TextureDescriptorPool* pool = texture_descriptor_pool_unallocated_first_;
    assert_not_zero(pool->unallocated_count);
    allocate_info.descriptorPool = pool->pool;
    uint32_t local_index =
        TextureDescriptorPool::kDescriptorCount - pool->unallocated_count;
    VkResult allocate_result = dfn.vkAllocateDescriptorSets(
        device, &allocate_info, &pool->sets[local_index]);
    if (allocate_result == VK_SUCCESS) {
      --pool->unallocated_count;
    } else {
      // Failed to allocate for some reason, don't try again for this pool.
      pool->unallocated_count = 0;
    }
    if (!pool->unallocated_count) {
      texture_descriptor_pool_unallocated_first_ = pool->unallocated_next;
    }
    if (allocate_result == VK_SUCCESS) {
      return (pool->index << 6) | local_index;
    }
  }

  // Create a new pool and allocate the descriptor from it.
  VkDescriptorPoolSize descriptor_pool_size;
  descriptor_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptor_pool_size.descriptorCount =
      TextureDescriptorPool::kDescriptorCount;
  VkDescriptorPoolCreateInfo descriptor_pool_create_info;
  descriptor_pool_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_create_info.pNext = nullptr;
  descriptor_pool_create_info.flags = 0;
  descriptor_pool_create_info.maxSets = TextureDescriptorPool::kDescriptorCount;
  descriptor_pool_create_info.poolSizeCount = 1;
  descriptor_pool_create_info.pPoolSizes = &descriptor_pool_size;
  VkDescriptorPool descriptor_pool;
  if (dfn.vkCreateDescriptorPool(device, &descriptor_pool_create_info, nullptr,
                                 &descriptor_pool) != VK_SUCCESS) {
    XELOGE(
        "Failed to create an immediate drawer Vulkan combined image sampler "
        "descriptor pool with {} descriptors",
        TextureDescriptorPool::kDescriptorCount);
    return UINT32_MAX;
  }
  allocate_info.descriptorPool = descriptor_pool;
  VkDescriptorSet descriptor_set;
  if (dfn.vkAllocateDescriptorSets(device, &allocate_info, &descriptor_set) !=
      VK_SUCCESS) {
    XELOGE(
        "Failed to allocate an immediate drawer Vulkan combined image sampler "
        "descriptor");
    dfn.vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    return UINT32_MAX;
  }
  TextureDescriptorPool* new_pool = new TextureDescriptorPool;
  new_pool->pool = descriptor_pool;
  new_pool->sets[0] = descriptor_set;
  uint32_t new_pool_index = uint32_t(texture_descriptor_pools_.size());
  new_pool->index = new_pool_index;
  new_pool->unallocated_count = TextureDescriptorPool::kDescriptorCount - 1;
  new_pool->recycled_bits = 0;
  new_pool->unallocated_next = texture_descriptor_pool_unallocated_first_;
  texture_descriptor_pool_unallocated_first_ = new_pool;
  new_pool->recycled_next = nullptr;
  texture_descriptor_pools_.push_back(new_pool);
  return new_pool_index << 6;
}

VkDescriptorSet VulkanImmediateDrawer::GetTextureDescriptor(
    uint32_t descriptor_index) const {
  uint32_t pool_index = descriptor_index >> 6;
  assert_true(pool_index < texture_descriptor_pools_.size());
  const TextureDescriptorPool* pool = texture_descriptor_pools_[pool_index];
  uint32_t allocation_index = descriptor_index & 63;
  assert_true(allocation_index < TextureDescriptorPool::kDescriptorCount -
                                     pool->unallocated_count);
  return pool->sets[allocation_index];
}

void VulkanImmediateDrawer::FreeTextureDescriptor(uint32_t descriptor_index) {
  uint32_t pool_index = descriptor_index >> 6;
  assert_true(pool_index < texture_descriptor_pools_.size());
  TextureDescriptorPool* pool = texture_descriptor_pools_[pool_index];
  uint32_t allocation_index = descriptor_index & 63;
  assert_true(allocation_index < TextureDescriptorPool::kDescriptorCount -
                                     pool->unallocated_count);
  assert_zero(pool->recycled_bits & (uint64_t(1) << allocation_index));
  if (!pool->recycled_bits) {
    // Add to the free list if not already in it.
    pool->recycled_next = texture_descriptor_pool_recycled_first_;
    texture_descriptor_pool_recycled_first_ = pool;
  }
  pool->recycled_bits |= uint64_t(1) << allocation_index;
}

bool VulkanImmediateDrawer::CreateTextureResource(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter,
    bool is_repeated, const uint8_t* data,
    VulkanImmediateTexture::Resource& resource_out,
    size_t& pending_upload_index_out) {
  const VulkanProvider& provider = context_.GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // Create the image and the descriptor.

  VkImageCreateInfo image_create_info;
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.pNext = nullptr;
  image_create_info.flags = 0;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_create_info.extent.width = width;
  image_create_info.extent.height = height;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = nullptr;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage image;
  if (dfn.vkCreateImage(device, &image_create_info, nullptr, &image) !=
      VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan image for a {}x{} immediate drawer texture",
        width, height);
    return false;
  }

  VkMemoryAllocateInfo image_memory_allocate_info;
  VkMemoryRequirements image_memory_requirements;
  dfn.vkGetImageMemoryRequirements(device, image, &image_memory_requirements);
  if (!xe::bit_scan_forward(image_memory_requirements.memoryTypeBits &
                                provider.memory_types_device_local(),
                            &image_memory_allocate_info.memoryTypeIndex)) {
    XELOGE(
        "Failed to get a device-local memory type for a {}x{} immediate "
        "drawer Vulkan image",
        width, height);
    dfn.vkDestroyImage(device, image, nullptr);
    return false;
  }
  image_memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  VkMemoryDedicatedAllocateInfoKHR image_memory_dedicated_allocate_info;
  if (provider.device_extensions().khr_dedicated_allocation) {
    image_memory_dedicated_allocate_info.sType =
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
    image_memory_dedicated_allocate_info.pNext = nullptr;
    image_memory_dedicated_allocate_info.image = image;
    image_memory_dedicated_allocate_info.buffer = VK_NULL_HANDLE;
    image_memory_allocate_info.pNext = &image_memory_dedicated_allocate_info;
  } else {
    image_memory_allocate_info.pNext = nullptr;
  }
  image_memory_allocate_info.allocationSize = image_memory_requirements.size;
  VkDeviceMemory image_memory;
  if (dfn.vkAllocateMemory(device, &image_memory_allocate_info, nullptr,
                           &image_memory) != VK_SUCCESS) {
    XELOGE(
        "Failed to allocate memory for a {}x{} immediate drawer Vulkan "
        "image",
        width, height);
    dfn.vkDestroyImage(device, image, nullptr);
    return false;
  }
  if (dfn.vkBindImageMemory(device, image, image_memory, 0) != VK_SUCCESS) {
    XELOGE("Failed to bind memory to a {}x{} immediate drawer Vulkan image",
           width, height);
    dfn.vkDestroyImage(device, image, nullptr);
    dfn.vkFreeMemory(device, image_memory, nullptr);
    return false;
  }

  VkImageViewCreateInfo image_view_create_info;
  image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image_view_create_info.pNext = nullptr;
  image_view_create_info.flags = 0;
  image_view_create_info.image = image;
  image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image_view_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  // data == nullptr is a special case for (1, 1, 1, 1).
  VkComponentSwizzle swizzle =
      data ? VK_COMPONENT_SWIZZLE_IDENTITY : VK_COMPONENT_SWIZZLE_ONE;
  image_view_create_info.components.r = swizzle;
  image_view_create_info.components.g = swizzle;
  image_view_create_info.components.b = swizzle;
  image_view_create_info.components.a = swizzle;
  util::InitializeSubresourceRange(image_view_create_info.subresourceRange);
  VkImageView image_view;
  if (dfn.vkCreateImageView(device, &image_view_create_info, nullptr,
                            &image_view) != VK_SUCCESS) {
    XELOGE(
        "Failed to create an image view for a {}x{} immediate drawer Vulkan "
        "image",
        width, height);
    dfn.vkDestroyImage(device, image, nullptr);
    dfn.vkFreeMemory(device, image_memory, nullptr);
    return false;
  }

  uint32_t descriptor_index = AllocateTextureDescriptor();
  if (descriptor_index == UINT32_MAX) {
    XELOGE(
        "Failed to allocate a Vulkan descriptor for a {}x{} immediate drawer "
        "texture",
        width, height);
    dfn.vkDestroyImageView(device, image_view, nullptr);
    dfn.vkDestroyImage(device, image, nullptr);
    dfn.vkFreeMemory(device, image_memory, nullptr);
    return false;
  }
  VkDescriptorImageInfo descriptor_image_info;
  VulkanProvider::HostSampler host_sampler;
  if (filter == ImmediateTextureFilter::kLinear) {
    host_sampler = is_repeated ? VulkanProvider::HostSampler::kLinearRepeat
                               : VulkanProvider::HostSampler::kLinearClamp;
  } else {
    host_sampler = is_repeated ? VulkanProvider::HostSampler::kNearestRepeat
                               : VulkanProvider::HostSampler::kNearestClamp;
  }
  descriptor_image_info.sampler = provider.GetHostSampler(host_sampler);
  descriptor_image_info.imageView = image_view;
  descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet descriptor_write;
  descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_write.pNext = nullptr;
  descriptor_write.dstSet = GetTextureDescriptor(descriptor_index);
  descriptor_write.dstBinding = 0;
  descriptor_write.dstArrayElement = 0;
  descriptor_write.descriptorCount = 1;
  descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptor_write.pImageInfo = &descriptor_image_info;
  descriptor_write.pBufferInfo = nullptr;
  descriptor_write.pTexelBufferView = nullptr;
  dfn.vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, nullptr);

  // Create and fill the upload buffer.

  // data == nullptr is a special case for (1, 1, 1, 1), clearing rather than
  // uploading in this case.
  VkBuffer upload_buffer = VK_NULL_HANDLE;
  VkDeviceMemory upload_buffer_memory = VK_NULL_HANDLE;
  if (data) {
    size_t data_size = sizeof(uint32_t) * width * height;
    uint32_t upload_buffer_memory_type;
    if (!util::CreateDedicatedAllocationBuffer(
            provider, VkDeviceSize(data_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            util::MemoryPurpose::kUpload, upload_buffer, upload_buffer_memory,
            &upload_buffer_memory_type)) {
      XELOGE(
          "Failed to create a Vulkan upload buffer for a {}x{} immediate "
          "drawer texture",
          width, height);
      FreeTextureDescriptor(descriptor_index);
      dfn.vkDestroyImageView(device, image_view, nullptr);
      dfn.vkDestroyImage(device, image, nullptr);
      dfn.vkFreeMemory(device, image_memory, nullptr);
      return false;
    }
    void* upload_buffer_mapping;
    if (dfn.vkMapMemory(device, upload_buffer_memory, 0, VK_WHOLE_SIZE, 0,
                        &upload_buffer_mapping) != VK_SUCCESS) {
      XELOGE(
          "Failed to map Vulkan upload buffer memory for a {}x{} immediate "
          "drawer texture",
          width, height);
      dfn.vkDestroyBuffer(device, upload_buffer, nullptr);
      dfn.vkFreeMemory(device, upload_buffer_memory, nullptr);
      FreeTextureDescriptor(descriptor_index);
      dfn.vkDestroyImageView(device, image_view, nullptr);
      dfn.vkDestroyImage(device, image, nullptr);
      dfn.vkFreeMemory(device, image_memory, nullptr);
      return false;
    }
    std::memcpy(upload_buffer_mapping, data, data_size);
    util::FlushMappedMemoryRange(provider, upload_buffer_memory,
                                 upload_buffer_memory_type);
    dfn.vkUnmapMemory(device, upload_buffer_memory);
  }

  resource_out.image = image;
  resource_out.memory = image_memory;
  resource_out.image_view = image_view;
  resource_out.descriptor_index = descriptor_index;

  pending_upload_index_out = texture_uploads_pending_.size();
  PendingTextureUpload& pending_upload =
      texture_uploads_pending_.emplace_back();
  // The caller will set the ImmedateTexture pointer if needed.
  pending_upload.texture = nullptr;
  pending_upload.buffer = upload_buffer;
  pending_upload.buffer_memory = upload_buffer_memory;
  pending_upload.image = image;
  pending_upload.width = width;
  pending_upload.height = height;

  return true;
}

void VulkanImmediateDrawer::DestroyTextureResource(
    VulkanImmediateTexture::Resource& resource) {
  FreeTextureDescriptor(resource.descriptor_index);
  const VulkanProvider& provider = context_.GetVulkanProvider();
  const VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
  dfn.vkDestroyImageView(device, resource.image_view, nullptr);
  dfn.vkDestroyImage(device, resource.image, nullptr);
  dfn.vkFreeMemory(device, resource.memory, nullptr);
}

void VulkanImmediateDrawer::OnImmediateTextureDestroyed(
    VulkanImmediateTexture& texture) {
  // Remove from the pending uploads.
  size_t pending_upload_index = texture.pending_upload_index_;
  if (pending_upload_index != SIZE_MAX) {
    if (pending_upload_index + 1 < texture_uploads_pending_.size()) {
      PendingTextureUpload& pending_upload =
          texture_uploads_pending_[pending_upload_index];
      pending_upload = texture_uploads_pending_.back();
      if (pending_upload.texture) {
        pending_upload.texture->pending_upload_index_ = pending_upload_index;
      }
    }
    texture_uploads_pending_.pop_back();
  }

  // Remove from the texture list.
  VulkanImmediateTexture*& texture_at_index =
      textures_[texture.immediate_drawer_index_];
  texture_at_index = textures_.back();
  texture_at_index->immediate_drawer_index_ = texture.immediate_drawer_index_;
  textures_.pop_back();

  // Destroy immediately or queue for destruction if in use.
  if (texture.last_usage_submission_ > context_.swap_submission_completed()) {
    textures_deleted_.emplace_back(
        std::make_pair(texture.resource_, texture.last_usage_submission_));
  } else {
    DestroyTextureResource(texture.resource_);
  }
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

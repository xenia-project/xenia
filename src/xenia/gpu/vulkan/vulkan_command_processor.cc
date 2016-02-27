/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_command_processor.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

using namespace xe::gpu::xenos;
using xe::ui::vulkan::CheckResult;

constexpr size_t kDefaultBufferCacheCapacity = 256 * 1024 * 1024;

VulkanCommandProcessor::VulkanCommandProcessor(
    VulkanGraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}

VulkanCommandProcessor::~VulkanCommandProcessor() = default;

void VulkanCommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();

  buffer_cache_->ClearCache();
  pipeline_cache_->ClearCache();
  render_cache_->ClearCache();
  texture_cache_->ClearCache();
}

bool VulkanCommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Unable to initialize base command processor context");
    return false;
  }

  // Acquire our device and queue.
  auto context = static_cast<xe::ui::vulkan::VulkanContext*>(context_.get());
  device_ = context->device();
  queue_ = device_->AcquireQueue();
  if (!queue_) {
    // Need to reuse primary queue (with locks).
    queue_ = device_->primary_queue();
    queue_mutex_ = &device_->primary_queue_mutex();
  }

  // Setup fenced pools used for all our per-frame/per-draw resources.
  command_buffer_pool_ = std::make_unique<ui::vulkan::CommandBufferPool>(
      *device_, device_->queue_family_index(), VK_COMMAND_BUFFER_LEVEL_PRIMARY);

  // Initialize the state machine caches.
  buffer_cache_ = std::make_unique<BufferCache>(register_file_, device_,
                                                kDefaultBufferCacheCapacity);
  texture_cache_ =
      std::make_unique<TextureCache>(register_file_, &trace_writer_, device_);
  pipeline_cache_ = std::make_unique<PipelineCache>(
      register_file_, device_, buffer_cache_->constant_descriptor_set_layout(),
      texture_cache_->texture_descriptor_set_layout());
  render_cache_ = std::make_unique<RenderCache>(register_file_, device_);

  return true;
}

void VulkanCommandProcessor::ShutdownContext() {
  // TODO(benvanik): wait until idle.

  buffer_cache_.reset();
  pipeline_cache_.reset();
  render_cache_.reset();
  texture_cache_.reset();

  // Free all pools. This must come after all of our caches clean up.
  command_buffer_pool_.reset();

  // Release queue, if were using an acquired one.
  if (!queue_mutex_) {
    device_->ReleaseQueue(queue_);
    queue_ = nullptr;
  }

  CommandProcessor::ShutdownContext();
}

void VulkanCommandProcessor::MakeCoherent() {
  RegisterFile* regs = register_file_;
  auto status_host = regs->values[XE_GPU_REG_COHER_STATUS_HOST].u32;

  CommandProcessor::MakeCoherent();

  if (status_host & 0x80000000ul) {
    // TODO(benvanik): less-fine-grained clearing.
    buffer_cache_->InvalidateCache();
  }
}

void VulkanCommandProcessor::PrepareForWait() {
  SCOPE_profile_cpu_f("gpu");

  CommandProcessor::PrepareForWait();

  // TODO(benvanik): fences and fancy stuff. We should figure out a way to
  // make interrupt callbacks from the GPU so that we don't have to do a full
  // synchronize here.
  // glFlush();
  // glFinish();

  context_->ClearCurrent();
}

void VulkanCommandProcessor::ReturnFromWait() {
  context_->MakeCurrent();

  CommandProcessor::ReturnFromWait();
}

void VulkanCommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                         uint32_t frontbuffer_width,
                                         uint32_t frontbuffer_height) {
  // Ensure we issue any pending draws.
  // draw_batcher_.Flush(DrawBatcher::FlushMode::kMakeCoherent);

  // Need to finish to be sure the other context sees the right data.
  // TODO(benvanik): prevent this? fences?
  // glFinish();

  if (context_->WasLost()) {
    // We've lost the context due to a TDR.
    // TODO: Dump the current commands to a tracefile.
    assert_always();
  }

  // Remove any dead textures, etc.
  // texture_cache_.Scavenge();
}

Shader* VulkanCommandProcessor::LoadShader(ShaderType shader_type,
                                           uint32_t guest_address,
                                           const uint32_t* host_address,
                                           uint32_t dword_count) {
  return pipeline_cache_->LoadShader(shader_type, guest_address, host_address,
                                     dword_count);
}

bool VulkanCommandProcessor::IssueDraw(PrimitiveType primitive_type,
                                       uint32_t index_count,
                                       IndexBufferInfo* index_buffer_info) {
  auto& regs = *register_file_;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto enable_mode =
      static_cast<ModeControl>(regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7);
  if (enable_mode == ModeControl::kIgnore) {
    // Ignored.
    return true;
  } else if (enable_mode == ModeControl::kCopy) {
    // Special copy handling.
    return IssueCopy();
  }

  if ((regs[XE_GPU_REG_RB_SURFACE_INFO].u32 & 0x3FFF) == 0) {
    // Doesn't actually draw.
    return true;
  }

  // TODO(benvanik): move to CP or to host (trace dump, etc).
  if (FLAGS_vulkan_renderdoc_capture_all && device_->is_renderdoc_attached()) {
    device_->BeginRenderDocFrameCapture();
  }

  // Shaders will have already been defined by previous loads.
  // We need the to do just about anything so validate here.
  auto vertex_shader = static_cast<VulkanShader*>(active_vertex_shader());
  auto pixel_shader = static_cast<VulkanShader*>(active_pixel_shader());
  if (!vertex_shader || !vertex_shader->is_valid()) {
    // Always need a vertex shader.
    return true;
  }
  // Depth-only mode doesn't need a pixel shader (we'll use a fake one).
  if (enable_mode == ModeControl::kDepth) {
    // Use a dummy pixel shader when required.
    // TODO(benvanik): dummy pixel shader.
    assert_not_null(pixel_shader);
  } else if (!pixel_shader || !pixel_shader->is_valid()) {
    // Need a pixel shader in normal color mode.
    return true;
  }

  // TODO(benvanik): bigger batches.
  command_buffer_pool_->BeginBatch();
  VkCommandBuffer command_buffer = command_buffer_pool_->AcquireEntry();
  VkCommandBufferBeginInfo command_buffer_begin_info;
  command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_begin_info.pNext = nullptr;
  command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  command_buffer_begin_info.pInheritanceInfo = nullptr;
  auto err = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
  CheckResult(err, "vkBeginCommandBuffer");

  // Begin the render pass.
  // This will setup our framebuffer and begin the pass in the command buffer.
  auto render_state = render_cache_->BeginRenderPass(
      command_buffer, vertex_shader, pixel_shader);
  if (!render_state) {
    return false;
  }

  // Configure the pipeline for drawing.
  // This encodes all render state (blend, depth, etc), our shader stages,
  // and our vertex input layout.
  if (!pipeline_cache_->ConfigurePipeline(command_buffer, render_state,
                                          vertex_shader, pixel_shader,
                                          primitive_type)) {
    render_cache_->EndRenderPass();
    return false;
  }

  // Pass registers to the shaders.
  if (!PopulateConstants(command_buffer, vertex_shader, pixel_shader)) {
    render_cache_->EndRenderPass();
    return false;
  }

  // Upload and bind index buffer data (if we have any).
  if (!PopulateIndexBuffer(command_buffer, index_buffer_info)) {
    render_cache_->EndRenderPass();
    return false;
  }

  // Upload and bind all vertex buffer data.
  if (!PopulateVertexBuffers(command_buffer, vertex_shader)) {
    render_cache_->EndRenderPass();
    return false;
  }

  // Upload and set descriptors for all textures.
  if (!PopulateSamplers(command_buffer, vertex_shader, pixel_shader)) {
    render_cache_->EndRenderPass();
    return false;
  }

  // Actually issue the draw.
  if (!index_buffer_info) {
    // Auto-indexed draw.
    uint32_t instance_count = 1;
    uint32_t first_vertex = 0;
    uint32_t first_instance = 0;
    vkCmdDraw(command_buffer, index_count, instance_count, first_vertex,
              first_instance);
  } else {
    // Index buffer draw.
    uint32_t instance_count = 1;
    uint32_t first_index =
        register_file_->values[XE_GPU_REG_VGT_INDX_OFFSET].u32;
    uint32_t vertex_offset = 0;
    uint32_t first_instance = 0;
    vkCmdDrawIndexed(command_buffer, index_count, instance_count, first_index,
                     vertex_offset, first_instance);
  }

  // End the rendering pass.
  render_cache_->EndRenderPass();

  // TODO(benvanik): bigger batches.
  err = vkEndCommandBuffer(command_buffer);
  CheckResult(err, "vkEndCommandBuffer");
  VkFence fence;
  VkFenceCreateInfo fence_info;
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.pNext = nullptr;
  fence_info.flags = 0;
  vkCreateFence(*device_, &fence_info, nullptr, &fence);
  command_buffer_pool_->EndBatch(fence);
  VkSubmitInfo submit_info;
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.pNext = nullptr;
  submit_info.waitSemaphoreCount = 0;
  submit_info.pWaitSemaphores = nullptr;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;
  submit_info.signalSemaphoreCount = 0;
  submit_info.pSignalSemaphores = nullptr;
  if (queue_mutex_) {
    queue_mutex_->lock();
  }
  err = vkQueueSubmit(queue_, 1, &submit_info, fence);
  if (queue_mutex_) {
    queue_mutex_->unlock();
  }
  CheckResult(err, "vkQueueSubmit");
  if (queue_mutex_) {
    queue_mutex_->lock();
  }
  err = vkQueueWaitIdle(queue_);
  CheckResult(err, "vkQueueWaitIdle");
  err = vkDeviceWaitIdle(*device_);
  CheckResult(err, "vkDeviceWaitIdle");
  if (queue_mutex_) {
    queue_mutex_->unlock();
  }
  while (command_buffer_pool_->has_pending()) {
    command_buffer_pool_->Scavenge();
    xe::threading::MaybeYield();
  }
  vkDestroyFence(*device_, fence, nullptr);

  // TODO(benvanik): move to CP or to host (trace dump, etc).
  if (FLAGS_vulkan_renderdoc_capture_all && device_->is_renderdoc_attached()) {
    device_->EndRenderDocFrameCapture();
  }

  return true;
}

bool VulkanCommandProcessor::PopulateConstants(VkCommandBuffer command_buffer,
                                               VulkanShader* vertex_shader,
                                               VulkanShader* pixel_shader) {
  // Upload the constants the shaders require.
  // These are optional, and if none are defined 0 will be returned.
  auto constant_offsets = buffer_cache_->UploadConstantRegisters(
      vertex_shader->constant_register_map(),
      pixel_shader->constant_register_map());
  if (constant_offsets.first == VK_WHOLE_SIZE ||
      constant_offsets.second == VK_WHOLE_SIZE) {
    // Shader wants constants but we couldn't upload them.
    return false;
  }

  // Configure constant uniform access to point at our offsets.
  auto constant_descriptor_set = buffer_cache_->constant_descriptor_set();
  auto pipeline_layout = pipeline_cache_->pipeline_layout();
  uint32_t set_constant_offsets[2] = {
      static_cast<uint32_t>(constant_offsets.first),
      static_cast<uint32_t>(constant_offsets.second)};
  vkCmdBindDescriptorSets(
      command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
      &constant_descriptor_set,
      static_cast<uint32_t>(xe::countof(set_constant_offsets)),
      set_constant_offsets);

  return true;
}

bool VulkanCommandProcessor::PopulateIndexBuffer(
    VkCommandBuffer command_buffer, IndexBufferInfo* index_buffer_info) {
  auto& regs = *register_file_;
  if (!index_buffer_info || !index_buffer_info->guest_base) {
    // No index buffer or auto draw.
    return true;
  }
  auto& info = *index_buffer_info;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  // Min/max index ranges for clamping. This is often [0g,FFFF|FFFFFF].
  // All indices should be clamped to [min,max]. May be a way to do this in GL.
  uint32_t min_index = regs[XE_GPU_REG_VGT_MIN_VTX_INDX].u32;
  uint32_t max_index = regs[XE_GPU_REG_VGT_MAX_VTX_INDX].u32;
  assert_true(min_index == 0);
  assert_true(max_index == 0xFFFF || max_index == 0xFFFFFF);

  assert_true(info.endianness == Endian::k8in16 ||
              info.endianness == Endian::k8in32);

  trace_writer_.WriteMemoryRead(info.guest_base, info.length);

  // Upload (or get a cached copy of) the buffer.
  const void* source_ptr =
      memory_->TranslatePhysical<const void*>(info.guest_base);
  size_t source_length =
      info.count * (info.format == IndexFormat::kInt32 ? sizeof(uint32_t)
                                                       : sizeof(uint16_t));
  auto buffer_ref =
      buffer_cache_->UploadIndexBuffer(source_ptr, source_length, info.format);
  if (buffer_ref.second == VK_WHOLE_SIZE) {
    // Failed to upload buffer.
    return false;
  }

  // Bind the buffer.
  VkIndexType index_type = info.format == IndexFormat::kInt32
                               ? VK_INDEX_TYPE_UINT32
                               : VK_INDEX_TYPE_UINT16;
  vkCmdBindIndexBuffer(command_buffer, buffer_ref.first, buffer_ref.second,
                       index_type);

  return true;
}

bool VulkanCommandProcessor::PopulateVertexBuffers(
    VkCommandBuffer command_buffer, VulkanShader* vertex_shader) {
  auto& regs = *register_file_;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto& vertex_bindings = vertex_shader->vertex_bindings();
  assert_true(vertex_bindings.size() <= 32);
  VkBuffer all_buffers[32];
  VkDeviceSize all_buffer_offsets[32];
  uint32_t buffer_index = 0;

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
    assert_true(fetch->endian == 2);

    // TODO(benvanik): compute based on indices or vertex count.
    //     THIS CAN BE MASSIVELY INCORRECT (too large).
    size_t valid_range = size_t(fetch->size * 4);

    trace_writer_.WriteMemoryRead(fetch->address << 2, valid_range);

    // Upload (or get a cached copy of) the buffer.
    const void* source_ptr =
        memory_->TranslatePhysical<const void*>(fetch->address << 2);
    size_t source_length = valid_range;
    auto buffer_ref =
        buffer_cache_->UploadVertexBuffer(source_ptr, source_length);
    if (buffer_ref.second == VK_WHOLE_SIZE) {
      // Failed to upload buffer.
      return false;
    }

    // Stash the buffer reference for our bulk bind at the end.
    all_buffers[buffer_index] = buffer_ref.first;
    all_buffer_offsets[buffer_index] = buffer_ref.second;
    ++buffer_index;
  }

  // Bind buffers.
  vkCmdBindVertexBuffers(command_buffer, 0, buffer_index, all_buffers,
                         all_buffer_offsets);

  return true;
}

bool VulkanCommandProcessor::PopulateSamplers(VkCommandBuffer command_buffer,
                                              VulkanShader* vertex_shader,
                                              VulkanShader* pixel_shader) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto descriptor_set = texture_cache_->PrepareTextureSet(
      command_buffer, vertex_shader->texture_bindings(),
      pixel_shader->texture_bindings());
  if (!descriptor_set) {
    // Unable to bind set.
    return false;
  }

  // Bind samplers/textures.
  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_cache_->pipeline_layout(), 1, 1,
                          &descriptor_set, 0, nullptr);

  return true;
}

bool VulkanCommandProcessor::IssueCopy() {
  SCOPE_profile_cpu_f("gpu");
  // TODO(benvanik): resolve.
  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

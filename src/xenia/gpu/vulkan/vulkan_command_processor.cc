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

void VulkanCommandProcessor::RequestFrameTrace(const std::wstring& root_path) {
  // Override traces if renderdoc is attached.
  if (device_->is_renderdoc_attached()) {
    trace_requested_ = true;
    return;
  }

  return CommandProcessor::RequestFrameTrace(root_path);
}

void VulkanCommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();

  auto status = vkQueueWaitIdle(queue_);
  CheckResult(status, "vkQueueWaitIdle");

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
  texture_cache_ = std::make_unique<TextureCache>(memory_, register_file_,
                                                  &trace_writer_, device_);
  pipeline_cache_ = std::make_unique<PipelineCache>(
      register_file_, device_, buffer_cache_->constant_descriptor_set_layout(),
      texture_cache_->texture_descriptor_set_layout());
  render_cache_ = std::make_unique<RenderCache>(register_file_, device_);

  return true;
}

void VulkanCommandProcessor::ShutdownContext() {
  // TODO(benvanik): wait until idle.

  if (swap_state_.front_buffer_texture) {
    // Free swap chain images.
    DestroySwapImages();
  }

  buffer_cache_.reset();
  pipeline_cache_.reset();
  render_cache_.reset();
  texture_cache_.reset();

  // Free all pools. This must come after all of our caches clean up.
  command_buffer_pool_.reset();

  // Release queue, if we were using an acquired one.
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

void VulkanCommandProcessor::CreateSwapImages(VkCommandBuffer setup_buffer,
                                              VkExtent2D extents) {
  VkImageCreateInfo image_info;
  std::memset(&image_info, 0, sizeof(VkImageCreateInfo));
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_info.extent = {extents.width, extents.height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.queueFamilyIndexCount = 0;
  image_info.pQueueFamilyIndices = nullptr;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage image_fb, image_bb;
  auto status = vkCreateImage(*device_, &image_info, nullptr, &image_fb);
  CheckResult(status, "vkCreateImage");

  status = vkCreateImage(*device_, &image_info, nullptr, &image_bb);
  CheckResult(status, "vkCreateImage");

  // Bind memory to images.
  VkMemoryRequirements mem_requirements;
  vkGetImageMemoryRequirements(*device_, image_fb, &mem_requirements);
  fb_memory = device_->AllocateMemory(mem_requirements, 0);
  assert_not_null(fb_memory);

  status = vkBindImageMemory(*device_, image_fb, fb_memory, 0);
  CheckResult(status, "vkBindImageMemory");

  vkGetImageMemoryRequirements(*device_, image_fb, &mem_requirements);
  bb_memory = device_->AllocateMemory(mem_requirements, 0);
  assert_not_null(bb_memory);

  status = vkBindImageMemory(*device_, image_bb, bb_memory, 0);
  CheckResult(status, "vkBindImageMemory");

  std::lock_guard<std::mutex> lock(swap_state_.mutex);
  swap_state_.front_buffer_texture = reinterpret_cast<uintptr_t>(image_fb);
  swap_state_.back_buffer_texture = reinterpret_cast<uintptr_t>(image_bb);

  // Transition both images to general layout.
  VkImageMemoryBarrier barrier;
  std::memset(&barrier, 0, sizeof(VkImageMemoryBarrier));
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = 0;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image_fb;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  vkCmdPipelineBarrier(setup_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  barrier.image = image_bb;

  vkCmdPipelineBarrier(setup_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
}

void VulkanCommandProcessor::DestroySwapImages() {
  std::lock_guard<std::mutex> lock(swap_state_.mutex);
  vkDestroyImage(*device_,
                 reinterpret_cast<VkImage>(swap_state_.front_buffer_texture),
                 nullptr);
  vkDestroyImage(*device_,
                 reinterpret_cast<VkImage>(swap_state_.back_buffer_texture),
                 nullptr);
  vkFreeMemory(*device_, fb_memory, nullptr);
  vkFreeMemory(*device_, bb_memory, nullptr);

  swap_state_.front_buffer_texture = 0;
  swap_state_.back_buffer_texture = 0;
  fb_memory = nullptr;
  bb_memory = nullptr;
}

void VulkanCommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                         uint32_t frontbuffer_width,
                                         uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");

  // Build a final command buffer that copies the game's frontbuffer texture
  // into our backbuffer texture.
  VkCommandBuffer copy_commands = nullptr;
  bool opened_batch;
  if (command_buffer_pool_->has_open_batch()) {
    copy_commands = command_buffer_pool_->AcquireEntry();
    opened_batch = false;
  } else {
    command_buffer_pool_->BeginBatch();
    copy_commands = command_buffer_pool_->AcquireEntry();
    current_batch_fence_.reset(new ui::vulkan::Fence(*device_));
    opened_batch = true;
  }

  VkCommandBufferBeginInfo begin_info;
  std::memset(&begin_info, 0, sizeof(begin_info));
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  auto status = vkBeginCommandBuffer(copy_commands, &begin_info);
  CheckResult(status, "vkBeginCommandBuffer");

  if (!frontbuffer_ptr) {
    // Trace viewer does this.
    frontbuffer_ptr = last_copy_base_;
  }

  if (!swap_state_.back_buffer_texture) {
    CreateSwapImages(copy_commands, {frontbuffer_width, frontbuffer_height});
  }
  auto swap_bb = reinterpret_cast<VkImage>(swap_state_.back_buffer_texture);

  // Issue the commands to copy the game's frontbuffer to our backbuffer.
  auto texture = texture_cache_->LookupAddress(
      frontbuffer_ptr, xe::round_up(frontbuffer_width, 32),
      xe::round_up(frontbuffer_height, 32), TextureFormat::k_8_8_8_8);
  if (texture) {
    texture->in_flight_fence = current_batch_fence_;

    // Insert a barrier so the GPU finishes writing to the image.
    VkImageMemoryBarrier barrier;
    std::memset(&barrier, 0, sizeof(VkImageMemoryBarrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = texture->image_layout;
    barrier.newLayout = texture->image_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(copy_commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    // Now issue a blit command.
    VkImageBlit blit;
    std::memset(&blit, 0, sizeof(VkImageBlit));
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {int32_t(frontbuffer_width),
                          int32_t(frontbuffer_height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {int32_t(frontbuffer_width),
                          int32_t(frontbuffer_height), 1};

    vkCmdBlitImage(copy_commands, texture->image, texture->image_layout,
                   swap_bb, VK_IMAGE_LAYOUT_GENERAL, 1, &blit,
                   VK_FILTER_LINEAR);

    std::lock_guard<std::mutex> lock(swap_state_.mutex);
    swap_state_.width = frontbuffer_width;
    swap_state_.height = frontbuffer_height;
  }

  status = vkEndCommandBuffer(copy_commands);
  CheckResult(status, "vkEndCommandBuffer");

  // Queue up current command buffers.
  // TODO(benvanik): bigger batches.
  std::vector<VkCommandBuffer> submit_buffers;
  if (current_command_buffer_) {
    if (current_render_state_) {
      render_cache_->EndRenderPass();
      current_render_state_ = nullptr;
    }

    status = vkEndCommandBuffer(current_setup_buffer_);
    CheckResult(status, "vkEndCommandBuffer");
    status = vkEndCommandBuffer(current_command_buffer_);
    CheckResult(status, "vkEndCommandBuffer");

    // TODO(DrChat): If the setup buffer is empty, don't bother queueing it up.
    submit_buffers.push_back(current_setup_buffer_);
    submit_buffers.push_back(current_command_buffer_);

    current_command_buffer_ = nullptr;
    current_setup_buffer_ = nullptr;
  }

  submit_buffers.push_back(copy_commands);
  if (!submit_buffers.empty()) {
    // TODO(benvanik): move to CP or to host (trace dump, etc).
    // This only needs to surround a vkQueueSubmit.
    if (queue_mutex_) {
      queue_mutex_->lock();
    }

    VkSubmitInfo submit_info;
    std::memset(&submit_info, 0, sizeof(VkSubmitInfo));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = uint32_t(submit_buffers.size());
    submit_info.pCommandBuffers = submit_buffers.data();
    status = vkQueueSubmit(queue_, 1, &submit_info, *current_batch_fence_);
    CheckResult(status, "vkQueueSubmit");

    if (device_->is_renderdoc_attached() && capturing_) {
      device_->EndRenderDocFrameCapture();
      capturing_ = false;
    }
    if (queue_mutex_) {
      queue_mutex_->unlock();
    }
  }

  command_buffer_pool_->EndBatch(current_batch_fence_);

  // Scavenging.
  {
#if FINE_GRAINED_DRAW_SCOPES
    SCOPE_profile_cpu_i(
        "gpu",
        "xe::gpu::vulkan::VulkanCommandProcessor::PerformSwap Scavenging");
#endif  // FINE_GRAINED_DRAW_SCOPES
    command_buffer_pool_->Scavenge();

    texture_cache_->Scavenge();
    buffer_cache_->Scavenge();
  }

  current_batch_fence_ = nullptr;
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

  // Shaders will have already been defined by previous loads.
  // We need them to do just about anything so validate here.
  auto vertex_shader = static_cast<VulkanShader*>(active_vertex_shader());
  auto pixel_shader = static_cast<VulkanShader*>(active_pixel_shader());
  if (!vertex_shader) {
    // Always need a vertex shader.
    return true;
  }
  // Depth-only mode doesn't need a pixel shader (we'll use a fake one).
  if (enable_mode == ModeControl::kDepth) {
    // Use a dummy pixel shader when required.
    pixel_shader = nullptr;
  } else if (!pixel_shader) {
    // Need a pixel shader in normal color mode.
    return true;
  }

  bool started_command_buffer = false;
  if (!current_command_buffer_) {
    // TODO(benvanik): bigger batches.
    // TODO(DrChat): Decouple setup buffer from current batch.
    command_buffer_pool_->BeginBatch();
    current_command_buffer_ = command_buffer_pool_->AcquireEntry();
    current_setup_buffer_ = command_buffer_pool_->AcquireEntry();
    current_batch_fence_.reset(new ui::vulkan::Fence(*device_));

    VkCommandBufferBeginInfo command_buffer_begin_info;
    command_buffer_begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.pNext = nullptr;
    command_buffer_begin_info.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    command_buffer_begin_info.pInheritanceInfo = nullptr;
    auto status = vkBeginCommandBuffer(current_command_buffer_,
                                       &command_buffer_begin_info);
    CheckResult(status, "vkBeginCommandBuffer");

    status =
        vkBeginCommandBuffer(current_setup_buffer_, &command_buffer_begin_info);
    CheckResult(status, "vkBeginCommandBuffer");

    static uint32_t frame = 0;
    if (device_->is_renderdoc_attached() && !capturing_ &&
        (FLAGS_vulkan_renderdoc_capture_all || trace_requested_)) {
      if (queue_mutex_) {
        queue_mutex_->lock();
      }

      capturing_ = true;
      trace_requested_ = false;
      device_->BeginRenderDocFrameCapture();

      if (queue_mutex_) {
        queue_mutex_->unlock();
      }
    }

    started_command_buffer = true;
  }
  auto command_buffer = current_command_buffer_;
  auto setup_buffer = current_setup_buffer_;

  // Begin the render pass.
  // This will setup our framebuffer and begin the pass in the command buffer.
  // This reuses a previous render pass if one is already open.
  if (render_cache_->dirty() || !current_render_state_) {
    if (current_render_state_) {
      render_cache_->EndRenderPass();
      current_render_state_ = nullptr;
    }

    current_render_state_ = render_cache_->BeginRenderPass(
        command_buffer, vertex_shader, pixel_shader);
    if (!current_render_state_) {
      command_buffer_pool_->CancelBatch();
      current_command_buffer_ = nullptr;
      current_setup_buffer_ = nullptr;
      current_batch_fence_ = nullptr;
      return false;
    }
  }

  // Configure the pipeline for drawing.
  // This encodes all render state (blend, depth, etc), our shader stages,
  // and our vertex input layout.
  VkPipeline pipeline = nullptr;
  auto pipeline_status = pipeline_cache_->ConfigurePipeline(
      command_buffer, current_render_state_, vertex_shader, pixel_shader,
      primitive_type, &pipeline);
  if (pipeline_status == PipelineCache::UpdateStatus::kMismatch ||
      started_command_buffer) {
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline);
  } else if (pipeline_status == PipelineCache::UpdateStatus::kError) {
    render_cache_->EndRenderPass();
    command_buffer_pool_->CancelBatch();
    current_command_buffer_ = nullptr;
    current_setup_buffer_ = nullptr;
    current_batch_fence_ = nullptr;
    current_render_state_ = nullptr;
    return false;
  }
  pipeline_cache_->SetDynamicState(command_buffer, started_command_buffer);

  // Pass registers to the shaders.
  if (!PopulateConstants(command_buffer, vertex_shader, pixel_shader)) {
    render_cache_->EndRenderPass();
    command_buffer_pool_->CancelBatch();
    current_command_buffer_ = nullptr;
    current_setup_buffer_ = nullptr;
    current_batch_fence_ = nullptr;
    current_render_state_ = nullptr;
    return false;
  }

  // Upload and bind index buffer data (if we have any).
  if (!PopulateIndexBuffer(command_buffer, index_buffer_info)) {
    render_cache_->EndRenderPass();
    command_buffer_pool_->CancelBatch();
    current_command_buffer_ = nullptr;
    current_setup_buffer_ = nullptr;
    current_batch_fence_ = nullptr;
    current_render_state_ = nullptr;
    return false;
  }

  // Upload and bind all vertex buffer data.
  if (!PopulateVertexBuffers(command_buffer, vertex_shader)) {
    render_cache_->EndRenderPass();
    command_buffer_pool_->CancelBatch();
    current_command_buffer_ = nullptr;
    current_setup_buffer_ = nullptr;
    current_batch_fence_ = nullptr;
    current_render_state_ = nullptr;
    return false;
  }

  // Bind samplers/textures.
  // Uploads all textures that need it.
  // Setup buffer may be flushed to GPU if the texture cache needs it.
  if (!PopulateSamplers(command_buffer, setup_buffer, vertex_shader,
                        pixel_shader)) {
    render_cache_->EndRenderPass();
    command_buffer_pool_->CancelBatch();
    current_command_buffer_ = nullptr;
    current_setup_buffer_ = nullptr;
    current_batch_fence_ = nullptr;
    current_render_state_ = nullptr;
    return false;
  }

  // Actually issue the draw.
  if (!index_buffer_info) {
    // Auto-indexed draw.
    uint32_t instance_count = 1;
    uint32_t first_vertex =
        register_file_->values[XE_GPU_REG_VGT_INDX_OFFSET].u32;
    uint32_t first_instance = 0;
    vkCmdDraw(command_buffer, index_count, instance_count, first_vertex,
              first_instance);
  } else {
    // Index buffer draw.
    uint32_t instance_count = 1;
    uint32_t first_index = 0;
    uint32_t vertex_offset =
        register_file_->values[XE_GPU_REG_VGT_INDX_OFFSET].u32;
    uint32_t first_instance = 0;
    vkCmdDrawIndexed(command_buffer, index_count, instance_count, first_index,
                     vertex_offset, first_instance);
  }

  return true;
}

bool VulkanCommandProcessor::PopulateConstants(VkCommandBuffer command_buffer,
                                               VulkanShader* vertex_shader,
                                               VulkanShader* pixel_shader) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  xe::gpu::Shader::ConstantRegisterMap dummy_map;
  std::memset(&dummy_map, 0, sizeof(dummy_map));

  // Upload the constants the shaders require.
  // These are optional, and if none are defined 0 will be returned.
  auto constant_offsets = buffer_cache_->UploadConstantRegisters(
      vertex_shader->constant_register_map(),
      pixel_shader ? pixel_shader->constant_register_map() : dummy_map,
      current_batch_fence_);
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
  auto buffer_ref = buffer_cache_->UploadIndexBuffer(
      source_ptr, source_length, info.format, current_batch_fence_);
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
  if (vertex_bindings.empty()) {
    // No bindings.
    return true;
  }

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

    assert_true(fetch->type == 0x3);

    // TODO(benvanik): compute based on indices or vertex count.
    //     THIS CAN BE MASSIVELY INCORRECT (too large).
    size_t valid_range = size_t(fetch->size * 4);

    trace_writer_.WriteMemoryRead(fetch->address << 2, valid_range);

    // Upload (or get a cached copy of) the buffer.
    const void* source_ptr =
        memory_->TranslatePhysical<const void*>(fetch->address << 2);
    size_t source_length = valid_range;
    auto buffer_ref = buffer_cache_->UploadVertexBuffer(
        source_ptr, source_length, static_cast<Endian>(fetch->endian),
        current_batch_fence_);
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
                                              VkCommandBuffer setup_buffer,
                                              VulkanShader* vertex_shader,
                                              VulkanShader* pixel_shader) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  std::vector<xe::gpu::Shader::TextureBinding> dummy_bindings;
  auto descriptor_set = texture_cache_->PrepareTextureSet(
      setup_buffer, current_batch_fence_, vertex_shader->texture_bindings(),
      pixel_shader ? pixel_shader->texture_bindings() : dummy_bindings);
  if (!descriptor_set) {
    // Unable to bind set.
    return false;
  }

  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_cache_->pipeline_layout(), 1, 1,
                          &descriptor_set, 0, nullptr);

  return true;
}

bool VulkanCommandProcessor::IssueCopy() {
  SCOPE_profile_cpu_f("gpu");
  auto& regs = *register_file_;

  // This is used to resolve surfaces, taking them from EDRAM render targets
  // to system memory. It can optionally clear color/depth surfaces, too.
  // The command buffer has stuff for actually doing this by drawing, however
  // we should be able to do it without that much easier.

  uint32_t copy_control = regs[XE_GPU_REG_RB_COPY_CONTROL].u32;
  // Render targets 0-3, 4 = depth
  uint32_t copy_src_select = copy_control & 0x7;
  bool color_clear_enabled = (copy_control >> 8) & 0x1;
  bool depth_clear_enabled = (copy_control >> 9) & 0x1;
  auto copy_command = static_cast<CopyCommand>((copy_control >> 20) & 0x3);

  uint32_t copy_dest_info = regs[XE_GPU_REG_RB_COPY_DEST_INFO].u32;
  auto copy_dest_endian = static_cast<Endian128>(copy_dest_info & 0x7);
  uint32_t copy_dest_array = (copy_dest_info >> 3) & 0x1;
  assert_true(copy_dest_array == 0);
  uint32_t copy_dest_slice = (copy_dest_info >> 4) & 0x7;
  assert_true(copy_dest_slice == 0);
  auto copy_dest_format = ColorFormatToTextureFormat(
      static_cast<ColorFormat>((copy_dest_info >> 7) & 0x3F));
  uint32_t copy_dest_number = (copy_dest_info >> 13) & 0x7;
  // assert_true(copy_dest_number == 0); // ?
  uint32_t copy_dest_bias = (copy_dest_info >> 16) & 0x3F;
  // assert_true(copy_dest_bias == 0);
  uint32_t copy_dest_swap = (copy_dest_info >> 25) & 0x1;

  uint32_t copy_dest_base = regs[XE_GPU_REG_RB_COPY_DEST_BASE].u32;
  uint32_t copy_dest_pitch = regs[XE_GPU_REG_RB_COPY_DEST_PITCH].u32;
  uint32_t copy_dest_height = (copy_dest_pitch >> 16) & 0x3FFF;
  copy_dest_pitch &= 0x3FFF;

  // None of this is supported yet:
  uint32_t copy_surface_slice = regs[XE_GPU_REG_RB_COPY_SURFACE_SLICE].u32;
  assert_true(copy_surface_slice == 0);
  uint32_t copy_func = regs[XE_GPU_REG_RB_COPY_FUNC].u32;
  assert_true(copy_func == 0);
  uint32_t copy_ref = regs[XE_GPU_REG_RB_COPY_REF].u32;
  assert_true(copy_ref == 0);
  uint32_t copy_mask = regs[XE_GPU_REG_RB_COPY_MASK].u32;
  assert_true(copy_mask == 0);

  // Supported in GL4, not supported here yet.
  assert_zero(copy_dest_swap);

  // RB_SURFACE_INFO
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  uint32_t surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t surface_pitch = surface_info & 0x3FFF;
  auto surface_msaa = static_cast<MsaaSamples>((surface_info >> 16) & 0x3);

  // TODO(benvanik): any way to scissor this? a200 has:
  // REG_A2XX_RB_COPY_DEST_OFFSET = A2XX_RB_COPY_DEST_OFFSET_X(tile->xoff) |
  //                                A2XX_RB_COPY_DEST_OFFSET_Y(tile->yoff);
  // but I can't seem to find something similar.
  uint32_t dest_logical_width = copy_dest_pitch;
  uint32_t dest_logical_height = copy_dest_height;
  uint32_t dest_block_width = xe::round_up(dest_logical_width, 32);
  uint32_t dest_block_height = xe::round_up(dest_logical_height, 32);

  uint32_t window_offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32;
  int16_t window_offset_x = window_offset & 0x7FFF;
  int16_t window_offset_y = (window_offset >> 16) & 0x7FFF;
  // Sign-extension
  if (window_offset_x & 0x4000) {
    window_offset_x |= 0x8000;
  }
  if (window_offset_y & 0x4000) {
    window_offset_y |= 0x8000;
  }

  size_t read_size = GetTexelSize(copy_dest_format);

  // Adjust the copy base offset to point to the beginning of the texture, so
  // we don't run into hiccups down the road (e.g. resolving the last part going
  // backwards).
  int32_t dest_offset = window_offset_y * copy_dest_pitch * int(read_size);
  dest_offset += window_offset_x * 32 * int(read_size);
  copy_dest_base += dest_offset;

  // HACK: vertices to use are always in vf0.
  int copy_vertex_fetch_slot = 0;
  int r =
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + (copy_vertex_fetch_slot / 3) * 6;
  const auto group = reinterpret_cast<xe_gpu_fetch_group_t*>(&regs.values[r]);
  const xe_gpu_vertex_fetch_t* fetch = nullptr;
  switch (copy_vertex_fetch_slot % 3) {
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
  assert_true(fetch->type == 3);
  assert_true(fetch->endian == 2);
  assert_true(fetch->size == 6);
  const uint8_t* vertex_addr = memory_->TranslatePhysical(fetch->address << 2);
  trace_writer_.WriteMemoryRead(fetch->address << 2, fetch->size * 4);
  int32_t dest_min_x = int32_t((std::min(
      std::min(
          GpuSwap(xe::load<float>(vertex_addr + 0), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 8), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 16), Endian(fetch->endian)))));
  int32_t dest_max_x = int32_t((std::max(
      std::max(
          GpuSwap(xe::load<float>(vertex_addr + 0), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 8), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 16), Endian(fetch->endian)))));
  int32_t dest_min_y = int32_t((std::min(
      std::min(
          GpuSwap(xe::load<float>(vertex_addr + 4), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 12), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 20), Endian(fetch->endian)))));
  int32_t dest_max_y = int32_t((std::max(
      std::max(
          GpuSwap(xe::load<float>(vertex_addr + 4), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 12), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 20), Endian(fetch->endian)))));

  uint32_t color_edram_base = 0;
  uint32_t depth_edram_base = 0;
  ColorRenderTargetFormat color_format;
  DepthRenderTargetFormat depth_format;
  if (copy_src_select <= 3) {
    // Source from a color target.
    uint32_t color_info[4] = {
        regs[XE_GPU_REG_RB_COLOR_INFO].u32, regs[XE_GPU_REG_RB_COLOR1_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR2_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR3_INFO].u32,
    };
    color_edram_base = color_info[copy_src_select] & 0xFFF;

    color_format = static_cast<ColorRenderTargetFormat>(
        (color_info[copy_src_select] >> 16) & 0xF);
  }

  if (copy_src_select > 3 || depth_clear_enabled) {
    // Source from a depth target.
    uint32_t depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
    depth_edram_base = depth_info & 0xFFF;

    depth_format =
        static_cast<DepthRenderTargetFormat>((depth_info >> 16) & 0x1);
    if (!depth_clear_enabled) {
      copy_dest_format = TextureFormat::k_24_8;
    }
  }

  // Demand a resolve texture from the texture cache.
  TextureInfo tex_info = {};
  tex_info.guest_address = copy_dest_base;
  tex_info.width = dest_logical_width - 1;
  tex_info.height = dest_logical_height - 1;
  tex_info.dimension = gpu::Dimension::k2D;
  tex_info.input_length = copy_dest_pitch * copy_dest_height * 4;
  tex_info.format_info = FormatInfo::Get(uint32_t(copy_dest_format));
  tex_info.size_2d.logical_width = dest_logical_width;
  tex_info.size_2d.logical_height = dest_logical_height;
  tex_info.size_2d.block_width = dest_block_width;
  tex_info.size_2d.block_height = dest_block_height;
  tex_info.size_2d.input_width = dest_block_width;
  tex_info.size_2d.input_height = dest_block_height;
  tex_info.size_2d.input_pitch = copy_dest_pitch * 4;
  auto texture =
      texture_cache_->DemandResolveTexture(tex_info, copy_dest_format, nullptr);
  assert_not_null(texture);
  texture->in_flight_fence = current_batch_fence_;

  // For debugging purposes only (trace viewer)
  last_copy_base_ = texture->texture_info.guest_address;

  if (!current_command_buffer_) {
    command_buffer_pool_->BeginBatch();
    current_command_buffer_ = command_buffer_pool_->AcquireEntry();
    current_setup_buffer_ = command_buffer_pool_->AcquireEntry();
    current_batch_fence_.reset(new ui::vulkan::Fence(*device_));

    VkCommandBufferBeginInfo command_buffer_begin_info;
    command_buffer_begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.pNext = nullptr;
    command_buffer_begin_info.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    command_buffer_begin_info.pInheritanceInfo = nullptr;
    auto status = vkBeginCommandBuffer(current_command_buffer_,
                                       &command_buffer_begin_info);
    CheckResult(status, "vkBeginCommandBuffer");

    status =
        vkBeginCommandBuffer(current_setup_buffer_, &command_buffer_begin_info);
    CheckResult(status, "vkBeginCommandBuffer");
  } else if (current_render_state_) {
    render_cache_->EndRenderPass();
    current_render_state_ = nullptr;
  }
  auto command_buffer = current_command_buffer_;

  if (texture->image_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
    // Transition the image to a general layout.
    VkImageMemoryBarrier image_barrier;
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.pNext = nullptr;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.srcAccessMask = 0;
    image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.image = texture->image;
    image_barrier.subresourceRange = {0, 0, 1, 0, 1};
    image_barrier.subresourceRange.aspectMask =
        copy_src_select <= 3
            ? VK_IMAGE_ASPECT_COLOR_BIT
            : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    texture->image_layout = VK_IMAGE_LAYOUT_GENERAL;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &image_barrier);
  }

  VkOffset3D resolve_offset = {dest_min_x, dest_min_y, 0};
  VkExtent3D resolve_extent = {uint32_t(dest_max_x - dest_min_x),
                               uint32_t(dest_max_y - dest_min_y), 1};

  // Ask the render cache to copy to the resolve texture.
  auto edram_base = copy_src_select <= 3 ? color_edram_base : depth_edram_base;
  uint32_t src_format = copy_src_select <= 3
                            ? static_cast<uint32_t>(color_format)
                            : static_cast<uint32_t>(depth_format);
  VkFilter filter = copy_src_select <= 3 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  switch (copy_command) {
    case CopyCommand::kRaw:
    /*
      render_cache_->RawCopyToImage(command_buffer, edram_base, texture->image,
                                    texture->image_layout, copy_src_select <= 3,
                                    resolve_offset, resolve_extent);
      break;
    */
    case CopyCommand::kConvert:
      render_cache_->BlitToImage(command_buffer, edram_base, surface_pitch,
                                 resolve_extent.height, surface_msaa,
                                 texture->image, texture->image_layout,
                                 copy_src_select <= 3, src_format, filter,
                                 resolve_offset, resolve_extent);
      break;

    case CopyCommand::kConstantOne:
    case CopyCommand::kNull:
      assert_always();
      break;
  }

  // Perform any requested clears.
  uint32_t copy_depth_clear = regs[XE_GPU_REG_RB_DEPTH_CLEAR].u32;
  uint32_t copy_color_clear = regs[XE_GPU_REG_RB_COLOR_CLEAR].u32;
  uint32_t copy_color_clear_low = regs[XE_GPU_REG_RB_COLOR_CLEAR_LOW].u32;
  assert_true(copy_color_clear == copy_color_clear_low);

  if (color_clear_enabled) {
    // If color clear is enabled, we can only clear a selected color target!
    assert_true(copy_src_select <= 3);

    // TODO(benvanik): verify color order.
    float color[] = {((copy_color_clear >> 0) & 0xFF) / 255.0f,
                     ((copy_color_clear >> 8) & 0xFF) / 255.0f,
                     ((copy_color_clear >> 16) & 0xFF) / 255.0f,
                     ((copy_color_clear >> 24) & 0xFF) / 255.0f};

    // TODO(DrChat): Do we know the surface height at this point?
    render_cache_->ClearEDRAMColor(command_buffer, color_edram_base,
                                   color_format, surface_pitch,
                                   resolve_extent.height, surface_msaa, color);
  }

  if (depth_clear_enabled) {
    float depth =
        (copy_depth_clear & 0xFFFFFF00) / static_cast<float>(0xFFFFFF00);
    uint8_t stencil = copy_depth_clear & 0xFF;

    // TODO(DrChat): Do we know the surface height at this point?
    render_cache_->ClearEDRAMDepthStencil(
        command_buffer, depth_edram_base, depth_format, surface_pitch,
        resolve_extent.height, surface_msaa, depth, stencil);
  }

  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

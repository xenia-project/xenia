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
#include "xenia/gpu/registers.h"
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
  cache_clear_requested_ = true;
}

bool VulkanCommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Unable to initialize base command processor context");
    return false;
  }

  // Acquire our device and queue.
  auto context = static_cast<xe::ui::vulkan::VulkanContext*>(context_.get());
  device_ = context->device();
  queue_ = device_->AcquireQueue(device_->queue_family_index());
  if (!queue_) {
    // Need to reuse primary queue (with locks).
    queue_ = device_->primary_queue();
    queue_mutex_ = &device_->primary_queue_mutex();
  }

  VkResult status = VK_SUCCESS;

  // Setup a blitter.
  blitter_ = std::make_unique<ui::vulkan::Blitter>();
  status = blitter_->Initialize(device_);
  if (status != VK_SUCCESS) {
    XELOGE("Unable to initialize blitter");
    blitter_->Shutdown();
    return false;
  }

  // Setup fenced pools used for all our per-frame/per-draw resources.
  command_buffer_pool_ = std::make_unique<ui::vulkan::CommandBufferPool>(
      *device_, device_->queue_family_index());

  // Initialize the state machine caches.
  buffer_cache_ = std::make_unique<BufferCache>(
      register_file_, memory_, device_, kDefaultBufferCacheCapacity);
  status = buffer_cache_->Initialize();
  if (status != VK_SUCCESS) {
    XELOGE("Unable to initialize buffer cache");
    buffer_cache_->Shutdown();
    return false;
  }

  texture_cache_ = std::make_unique<TextureCache>(memory_, register_file_,
                                                  &trace_writer_, device_);
  status = texture_cache_->Initialize();
  if (status != VK_SUCCESS) {
    XELOGE("Unable to initialize texture cache");
    texture_cache_->Shutdown();
    return false;
  }

  pipeline_cache_ = std::make_unique<PipelineCache>(register_file_, device_);
  status = pipeline_cache_->Initialize(
      buffer_cache_->constant_descriptor_set_layout(),
      texture_cache_->texture_descriptor_set_layout(),
      buffer_cache_->vertex_descriptor_set_layout());
  if (status != VK_SUCCESS) {
    XELOGE("Unable to initialize pipeline cache");
    pipeline_cache_->Shutdown();
    return false;
  }

  render_cache_ = std::make_unique<RenderCache>(register_file_, device_);
  status = render_cache_->Initialize();
  if (status != VK_SUCCESS) {
    XELOGE("Unable to initialize render cache");
    render_cache_->Shutdown();
    return false;
  }

  return true;
}

void VulkanCommandProcessor::ShutdownContext() {
  // TODO(benvanik): wait until idle.

  if (swap_state_.front_buffer_texture) {
    // Free swap chain image.
    DestroySwapImage();
  }

  buffer_cache_.reset();
  pipeline_cache_.reset();
  render_cache_.reset();
  texture_cache_.reset();

  blitter_.reset();

  // Free all pools. This must come after all of our caches clean up.
  command_buffer_pool_.reset();

  // Release queue, if we were using an acquired one.
  if (!queue_mutex_) {
    device_->ReleaseQueue(queue_, device_->queue_family_index());
    queue_ = nullptr;
  }

  CommandProcessor::ShutdownContext();
}

void VulkanCommandProcessor::MakeCoherent() {
  RegisterFile* regs = register_file_;
  auto status_host = regs->values[XE_GPU_REG_COHER_STATUS_HOST].u32;

  CommandProcessor::MakeCoherent();

  // Make region coherent
  if (status_host & 0x80000000ul) {
    // TODO(benvanik): less-fine-grained clearing.
    buffer_cache_->InvalidateCache();

    if ((status_host & 0x01000000) != 0 && (status_host & 0x02000000) == 0) {
      coher_base_vc_ = regs->values[XE_GPU_REG_COHER_BASE_HOST].u32;
      coher_size_vc_ = regs->values[XE_GPU_REG_COHER_SIZE_HOST].u32;
    }
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

void VulkanCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  CommandProcessor::WriteRegister(index, value);

  if (index >= XE_GPU_REG_SHADER_CONSTANT_000_X &&
      index <= XE_GPU_REG_SHADER_CONSTANT_511_W) {
    uint32_t offset = index - XE_GPU_REG_SHADER_CONSTANT_000_X;
    offset /= 4 * 4;
    offset ^= 0x3F;

    dirty_float_constants_ |= (1ull << offset);
  } else if (index >= XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 &&
             index <= XE_GPU_REG_SHADER_CONSTANT_BOOL_224_255) {
    uint32_t offset = index - XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031;
    offset ^= 0x7;

    dirty_bool_constants_ |= (1 << offset);
  } else if (index >= XE_GPU_REG_SHADER_CONSTANT_LOOP_00 &&
             index <= XE_GPU_REG_SHADER_CONSTANT_LOOP_31) {
    uint32_t offset = index - XE_GPU_REG_SHADER_CONSTANT_LOOP_00;
    offset ^= 0x1F;

    dirty_loop_constants_ |= (1 << offset);
  } else if (index == XE_GPU_REG_DC_LUT_PWL_DATA) {
    UpdateGammaRampValue(GammaRampType::kPWL, value);
  } else if (index == XE_GPU_REG_DC_LUT_30_COLOR) {
    UpdateGammaRampValue(GammaRampType::kNormal, value);
  } else if (index >= XE_GPU_REG_DC_LUT_RW_MODE &&
             index <= XE_GPU_REG_DC_LUTA_CONTROL) {
    uint32_t offset = index - XE_GPU_REG_DC_LUT_RW_MODE;
    offset ^= 0x05;

    dirty_gamma_constants_ |= (1 << offset);

    if (index == XE_GPU_REG_DC_LUT_RW_INDEX) {
      gamma_ramp_rw_subindex_ = 0;
    }
  }
}

void VulkanCommandProcessor::CreateSwapImage(VkCommandBuffer setup_buffer,
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
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.queueFamilyIndexCount = 0;
  image_info.pQueueFamilyIndices = nullptr;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage image_fb;
  auto status = vkCreateImage(*device_, &image_info, nullptr, &image_fb);
  CheckResult(status, "vkCreateImage");

  // Bind memory to image.
  VkMemoryRequirements mem_requirements;
  vkGetImageMemoryRequirements(*device_, image_fb, &mem_requirements);
  fb_memory_ = device_->AllocateMemory(mem_requirements, 0);
  assert_not_null(fb_memory_);

  status = vkBindImageMemory(*device_, image_fb, fb_memory_, 0);
  CheckResult(status, "vkBindImageMemory");

  std::lock_guard<std::mutex> lock(swap_state_.mutex);
  swap_state_.front_buffer_texture = reinterpret_cast<uintptr_t>(image_fb);

  VkImageViewCreateInfo view_create_info = {
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      nullptr,
      0,
      image_fb,
      VK_IMAGE_VIEW_TYPE_2D,
      VK_FORMAT_R8G8B8A8_UNORM,
      {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
       VK_COMPONENT_SWIZZLE_A},
      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
  };
  status =
      vkCreateImageView(*device_, &view_create_info, nullptr, &fb_image_view_);
  CheckResult(status, "vkCreateImageView");

  VkFramebufferCreateInfo framebuffer_create_info = {
      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      nullptr,
      0,
      blitter_->GetRenderPass(VK_FORMAT_R8G8B8A8_UNORM, true),
      1,
      &fb_image_view_,
      extents.width,
      extents.height,
      1,
  };
  status = vkCreateFramebuffer(*device_, &framebuffer_create_info, nullptr,
                               &fb_framebuffer_);
  CheckResult(status, "vkCreateFramebuffer");

  // Transition image to general layout.
  VkImageMemoryBarrier barrier;
  std::memset(&barrier, 0, sizeof(VkImageMemoryBarrier));
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image_fb;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  vkCmdPipelineBarrier(setup_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);
}

void VulkanCommandProcessor::DestroySwapImage() {
  vkDestroyFramebuffer(*device_, fb_framebuffer_, nullptr);
  vkDestroyImageView(*device_, fb_image_view_, nullptr);

  std::lock_guard<std::mutex> lock(swap_state_.mutex);
  vkDestroyImage(*device_,
                 reinterpret_cast<VkImage>(swap_state_.front_buffer_texture),
                 nullptr);
  vkFreeMemory(*device_, fb_memory_, nullptr);

  swap_state_.front_buffer_texture = 0;
  fb_memory_ = nullptr;
  fb_framebuffer_ = nullptr;
  fb_image_view_ = nullptr;
}

void VulkanCommandProcessor::BeginFrame() {
  assert_false(frame_open_);

  // TODO(benvanik): bigger batches.
  // TODO(DrChat): Decouple setup buffer from current batch.
  // Begin a new batch, and allocate and begin a command buffer and setup
  // buffer.
  current_batch_fence_ = command_buffer_pool_->BeginBatch();
  current_command_buffer_ = command_buffer_pool_->AcquireEntry();
  current_setup_buffer_ = command_buffer_pool_->AcquireEntry();

  VkCommandBufferBeginInfo command_buffer_begin_info;
  command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_begin_info.pNext = nullptr;
  command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  command_buffer_begin_info.pInheritanceInfo = nullptr;
  auto status =
      vkBeginCommandBuffer(current_command_buffer_, &command_buffer_begin_info);
  CheckResult(status, "vkBeginCommandBuffer");

  status =
      vkBeginCommandBuffer(current_setup_buffer_, &command_buffer_begin_info);
  CheckResult(status, "vkBeginCommandBuffer");

  // Flag renderdoc down to start a capture if requested.
  // The capture will end when these commands are submitted to the queue.
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

  frame_open_ = true;
}

void VulkanCommandProcessor::EndFrame() {
  if (current_render_state_) {
    render_cache_->EndRenderPass();
    current_render_state_ = nullptr;
  }

  VkResult status = VK_SUCCESS;
  status = vkEndCommandBuffer(current_setup_buffer_);
  CheckResult(status, "vkEndCommandBuffer");
  status = vkEndCommandBuffer(current_command_buffer_);
  CheckResult(status, "vkEndCommandBuffer");

  current_command_buffer_ = nullptr;
  current_setup_buffer_ = nullptr;
  command_buffer_pool_->EndBatch();

  frame_open_ = false;
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
    current_batch_fence_ = command_buffer_pool_->BeginBatch();
    copy_commands = command_buffer_pool_->AcquireEntry();
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

  if (!swap_state_.front_buffer_texture) {
    CreateSwapImage(copy_commands, {frontbuffer_width, frontbuffer_height});
  }
  auto swap_fb = reinterpret_cast<VkImage>(swap_state_.front_buffer_texture);

  auto& regs = *register_file_;
  int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
  auto group =
      reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(&regs.values[r]);
  auto& fetch = group->texture_fetch;

  TextureInfo texture_info;
  if (!TextureInfo::Prepare(group->texture_fetch, &texture_info)) {
    assert_always();
  }

  // Issue the commands to copy the game's frontbuffer to our backbuffer.
  auto texture = texture_cache_->Lookup(texture_info);
  if (texture) {
    texture->in_flight_fence = current_batch_fence_;

    // Insert a barrier so the GPU finishes writing to the image.
    VkImageMemoryBarrier barrier;
    std::memset(&barrier, 0, sizeof(VkImageMemoryBarrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = texture->image_layout;
    barrier.newLayout = texture->image_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(copy_commands,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);

    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.image = swap_fb;
    vkCmdPipelineBarrier(copy_commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    // Part of the source image that we want to blit from.
    VkRect2D src_rect = {
        {0, 0},
        {texture->texture_info.width + 1, texture->texture_info.height + 1},
    };
    VkRect2D dst_rect = {{0, 0}, {frontbuffer_width, frontbuffer_height}};

    VkViewport viewport = {
        0.f, 0.f, float(frontbuffer_width), float(frontbuffer_height),
        0.f, 1.f};

    VkRect2D scissor = {{0, 0}, {frontbuffer_width, frontbuffer_height}};

    blitter_->BlitTexture2D(
        copy_commands, current_batch_fence_,
        texture_cache_->DemandView(texture, 0x688)->view, src_rect,
        {texture->texture_info.width + 1, texture->texture_info.height + 1},
        VK_FORMAT_R8G8B8A8_UNORM, dst_rect,
        {frontbuffer_width, frontbuffer_height}, fb_framebuffer_, viewport,
        scissor, VK_FILTER_LINEAR, true, true);

    std::swap(barrier.oldLayout, barrier.newLayout);
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(
        copy_commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    std::lock_guard<std::mutex> lock(swap_state_.mutex);
    swap_state_.width = frontbuffer_width;
    swap_state_.height = frontbuffer_height;
  }

  status = vkEndCommandBuffer(copy_commands);
  CheckResult(status, "vkEndCommandBuffer");

  // Queue up current command buffers.
  // TODO(benvanik): bigger batches.
  std::vector<VkCommandBuffer> submit_buffers;
  if (frame_open_) {
    // TODO(DrChat): If the setup buffer is empty, don't bother queueing it up.
    submit_buffers.push_back(current_setup_buffer_);
    submit_buffers.push_back(current_command_buffer_);
    EndFrame();
  }

  if (opened_batch) {
    command_buffer_pool_->EndBatch();
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

    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = nullptr;

    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = nullptr;

    status = vkQueueSubmit(queue_, 1, &submit_info, current_batch_fence_);
    if (device_->is_renderdoc_attached() && capturing_) {
      device_->EndRenderDocFrameCapture();
      capturing_ = false;
    }
    if (queue_mutex_) {
      queue_mutex_->unlock();
    }
  }

  vkWaitForFences(*device_, 1, &current_batch_fence_, VK_TRUE, -1);
  if (cache_clear_requested_) {
    cache_clear_requested_ = false;

    buffer_cache_->ClearCache();
    pipeline_cache_->ClearCache();
    render_cache_->ClearCache();
    texture_cache_->ClearCache();
  }

  // Scavenging.
  {
#if FINE_GRAINED_DRAW_SCOPES
    SCOPE_profile_cpu_i(
        "gpu",
        "xe::gpu::vulkan::VulkanCommandProcessor::PerformSwap Scavenging");
#endif  // FINE_GRAINED_DRAW_SCOPES
    // Command buffers must be scavenged first to avoid a race condition.
    // We don't want to reuse a batch when the caches haven't yet cleared old
    // resources!
    command_buffer_pool_->Scavenge();

    blitter_->Scavenge();
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
    return false;
  }
  // Depth-only mode doesn't need a pixel shader (we'll use a fake one).
  if (enable_mode == ModeControl::kDepth) {
    // Use a dummy pixel shader when required.
    pixel_shader = nullptr;
  } else if (!pixel_shader) {
    // Need a pixel shader in normal color mode.
    return true;
  }

  bool full_update = false;
  if (!frame_open_) {
    BeginFrame();
    full_update = true;
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

    full_update = true;
    current_render_state_ = render_cache_->BeginRenderPass(
        command_buffer, vertex_shader, pixel_shader);
    if (!current_render_state_) {
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
  if (pipeline_status == PipelineCache::UpdateStatus::kError) {
    return false;
  } else if (pipeline_status == PipelineCache::UpdateStatus::kMismatch ||
             full_update) {
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline);
  }
  pipeline_cache_->SetDynamicState(command_buffer, full_update);

  // Pass registers to the shaders.
  if (!PopulateConstants(command_buffer, vertex_shader, pixel_shader)) {
    return false;
  }

  // Upload and bind index buffer data (if we have any).
  if (!PopulateIndexBuffer(command_buffer, index_buffer_info)) {
    return false;
  }

  // Upload and bind all vertex buffer data.
  if (!PopulateVertexBuffers(command_buffer, setup_buffer, vertex_shader)) {
    return false;
  }

  // Bind samplers/textures.
  // Uploads all textures that need it.
  // Setup buffer may be flushed to GPU if the texture cache needs it.
  if (!PopulateSamplers(command_buffer, setup_buffer, vertex_shader,
                        pixel_shader)) {
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
      current_setup_buffer_, vertex_shader->constant_register_map(),
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
  uint32_t source_addr = info.guest_base;
  uint32_t source_length =
      info.count * (info.format == IndexFormat::kInt32 ? sizeof(uint32_t)
                                                       : sizeof(uint16_t));
  auto buffer_ref = buffer_cache_->UploadIndexBuffer(
      current_setup_buffer_, source_addr, source_length, info.format,
      current_batch_fence_);
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
    VkCommandBuffer command_buffer, VkCommandBuffer setup_buffer,
    VulkanShader* vertex_shader) {
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
  auto descriptor_set = buffer_cache_->PrepareVertexSet(
      setup_buffer, current_batch_fence_, vertex_bindings);
  if (!descriptor_set) {
    XELOGW("Failed to prepare vertex set!");
    return false;
  }

  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_cache_->pipeline_layout(), 2, 1,
                          &descriptor_set, 0, nullptr);
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
    XELOGW("Failed to prepare texture set!");
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

  struct {
    reg::RB_COPY_CONTROL copy_control;
    uint32_t copy_dest_base;
    reg::RB_COPY_DEST_PITCH copy_dest_pitch;
    reg::RB_COPY_DEST_INFO copy_dest_info;
    uint32_t tile_clear;
    uint32_t depth_clear;
    uint32_t color_clear;
    uint32_t color_clear_low;
    uint32_t copy_func;
    uint32_t copy_ref;
    uint32_t copy_mask;
    uint32_t copy_surface_slice;
  }* copy_regs = reinterpret_cast<decltype(copy_regs)>(
      &regs[XE_GPU_REG_RB_COPY_CONTROL].u32);

  struct {
    reg::PA_SC_WINDOW_OFFSET window_offset;
    reg::PA_SC_WINDOW_SCISSOR_TL window_scissor_tl;
    reg::PA_SC_WINDOW_SCISSOR_BR window_scissor_br;
  }* window_regs = reinterpret_cast<decltype(window_regs)>(
      &regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32);

  // True if the source tile is a color target
  bool is_color_source = copy_regs->copy_control.copy_src_select <= 3;

  // Render targets 0-3, 4 = depth
  uint32_t copy_src_select = copy_regs->copy_control.copy_src_select;
  bool color_clear_enabled = copy_regs->copy_control.color_clear_enable != 0;
  bool depth_clear_enabled = copy_regs->copy_control.depth_clear_enable != 0;
  CopyCommand copy_command = copy_regs->copy_control.copy_command;

  assert_true(copy_regs->copy_dest_info.copy_dest_array == 0);
  assert_true(copy_regs->copy_dest_info.copy_dest_slice == 0);
  auto copy_dest_format =
      ColorFormatToTextureFormat(copy_regs->copy_dest_info.copy_dest_format);
  // TODO: copy dest number / bias

  uint32_t copy_dest_base = copy_regs->copy_dest_base;
  uint32_t copy_dest_pitch = copy_regs->copy_dest_pitch.copy_dest_pitch;
  uint32_t copy_dest_height = copy_regs->copy_dest_pitch.copy_dest_height;

  // None of this is supported yet:
  assert_true(copy_regs->copy_surface_slice == 0);
  assert_true(copy_regs->copy_func == 0);
  assert_true(copy_regs->copy_ref == 0);
  assert_true(copy_regs->copy_mask == 0);

  // RB_SURFACE_INFO
  // https://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  uint32_t surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t surface_pitch = surface_info & 0x3FFF;
  auto surface_msaa = static_cast<MsaaSamples>((surface_info >> 16) & 0x3);

  // TODO(benvanik): any way to scissor this? a200 has:
  // REG_A2XX_RB_COPY_DEST_OFFSET = A2XX_RB_COPY_DEST_OFFSET_X(tile->xoff) |
  //                                A2XX_RB_COPY_DEST_OFFSET_Y(tile->yoff);
  // but I can't seem to find something similar.
  uint32_t dest_logical_width = copy_dest_pitch;
  uint32_t dest_logical_height = copy_dest_height;

  // vtx_window_offset_enable
  assert_true(regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32 & 0x00010000);
  uint32_t window_offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32;
  int32_t window_offset_x = window_regs->window_offset.window_x_offset;
  int32_t window_offset_y = window_regs->window_offset.window_y_offset;

  uint32_t dest_texel_size = uint32_t(GetTexelSize(copy_dest_format));

  // Adjust the copy base offset to point to the beginning of the texture, so
  // we don't run into hiccups down the road (e.g. resolving the last part going
  // backwards).
  int32_t dest_offset =
      window_offset_y * copy_dest_pitch * int(dest_texel_size);
  dest_offset += window_offset_x * 32 * int(dest_texel_size);
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

  // Most vertices have a negative half pixel offset applied, which we reverse.
  auto& vtx_cntl = *(reg::PA_SU_VTX_CNTL*)&regs[XE_GPU_REG_PA_SU_VTX_CNTL].u32;
  float vtx_offset = vtx_cntl.pix_center == 0 ? 0.5f : 0.f;

  float dest_points[6];
  for (int i = 0; i < 6; i++) {
    dest_points[i] =
        GpuSwap(xe::load<float>(vertex_addr + i * 4), Endian(fetch->endian)) +
        vtx_offset;
  }

  // Note: The xenos only supports rectangle copies (luckily)
  int32_t dest_min_x = int32_t(
      (std::min(std::min(dest_points[0], dest_points[2]), dest_points[4])));
  int32_t dest_max_x = int32_t(
      (std::max(std::max(dest_points[0], dest_points[2]), dest_points[4])));

  int32_t dest_min_y = int32_t(
      (std::min(std::min(dest_points[1], dest_points[3]), dest_points[5])));
  int32_t dest_max_y = int32_t(
      (std::max(std::max(dest_points[1], dest_points[3]), dest_points[5])));

  VkOffset2D resolve_offset = {dest_min_x, dest_min_y};
  VkExtent2D resolve_extent = {uint32_t(dest_max_x - dest_min_x),
                               uint32_t(dest_max_y - dest_min_y)};

  uint32_t color_edram_base = 0;
  uint32_t depth_edram_base = 0;
  ColorRenderTargetFormat color_format;
  DepthRenderTargetFormat depth_format;
  if (is_color_source) {
    // Source from a color target.
    reg::RB_COLOR_INFO color_info[4] = {
        regs[XE_GPU_REG_RB_COLOR_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR1_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR2_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR3_INFO].u32,
    };
    color_edram_base = color_info[copy_src_select].color_base;
    color_format = color_info[copy_src_select].color_format;
    assert_true(color_info[copy_src_select].color_exp_bias == 0);
  }

  if (!is_color_source || depth_clear_enabled) {
    // Source from or clear a depth target.
    reg::RB_DEPTH_INFO depth_info = {regs[XE_GPU_REG_RB_DEPTH_INFO].u32};
    depth_edram_base = depth_info.depth_base;
    depth_format = depth_info.depth_format;
    if (!is_color_source) {
      copy_dest_format = DepthRenderTargetToTextureFormat(depth_format);
    }
  }

  Endian resolve_endian = Endian::k8in32;
  if (copy_regs->copy_dest_info.copy_dest_endian <= Endian128::k16in32) {
    resolve_endian =
        static_cast<Endian>(copy_regs->copy_dest_info.copy_dest_endian.value());
  }

  // Demand a resolve texture from the texture cache.
  TextureInfo texture_info;
  TextureInfo::PrepareResolve(
      copy_dest_base, copy_dest_format, resolve_endian, copy_dest_pitch,
      dest_logical_width, std::max(1u, dest_logical_height), 1, &texture_info);

  auto texture = texture_cache_->DemandResolveTexture(texture_info);
  if (!texture) {
    // Out of memory.
    XELOGD("Failed to demand resolve texture!");
    return false;
  }

  if (!(texture->usage_flags & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))) {
    // Resolve image doesn't support drawing, and we don't support conversion.
    return false;
  }

  texture->in_flight_fence = current_batch_fence_;

  // For debugging purposes only (trace viewer)
  last_copy_base_ = texture->texture_info.memory.base_address;

  if (!frame_open_) {
    BeginFrame();
  } else if (current_render_state_) {
    // Copy commands cannot be issued within a render pass.
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
    image_barrier.dstAccessMask = 0;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.image = texture->image;
    image_barrier.subresourceRange = {0, 0, 1, 0, 1};
    image_barrier.subresourceRange.aspectMask =
        is_color_source
            ? VK_IMAGE_ASPECT_COLOR_BIT
            : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    texture->image_layout = VK_IMAGE_LAYOUT_GENERAL;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &image_barrier);
  }

  // Transition the image into a transfer destination layout, if needed.
  // TODO: If blitting, layout should be color attachment.
  VkImageMemoryBarrier image_barrier;
  image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_barrier.pNext = nullptr;
  image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.srcAccessMask = 0;
  image_barrier.dstAccessMask =
      is_color_source ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  image_barrier.oldLayout = texture->image_layout;
  image_barrier.newLayout =
      is_color_source ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                      : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  image_barrier.image = texture->image;
  image_barrier.subresourceRange = {0, 0, 1, 0, 1};
  image_barrier.subresourceRange.aspectMask =
      is_color_source ? VK_IMAGE_ASPECT_COLOR_BIT
                      : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &image_barrier);

  // Ask the render cache to copy to the resolve texture.
  auto edram_base = is_color_source ? color_edram_base : depth_edram_base;
  uint32_t src_format = is_color_source ? static_cast<uint32_t>(color_format)
                                        : static_cast<uint32_t>(depth_format);
  VkFilter filter = is_color_source ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

  XELOGGPU("Resolve RT %.8X %.8X(%d) -> 0x%.8X (%dx%d, format: %s)", edram_base,
           surface_pitch, surface_pitch, copy_dest_base, copy_dest_pitch,
           copy_dest_height, texture_info.format_info()->name);
  switch (copy_command) {
    case CopyCommand::kRaw:
      /*
        render_cache_->RawCopyToImage(command_buffer, edram_base,
        texture->image, texture->image_layout, is_color_source, resolve_offset,
        resolve_extent); break;
      */

    case CopyCommand::kConvert: {
      /*
      if (!is_color_source && copy_regs->copy_dest_info.copy_dest_swap == 0) {
        // Depth images are a bit more complicated. Try a blit!
        render_cache_->BlitToImage(
            command_buffer, edram_base, surface_pitch, resolve_extent.height,
            surface_msaa, texture->image, texture->image_layout,
            is_color_source, src_format, filter,
            {resolve_offset.x, resolve_offset.y, 0},
            {resolve_extent.width, resolve_extent.height, 1});
        break;
      }
      */

      // Blit with blitter.
      auto view = render_cache_->FindTileView(
          edram_base, surface_pitch, surface_msaa, is_color_source, src_format);
      if (!view) {
        XELOGGPU("Failed to find tile view!");
        break;
      }

      // Convert the tile view to a sampled image.
      // Put a barrier on the tile view.
      VkImageMemoryBarrier tile_image_barrier;
      tile_image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      tile_image_barrier.pNext = nullptr;
      tile_image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      tile_image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      tile_image_barrier.srcAccessMask =
          is_color_source ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                          : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      tile_image_barrier.dstAccessMask =
          VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
      tile_image_barrier.oldLayout = view->image_layout;
      tile_image_barrier.newLayout = view->image_layout;
      tile_image_barrier.image = view->image;
      tile_image_barrier.subresourceRange = {0, 0, 1, 0, 1};
      tile_image_barrier.subresourceRange.aspectMask =
          is_color_source
              ? VK_IMAGE_ASPECT_COLOR_BIT
              : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
      vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT |
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &tile_image_barrier);

      auto render_pass =
          blitter_->GetRenderPass(texture->format, is_color_source);

      // Create a framebuffer containing our image.
      if (!texture->framebuffer) {
        auto texture_view = texture_cache_->DemandView(texture, 0x688);

        VkFramebufferCreateInfo fb_create_info = {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            nullptr,
            0,
            render_pass,
            1,
            &texture_view->view,
            texture->texture_info.width + 1,
            texture->texture_info.height + 1,
            1,
        };

        VkResult res = vkCreateFramebuffer(*device_, &fb_create_info, nullptr,
                                           &texture->framebuffer);
        CheckResult(res, "vkCreateFramebuffer");
      }

      VkRect2D src_rect = {
          {0, 0},
          resolve_extent,
      };

      VkRect2D dst_rect = {
          {resolve_offset.x, resolve_offset.y},
          resolve_extent,
      };

      // If the destination rectangle lies outside the window, make it start
      // inside. The Xenos does not copy pixel data at any offset in screen
      // coordinates.
      int32_t dst_adj_x =
          std::max(dst_rect.offset.x, -window_offset_x) - dst_rect.offset.x;
      int32_t dst_adj_y =
          std::max(dst_rect.offset.y, -window_offset_y) - dst_rect.offset.y;

      if (uint32_t(dst_adj_x) > dst_rect.extent.width ||
          uint32_t(dst_adj_y) > dst_rect.extent.height) {
        // No-op?
        break;
      }

      dst_rect.offset.x += dst_adj_x;
      dst_rect.offset.y += dst_adj_y;
      dst_rect.extent.width -= dst_adj_x;
      dst_rect.extent.height -= dst_adj_y;
      src_rect.extent.width -= dst_adj_x;
      src_rect.extent.height -= dst_adj_y;

      VkViewport viewport = {
          0.f, 0.f, float(copy_dest_pitch), float(copy_dest_height), 0.f, 1.f,
      };

      uint32_t scissor_tl_x = window_regs->window_scissor_tl.tl_x;
      uint32_t scissor_br_x = window_regs->window_scissor_br.br_x;
      uint32_t scissor_tl_y = window_regs->window_scissor_tl.tl_y;
      uint32_t scissor_br_y = window_regs->window_scissor_br.br_y;

      // Clamp the values to destination dimensions.
      scissor_tl_x = std::min(scissor_tl_x, copy_dest_pitch);
      scissor_br_x = std::min(scissor_br_x, copy_dest_pitch);
      scissor_tl_y = std::min(scissor_tl_y, copy_dest_height);
      scissor_br_y = std::min(scissor_br_y, copy_dest_height);

      VkRect2D scissor = {
          {int32_t(scissor_tl_x), int32_t(scissor_tl_y)},
          {scissor_br_x - scissor_tl_x, scissor_br_y - scissor_tl_y},
      };

      blitter_->BlitTexture2D(
          command_buffer, current_batch_fence_,
          is_color_source ? view->image_view : view->image_view_depth, src_rect,
          view->GetSize(), texture->format, dst_rect,
          {copy_dest_pitch, copy_dest_height}, texture->framebuffer, viewport,
          scissor, filter, is_color_source,
          copy_regs->copy_dest_info.copy_dest_swap != 0);

      // Pull the tile view back to a color/depth attachment.
      std::swap(tile_image_barrier.srcAccessMask,
                tile_image_barrier.dstAccessMask);
      std::swap(tile_image_barrier.oldLayout, tile_image_barrier.newLayout);
      vkCmdPipelineBarrier(command_buffer,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &tile_image_barrier);
    } break;

    case CopyCommand::kConstantOne:
    case CopyCommand::kNull:
      assert_always();
      break;
  }

  // And pull it back from a transfer destination.
  image_barrier.srcAccessMask = image_barrier.dstAccessMask;
  image_barrier.dstAccessMask =
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
  std::swap(image_barrier.newLayout, image_barrier.oldLayout);
  vkCmdPipelineBarrier(command_buffer,
                       is_color_source
                           ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                           : VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &image_barrier);

  // Perform any requested clears.
  uint32_t copy_depth_clear = regs[XE_GPU_REG_RB_DEPTH_CLEAR].u32;
  uint32_t copy_color_clear = regs[XE_GPU_REG_RB_COLOR_CLEAR].u32;
  uint32_t copy_color_clear_low = regs[XE_GPU_REG_RB_COLOR_CLEAR_LOW].u32;
  assert_true(copy_color_clear == copy_color_clear_low);

  if (color_clear_enabled) {
    // If color clear is enabled, we can only clear a selected color target!
    assert_true(is_color_source);

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

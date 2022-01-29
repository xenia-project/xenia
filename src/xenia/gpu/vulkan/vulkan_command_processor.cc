/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
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
#include "xenia/ui/vulkan/vulkan_presenter.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

using namespace xe::literals;
using namespace xe::gpu::xenos;
using xe::ui::vulkan::util::CheckResult;

constexpr size_t kDefaultBufferCacheCapacity = 256_MiB;

VulkanCommandProcessor::VulkanCommandProcessor(
    VulkanGraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state),
      swap_submission_tracker_(GetVulkanProvider()) {}

VulkanCommandProcessor::~VulkanCommandProcessor() = default;

void VulkanCommandProcessor::RequestFrameTrace(
    const std::filesystem::path& root_path) {
  // Override traces if renderdoc is attached.
  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  if (provider.renderdoc_api().api_1_0_0()) {
    trace_requested_ = true;
    return;
  }

  return CommandProcessor::RequestFrameTrace(root_path);
}

void VulkanCommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                      uint32_t length) {}

void VulkanCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {}

void VulkanCommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();
  cache_clear_requested_ = true;
}

bool VulkanCommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Unable to initialize base command processor context");
    return false;
  }

  ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  VkResult status = VK_SUCCESS;

  // Setup a blitter.
  blitter_ = std::make_unique<ui::vulkan::Blitter>(provider);
  status = blitter_->Initialize();
  if (status != VK_SUCCESS) {
    XELOGE("Unable to initialize blitter");
    blitter_->Shutdown();
    return false;
  }

  // Setup fenced pools used for all our per-frame/per-draw resources.
  command_buffer_pool_ = std::make_unique<ui::vulkan::CommandBufferPool>(
      provider, provider.queue_family_graphics_compute());

  // Initialize the state machine caches.
  buffer_cache_ = std::make_unique<BufferCache>(
      register_file_, memory_, provider, kDefaultBufferCacheCapacity);
  status = buffer_cache_->Initialize();
  if (status != VK_SUCCESS) {
    XELOGE("Unable to initialize buffer cache");
    buffer_cache_->Shutdown();
    return false;
  }

  texture_cache_ = std::make_unique<TextureCache>(memory_, register_file_,
                                                  &trace_writer_, provider);
  status = texture_cache_->Initialize();
  if (status != VK_SUCCESS) {
    XELOGE("Unable to initialize texture cache");
    texture_cache_->Shutdown();
    return false;
  }

  pipeline_cache_ = std::make_unique<PipelineCache>(register_file_, provider);
  status = pipeline_cache_->Initialize(
      buffer_cache_->constant_descriptor_set_layout(),
      texture_cache_->texture_descriptor_set_layout(),
      buffer_cache_->vertex_descriptor_set_layout());
  if (status != VK_SUCCESS) {
    XELOGE("Unable to initialize pipeline cache");
    pipeline_cache_->Shutdown();
    return false;
  }

  render_cache_ = std::make_unique<RenderCache>(register_file_, provider);
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

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  swap_submission_tracker_.Shutdown();
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device,
                                         swap_framebuffer_);
  swap_framebuffer_version_ = UINT64_MAX;

  buffer_cache_.reset();
  pipeline_cache_.reset();
  render_cache_.reset();
  texture_cache_.reset();

  blitter_.reset();

  // Free all pools. This must come after all of our caches clean up.
  command_buffer_pool_.reset();

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
    UpdateGammaRampValue(GammaRampType::kTable, value);
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

void VulkanCommandProcessor::BeginFrame() {
  assert_false(frame_open_);

  // TODO(benvanik): bigger batches.
  // TODO(DrChat): Decouple setup buffer from current batch.
  // Begin a new batch, and allocate and begin a command buffer and setup
  // buffer.
  current_batch_fence_ = command_buffer_pool_->BeginBatch();
  current_command_buffer_ = command_buffer_pool_->AcquireEntry();
  current_setup_buffer_ = command_buffer_pool_->AcquireEntry();

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();

  VkCommandBufferBeginInfo command_buffer_begin_info;
  command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_begin_info.pNext = nullptr;
  command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  command_buffer_begin_info.pInheritanceInfo = nullptr;
  auto status = dfn.vkBeginCommandBuffer(current_command_buffer_,
                                         &command_buffer_begin_info);
  CheckResult(status, "vkBeginCommandBuffer");

  status = dfn.vkBeginCommandBuffer(current_setup_buffer_,
                                    &command_buffer_begin_info);
  CheckResult(status, "vkBeginCommandBuffer");

  // Flag renderdoc down to start a capture if requested.
  // The capture will end when these commands are submitted to the queue.
  if ((cvars::vulkan_renderdoc_capture_all || trace_requested_) &&
      !capturing_) {
    const RENDERDOC_API_1_0_0* renderdoc_api =
        provider.renderdoc_api().api_1_0_0();
    if (renderdoc_api && !renderdoc_api->IsFrameCapturing()) {
      capturing_ = true;
      trace_requested_ = false;
      renderdoc_api->StartFrameCapture(nullptr, nullptr);
    }
  }

  frame_open_ = true;
}

void VulkanCommandProcessor::EndFrame() {
  if (current_render_state_) {
    render_cache_->EndRenderPass();
    current_render_state_ = nullptr;
  }

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkResult status = VK_SUCCESS;
  status = dfn.vkEndCommandBuffer(current_setup_buffer_);
  CheckResult(status, "vkEndCommandBuffer");
  status = dfn.vkEndCommandBuffer(current_command_buffer_);
  CheckResult(status, "vkEndCommandBuffer");

  current_command_buffer_ = nullptr;
  current_setup_buffer_ = nullptr;
  command_buffer_pool_->EndBatch();

  frame_open_ = false;
}

void VulkanCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                       uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");

  ui::Presenter* presenter = graphics_system_->presenter();
  if (!presenter) {
    return;
  }

  if (!frontbuffer_ptr) {
    // Trace viewer does this.
    frontbuffer_ptr = last_copy_base_;
  }

  std::vector<VkCommandBuffer> submit_buffers;
  if (frame_open_) {
    // TODO(DrChat): If the setup buffer is empty, don't bother queueing it up.
    submit_buffers.push_back(current_setup_buffer_);
    submit_buffers.push_back(current_command_buffer_);
  }
  bool submitted = false;

  auto& regs = *register_file_;
  int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
  auto group =
      reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(&regs.values[r]);
  TextureInfo texture_info;
  if (!TextureInfo::Prepare(group->texture_fetch, &texture_info)) {
    assert_always();
  }
  auto texture = texture_cache_->Lookup(texture_info);
  if (texture) {
    presenter->RefreshGuestOutput(
        frontbuffer_width, frontbuffer_height, 1280, 720,
        [this, frontbuffer_width, frontbuffer_height, texture, &submit_buffers,
         &submitted](
            ui::Presenter::GuestOutputRefreshContext& context) -> bool {
          auto& vulkan_context = static_cast<
              ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext&>(
              context);

          ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
          const ui::vulkan::VulkanProvider::DeviceFunctions& dfn =
              provider.dfn();
          VkDevice device = provider.device();

          // Make sure the framebuffer is for the current guest output image.
          if (swap_framebuffer_ != VK_NULL_HANDLE &&
              swap_framebuffer_version_ != vulkan_context.image_version()) {
            swap_submission_tracker_.AwaitAllSubmissionsCompletion();
            dfn.vkDestroyFramebuffer(device, swap_framebuffer_, nullptr);
            swap_framebuffer_ = VK_NULL_HANDLE;
          }
          if (swap_framebuffer_ == VK_NULL_HANDLE) {
            VkRenderPass render_pass = blitter_->GetRenderPass(
                ui::vulkan::VulkanPresenter::kGuestOutputFormat, true);
            if (render_pass == VK_NULL_HANDLE) {
              return false;
            }
            VkImageView guest_output_image_view = vulkan_context.image_view();
            VkFramebufferCreateInfo swap_framebuffer_create_info;
            swap_framebuffer_create_info.sType =
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            swap_framebuffer_create_info.pNext = nullptr;
            swap_framebuffer_create_info.flags = 0;
            swap_framebuffer_create_info.renderPass = render_pass;
            swap_framebuffer_create_info.attachmentCount = 1;
            swap_framebuffer_create_info.pAttachments =
                &guest_output_image_view;
            swap_framebuffer_create_info.width = frontbuffer_width;
            swap_framebuffer_create_info.height = frontbuffer_height;
            swap_framebuffer_create_info.layers = 1;
            if (dfn.vkCreateFramebuffer(device, &swap_framebuffer_create_info,
                                        nullptr,
                                        &swap_framebuffer_) != VK_SUCCESS) {
              XELOGE(
                  "Failed to create the Vulkan framebuffer for presentation");
              return false;
            }
            swap_framebuffer_version_ = vulkan_context.image_version();
          }

          // Build a final command buffer that copies the game's frontbuffer
          // texture into our backbuffer texture.
          VkCommandBuffer copy_commands = nullptr;
          bool opened_batch = !command_buffer_pool_->has_open_batch();
          if (!command_buffer_pool_->has_open_batch()) {
            current_batch_fence_ = command_buffer_pool_->BeginBatch();
          }
          copy_commands = command_buffer_pool_->AcquireEntry();

          VkCommandBufferBeginInfo command_buffer_begin_info;
          command_buffer_begin_info.sType =
              VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
          command_buffer_begin_info.pNext = nullptr;
          command_buffer_begin_info.flags =
              VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
          command_buffer_begin_info.pInheritanceInfo = nullptr;
          dfn.vkBeginCommandBuffer(copy_commands, &command_buffer_begin_info);

          texture->in_flight_fence = current_batch_fence_;

          // Insert a barrier so the GPU finishes writing to the image, and a
          // barrier after the last presenter's usage of the guest output image.
          VkPipelineStageFlags acquire_barrier_src_stages = 0;
          VkPipelineStageFlags acquire_barrier_dst_stages = 0;
          VkImageMemoryBarrier acquire_image_memory_barriers[2];
          uint32_t acquire_image_memory_barrier_count = 0;
          {
            acquire_barrier_src_stages |=
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_TRANSFER_BIT;
            acquire_barrier_dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            VkImageMemoryBarrier& acquire_image_memory_barrier =
                acquire_image_memory_barriers
                    [acquire_image_memory_barrier_count++];
            acquire_image_memory_barrier.sType =
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            acquire_image_memory_barrier.pNext = nullptr;
            acquire_image_memory_barrier.srcAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_TRANSFER_WRITE_BIT;
            acquire_image_memory_barrier.dstAccessMask =
                VK_ACCESS_SHADER_READ_BIT;
            acquire_image_memory_barrier.oldLayout = texture->image_layout;
            acquire_image_memory_barrier.newLayout = texture->image_layout;
            acquire_image_memory_barrier.srcQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            acquire_image_memory_barrier.dstQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            acquire_image_memory_barrier.image = texture->image;
            ui::vulkan::util::InitializeSubresourceRange(
                acquire_image_memory_barrier.subresourceRange);
          }
          {
            acquire_barrier_dst_stages |=
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkImageMemoryBarrier& acquire_image_memory_barrier =
                acquire_image_memory_barriers
                    [acquire_image_memory_barrier_count++];
            acquire_image_memory_barrier.sType =
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            acquire_image_memory_barrier.pNext = nullptr;
            acquire_image_memory_barrier.dstAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            // Will be overwriting all the contents.
            acquire_image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            acquire_image_memory_barrier.newLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            acquire_image_memory_barrier.srcQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            acquire_image_memory_barrier.dstQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            acquire_image_memory_barrier.image = vulkan_context.image();
            ui::vulkan::util::InitializeSubresourceRange(
                acquire_image_memory_barrier.subresourceRange);
            if (vulkan_context.image_ever_written_previously()) {
              acquire_barrier_src_stages |=
                  ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask;
              acquire_image_memory_barrier.srcAccessMask =
                  ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask;
            } else {
              acquire_image_memory_barrier.srcAccessMask = 0;
            }
          }
          assert_not_zero(acquire_barrier_src_stages);
          assert_not_zero(acquire_barrier_dst_stages);
          assert_not_zero(acquire_image_memory_barrier_count);
          dfn.vkCmdPipelineBarrier(copy_commands, acquire_barrier_src_stages,
                                   acquire_barrier_dst_stages, 0, 0, nullptr, 0,
                                   nullptr, acquire_image_memory_barrier_count,
                                   acquire_image_memory_barriers);

          // Part of the source image that we want to blit from.
          VkRect2D src_rect = {
              {0, 0},
              {texture->texture_info.width + 1,
               texture->texture_info.height + 1},
          };
          VkRect2D dst_rect = {{0, 0}, {frontbuffer_width, frontbuffer_height}};

          VkViewport viewport = {
              0.f, 0.f, float(frontbuffer_width), float(frontbuffer_height),
              0.f, 1.f};

          VkRect2D scissor = {{0, 0}, {frontbuffer_width, frontbuffer_height}};

          blitter_->BlitTexture2D(
              copy_commands, current_batch_fence_,
              texture_cache_->DemandView(texture, 0x688)->view, src_rect,
              {texture->texture_info.width + 1,
               texture->texture_info.height + 1},
              ui::vulkan::VulkanPresenter::kGuestOutputFormat, dst_rect,
              {frontbuffer_width, frontbuffer_height}, swap_framebuffer_,
              viewport, scissor, VK_FILTER_LINEAR, true, true);

          VkPipelineStageFlags release_barrier_src_stages = 0;
          VkPipelineStageFlags release_barrier_dst_stages = 0;
          VkImageMemoryBarrier release_image_memory_barriers[2];
          uint32_t release_image_memory_barrier_count = 0;
          {
            release_barrier_src_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            release_barrier_dst_stages |=
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkImageMemoryBarrier& release_image_memory_barrier =
                release_image_memory_barriers
                    [release_image_memory_barrier_count++];
            release_image_memory_barrier.sType =
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            release_image_memory_barrier.pNext = nullptr;
            release_image_memory_barrier.srcAccessMask =
                VK_ACCESS_SHADER_READ_BIT;
            release_image_memory_barrier.dstAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_TRANSFER_WRITE_BIT;
            release_image_memory_barrier.oldLayout = texture->image_layout;
            release_image_memory_barrier.newLayout = texture->image_layout;
            release_image_memory_barrier.srcQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            release_image_memory_barrier.dstQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            release_image_memory_barrier.image = texture->image;
            ui::vulkan::util::InitializeSubresourceRange(
                release_image_memory_barrier.subresourceRange);
          }
          {
            release_barrier_src_stages |=
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            release_barrier_dst_stages |=
                ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask;
            VkImageMemoryBarrier& release_image_memory_barrier =
                release_image_memory_barriers
                    [release_image_memory_barrier_count++];
            release_image_memory_barrier.sType =
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            release_image_memory_barrier.pNext = nullptr;
            release_image_memory_barrier.srcAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            release_image_memory_barrier.dstAccessMask =
                ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask;
            release_image_memory_barrier.oldLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            release_image_memory_barrier.newLayout =
                ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout;
            release_image_memory_barrier.srcQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            release_image_memory_barrier.dstQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            release_image_memory_barrier.image = vulkan_context.image();
            ui::vulkan::util::InitializeSubresourceRange(
                release_image_memory_barrier.subresourceRange);
          }
          assert_not_zero(release_barrier_src_stages);
          assert_not_zero(release_barrier_dst_stages);
          assert_not_zero(release_image_memory_barrier_count);
          dfn.vkCmdPipelineBarrier(copy_commands, release_barrier_src_stages,
                                   release_barrier_dst_stages, 0, 0, nullptr, 0,
                                   nullptr, release_image_memory_barrier_count,
                                   release_image_memory_barriers);

          dfn.vkEndCommandBuffer(copy_commands);

          // Need to submit all the commands before giving the image back to the
          // presenter so it can submit its own commands for displaying it to
          // the queue.

          if (frame_open_) {
            EndFrame();
          }

          if (opened_batch) {
            command_buffer_pool_->EndBatch();
          }

          submit_buffers.push_back(copy_commands);

          VkSubmitInfo submit_info = {};
          submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
          submit_info.commandBufferCount = uint32_t(submit_buffers.size());
          submit_info.pCommandBuffers = submit_buffers.data();
          VkResult submit_result;
          {
            ui::vulkan::VulkanProvider::QueueAcquisition queue_acquisition(
                provider.AcquireQueue(provider.queue_family_graphics_compute(),
                                      0));
            submit_result = dfn.vkQueueSubmit(
                queue_acquisition.queue, 1, &submit_info, current_batch_fence_);
          }
          if (submit_result != VK_SUCCESS) {
            return false;
          }
          submitted = true;

          // Signal the fence for destroying objects depending on the guest
          // output image.
          {
            ui::vulkan::VulkanSubmissionTracker::FenceAcquisition
                fence_acqusition =
                    swap_submission_tracker_.AcquireFenceToAdvanceSubmission();
            ui::vulkan::VulkanProvider::QueueAcquisition queue_acquisition(
                provider.AcquireQueue(provider.queue_family_graphics_compute(),
                                      0));
            if (dfn.vkQueueSubmit(queue_acquisition.queue, 0, nullptr,
                                  fence_acqusition.fence()) != VK_SUCCESS) {
              fence_acqusition.SubmissionSucceededSignalFailed();
            }
          }

          return true;
        });
  }

  ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  if (!submitted) {
    // End the frame even if failed to refresh the guest output.
    if (frame_open_) {
      EndFrame();
    }
    if (!submit_buffers.empty() || current_batch_fence_ != VK_NULL_HANDLE) {
      VkSubmitInfo submit_info = {};
      submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit_info.commandBufferCount = uint32_t(submit_buffers.size());
      submit_info.pCommandBuffers = submit_buffers.data();
      VkResult submit_result;
      {
        ui::vulkan::VulkanProvider::QueueAcquisition queue_acquisition(
            provider.AcquireQueue(provider.queue_family_graphics_compute(), 0));
        submit_result = dfn.vkQueueSubmit(queue_acquisition.queue, 1,
                                          &submit_info, current_batch_fence_);
      }
      CheckResult(submit_result, "vkQueueSubmit");
    }
  }

  if (current_batch_fence_ != VK_NULL_HANDLE) {
    dfn.vkWaitForFences(device, 1, &current_batch_fence_, VK_TRUE, -1);
  }
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

Shader* VulkanCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                           uint32_t guest_address,
                                           const uint32_t* host_address,
                                           uint32_t dword_count) {
  return pipeline_cache_->LoadShader(shader_type, guest_address, host_address,
                                     dword_count);
}

bool VulkanCommandProcessor::IssueDraw(xenos::PrimitiveType primitive_type,
                                       uint32_t index_count,
                                       IndexBufferInfo* index_buffer_info,
                                       bool major_mode_explicit) {
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

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();

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
    dfn.vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
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
    dfn.vkCmdDraw(command_buffer, index_count, instance_count, first_vertex,
                  first_instance);
  } else {
    // Index buffer draw.
    uint32_t instance_count = 1;
    uint32_t first_index = 0;
    uint32_t vertex_offset =
        register_file_->values[XE_GPU_REG_VGT_INDX_OFFSET].u32;
    uint32_t first_instance = 0;
    dfn.vkCmdDrawIndexed(command_buffer, index_count, instance_count,
                         first_index, vertex_offset, first_instance);
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
  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  dfn.vkCmdBindDescriptorSets(
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

  assert_true(info.endianness == xenos::Endian::k8in16 ||
              info.endianness == xenos::Endian::k8in32);

  trace_writer_.WriteMemoryRead(info.guest_base, info.length);

  // Upload (or get a cached copy of) the buffer.
  uint32_t source_addr = info.guest_base;
  uint32_t source_length =
      info.count * (info.format == xenos::IndexFormat::kInt32
                        ? sizeof(uint32_t)
                        : sizeof(uint16_t));
  auto buffer_ref = buffer_cache_->UploadIndexBuffer(
      current_setup_buffer_, source_addr, source_length, info.format,
      current_batch_fence_);
  if (buffer_ref.second == VK_WHOLE_SIZE) {
    // Failed to upload buffer.
    return false;
  }

  // Bind the buffer.
  VkIndexType index_type = info.format == xenos::IndexFormat::kInt32
                               ? VK_INDEX_TYPE_UINT32
                               : VK_INDEX_TYPE_UINT16;
  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  dfn.vkCmdBindIndexBuffer(command_buffer, buffer_ref.first, buffer_ref.second,
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

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  dfn.vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
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

  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  dfn.vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
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
  auto surface_msaa =
      static_cast<xenos::MsaaSamples>((surface_info >> 16) & 0x3);

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
  assert_true(fetch->type == xenos::FetchConstantType::kVertex);
  assert_true(fetch->endian == xenos::Endian::k8in32);
  assert_true(fetch->size == 6);
  const uint8_t* vertex_addr = memory_->TranslatePhysical(fetch->address << 2);
  trace_writer_.WriteMemoryRead(fetch->address << 2, fetch->size * 4);

  // Most vertices have a negative half pixel offset applied, which we reverse.
  auto& vtx_cntl = *(reg::PA_SU_VTX_CNTL*)&regs[XE_GPU_REG_PA_SU_VTX_CNTL].u32;
  float vtx_offset = vtx_cntl.pix_center == 0 ? 0.5f : 0.f;

  float dest_points[6];
  for (int i = 0; i < 6; i++) {
    dest_points[i] =
        GpuSwap(xe::load<float>(vertex_addr + i * 4), fetch->endian) +
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
  xenos::ColorRenderTargetFormat color_format;
  xenos::DepthRenderTargetFormat depth_format;
  if (is_color_source) {
    // Source from a color target.
    reg::RB_COLOR_INFO color_info[4] = {
        regs.Get<reg::RB_COLOR_INFO>(),
        regs.Get<reg::RB_COLOR_INFO>(XE_GPU_REG_RB_COLOR1_INFO),
        regs.Get<reg::RB_COLOR_INFO>(XE_GPU_REG_RB_COLOR2_INFO),
        regs.Get<reg::RB_COLOR_INFO>(XE_GPU_REG_RB_COLOR3_INFO),
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

  xenos::Endian resolve_endian = xenos::Endian::k8in32;
  if (copy_regs->copy_dest_info.copy_dest_endian <= xenos::Endian128::k16in32) {
    resolve_endian =
        static_cast<xenos::Endian>(copy_regs->copy_dest_info.copy_dest_endian);
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
  const ui::vulkan::VulkanProvider& provider = GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();
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

    dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &image_barrier);
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

  dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &image_barrier);

  // Ask the render cache to copy to the resolve texture.
  auto edram_base = is_color_source ? color_edram_base : depth_edram_base;
  uint32_t src_format = is_color_source ? static_cast<uint32_t>(color_format)
                                        : static_cast<uint32_t>(depth_format);
  VkFilter filter = is_color_source ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

  XELOGGPU("Resolve RT {:08X} {:08X}({}) -> 0x{:08X} ({}x{}, format: {})",
           edram_base, surface_pitch, surface_pitch, copy_dest_base,
           copy_dest_pitch, copy_dest_height, texture_info.format_info()->name);
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
      dfn.vkCmdPipelineBarrier(
          command_buffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
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

        VkResult res = dfn.vkCreateFramebuffer(device, &fb_create_info, nullptr,
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
      dfn.vkCmdPipelineBarrier(command_buffer,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0,
                               nullptr, 0, nullptr, 1, &tile_image_barrier);
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
  dfn.vkCmdPipelineBarrier(command_buffer,
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
  uint32_t copy_color_clear_low = regs[XE_GPU_REG_RB_COLOR_CLEAR_LO].u32;
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

void VulkanCommandProcessor::InitializeTrace() {}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

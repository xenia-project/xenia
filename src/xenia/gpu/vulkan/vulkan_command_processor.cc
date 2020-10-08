/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_command_processor.h"

#include <cstdint>
#include <iterator>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/gpu/vulkan/vulkan_shared_memory.h"
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
                                                      uint32_t length) {}

void VulkanCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {}

bool VulkanCommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Failed to initialize base command processor context");
    return false;
  }

  shared_memory_ =
      std::make_unique<VulkanSharedMemory>(*this, *memory_, trace_writer_);
  if (!shared_memory_->Initialize()) {
    XELOGE("Failed to initialize shared memory");
    return false;
  }

  return true;
}

void VulkanCommandProcessor::ShutdownContext() {
  AwaitAllQueueOperationsCompletion();

  shared_memory_.reset();

  const ui::vulkan::VulkanProvider& provider =
      GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

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

Shader* VulkanCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                           uint32_t guest_address,
                                           const uint32_t* host_address,
                                           uint32_t dword_count) {
  return nullptr;
}

bool VulkanCommandProcessor::IssueDraw(xenos::PrimitiveType prim_type,
                                       uint32_t index_count,
                                       IndexBufferInfo* index_buffer_info,
                                       bool major_mode_explicit) {
#if XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_VULKAN_FINE_GRAINED_DRAW_SCOPES

  BeginSubmission(true);

  bool indexed = index_buffer_info != nullptr && index_buffer_info->guest_base;

  // Actually draw.
  if (indexed) {
    uint32_t index_size =
        index_buffer_info->format == xenos::IndexFormat::kInt32
            ? sizeof(uint32_t)
            : sizeof(uint16_t);
    assert_false(index_buffer_info->guest_base & (index_size - 1));
    uint32_t index_base =
        index_buffer_info->guest_base & 0x1FFFFFFF & ~(index_size - 1);
    uint32_t index_buffer_size = index_buffer_info->count * index_size;
    if (!shared_memory_->RequestRange(index_base, index_buffer_size)) {
      XELOGE(
          "Failed to request index buffer at 0x{:08X} (size {}) in the shared "
          "memory",
          index_base, index_buffer_size);
      return false;
    }
    deferred_command_buffer_.CmdVkBindIndexBuffer(
        shared_memory_->buffer(), index_base,
        index_buffer_info->format == xenos::IndexFormat::kInt32
            ? VK_INDEX_TYPE_UINT32
            : VK_INDEX_TYPE_UINT16);
  }
  shared_memory_->Use(VulkanSharedMemory::Usage::kRead);

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
    // the end of the submission (when async pipeline state object creation
    // requests are fulfilled).
    deferred_command_buffer_.Reset();
  }

  if (is_opening_frame) {
    frame_open_ = true;
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

  if (submission_open_) {
    shared_memory_->EndSubmission();

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

      for (const CommandBuffer& command_buffer : command_buffers_writable_) {
        dfn.vkDestroyCommandPool(device, command_buffer.pool, nullptr);
      }
      command_buffers_writable_.clear();
    }
  }

  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

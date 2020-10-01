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
#include "xenia/ui/vulkan/vulkan_context.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

VulkanCommandProcessor::VulkanCommandProcessor(
    VulkanGraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}
VulkanCommandProcessor::~VulkanCommandProcessor() = default;

void VulkanCommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                      uint32_t length) {}

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

  return true;
}

void VulkanCommandProcessor::ShutdownContext() {
  AwaitAllQueueOperationsCompletion();

  const ui::vulkan::VulkanProvider& provider =
      GetVulkanContext().GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

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

  for (const auto& semaphore :
       submissions_in_flight_sparse_binding_semaphores_) {
    dfn.vkDestroySemaphore(device, semaphore.first, nullptr);
  }
  submissions_in_flight_sparse_binding_semaphores_.clear();
  for (VkFence& fence : submissions_in_flight_fences_) {
    dfn.vkDestroyFence(device, fence, nullptr);
  }
  submissions_in_flight_fences_.clear();
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
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  BeginSubmission(true);

  return true;
}

bool VulkanCommandProcessor::IssueCopy() {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  BeginSubmission(true);

  return true;
}

void VulkanCommandProcessor::InitializeTrace() {}

void VulkanCommandProcessor::FinalizeTrace() {}

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

  // Reclaim semaphores used for sparse binding and graphics synchronization.
  while (!submissions_in_flight_sparse_binding_semaphores_.empty()) {
    const auto& semaphore_submission =
        submissions_in_flight_sparse_binding_semaphores_.front();
    if (semaphore_submission.second > submission_completed_) {
      break;
    }
    semaphores_free_.push_back(semaphore_submission.first);
    submissions_in_flight_sparse_binding_semaphores_.pop_front();
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
}

void VulkanCommandProcessor::BeginSubmission(bool is_guest_command) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

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
        XELOGE("Failed to create a Vulkan submission fence");
        // Try to submit later. Completely dropping the submission is not
        // permitted because resources would be left in an undefined state.
        return false;
      }
      fences_free_.push_back(fence);
    }
    // TODO(Triang3l): Create a sparse binding semaphore.
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
    // TODO(Triang3l): Write deferred command buffer commands.
    if (dfn.vkEndCommandBuffer(command_buffer.buffer) != VK_SUCCESS) {
      XELOGE("Failed to end a Vulkan command buffer");
      return false;
    }
    // TODO(Triang3l): Submit sparse binding.
    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = nullptr;
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
    command_buffers_submitted_.push_back(
        std::make_pair(command_buffer, GetCurrentSubmission()));
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

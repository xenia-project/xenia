/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_gpu_completion_timeline.h"

#include <algorithm>
#include <iterator>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace vulkan {

VulkanGPUCompletionTimeline::~VulkanGPUCompletionTimeline() {
#ifndef NDEBUG
  assert_zero(fences_acquired_);
#endif

  if (!pending_submission_fences_.empty()) {
    if (vulkan_device_->functions().vkWaitForFences(
            vulkan_device_->device(), 1,
            &pending_submission_fences_.back().second, VK_TRUE,
            UINT64_MAX) == VK_ERROR_DEVICE_LOST) {
      vulkan_device_->SetLost();
    }

    while (!pending_submission_fences_.empty()) {
      vulkan_device_->functions().vkDestroyFence(
          vulkan_device_->device(), pending_submission_fences_.back().second,
          nullptr);
      pending_submission_fences_.pop_back();
    }
  }

  while (!free_fences_.empty()) {
    vulkan_device_->functions().vkDestroyFence(vulkan_device_->device(),
                                               free_fences_.back(), nullptr);
    free_fences_.pop_back();
  }
}

std::optional<VulkanGPUCompletionTimeline::FenceAcquisition>
VulkanGPUCompletionTimeline::AcquireFenceForSubmission(
    VkResult* const result_out_opt) {
  // Reuse fences if completion was not awaited or updated explicitly.
  UpdateAndGetCompletedSubmission();

  VkFence fence = VK_NULL_HANDLE;

  if (!free_fences_.empty()) {
    const VkResult fence_reset_result =
        vulkan_device_->functions().vkResetFences(vulkan_device_->device(), 1,
                                                  &free_fences_.back());
    if (fence_reset_result != VK_SUCCESS) {
      XELOGE("Failed to reset a Vulkan fence: {}",
             vk::to_string(vk::Result(fence_reset_result)));
    } else {
      fence = free_fences_.back();
      free_fences_.pop_back();
    }
  }

  if (fence == VK_NULL_HANDLE) {
    const VkFenceCreateInfo fence_create_info = {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    const VkResult fence_create_result =
        vulkan_device_->functions().vkCreateFence(
            vulkan_device_->device(), &fence_create_info, nullptr, &fence);
    if (fence_create_result != VK_SUCCESS) {
      XELOGE("Failed to create a Vulkan fence: {}",
             vk::to_string(vk::Result(fence_create_result)));
      if (result_out_opt != nullptr) {
        *result_out_opt = fence_create_result;
      }
      return std::nullopt;
    }
  }

#ifndef NDEBUG
  ++fences_acquired_;
#endif

  if (result_out_opt != nullptr) {
    *result_out_opt = VK_SUCCESS;
  }
  return FenceAcquisition(this, fence);
}

VkResult VulkanGPUCompletionTimeline::AcquireFenceAndSubmit(
    const uint32_t queue_family_index, const uint32_t queue_index,
    const uint32_t submit_count, const VkSubmitInfo* const submits) {
  VkResult fence_acquire_result;
  std::optional<FenceAcquisition> fence_acquisition =
      AcquireFenceForSubmission(&fence_acquire_result);
  if (!fence_acquisition.has_value()) {
    return fence_acquire_result;
  }

  VkResult submit_result;
  {
    const VulkanDevice::Queue::Acquisition queue_acquisition =
        vulkan_device_->AcquireQueue(queue_family_index, queue_index);
    submit_result = vulkan_device_->SubmitAndUpdateLost(
        queue_acquisition.queue(), submit_count, submits,
        fence_acquisition->GetFenceForSubmitting());
  }
  if (submit_result != VK_SUCCESS) {
    fence_acquisition->SetSubmissionFailedOrAborted();
  }
  return submit_result;
}

void VulkanGPUCompletionTimeline::UpdateCompletedSubmission() {
  while (!pending_submission_fences_.empty()) {
    const VkResult fence_status = vulkan_device_->functions().vkGetFenceStatus(
        vulkan_device_->device(), pending_submission_fences_.front().second);
    if (fence_status != VK_SUCCESS) {
      // Not ready, or an error.
      if (fence_status == VK_ERROR_DEVICE_LOST) {
        vulkan_device_->SetLost();
      }
      break;
    }
    SetCompletedSubmission(pending_submission_fences_.front().first);
    free_fences_.push_back(pending_submission_fences_.front().second);
    pending_submission_fences_.pop_front();
  }
}

void VulkanGPUCompletionTimeline::AwaitSubmissionImpl(
    const uint64_t awaited_submission) {
  // According to the Vulkan 1.4.335 specification:
  // "The first synchronization scope includes every batch submitted in the same
  // queue submission command. Fence signal operations that are defined by
  // vkQueueSubmit or vkQueueSubmit2 additionally include in the first
  // synchronization scope all commands that occur earlier in submission order.
  // Fence signal operations that are defined by vkQueueSubmit or vkQueueSubmit2
  // or vkQueueBindSparse additionally include in the first synchronization
  // scope any semaphore and fence signal operations that occur earlier in
  // signal operation order."
  auto submission_end_iterator = pending_submission_fences_.cbegin();
  while (submission_end_iterator != pending_submission_fences_.cend() &&
         submission_end_iterator->first <= awaited_submission) {
    submission_end_iterator = std::next(submission_end_iterator);
  }
  if (submission_end_iterator != pending_submission_fences_.cbegin()) {
    const VkResult fence_wait_result =
        vulkan_device_->functions().vkWaitForFences(
            vulkan_device_->device(), 1,
            &std::prev(submission_end_iterator)->second, VK_TRUE, UINT64_MAX);
    if (fence_wait_result != VK_SUCCESS) {
      XELOGE("Failed to wait for a Vulkan fence: {}",
             vk::to_string(vk::Result(fence_wait_result)));
      if (fence_wait_result == VK_ERROR_DEVICE_LOST) {
        vulkan_device_->SetLost();
      }
      return;
    }
    for (auto free_fence_iterator = pending_submission_fences_.cbegin();
         free_fence_iterator != submission_end_iterator;
         free_fence_iterator = std::next(free_fence_iterator)) {
      free_fences_.push_back(free_fence_iterator->second);
    }
    pending_submission_fences_.erase(pending_submission_fences_.cbegin(),
                                     submission_end_iterator);
  }
  if (GetCompletedSubmissionFromLastUpdate() < awaited_submission) {
    SetCompletedSubmission(awaited_submission);
  }
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

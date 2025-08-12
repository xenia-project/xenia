/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_SUBMISSION_TRACKER_H_
#define XENIA_UI_VULKAN_VULKAN_SUBMISSION_TRACKER_H_

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace ui {
namespace vulkan {

// Fence wrapper, safely handling cases when the fence has not been initialized
// yet or has already been shut down, and failed or dropped submissions.
//
// The current submission index can be associated with the usage of objects to
// release them when the GPU isn't potentially referencing them anymore, and
// should be incremented only
//
// 0 can be used as a "never referenced" submission index.
//
// The submission index timeline survives Shutdown, so submission indices can be
// given to clients that are not aware of the lifetime of the tracker.
//
// To transfer the tracker to another queue (to make sure the first signal on
// the new queue does not happen before the last signal on the old one), call
// AwaitAllSubmissionsCompletion before doing the first submission on the new
// one.
class VulkanSubmissionTracker {
 public:
  class FenceAcquisition {
   public:
    FenceAcquisition() : submission_tracker_(nullptr), fence_(VK_NULL_HANDLE) {}
    FenceAcquisition(VulkanSubmissionTracker& submission_tracker, VkFence fence)
        : submission_tracker_(&submission_tracker), fence_(fence) {}
    FenceAcquisition(const FenceAcquisition& fence_acquisition) = delete;
    FenceAcquisition& operator=(const FenceAcquisition& fence_acquisition) =
        delete;
    FenceAcquisition(FenceAcquisition&& fence_acquisition) {
      *this = std::move(fence_acquisition);
    }
    FenceAcquisition& operator=(FenceAcquisition&& fence_acquisition) {
      if (this == &fence_acquisition) {
        return *this;
      }
      submission_tracker_ = fence_acquisition.submission_tracker_;
      fence_acquisition.submission_tracker_ = nullptr;
      fence_ = fence_acquisition.fence_;
      fence_acquisition.fence_ = VK_NULL_HANDLE;
      return *this;
    }
    ~FenceAcquisition();

    // In unsignaled state. May be null if failed to create or to reset a fence.
    VkFence fence() { return fence_; }

    // Call if vkQueueSubmit has failed (or it was decided not to commit the
    // submission), and the submission index shouldn't be incremented by
    // releasing this submission (for instance, to retry commands with long-term
    // effects like copying or image layout changes later if in the attempt the
    // submission index stays the same).
    void SubmissionFailedOrDropped();
    // Call if for some reason (like signaling multiple fences) the fence
    // signaling was done in a separate submission than the command buffer, and
    // the command buffer vkQueueSubmit succeeded (so commands with long-term
    // effects will be executed), but the fence-only vkQueueSubmit has failed,
    // thus the tracker shouldn't attempt to wait for that fence (it will be in
    // the unsignaled state).
    void SubmissionSucceededSignalFailed() { signal_failed_ = true; }

   private:
    // If nullptr, has been moved to another FenceAcquisition - not holding a
    // fence from now on.
    VulkanSubmissionTracker* submission_tracker_;
    VkFence fence_;
    bool signal_failed_ = false;
  };

  VulkanSubmissionTracker(const VulkanDevice* vulkan_device)
      : vulkan_device_(vulkan_device) {
    assert_not_null(vulkan_device);
  }

  VulkanSubmissionTracker(const VulkanSubmissionTracker& submission_tracker) =
      delete;
  VulkanSubmissionTracker& operator=(
      const VulkanSubmissionTracker& submission_tracker) = delete;

  ~VulkanSubmissionTracker() { Shutdown(); }

  void Shutdown();

  uint64_t GetCurrentSubmission() const { return submission_current_; }
  uint64_t UpdateAndGetCompletedSubmission();

  // Returns whether the expected GPU signal has actually been reached (rather
  // than some fallback condition) for cases when stronger completeness
  // guarantees as needed (when downloading, as opposed to just destroying).
  // If false is returned, it's also not guaranteed that GetCompletedSubmission
  // will return a value >= submission_index.
  bool AwaitSubmissionCompletion(uint64_t submission_index);
  bool AwaitAllSubmissionsCompletion() {
    return AwaitSubmissionCompletion(submission_current_ - 1);
  }

  [[nodiscard]] FenceAcquisition AcquireFenceToAdvanceSubmission();

 private:
  const VulkanDevice* vulkan_device_;
  uint64_t submission_current_ = 1;
  // Last submission with a successful fence signal as well as a successful
  // fence wait / query.
  uint64_t submission_completed_on_gpu_ = 0;
  // The flow is:
  // Reclaimed (or create if empty) > acquired > pending > reclaimed.
  // Or, if dropped the submission while acquired:
  // Reclaimed (or create if empty) > acquired > reclaimed.
  VkFence fence_acquired_ = VK_NULL_HANDLE;
  // Ordered by the submission index (the first pair member).
  std::deque<std::pair<uint64_t, VkFence>> fences_pending_;
  // Fences are reclaimed when awaiting or when refreshing the completed value.
  std::vector<VkFence> fences_reclaimed_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_SUBMISSION_TRACKER_H_

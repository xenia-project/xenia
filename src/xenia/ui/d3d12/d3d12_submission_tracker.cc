/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_submission_tracker.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace d3d12 {

bool D3D12SubmissionTracker::Initialize(ID3D12Device* device,
                                        ID3D12CommandQueue* queue) {
  Shutdown();
  fence_completion_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!fence_completion_event_) {
    XELOGE(
        "D3D12SubmissionTracker: Failed to create the fence completion event");
    Shutdown();
    return false;
  }
  // Continue where the tracker was left at the last shutdown.
  if (FAILED(device->CreateFence(submission_current_ - 1, D3D12_FENCE_FLAG_NONE,
                                 IID_PPV_ARGS(&fence_)))) {
    XELOGE("D3D12SubmissionTracker: Failed to create the fence");
    Shutdown();
    return false;
  }
  queue_ = queue;
  submission_signal_queued_ = submission_current_ - 1;
  return true;
}

void D3D12SubmissionTracker::Shutdown() {
  AwaitAllSubmissionsCompletion();
  queue_.Reset();
  fence_.Reset();
  if (fence_completion_event_) {
    CloseHandle(fence_completion_event_);
    fence_completion_event_ = nullptr;
  }
}

bool D3D12SubmissionTracker::AwaitSubmissionCompletion(
    UINT64 submission_index) {
  if (!fence_ || !fence_completion_event_) {
    // Not fully initialized yet or already shut down.
    return false;
  }
  // The tracker itself can't give a submission index for a submission that
  // hasn't even started being recorded yet, the client has provided a
  // completely invalid value or has done overly optimistic math if such an
  // index has been obtained somehow.
  assert_true(submission_index <= submission_current_);
  // Waiting for the current submission is fine if there was a refusal to
  // submit, and the submission index wasn't incremented, but still need to
  // release objects referenced in the dropped submission (while shutting down,
  // for instance - in this case, waiting for the last successful submission,
  // which could have also referenced the objects from the new submission - we
  // can't know since the client has already overwritten its last usage index,
  // would correctly ensure that GPU usage of the objects is not pending).
  // Waiting for successful submissions, but failed signals, will result in a
  // true race condition, however, but waiting for the closest successful signal
  // is the best approximation - also retrying to signal in this case.
  UINT64 fence_value = submission_index;
  if (submission_index > submission_signal_queued_) {
    TrySignalEnqueueing();
    fence_value = submission_signal_queued_;
  }
  if (fence_->GetCompletedValue() < fence_value) {
    if (FAILED(fence_->SetEventOnCompletion(fence_value, nullptr))) {
      return false;
    }
  }
  return fence_value == submission_index;
}

void D3D12SubmissionTracker::SetQueue(ID3D12CommandQueue* new_queue) {
  if (queue_.Get() == new_queue) {
    return;
  }
  if (queue_) {
    // Make sure the first signal on the new queue won't happen before the last
    // signal, if pending, on the old one, as that would result first in too
    // early submission completion indication, and then in rewinding.
    AwaitAllSubmissionsCompletion();
  }
  queue_ = new_queue;
}

bool D3D12SubmissionTracker::NextSubmission() {
  ++submission_current_;
  assert_not_null(queue_);
  assert_not_null(fence_);
  return TrySignalEnqueueing();
}

bool D3D12SubmissionTracker::TrySignalEnqueueing() {
  if (submission_signal_queued_ + 1 >= submission_current_) {
    return true;
  }
  if (!queue_ || !fence_) {
    return false;
  }
  if (FAILED(queue_->Signal(fence_.Get(), submission_current_ - 1))) {
    return false;
  }
  submission_signal_queued_ = submission_current_ - 1;
  return true;
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

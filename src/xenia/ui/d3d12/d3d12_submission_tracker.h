/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_SUBMISSION_TRACKER_H_
#define XENIA_UI_D3D12_D3D12_SUBMISSION_TRACKER_H_

#include "xenia/ui/d3d12/d3d12_api.h"

namespace xe {
namespace ui {
namespace d3d12 {

// GPU > CPU fence wrapper, safely handling cases when the fence has not been
// initialized yet or has already been shut down, dropped submissions, and also
// transfers between queues so signals stay ordered.
//
// The current submission index can be associated with the usage of objects to
// release them when the GPU isn't potentially referencing them anymore, and
// should be incremented only
//
// 0 can be used as a "never referenced" submission index.
//
// The submission index timeline survives Shutdown / Initialize, so submission
// indices can be given to clients that are not aware of the lifetime of the
// tracker.
class D3D12SubmissionTracker {
 public:
  D3D12SubmissionTracker() = default;
  D3D12SubmissionTracker(const D3D12SubmissionTracker& submission_tracker) =
      delete;
  D3D12SubmissionTracker& operator=(
      const D3D12SubmissionTracker& submission_tracker) = delete;
  ~D3D12SubmissionTracker() { Shutdown(); }

  // The queue may be null if it's going to be set dynamically. Will also take a
  // reference to the queue.
  bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue);
  void Shutdown();

  // Will perform an ownership transfer if the queue is different than the
  // current one, and take a reference to the queue.
  void SetQueue(ID3D12CommandQueue* new_queue);

  UINT64 GetCurrentSubmission() const { return submission_current_; }
  // May be lower than a value awaited by AwaitSubmissionCompletion if it
  // returned false.
  UINT64 GetCompletedSubmission() const {
    // If shut down already or haven't fully initialized yet, don't care, for
    // simplicity of external code, as any downloads are unlikely in this case,
    // but destruction can be simplified.
    return fence_ ? fence_->GetCompletedValue() : (GetCurrentSubmission() - 1);
  }

  // Returns whether the expected GPU signal has actually been reached (rather
  // than some fallback condition) for cases when stronger completeness
  // guarantees as needed (when downloading, as opposed to just destroying).
  // If false is returned, it's also not guaranteed that GetCompletedSubmission
  // will return a value >= submission_index.
  bool AwaitSubmissionCompletion(UINT64 submission_index);
  bool AwaitAllSubmissionsCompletion() {
    return AwaitSubmissionCompletion(GetCurrentSubmission() - 1);
  }

  // Call after a successful ExecuteCommandList. Unconditionally increments the
  // current submission index, and tries to enqueue the fence signal. Returns
  // true if enqueued successfully, but even if not, waiting for submissions
  // without a successfully enqueued signal is handled in the tracker in a way
  // that it won't be infinite, so there's no need for clients to revert updates
  // to submission indices associated with GPU usage of objects.
  bool NextSubmission();
  // If NextSubmission has failed, but it's important that the signal is
  // enqueued, can be used to retry enqueueing the signal.
  bool TrySignalEnqueueing();

 private:
  UINT64 submission_current_ = 1;
  UINT64 submission_signal_queued_ = 0;
  HANDLE fence_completion_event_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_SUBMISSION_TRACKER_H_

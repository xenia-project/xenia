/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GPU_COMPLETION_TIMELINE_H_
#define XENIA_UI_GPU_COMPLETION_TIMELINE_H_

#include <algorithm>
#include <cstdint>

#include "xenia/base/assert.h"

namespace xe {
namespace ui {

class GPUCompletionTimeline {
 public:
  GPUCompletionTimeline(const GPUCompletionTimeline&) = delete;
  GPUCompletionTimeline& operator=(const GPUCompletionTimeline&) = delete;
  GPUCompletionTimeline(GPUCompletionTimeline&&) = delete;
  GPUCompletionTimeline& operator=(GPUCompletionTimeline&&) = delete;

  // The implemention of completion timeline destruction must await all pending
  // submissions (including for releasing its own GPU API fences, but also
  // because code that uses the completion timeline may expect that, since the
  // completion timeline is generally the only way for it to know when its GPU
  // objects can be destroyed safely).
  virtual ~GPUCompletionTimeline() = default;

  // Always >= 1, so objects never used yet may be associated with the
  // submission index 0.
  uint64_t GetUpcomingSubmission() const { return upcoming_submission_; }

  // Awaiting completion or updating the completed submission may mark the
  // device as lost.

  // The implementation is required to call `SetCompletedSubmission` if it has
  // been made aware that a submission has been completed.
  virtual void UpdateCompletedSubmission() = 0;

  uint64_t GetCompletedSubmissionFromLastUpdate() const {
    return completed_submission_;
  }

  uint64_t UpdateAndGetCompletedSubmission() {
    UpdateCompletedSubmission();
    return GetCompletedSubmissionFromLastUpdate();
  }

  bool AwaitSubmissionAndUpdateCompleted(const uint64_t awaited_submission) {
    assert_true(awaited_submission < upcoming_submission_);
    if (UpdateAndGetCompletedSubmission() < awaited_submission) {
      AwaitSubmissionImpl(awaited_submission);
    }
    // Recheck, the wait might have been incomplete if there was an error.
    return UpdateAndGetCompletedSubmission() >= awaited_submission;
  }

  bool AwaitMaxSubmissionsPendingAndUpdateCompleted(
      const uint64_t max_submissions_pending) {
    assert_not_zero(max_submissions_pending);
    return AwaitSubmissionAndUpdateCompleted(
        GetUpcomingSubmission() -
        std::min(max_submissions_pending, GetUpcomingSubmission()));
  }

  bool AwaitAllSubmissions() {
    return AwaitSubmissionAndUpdateCompleted(GetUpcomingSubmission() - 1);
  }

 protected:
  explicit GPUCompletionTimeline() = default;

  // Call only after a successful submission, so that it can be awaited later.
  void IncrementUpcomingSubmission() { ++upcoming_submission_; }

  void SetCompletedSubmission(const uint64_t new_completed_submission) {
    assert_true(new_completed_submission < upcoming_submission_);
    assert_true(new_completed_submission >= completed_submission_);
    completed_submission_ = new_completed_submission;
  }

  // The implementation may call `SetCompletedSubmission`, but is not required
  // to.
  virtual void AwaitSubmissionImpl(uint64_t awaited_submission) = 0;

 private:
  uint64_t upcoming_submission_ = 1;

  uint64_t completed_submission_ = 0;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GPU_COMPLETION_TIMELINE_H_

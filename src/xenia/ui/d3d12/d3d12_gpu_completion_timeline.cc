/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_gpu_completion_timeline.h"

#include <utility>

#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace d3d12 {

std::unique_ptr<D3D12GPUCompletionTimeline> D3D12GPUCompletionTimeline::Create(
    ID3D12Device* const device) {
  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  const HRESULT fence_create_result =
      device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  if (FAILED(fence_create_result)) {
    XELOGE("Failed to create a Direct3D 12 fence, result 0x{:08X}",
           fence_create_result);
    return nullptr;
  }
  return std::unique_ptr<D3D12GPUCompletionTimeline>(
      new D3D12GPUCompletionTimeline(fence.Get()));
}

D3D12GPUCompletionTimeline::~D3D12GPUCompletionTimeline() {
  fence_->SetEventOnCompletion(GetUpcomingSubmission() - 1, nullptr);
}

void D3D12GPUCompletionTimeline::UpdateCompletedSubmission() {
  uint64_t actual_completed_submission = fence_->GetCompletedValue();
  // The device has been removed if the completed value is UINT64_MAX.
  if (actual_completed_submission == UINT64_MAX) {
    return;
  }
  SetCompletedSubmission(actual_completed_submission);
}

void D3D12GPUCompletionTimeline::AwaitSubmissionImpl(
    const uint64_t awaited_submission) {
  fence_->SetEventOnCompletion(awaited_submission, nullptr);
}

HRESULT D3D12GPUCompletionTimeline::SignalAndAdvance(
    ID3D12CommandQueue* const queue) {
  const HRESULT signal_result =
      queue->Signal(fence_.Get(), GetUpcomingSubmission());
  if (FAILED(signal_result)) {
    XELOGE("Failed to signal a Direct3D 12 fence, result 0x{:08X}",
           signal_result);
  } else {
    IncrementUpcomingSubmission();
  }
  return signal_result;
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

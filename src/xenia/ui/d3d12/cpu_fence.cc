/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/cpu_fence.h"

#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace d3d12 {

std::unique_ptr<CPUFence> CPUFence::Create(ID3D12Device* device,
                                           ID3D12CommandQueue* queue) {
  std::unique_ptr<CPUFence> fence(new CPUFence(device, queue));
  if (!fence->Initialize()) {
    return nullptr;
  }
  return fence;
}

CPUFence::CPUFence(ID3D12Device* device, ID3D12CommandQueue* queue)
    : device_(device), queue_(queue) {}

CPUFence::~CPUFence() {
  // First destroying the fence because it may reference the event.
  if (fence_ != nullptr) {
    fence_->Release();
  }
  if (completion_event_ != nullptr) {
    CloseHandle(completion_event_);
  }
}

bool CPUFence::Initialize() {
  if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                  IID_PPV_ARGS(&fence_)))) {
    XELOGE("Failed to create a fence");
    return false;
  }
  completion_event_ = CreateEvent(nullptr, false, false, nullptr);
  if (completion_event_ == nullptr) {
    XELOGE("Failed to create a fence completion event");
    fence_->Release();
    fence_ = nullptr;
    return false;
  }
  queued_value_ = 0;
  return true;
}

void CPUFence::Enqueue() {
  ++queued_value_;
  queue_->Signal(fence_, queued_value_);
}

bool CPUFence::IsCompleted() {
  return fence_->GetCompletedValue() >= queued_value_;
}

void CPUFence::Await() {
  if (fence_->GetCompletedValue() < queued_value_) {
    fence_->SetEventOnCompletion(queued_value_, completion_event_);
    WaitForSingleObject(completion_event_, INFINITE);
  }
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

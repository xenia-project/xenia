/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_CPU_FENCE_H_
#define XENIA_UI_D3D12_CPU_FENCE_H_

#include <memory>

#include "xenia/ui/d3d12/d3d12_api.h"

namespace xe {
namespace ui {
namespace d3d12 {

class CPUFence {
 public:
  ~CPUFence();

  static std::unique_ptr<CPUFence> Create(ID3D12Device* device,
                                          ID3D12CommandQueue* queue);

  // Submits the fence to the GPU command queue.
  void Enqueue();

  // Immediately returns whether the GPU has reached the fence.
  bool IsCompleted();
  // Blocks until the fence has been reached.
  void Await();

 private:
  CPUFence(ID3D12Device* device, ID3D12CommandQueue* queue);
  bool Initialize();

  ID3D12Device* device_;
  ID3D12CommandQueue* queue_;

  ID3D12Fence* fence_ = nullptr;
  HANDLE completion_event_ = nullptr;
  uint64_t queued_value_ = 0;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_CPU_FENCE_H_

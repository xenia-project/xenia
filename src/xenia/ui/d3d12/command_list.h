/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_COMMAND_LIST_H_
#define XENIA_UI_D3D12_COMMAND_LIST_H_

#include <memory>

#include "xenia/ui/d3d12/d3d12_api.h"

namespace xe {
namespace ui {
namespace d3d12 {

class CommandList {
 public:
  ~CommandList();

  static std::unique_ptr<CommandList> Create(ID3D12Device* device,
                                             ID3D12CommandQueue* queue,
                                             D3D12_COMMAND_LIST_TYPE type);

  ID3D12GraphicsCommandList* GetCommandList() const { return command_list_; }
  ID3D12GraphicsCommandList1* GetCommandList1() const {
    return command_list_1_;
  }

  ID3D12GraphicsCommandList* BeginRecording();
  void AbortRecording();
  void Execute();

 protected:
  CommandList(ID3D12Device* device, ID3D12CommandQueue* queue,
              D3D12_COMMAND_LIST_TYPE type);
  bool Initialize();

  ID3D12Device* device_;
  ID3D12CommandQueue* queue_;
  D3D12_COMMAND_LIST_TYPE type_;

  ID3D12CommandAllocator* command_allocator_ = nullptr;
  ID3D12GraphicsCommandList* command_list_ = nullptr;
  ID3D12GraphicsCommandList1* command_list_1_ = nullptr;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_CPU_FENCE_H_

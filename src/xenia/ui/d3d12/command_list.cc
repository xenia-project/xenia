/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/command_list.h"

#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace d3d12 {

std::unique_ptr<CommandList> CommandList::Create(ID3D12Device* device,
                                                 ID3D12CommandQueue* queue,
                                                 D3D12_COMMAND_LIST_TYPE type) {
  std::unique_ptr<CommandList> command_list(
      new CommandList(device, queue, type));
  if (!command_list->Initialize()) {
    return nullptr;
  }
  return command_list;
}

CommandList::CommandList(ID3D12Device* device, ID3D12CommandQueue* queue,
                         D3D12_COMMAND_LIST_TYPE type)
    : device_(device), queue_(queue), type_(type) {}

CommandList::~CommandList() {
  if (command_list_1_ != nullptr) {
    command_list_1_->Release();
  }
  if (command_list_ != nullptr) {
    command_list_->Release();
  }
  if (command_allocator_ != nullptr) {
    command_allocator_->Release();
  }
}

bool CommandList::Initialize() {
  if (FAILED(device_->CreateCommandAllocator(
          type_, IID_PPV_ARGS(&command_allocator_)))) {
    XELOGE("Failed to create a command allocator");
    return false;
  }
  if (FAILED(device_->CreateCommandList(0, type_, command_allocator_, nullptr,
                                        IID_PPV_ARGS(&command_list_)))) {
    XELOGE("Failed to create a graphics command list");
    command_allocator_->Release();
    command_allocator_ = nullptr;
    return false;
  }
  // Optional - added in Creators Update (SDK 10.0.15063.0).
  command_list_->QueryInterface(IID_PPV_ARGS(&command_list_1_));
  // A command list is initially open, need to close it before resetting.
  command_list_->Close();
  return true;
}

ID3D12GraphicsCommandList* CommandList::BeginRecording() {
  command_allocator_->Reset();
  command_list_->Reset(command_allocator_, nullptr);
  return command_list_;
}

void CommandList::AbortRecording() { command_list_->Close(); }

void CommandList::Execute() {
  command_list_->Close();
  ID3D12CommandList* execute_lists[] = {command_list_};
  queue_->ExecuteCommandLists(1, execute_lists);
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

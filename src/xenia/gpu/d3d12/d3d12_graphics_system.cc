/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_graphics_system.h"

#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/xbox.h"

namespace xe {
namespace gpu {
namespace d3d12 {

D3D12GraphicsSystem::D3D12GraphicsSystem() {}

D3D12GraphicsSystem::~D3D12GraphicsSystem() {}

X_STATUS D3D12GraphicsSystem::Setup(cpu::Processor* processor,
                                    kernel::KernelState* kernel_state,
                                    ui::Window* target_window) {
  provider_ = xe::ui::d3d12::D3D12Provider::Create(target_window);

  return GraphicsSystem::Setup(processor, kernel_state, target_window);
}

void D3D12GraphicsSystem::Shutdown() { GraphicsSystem::Shutdown(); }

std::unique_ptr<CommandProcessor>
D3D12GraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new D3D12CommandProcessor(this, kernel_state_));
}

void D3D12GraphicsSystem::Swap(xe::ui::UIEvent* e) {
  if (!command_processor_) {
    return;
  }

  auto& swap_state = command_processor_->swap_state();
  std::lock_guard<std::mutex> lock(swap_state.mutex);
  swap_state.pending = false;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

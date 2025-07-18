/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_graphics_system.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/ui/d3d12/d3d12_util.h"
#include "xenia/xbox.h"

namespace xe {
namespace gpu {
namespace d3d12 {

D3D12GraphicsSystem::D3D12GraphicsSystem() {}

D3D12GraphicsSystem::~D3D12GraphicsSystem() {}

bool D3D12GraphicsSystem::IsAvailable() {
  return xe::ui::d3d12::D3D12Provider::IsD3D12APIAvailable();
}

std::string D3D12GraphicsSystem::name() const {
  auto d3d12_command_processor =
      static_cast<D3D12CommandProcessor*>(command_processor());
  if (d3d12_command_processor != nullptr) {
    return d3d12_command_processor->GetWindowTitleText();
  }
  return "Direct3D 12";
}

X_STATUS D3D12GraphicsSystem::Setup(cpu::Processor* processor,
                                    kernel::KernelState* kernel_state,
                                    ui::WindowedAppContext* app_context,
                                    bool is_surface_required) {
  provider_ = xe::ui::d3d12::D3D12Provider::Create();
  return GraphicsSystem::Setup(processor, kernel_state, app_context,
                               is_surface_required);
}

std::unique_ptr<CommandProcessor>
D3D12GraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new D3D12CommandProcessor(this, kernel_state_));
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

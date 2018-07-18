/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_D3D12_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_D3D12_D3D12_GRAPHICS_SYSTEM_H_

#include <memory>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12GraphicsSystem : public GraphicsSystem {
 public:
  D3D12GraphicsSystem();
  ~D3D12GraphicsSystem() override;

  std::wstring name() const override { return L"Direct3D 12"; }

  X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                 ui::Window* target_window) override;
  void Shutdown() override;

 private:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;

  void Swap(xe::ui::UIEvent* e) override;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_GRAPHICS_SYSTEM_H_

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
#include "xenia/gpu/d3d12/deferred_command_list.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/ui/d3d12/d3d12_context.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12GraphicsSystem : public GraphicsSystem {
 public:
  D3D12GraphicsSystem();
  ~D3D12GraphicsSystem() override;

  std::wstring name() const override;

  X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                 ui::Window* target_window) override;
  void Shutdown() override;

  void AwaitFrontBufferUnused();

  // Draws a texture covering the entire viewport to the render target currently
  // bound on the specified command list (in D3D12Context::kSwapChainFormat).
  // This changes the current pipeline, graphics root signature and primitive
  // topology. The gamma ramp texture must be 1D if present at all, for linear
  // space, pass nullptr as the gamma ramp.
  void StretchTextureToFrontBuffer(
      D3D12_GPU_DESCRIPTOR_HANDLE handle,
      D3D12_GPU_DESCRIPTOR_HANDLE* gamma_ramp_handle, float gamma_ramp_inv_size,
      ID3D12GraphicsCommandList* command_list);
  void StretchTextureToFrontBuffer(
      D3D12_GPU_DESCRIPTOR_HANDLE handle,
      D3D12_GPU_DESCRIPTOR_HANDLE* gamma_ramp_handle, float gamma_ramp_inv_size,
      DeferredCommandList& command_list);

 private:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;

  void Swap(xe::ui::UIEvent* e) override;

  ui::d3d12::D3D12Context* display_context_ = nullptr;

  ID3D12RootSignature* stretch_root_signature_ = nullptr;
  ID3D12RootSignature* stretch_gamma_root_signature_ = nullptr;
  ID3D12PipelineState* stretch_pipeline_ = nullptr;
  ID3D12PipelineState* stretch_gamma_pipeline_ = nullptr;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_GRAPHICS_SYSTEM_H_

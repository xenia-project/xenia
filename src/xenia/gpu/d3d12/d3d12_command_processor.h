/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_D3D12_COMMAND_PROCESSOR_H_
#define XENIA_GPU_D3D12_D3D12_COMMAND_PROCESSOR_H_

#include <memory>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#include "xenia/gpu/d3d12/pipeline_cache.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/ui/d3d12/d3d12_context.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor : public CommandProcessor {
 public:
  explicit D3D12CommandProcessor(D3D12GraphicsSystem* graphics_system,
                                 kernel::KernelState* kernel_state);
  ~D3D12CommandProcessor();

  void ClearCaches() override;

  // Needed by everything that owns transient objects.
  xe::ui::d3d12::D3D12Context* GetD3D12Context() const {
    return static_cast<xe::ui::d3d12::D3D12Context*>(context_.get());
  }

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;

  void PerformSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                   uint32_t frontbuffer_height) override;

  Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(PrimitiveType primitive_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info) override;
  bool IssueCopy() override;

 private:
  void BeginFrame();
  void EndFrame();

  bool cache_clear_requested_ = false;

  std::unique_ptr<PipelineCache> pipeline_cache_;

  uint32_t current_queue_frame_ = UINT32_MAX;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_COMMAND_PROCESSOR_H_

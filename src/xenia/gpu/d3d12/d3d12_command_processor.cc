/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_command_processor.h"

namespace xe {
namespace gpu {
namespace d3d12 {

D3D12CommandProcessor::D3D12CommandProcessor(
    D3D12GraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}
D3D12CommandProcessor::~D3D12CommandProcessor() = default;

bool D3D12CommandProcessor::SetupContext() {
  return CommandProcessor::SetupContext();
}

void D3D12CommandProcessor::ShutdownContext() {
  return CommandProcessor::ShutdownContext();
}

void D3D12CommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                        uint32_t frontbuffer_width,
                                        uint32_t frontbuffer_height) {}

Shader* D3D12CommandProcessor::LoadShader(ShaderType shader_type,
                                          uint32_t guest_address,
                                          const uint32_t* host_address,
                                          uint32_t dword_count) {
  return nullptr;
}

bool D3D12CommandProcessor::IssueDraw(PrimitiveType prim_type,
                                      uint32_t index_count,
                                      IndexBufferInfo* index_buffer_info) {
  return true;
}

bool D3D12CommandProcessor::IssueCopy() { return true; }

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_command_processor.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/d3d12_shader.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace d3d12 {

D3D12CommandProcessor::D3D12CommandProcessor(
    D3D12GraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}
D3D12CommandProcessor::~D3D12CommandProcessor() = default;

void D3D12CommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();
  cache_clear_requested_ = true;
}

bool D3D12CommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Unable to initialize base command processor context");
    return false;
  }

  auto context = GetD3D12Context();

  pipeline_cache_ = std::make_unique<PipelineCache>(register_file_, context);

  return true;
}

void D3D12CommandProcessor::ShutdownContext() {
  auto context = GetD3D12Context();
  context->AwaitAllFramesCompletion();

  pipeline_cache_.reset();

  CommandProcessor::ShutdownContext();
}

void D3D12CommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                        uint32_t frontbuffer_width,
                                        uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");

  if (current_queue_frame_ != UINT32_MAX) {
    EndFrame();
  }

  if (cache_clear_requested_) {
    cache_clear_requested_ = false;
    GetD3D12Context()->AwaitAllFramesCompletion();
    pipeline_cache_->ClearCache();
  }
}

Shader* D3D12CommandProcessor::LoadShader(ShaderType shader_type,
                                          uint32_t guest_address,
                                          const uint32_t* host_address,
                                          uint32_t dword_count) {
  return pipeline_cache_->LoadShader(shader_type, guest_address, host_address,
                                     dword_count);
}

bool D3D12CommandProcessor::IssueDraw(PrimitiveType primitive_type,
                                      uint32_t index_count,
                                      IndexBufferInfo* index_buffer_info) {
  auto& regs = *register_file_;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto enable_mode = static_cast<xenos::ModeControl>(
      regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7);
  if (enable_mode == xenos::ModeControl::kIgnore) {
    // Ignored.
    return true;
  } else if (enable_mode == xenos::ModeControl::kCopy) {
    // Special copy handling.
    return IssueCopy();
  }

  if ((regs[XE_GPU_REG_RB_SURFACE_INFO].u32 & 0x3FFF) == 0) {
    // Doesn't actually draw.
    return true;
  }

  // Shaders will have already been defined by previous loads.
  // We need them to do just about anything so validate here.
  auto vertex_shader = static_cast<D3D12Shader*>(active_vertex_shader());
  auto pixel_shader = static_cast<D3D12Shader*>(active_pixel_shader());
  if (!vertex_shader) {
    // Always need a vertex shader.
    return false;
  }
  // Depth-only mode doesn't need a pixel shader (we'll use a fake one).
  if (enable_mode == xenos::ModeControl::kDepth) {
    // Use a dummy pixel shader when required.
    pixel_shader = nullptr;
  } else if (!pixel_shader) {
    // Need a pixel shader in normal color mode.
    return true;
  }

  bool full_update = false;
  if (current_queue_frame_ == UINT32_MAX) {
    BeginFrame();
    full_update = true;
  }

  auto pipeline_status = pipeline_cache_->ConfigurePipeline(
      vertex_shader, pixel_shader, primitive_type);
  if (pipeline_status == PipelineCache::UpdateStatus::kError) {
    return false;
  }

  return true;
}

bool D3D12CommandProcessor::IssueCopy() { return true; }

void D3D12CommandProcessor::BeginFrame() {
  assert_true(current_queue_frame_ == UINT32_MAX);
  auto context = GetD3D12Context();

  context->BeginSwap();

  current_queue_frame_ = context->GetCurrentQueueFrame();
}

void D3D12CommandProcessor::EndFrame() {
  assert_true(current_queue_frame_ != UINT32_MAX);
  auto context = GetD3D12Context();

  context->EndSwap();

  current_queue_frame_ = UINT32_MAX;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

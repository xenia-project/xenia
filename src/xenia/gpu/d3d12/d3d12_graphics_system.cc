/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_graphics_system.h"

#include "xenia/base/logging.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/ui/d3d12/d3d12_util.h"
#include "xenia/xbox.h"

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildhlsl`.
#include "xenia/gpu/d3d12/shaders/dxbc/fullscreen_vs.h"
#include "xenia/gpu/d3d12/shaders/dxbc/stretch_gamma_ps.h"
#include "xenia/gpu/d3d12/shaders/dxbc/stretch_ps.h"

D3D12GraphicsSystem::D3D12GraphicsSystem() {}

D3D12GraphicsSystem::~D3D12GraphicsSystem() {}

std::wstring D3D12GraphicsSystem::name() const {
  auto d3d12_command_processor =
      static_cast<D3D12CommandProcessor*>(command_processor());
  if (d3d12_command_processor != nullptr) {
    return d3d12_command_processor->GetWindowTitleText();
  }
  return L"Direct3D 12";
}

X_STATUS D3D12GraphicsSystem::Setup(cpu::Processor* processor,
                                    kernel::KernelState* kernel_state,
                                    ui::Window* target_window) {
  provider_ = xe::ui::d3d12::D3D12Provider::Create(target_window);
  auto d3d12_provider = static_cast<xe::ui::d3d12::D3D12Provider*>(provider());
  auto device = d3d12_provider->GetDevice();

  auto result = GraphicsSystem::Setup(processor, kernel_state, target_window);
  if (result != X_STATUS_SUCCESS) {
    return result;
  }

  if (target_window) {
    display_context_ = reinterpret_cast<xe::ui::d3d12::D3D12Context*>(
        target_window->context());
  }

  // Create the stretch pipeline root signature, with 1 parameter (source
  // texture) for raw stretch and 3 parameters (source texture, gamma ramp LUT,
  // inverse of the size of the gamma ramp LUT) for gamma-correcting stretch.
  // Raw.
  D3D12_ROOT_PARAMETER stretch_root_parameters[3];
  stretch_root_parameters[0].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  stretch_root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  D3D12_DESCRIPTOR_RANGE stretch_root_texture_range;
  stretch_root_texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  stretch_root_texture_range.NumDescriptors = 1;
  stretch_root_texture_range.BaseShaderRegister = 0;
  stretch_root_texture_range.RegisterSpace = 0;
  stretch_root_texture_range.OffsetInDescriptorsFromTableStart = 0;
  stretch_root_parameters[0].DescriptorTable.pDescriptorRanges =
      &stretch_root_texture_range;
  stretch_root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC stretch_sampler_desc;
  stretch_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  stretch_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  stretch_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  stretch_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  stretch_sampler_desc.MipLODBias = 0.0f;
  stretch_sampler_desc.MaxAnisotropy = 1;
  stretch_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  stretch_sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
  stretch_sampler_desc.MinLOD = 0.0f;
  stretch_sampler_desc.MaxLOD = 0.0f;
  stretch_sampler_desc.ShaderRegister = 0;
  stretch_sampler_desc.RegisterSpace = 0;
  stretch_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC stretch_root_desc;
  stretch_root_desc.NumParameters = 1;
  stretch_root_desc.pParameters = stretch_root_parameters;
  stretch_root_desc.NumStaticSamplers = 1;
  stretch_root_desc.pStaticSamplers = &stretch_sampler_desc;
  stretch_root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
  stretch_root_signature_ =
      ui::d3d12::util::CreateRootSignature(d3d12_provider, stretch_root_desc);
  if (stretch_root_signature_ == nullptr) {
    XELOGE("Failed to create the front buffer stretch root signature");
    return X_STATUS_UNSUCCESSFUL;
  }
  // Gamma.
  stretch_root_parameters[1].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  stretch_root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  D3D12_DESCRIPTOR_RANGE stretch_root_gamma_ramp_range;
  stretch_root_gamma_ramp_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  stretch_root_gamma_ramp_range.NumDescriptors = 1;
  stretch_root_gamma_ramp_range.BaseShaderRegister = 1;
  stretch_root_gamma_ramp_range.RegisterSpace = 0;
  stretch_root_gamma_ramp_range.OffsetInDescriptorsFromTableStart = 0;
  stretch_root_parameters[1].DescriptorTable.pDescriptorRanges =
      &stretch_root_gamma_ramp_range;
  stretch_root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  stretch_root_parameters[2].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  stretch_root_parameters[2].Constants.ShaderRegister = 0;
  stretch_root_parameters[2].Constants.RegisterSpace = 0;
  stretch_root_parameters[2].Constants.Num32BitValues = 1;
  stretch_root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  stretch_root_desc.NumParameters = 3;
  stretch_root_desc.pParameters = stretch_root_parameters;
  stretch_gamma_root_signature_ =
      ui::d3d12::util::CreateRootSignature(d3d12_provider, stretch_root_desc);
  if (stretch_gamma_root_signature_ == nullptr) {
    XELOGE(
        "Failed to create the gamma-correcting front buffer stretch root "
        "signature");
    stretch_root_signature_->Release();
    stretch_root_signature_ = nullptr;
    return X_STATUS_UNSUCCESSFUL;
  }

  // Create the stretch pipelines.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC stretch_pipeline_desc = {};
  stretch_pipeline_desc.pRootSignature = stretch_root_signature_;
  stretch_pipeline_desc.VS.pShaderBytecode = fullscreen_vs;
  stretch_pipeline_desc.VS.BytecodeLength = sizeof(fullscreen_vs);
  stretch_pipeline_desc.PS.pShaderBytecode = stretch_ps;
  stretch_pipeline_desc.PS.BytecodeLength = sizeof(stretch_ps);
  stretch_pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  stretch_pipeline_desc.SampleMask = UINT_MAX;
  stretch_pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  stretch_pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  stretch_pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
  stretch_pipeline_desc.PrimitiveTopologyType =
      D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  stretch_pipeline_desc.NumRenderTargets = 1;
  stretch_pipeline_desc.RTVFormats[0] =
      ui::d3d12::D3D12Context::kSwapChainFormat;
  stretch_pipeline_desc.SampleDesc.Count = 1;
  if (FAILED(device->CreateGraphicsPipelineState(
          &stretch_pipeline_desc, IID_PPV_ARGS(&stretch_pipeline_)))) {
    XELOGE("Failed to create the front buffer stretch pipeline state");
    stretch_gamma_root_signature_->Release();
    stretch_gamma_root_signature_ = nullptr;
    stretch_root_signature_->Release();
    stretch_root_signature_ = nullptr;
    return X_STATUS_UNSUCCESSFUL;
  }
  stretch_pipeline_desc.pRootSignature = stretch_gamma_root_signature_;
  stretch_pipeline_desc.PS.pShaderBytecode = stretch_gamma_ps;
  stretch_pipeline_desc.PS.BytecodeLength = sizeof(stretch_gamma_ps);
  if (FAILED(device->CreateGraphicsPipelineState(
          &stretch_pipeline_desc, IID_PPV_ARGS(&stretch_gamma_pipeline_)))) {
    XELOGE(
        "Failed to create the gamma-correcting front buffer stretch "
        "pipeline state");
    stretch_pipeline_->Release();
    stretch_pipeline_ = nullptr;
    stretch_gamma_root_signature_->Release();
    stretch_gamma_root_signature_ = nullptr;
    stretch_root_signature_->Release();
    stretch_root_signature_ = nullptr;
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

void D3D12GraphicsSystem::Shutdown() {
  ui::d3d12::util::ReleaseAndNull(stretch_gamma_pipeline_);
  ui::d3d12::util::ReleaseAndNull(stretch_pipeline_);
  ui::d3d12::util::ReleaseAndNull(stretch_gamma_root_signature_);
  ui::d3d12::util::ReleaseAndNull(stretch_root_signature_);

  GraphicsSystem::Shutdown();
}

void D3D12GraphicsSystem::AwaitFrontBufferUnused() {
  if (display_context_ != nullptr) {
    display_context_->AwaitAllFramesCompletion();
  }
}

void D3D12GraphicsSystem::StretchTextureToFrontBuffer(
    D3D12_GPU_DESCRIPTOR_HANDLE handle,
    D3D12_GPU_DESCRIPTOR_HANDLE* gamma_ramp_handle, float gamma_ramp_inv_size,
    ID3D12GraphicsCommandList* command_list) {
  if (gamma_ramp_handle != nullptr) {
    command_list->SetPipelineState(stretch_gamma_pipeline_);
    command_list->SetGraphicsRootSignature(stretch_gamma_root_signature_);
    command_list->SetGraphicsRootDescriptorTable(1, *gamma_ramp_handle);
    command_list->SetGraphicsRoot32BitConstants(2, 1, &gamma_ramp_inv_size, 0);
  } else {
    command_list->SetPipelineState(stretch_pipeline_);
    command_list->SetGraphicsRootSignature(stretch_root_signature_);
  }
  command_list->SetGraphicsRootDescriptorTable(0, handle);
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list->DrawInstanced(3, 1, 0, 0);
}

void D3D12GraphicsSystem::StretchTextureToFrontBuffer(
    D3D12_GPU_DESCRIPTOR_HANDLE handle,
    D3D12_GPU_DESCRIPTOR_HANDLE* gamma_ramp_handle, float gamma_ramp_inv_size,
    DeferredCommandList& command_list) {
  if (gamma_ramp_handle != nullptr) {
    command_list.D3DSetPipelineState(stretch_gamma_pipeline_);
    command_list.D3DSetGraphicsRootSignature(stretch_gamma_root_signature_);
    command_list.D3DSetGraphicsRootDescriptorTable(1, *gamma_ramp_handle);
    command_list.D3DSetGraphicsRoot32BitConstants(2, 1, &gamma_ramp_inv_size,
                                                  0);
  } else {
    command_list.D3DSetPipelineState(stretch_pipeline_);
    command_list.D3DSetGraphicsRootSignature(stretch_root_signature_);
  }
  command_list.D3DSetGraphicsRootDescriptorTable(0, handle);
  command_list.D3DIASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list.D3DDrawInstanced(3, 1, 0, 0);
}

std::unique_ptr<CommandProcessor>
D3D12GraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new D3D12CommandProcessor(this, kernel_state_));
}

void D3D12GraphicsSystem::Swap(xe::ui::UIEvent* e) {
  if (display_context_->WasLost()) {
    // We're crashing. Cheese it.
    return;
  }

  if (!command_processor_) {
    return;
  }

  auto& swap_state = command_processor_->swap_state();
  ID3D12DescriptorHeap* swap_srv_heap;
  {
    std::lock_guard<std::mutex> lock(swap_state.mutex);
    swap_state.pending = false;
    swap_srv_heap = reinterpret_cast<ID3D12DescriptorHeap*>(
        swap_state.front_buffer_texture);
  }
  if (swap_srv_heap == nullptr) {
    // Not ready yet.
    return;
  }

  auto command_list = display_context_->GetSwapCommandList();
  uint32_t swap_width, swap_height;
  display_context_->GetSwapChainSize(swap_width, swap_height);
  D3D12_VIEWPORT viewport;
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = float(swap_width);
  viewport.Height = float(swap_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 0.0f;
  command_list->RSSetViewports(1, &viewport);
  D3D12_RECT scissor;
  scissor.left = 0;
  scissor.top = 0;
  scissor.right = swap_width;
  scissor.bottom = swap_height;
  command_list->RSSetScissorRects(1, &scissor);
  command_list->SetDescriptorHeaps(1, &swap_srv_heap);
  StretchTextureToFrontBuffer(
      swap_srv_heap->GetGPUDescriptorHandleForHeapStart(), nullptr, 0.0f,
      command_list);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

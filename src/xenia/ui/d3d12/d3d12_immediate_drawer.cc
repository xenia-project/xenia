/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_immediate_drawer.h"

#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace d3d12 {

// Generated with `xb buildhlsl`.
#include "xenia/ui/d3d12/shaders/bin/immediate_ps.h"
#include "xenia/ui/d3d12/shaders/bin/immediate_vs.h"

class D3D12ImmediateTexture : public ImmediateTexture {
 public:
  D3D12ImmediateTexture(uint32_t width, uint32_t height)
      : ImmediateTexture(width, height) {}
};

D3D12ImmediateDrawer::D3D12ImmediateDrawer(D3D12Context* graphics_context)
    : ImmediateDrawer(graphics_context), context_(graphics_context) {}

D3D12ImmediateDrawer::~D3D12ImmediateDrawer() { Shutdown(); }

bool D3D12ImmediateDrawer::Initialize() {
  auto provider = context_->GetD3D12Provider();
  auto device = provider->GetDevice();

  // Create the root signature.
  D3D12_ROOT_PARAMETER root_parameters[size_t(RootParameter::kCount)];
  D3D12_DESCRIPTOR_RANGE descriptor_range_texture, descriptor_range_sampler;
  {
    auto& root_parameter = root_parameters[size_t(RootParameter::kTexture)];
    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameter.DescriptorTable.NumDescriptorRanges = 1;
    root_parameter.DescriptorTable.pDescriptorRanges =
        &descriptor_range_texture;
    descriptor_range_texture.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range_texture.NumDescriptors = 1;
    descriptor_range_texture.BaseShaderRegister = 0;
    descriptor_range_texture.RegisterSpace = 0;
    descriptor_range_texture.OffsetInDescriptorsFromTableStart = 0;
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  {
    auto& root_parameter = root_parameters[size_t(RootParameter::kSampler)];
    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameter.DescriptorTable.NumDescriptorRanges = 1;
    root_parameter.DescriptorTable.pDescriptorRanges =
        &descriptor_range_sampler;
    descriptor_range_sampler.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    descriptor_range_sampler.NumDescriptors = 1;
    descriptor_range_sampler.BaseShaderRegister = 0;
    descriptor_range_sampler.RegisterSpace = 0;
    descriptor_range_sampler.OffsetInDescriptorsFromTableStart = 0;
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  {
    auto& root_parameter =
        root_parameters[size_t(RootParameter::kRestrictTextureSamples)];
    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameter.Constants.ShaderRegister = 0;
    root_parameter.Constants.RegisterSpace = 0;
    root_parameter.Constants.Num32BitValues = 1;
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  {
    auto& root_parameter =
        root_parameters[size_t(RootParameter::kViewportInvSize)];
    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameter.Constants.ShaderRegister = 0;
    root_parameter.Constants.RegisterSpace = 0;
    root_parameter.Constants.Num32BitValues = 2;
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  }
  D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.NumParameters = UINT(RootParameter::kCount);
  root_signature_desc.pParameters = root_parameters;
  root_signature_desc.NumStaticSamplers = 0;
  root_signature_desc.pStaticSamplers = nullptr;
  root_signature_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ID3DBlob* root_signature_blob;
  if (FAILED(D3D12SerializeRootSignature(&root_signature_desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1,
                                         &root_signature_blob, nullptr))) {
    XELOGE("Failed to serialize immediate drawer root signature");
    Shutdown();
    return false;
  }
  if (FAILED(device->CreateRootSignature(
          0, root_signature_blob->GetBufferPointer(),
          root_signature_blob->GetBufferSize(),
          IID_PPV_ARGS(&root_signature_)))) {
    XELOGE("Failed to create immediate drawer root signature");
    root_signature_blob->Release();
    Shutdown();
    return false;
  }
  root_signature_blob->Release();

  // Create the samplers.
  D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc;
  sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  sampler_heap_desc.NumDescriptors = UINT(SamplerIndex::kCount);
  sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  sampler_heap_desc.NodeMask = 0;
  if (FAILED(device->CreateDescriptorHeap(&sampler_heap_desc,
                                          IID_PPV_ARGS(&sampler_heap_)))) {
    XELOGE("Failed to create immediate drawer sampler descriptor heap");
    Shutdown();
    return false;
  }
  sampler_heap_cpu_start_ = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
  sampler_heap_gpu_start_ = sampler_heap_->GetGPUDescriptorHandleForHeapStart();
  uint32_t sampler_size = provider->GetDescriptorSizeSampler();
  // Nearest neighbor, clamp.
  D3D12_SAMPLER_DESC sampler_desc;
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.MipLODBias = 0.0f;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  sampler_desc.BorderColor[0] = 0.0f;
  sampler_desc.BorderColor[1] = 0.0f;
  sampler_desc.BorderColor[2] = 0.0f;
  sampler_desc.BorderColor[3] = 0.0f;
  sampler_desc.MinLOD = 0.0f;
  sampler_desc.MaxLOD = 0.0f;
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_handle;
  sampler_handle.ptr = sampler_heap_cpu_start_.ptr +
                       uint32_t(SamplerIndex::kNearestClamp) * sampler_size;
  device->CreateSampler(&sampler_desc, sampler_handle);
  // Bilinear, clamp.
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_handle.ptr = sampler_heap_cpu_start_.ptr +
                       uint32_t(SamplerIndex::kLinearClamp) * sampler_size;
  device->CreateSampler(&sampler_desc, sampler_handle);
  // Nearest neighbor, repeat.
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler_handle.ptr = sampler_heap_cpu_start_.ptr +
                       uint32_t(SamplerIndex::kNearestRepeat) * sampler_size;
  device->CreateSampler(&sampler_desc, sampler_handle);
  // Bilinear, repeat.
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_handle.ptr = sampler_heap_cpu_start_.ptr +
                       uint32_t(SamplerIndex::kLinearRepeat) * sampler_size;
  device->CreateSampler(&sampler_desc, sampler_handle);

  // Reset the current state.
  current_command_list_ = nullptr;

  return true;
}

void D3D12ImmediateDrawer::Shutdown() {
  for (auto& texture_upload : texture_uploads_submitted_) {
    texture_upload.data_resource->Release();
  }
  texture_uploads_submitted_.clear();

  if (sampler_heap_ != nullptr) {
    sampler_heap_->Release();
    sampler_heap_ = nullptr;
  }

  if (root_signature_ != nullptr) {
    root_signature_->Release();
    root_signature_ = nullptr;
  }
}

std::unique_ptr<ImmediateTexture> D3D12ImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter, bool repeat,
    const uint8_t* data) {
  // TODO(Triang3l): Implement CreateTexture.
  auto texture = std::make_unique<D3D12ImmediateTexture>(width, height);
  return std::unique_ptr<ImmediateTexture>(texture.release());
}

void D3D12ImmediateDrawer::UpdateTexture(ImmediateTexture* texture,
                                         const uint8_t* data) {
  // TODO(Triang3l): Implement UpdateTexture.
}

void D3D12ImmediateDrawer::Begin(int render_target_width,
                                 int render_target_height) {
  // Use the compositing command list.
  current_command_list_ = context_->GetSwapCommandList();

  uint32_t queue_frame = context_->GetCurrentQueueFrame();
  uint64_t last_completed_frame = context_->GetLastCompletedFrame();

  // Remove temporary buffers for completed texture uploads.
  auto erase_uploads_end = texture_uploads_submitted_.begin();
  while (erase_uploads_end != texture_uploads_submitted_.end()) {
    uint64_t upload_frame = erase_uploads_end->frame;
    if (upload_frame > last_completed_frame) {
      ++erase_uploads_end;
      break;
    }
    erase_uploads_end->data_resource->Release();
    ++erase_uploads_end;
  }
  texture_uploads_submitted_.erase(texture_uploads_submitted_.begin(),
                                   erase_uploads_end);
}

void D3D12ImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  // TODO(Triang3l): Implement BeginDrawBatch.
}

void D3D12ImmediateDrawer::Draw(const ImmediateDraw& draw) {
  // TODO(Triang3l): Implement Draw.
}

void D3D12ImmediateDrawer::EndDrawBatch() {
  // TODO(Triang3l): Implement EndDrawBatch.
}

void D3D12ImmediateDrawer::End() { current_command_list_ = nullptr; }

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

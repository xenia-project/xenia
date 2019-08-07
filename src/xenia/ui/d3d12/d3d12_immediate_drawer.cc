/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_immediate_drawer.h"

#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/d3d12/d3d12_util.h"

namespace xe {
namespace ui {
namespace d3d12 {

// Generated with `xb buildhlsl`.
#include "xenia/ui/d3d12/shaders/dxbc/immediate_ps.h"
#include "xenia/ui/d3d12/shaders/dxbc/immediate_vs.h"

class D3D12ImmediateTexture : public ImmediateTexture {
 public:
  static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

  D3D12ImmediateTexture(uint32_t width, uint32_t height,
                        ImmediateTextureFilter filter, bool repeat);
  ~D3D12ImmediateTexture() override;

  bool Initialize(ID3D12Device* device);
  void Shutdown();

  ID3D12Resource* GetResource() const { return resource_; }
  void Transition(D3D12_RESOURCE_STATES new_state,
                  ID3D12GraphicsCommandList* command_list);

  ImmediateTextureFilter GetFilter() const { return filter_; }
  bool IsRepeated() const { return repeat_; }

 private:
  ID3D12Resource* resource_ = nullptr;
  D3D12_RESOURCE_STATES state_;
  ImmediateTextureFilter filter_;
  bool repeat_;
};

D3D12ImmediateTexture::D3D12ImmediateTexture(uint32_t width, uint32_t height,
                                             ImmediateTextureFilter filter,
                                             bool repeat)
    : ImmediateTexture(width, height), filter_(filter), repeat_(repeat) {
  handle = reinterpret_cast<uintptr_t>(this);
}

D3D12ImmediateTexture::~D3D12ImmediateTexture() { Shutdown(); }

bool D3D12ImmediateTexture::Initialize(ID3D12Device* device) {
  // The first operation will likely be copying the contents.
  state_ = D3D12_RESOURCE_STATE_COPY_DEST;

  D3D12_RESOURCE_DESC resource_desc;
  resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Alignment = 0;
  resource_desc.Width = width;
  resource_desc.Height = height;
  resource_desc.DepthOrArraySize = 1;
  resource_desc.MipLevels = 1;
  resource_desc.Format = kFormat;
  resource_desc.SampleDesc.Count = 1;
  resource_desc.SampleDesc.Quality = 0;
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  if (FAILED(device->CreateCommittedResource(
          &util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE, &resource_desc,
          state_, nullptr, IID_PPV_ARGS(&resource_)))) {
    XELOGE("Failed to create a %ux%u texture for immediate drawing", width,
           height);
    return false;
  }

  return true;
}

void D3D12ImmediateTexture::Shutdown() {
  if (resource_ != nullptr) {
    resource_->Release();
    resource_ = nullptr;
  }
}

void D3D12ImmediateTexture::Transition(
    D3D12_RESOURCE_STATES new_state, ID3D12GraphicsCommandList* command_list) {
  if (resource_ == nullptr || state_ == new_state) {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource_;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = state_;
  barrier.Transition.StateAfter = new_state;
  command_list->ResourceBarrier(1, &barrier);
  state_ = new_state;
}

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
    auto& root_parameter =
        root_parameters[size_t(RootParameter::kRestrictTextureSamples)];
    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameter.Constants.ShaderRegister = 0;
    root_parameter.Constants.RegisterSpace = 0;
    root_parameter.Constants.Num32BitValues = 1;
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
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
  root_signature_ = util::CreateRootSignature(provider, root_signature_desc);
  if (root_signature_ == nullptr) {
    XELOGE("Failed to create the immediate drawer root signature");
    Shutdown();
    return false;
  }

  // Create the pipelines.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root_signature_;
  pipeline_desc.VS.pShaderBytecode = immediate_vs;
  pipeline_desc.VS.BytecodeLength = sizeof(immediate_vs);
  pipeline_desc.PS.pShaderBytecode = immediate_ps;
  pipeline_desc.PS.BytecodeLength = sizeof(immediate_ps);
  D3D12_RENDER_TARGET_BLEND_DESC& pipeline_blend_desc =
      pipeline_desc.BlendState.RenderTarget[0];
  pipeline_blend_desc.BlendEnable = TRUE;
  pipeline_blend_desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
  pipeline_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  pipeline_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
  pipeline_blend_desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
  pipeline_blend_desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  pipeline_blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  pipeline_blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline_desc.SampleMask = UINT_MAX;
  pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
  D3D12_INPUT_ELEMENT_DESC pipeline_input_elements[3] = {};
  pipeline_input_elements[0].SemanticName = "POSITION";
  pipeline_input_elements[0].Format = DXGI_FORMAT_R32G32_FLOAT;
  pipeline_input_elements[0].AlignedByteOffset = 0;
  pipeline_input_elements[1].SemanticName = "TEXCOORD";
  pipeline_input_elements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
  pipeline_input_elements[1].AlignedByteOffset = 8;
  pipeline_input_elements[2].SemanticName = "COLOR";
  pipeline_input_elements[2].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline_input_elements[2].AlignedByteOffset = 16;
  pipeline_desc.InputLayout.pInputElementDescs = pipeline_input_elements;
  pipeline_desc.InputLayout.NumElements =
      UINT(xe::countof(pipeline_input_elements));
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline_desc.NumRenderTargets = 1;
  pipeline_desc.RTVFormats[0] = D3D12Context::kSwapChainFormat;
  pipeline_desc.SampleDesc.Count = 1;
  if (FAILED(device->CreateGraphicsPipelineState(
          &pipeline_desc, IID_PPV_ARGS(&pipeline_triangle_)))) {
    XELOGE("Failed to create immediate drawer triangle pipeline state");
    Shutdown();
    return false;
  }
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
  if (FAILED(device->CreateGraphicsPipelineState(
          &pipeline_desc, IID_PPV_ARGS(&pipeline_line_)))) {
    XELOGE("Failed to create immediate drawer line pipeline state");
    Shutdown();
    return false;
  }

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
  uint32_t sampler_size = provider->GetSamplerDescriptorSize();
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

  // Create pools for draws.
  vertex_buffer_pool_ =
      std::make_unique<UploadBufferPool>(context_, 2 * 1024 * 1024);
  texture_descriptor_pool_ = std::make_unique<DescriptorHeapPool>(
      context_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048);

  // Reset the current state.
  current_command_list_ = nullptr;

  return true;
}

void D3D12ImmediateDrawer::Shutdown() {
  for (auto& texture_upload : texture_uploads_submitted_) {
    texture_upload.buffer->Release();
  }
  texture_uploads_submitted_.clear();

  for (auto& texture_upload : texture_uploads_pending_) {
    texture_upload.buffer->Release();
  }
  texture_uploads_pending_.clear();

  texture_descriptor_pool_.reset();
  vertex_buffer_pool_.reset();

  util::ReleaseAndNull(sampler_heap_);

  util::ReleaseAndNull(pipeline_line_);
  util::ReleaseAndNull(pipeline_triangle_);

  util::ReleaseAndNull(root_signature_);
}

std::unique_ptr<ImmediateTexture> D3D12ImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter, bool repeat,
    const uint8_t* data) {
  auto texture =
      std::make_unique<D3D12ImmediateTexture>(width, height, filter, repeat);
  texture->Initialize(context_->GetD3D12Provider()->GetDevice());
  if (data != nullptr) {
    UpdateTexture(texture.get(), data);
  }
  return std::unique_ptr<ImmediateTexture>(texture.release());
}

void D3D12ImmediateDrawer::UpdateTexture(ImmediateTexture* texture,
                                         const uint8_t* data) {
  D3D12ImmediateTexture* d3d_texture =
      static_cast<D3D12ImmediateTexture*>(texture);
  ID3D12Resource* texture_resource = d3d_texture->GetResource();
  if (texture_resource == nullptr) {
    return;
  }
  uint32_t width = d3d_texture->width, height = d3d_texture->height;

  auto device = context_->GetD3D12Provider()->GetDevice();

  // Create and fill the upload buffer.
  D3D12_RESOURCE_DESC texture_desc = texture_resource->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT upload_footprint;
  UINT64 upload_size;
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &upload_footprint,
                                nullptr, nullptr, &upload_size);
  D3D12_RESOURCE_DESC buffer_desc;
  util::FillBufferResourceDesc(buffer_desc, upload_size,
                               D3D12_RESOURCE_FLAG_NONE);
  ID3D12Resource* buffer;
  if (FAILED(device->CreateCommittedResource(
          &util::kHeapPropertiesUpload, D3D12_HEAP_FLAG_NONE, &buffer_desc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)))) {
    XELOGE(
        "Failed to create an upload buffer for a %ux%u texture for "
        "immediate drawing",
        width, height);
    return;
  }
  D3D12_RANGE buffer_read_range;
  buffer_read_range.Begin = 0;
  buffer_read_range.End = 0;
  void* buffer_mapping;
  if (FAILED(buffer->Map(0, &buffer_read_range, &buffer_mapping))) {
    XELOGE(
        "Failed to map an upload buffer for a %ux%u texture for immediate "
        "drawing",
        width, height);
    buffer->Release();
    return;
  }
  uint8_t* buffer_row =
      reinterpret_cast<uint8_t*>(buffer_mapping) + upload_footprint.Offset;
  for (uint32_t i = 0; i < height; ++i) {
    std::memcpy(buffer_row, data, width * 4);
    data += width * 4;
    buffer_row += upload_footprint.Footprint.RowPitch;
  }
  buffer->Unmap(0, nullptr);

  if (current_command_list_ != nullptr) {
    // Upload the texture right now if we can.
    d3d_texture->Transition(D3D12_RESOURCE_STATE_COPY_DEST,
                            current_command_list_);
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = buffer;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_source.PlacedFootprint = upload_footprint;
    location_dest.pResource = texture_resource;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_dest.SubresourceIndex = 0;
    current_command_list_->CopyTextureRegion(&location_dest, 0, 0, 0,
                                             &location_source, nullptr);
    SubmittedTextureUpload submitted_upload;
    submitted_upload.buffer = buffer;
    submitted_upload.frame = context_->GetCurrentFrame();
    texture_uploads_submitted_.push_back(submitted_upload);
  } else {
    // Defer uploading to the next frame when there's a command list.
    PendingTextureUpload pending_upload;
    pending_upload.texture = texture;
    pending_upload.buffer = buffer;
    texture_uploads_pending_.push_back(pending_upload);
  }
}

void D3D12ImmediateDrawer::Begin(int render_target_width,
                                 int render_target_height) {
  auto device = context_->GetD3D12Provider()->GetDevice();

  // Use the compositing command list.
  current_command_list_ = context_->GetSwapCommandList();

  uint64_t current_frame = context_->GetCurrentFrame();
  uint64_t last_completed_frame = context_->GetLastCompletedFrame();

  // Remove temporary buffers for completed texture uploads.
  auto erase_uploads_end = texture_uploads_submitted_.begin();
  while (erase_uploads_end != texture_uploads_submitted_.end()) {
    uint64_t upload_frame = erase_uploads_end->frame;
    if (upload_frame > last_completed_frame) {
      ++erase_uploads_end;
      break;
    }
    erase_uploads_end->buffer->Release();
    ++erase_uploads_end;
  }
  texture_uploads_submitted_.erase(texture_uploads_submitted_.begin(),
                                   erase_uploads_end);

  // Submit texture updates that happened between frames.
  while (!texture_uploads_pending_.empty()) {
    const PendingTextureUpload& pending_upload =
        texture_uploads_pending_.back();
    D3D12ImmediateTexture* texture =
        static_cast<D3D12ImmediateTexture*>(pending_upload.texture);
    texture->Transition(D3D12_RESOURCE_STATE_COPY_DEST, current_command_list_);
    ID3D12Resource* texture_resource = texture->GetResource();
    D3D12_RESOURCE_DESC texture_desc = texture_resource->GetDesc();
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = pending_upload.buffer;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    device->GetCopyableFootprints(&texture_desc, 0, 1, 0,
                                  &location_source.PlacedFootprint, nullptr,
                                  nullptr, nullptr);
    location_dest.pResource = texture_resource;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_dest.SubresourceIndex = 0;
    current_command_list_->CopyTextureRegion(&location_dest, 0, 0, 0,
                                             &location_source, nullptr);
    SubmittedTextureUpload submitted_upload;
    submitted_upload.buffer = pending_upload.buffer;
    submitted_upload.frame = current_frame;
    texture_uploads_submitted_.push_back(submitted_upload);
    texture_uploads_pending_.pop_back();
  }

  vertex_buffer_pool_->BeginFrame();
  texture_descriptor_pool_->BeginFrame();
  texture_descriptor_pool_full_update_ = 0;

  current_render_target_width_ = render_target_width;
  current_render_target_height_ = render_target_height;
  D3D12_VIEWPORT viewport;
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = float(render_target_width);
  viewport.Height = float(render_target_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  current_command_list_->RSSetViewports(1, &viewport);

  current_command_list_->SetGraphicsRootSignature(root_signature_);
  float viewport_inv_scale[2];
  viewport_inv_scale[0] = 1.0f / viewport.Width;
  viewport_inv_scale[1] = 1.0f / viewport.Height;
  current_command_list_->SetGraphicsRoot32BitConstants(
      UINT(RootParameter::kViewportInvSize), 2, viewport_inv_scale, 0);
}

void D3D12ImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  assert_not_null(current_command_list_);
  if (current_command_list_ == nullptr) {
    return;
  }

  batch_open_ = false;

  // Bind the vertices.
  D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
  vertex_buffer_view.StrideInBytes = UINT(sizeof(ImmediateVertex));
  vertex_buffer_view.SizeInBytes =
      batch.vertex_count * uint32_t(sizeof(ImmediateVertex));
  void* vertex_buffer_mapping = vertex_buffer_pool_->RequestFull(
      vertex_buffer_view.SizeInBytes, nullptr, nullptr,
      &vertex_buffer_view.BufferLocation);
  if (vertex_buffer_mapping == nullptr) {
    XELOGE("Failed to get a buffer for %u vertices in the immediate drawer",
           batch.vertex_count);
    return;
  }
  std::memcpy(vertex_buffer_mapping, batch.vertices,
              vertex_buffer_view.SizeInBytes);
  current_command_list_->IASetVertexBuffers(0, 1, &vertex_buffer_view);

  // Bind the indices.
  batch_has_index_buffer_ = batch.indices != nullptr;
  if (batch_has_index_buffer_) {
    D3D12_INDEX_BUFFER_VIEW index_buffer_view;
    index_buffer_view.SizeInBytes = batch.index_count * sizeof(uint16_t);
    index_buffer_view.Format = DXGI_FORMAT_R16_UINT;
    void* index_buffer_mapping = vertex_buffer_pool_->RequestFull(
        index_buffer_view.SizeInBytes, nullptr, nullptr,
        &index_buffer_view.BufferLocation);
    if (index_buffer_mapping == nullptr) {
      XELOGE("Failed to get a buffer for %u indices in the immediate drawer",
             batch.index_count);
      return;
    }
    std::memcpy(index_buffer_mapping, batch.indices,
                index_buffer_view.SizeInBytes);
    current_command_list_->IASetIndexBuffer(&index_buffer_view);
  }

  batch_open_ = true;
  current_primitive_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  current_texture_ = nullptr;
  current_sampler_index_ = SamplerIndex::kInvalid;
}

void D3D12ImmediateDrawer::Draw(const ImmediateDraw& draw) {
  assert_not_null(current_command_list_);
  if (current_command_list_ == nullptr) {
    return;
  }

  if (!batch_open_) {
    // Could be an error while obtaining the vertex and index buffers.
    return;
  }

  auto provider = context_->GetD3D12Provider();
  auto device = provider->GetDevice();

  // Bind the texture.
  auto texture = reinterpret_cast<D3D12ImmediateTexture*>(draw.texture_handle);
  ID3D12Resource* texture_resource;
  if (texture != nullptr) {
    texture->Transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        current_command_list_);
    texture_resource = texture->GetResource();
  } else {
    texture_resource = nullptr;
  }
  bool bind_texture = current_texture_ != texture;
  uint32_t texture_descriptor_index;
  uint64_t texture_full_update = texture_descriptor_pool_->Request(
      texture_descriptor_pool_full_update_, bind_texture ? 1 : 0, 1,
      texture_descriptor_index);
  if (texture_full_update == 0) {
    return;
  }
  if (texture_descriptor_pool_full_update_ != texture_full_update) {
    bind_texture = true;
    texture_descriptor_pool_full_update_ = texture_full_update;
    ID3D12DescriptorHeap* descriptor_heaps[] = {
        texture_descriptor_pool_->GetLastRequestHeap(), sampler_heap_};
    current_command_list_->SetDescriptorHeaps(2, descriptor_heaps);
  }
  if (bind_texture) {
    D3D12_SHADER_RESOURCE_VIEW_DESC texture_view_desc;
    texture_view_desc.Format = D3D12ImmediateTexture::kFormat;
    texture_view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    texture_view_desc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    texture_view_desc.Texture2D.MostDetailedMip = 0;
    texture_view_desc.Texture2D.MipLevels = 1;
    texture_view_desc.Texture2D.PlaneSlice = 0;
    texture_view_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(
        texture_resource, &texture_view_desc,
        provider->OffsetViewDescriptor(
            texture_descriptor_pool_->GetLastRequestHeapCPUStart(),
            texture_descriptor_index));
    current_command_list_->SetGraphicsRootDescriptorTable(
        UINT(RootParameter::kTexture),
        provider->OffsetViewDescriptor(
            texture_descriptor_pool_->GetLastRequestHeapGPUStart(),
            texture_descriptor_index));
    current_texture_ = texture;
  }

  // Bind the sampler.
  SamplerIndex sampler_index;
  if (texture != nullptr) {
    if (texture->GetFilter() == ImmediateTextureFilter::kLinear) {
      sampler_index = texture->IsRepeated() ? SamplerIndex::kLinearRepeat
                                            : SamplerIndex::kLinearClamp;
    } else {
      sampler_index = texture->IsRepeated() ? SamplerIndex::kNearestRepeat
                                            : SamplerIndex::kNearestClamp;
    }
  } else {
    sampler_index = SamplerIndex::kNearestClamp;
  }
  if (current_sampler_index_ != sampler_index) {
    current_command_list_->SetGraphicsRootDescriptorTable(
        UINT(RootParameter::kSampler),
        provider->OffsetSamplerDescriptor(sampler_heap_gpu_start_,
                                          uint32_t(sampler_index)));
    current_sampler_index_ = sampler_index;
  }

  // Set whether texture coordinates need to be restricted.
  uint32_t restrict_texture_samples = draw.restrict_texture_samples ? 1 : 0;
  current_command_list_->SetGraphicsRoot32BitConstants(
      UINT(RootParameter::kRestrictTextureSamples), 1,
      &restrict_texture_samples, 0);

  // Set the primitive type and the pipeline for it.
  D3D_PRIMITIVE_TOPOLOGY primitive_topology;
  ID3D12PipelineState* pipeline;
  switch (draw.primitive_type) {
    case ImmediatePrimitiveType::kLines:
      primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
      pipeline = pipeline_line_;
      break;
    case ImmediatePrimitiveType::kTriangles:
      primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      pipeline = pipeline_triangle_;
      break;
    default:
      assert_unhandled_case(draw.primitive_type);
      return;
  }
  if (current_primitive_topology_ != primitive_topology) {
    current_command_list_->IASetPrimitiveTopology(primitive_topology);
    current_command_list_->SetPipelineState(pipeline);
    current_primitive_topology_ = primitive_topology;
  }

  // Set the scissor rectangle if enabled.
  D3D12_RECT scissor;
  if (draw.scissor) {
    scissor.left = draw.scissor_rect[0];
    scissor.top = current_render_target_height_ -
                  (draw.scissor_rect[1] + draw.scissor_rect[3]);
    scissor.right = scissor.left + draw.scissor_rect[2];
    scissor.bottom = scissor.top + draw.scissor_rect[3];
  } else {
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = current_render_target_width_;
    scissor.bottom = current_render_target_height_;
  }
  current_command_list_->RSSetScissorRects(1, &scissor);

  // Draw.
  if (batch_has_index_buffer_) {
    current_command_list_->DrawIndexedInstanced(
        draw.count, 1, draw.index_offset, draw.base_vertex, 0);
  } else {
    current_command_list_->DrawInstanced(draw.count, 1, draw.base_vertex, 0);
  }
}

void D3D12ImmediateDrawer::EndDrawBatch() { batch_open_ = false; }

void D3D12ImmediateDrawer::End() {
  texture_descriptor_pool_->EndFrame();
  vertex_buffer_pool_->EndFrame();

  current_command_list_ = nullptr;
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

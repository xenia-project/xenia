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
#include <memory>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/d3d12/d3d12_context.h"
#include "xenia/ui/d3d12/d3d12_util.h"

namespace xe {
namespace ui {
namespace d3d12 {

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/ui/shaders/bytecode/d3d12_5_1/immediate_ps.h"
#include "xenia/ui/shaders/bytecode/d3d12_5_1/immediate_vs.h"
}  // namespace shaders

D3D12ImmediateDrawer::D3D12ImmediateTexture::D3D12ImmediateTexture(
    uint32_t width, uint32_t height, ID3D12Resource* resource,
    SamplerIndex sampler_index, D3D12ImmediateDrawer* immediate_drawer,
    size_t immediate_drawer_index)
    : ImmediateTexture(width, height),
      resource_(resource),
      sampler_index_(sampler_index),
      immediate_drawer_(immediate_drawer),
      immediate_drawer_index_(immediate_drawer_index) {
  if (resource_) {
    resource_->AddRef();
  }
}

D3D12ImmediateDrawer::D3D12ImmediateTexture::~D3D12ImmediateTexture() {
  if (immediate_drawer_) {
    immediate_drawer_->OnImmediateTextureDestroyed(*this);
  }
  if (resource_) {
    resource_->Release();
  }
}

void D3D12ImmediateDrawer::D3D12ImmediateTexture::OnImmediateDrawerShutdown() {
  immediate_drawer_ = nullptr;
  // Lifetime is not managed anymore, so don't keep the resource either.
  util::ReleaseAndNull(resource_);
}

D3D12ImmediateDrawer::D3D12ImmediateDrawer(D3D12Context& graphics_context)
    : ImmediateDrawer(&graphics_context), context_(graphics_context) {}

D3D12ImmediateDrawer::~D3D12ImmediateDrawer() { Shutdown(); }

bool D3D12ImmediateDrawer::Initialize() {
  const D3D12Provider& provider = context_.GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();

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
        root_parameters[size_t(RootParameter::kViewportSizeInv)];
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
    XELOGE("Failed to create the Direct3D 12 immediate drawer root signature");
    Shutdown();
    return false;
  }

  // Create the pipelines.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root_signature_;
  pipeline_desc.VS.pShaderBytecode = shaders::immediate_vs;
  pipeline_desc.VS.BytecodeLength = sizeof(shaders::immediate_vs);
  pipeline_desc.PS.pShaderBytecode = shaders::immediate_ps;
  pipeline_desc.PS.BytecodeLength = sizeof(shaders::immediate_ps);
  D3D12_RENDER_TARGET_BLEND_DESC& pipeline_blend_desc =
      pipeline_desc.BlendState.RenderTarget[0];
  pipeline_blend_desc.BlendEnable = TRUE;
  pipeline_blend_desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
  pipeline_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  pipeline_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
  // Don't change alpha (always 1).
  pipeline_blend_desc.SrcBlendAlpha = D3D12_BLEND_ZERO;
  pipeline_blend_desc.DestBlendAlpha = D3D12_BLEND_ONE;
  pipeline_blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  pipeline_blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED |
                                              D3D12_COLOR_WRITE_ENABLE_GREEN |
                                              D3D12_COLOR_WRITE_ENABLE_BLUE;
  pipeline_desc.SampleMask = UINT_MAX;
  pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline_desc.RasterizerState.FrontCounterClockwise = FALSE;
  pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
  D3D12_INPUT_ELEMENT_DESC pipeline_input_elements[3] = {};
  pipeline_input_elements[0].SemanticName = "POSITION";
  pipeline_input_elements[0].Format = DXGI_FORMAT_R32G32_FLOAT;
  pipeline_input_elements[0].AlignedByteOffset = offsetof(ImmediateVertex, x);
  pipeline_input_elements[1].SemanticName = "TEXCOORD";
  pipeline_input_elements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
  pipeline_input_elements[1].AlignedByteOffset = offsetof(ImmediateVertex, u);
  pipeline_input_elements[2].SemanticName = "COLOR";
  pipeline_input_elements[2].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline_input_elements[2].AlignedByteOffset =
      offsetof(ImmediateVertex, color);
  pipeline_desc.InputLayout.pInputElementDescs = pipeline_input_elements;
  pipeline_desc.InputLayout.NumElements =
      UINT(xe::countof(pipeline_input_elements));
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline_desc.NumRenderTargets = 1;
  pipeline_desc.RTVFormats[0] = D3D12Context::kSwapChainFormat;
  pipeline_desc.SampleDesc.Count = 1;
  if (FAILED(device->CreateGraphicsPipelineState(
          &pipeline_desc, IID_PPV_ARGS(&pipeline_triangle_)))) {
    XELOGE(
        "Failed to create the Direct3D 12 immediate drawer triangle pipeline "
        "state");
    Shutdown();
    return false;
  }
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
  if (FAILED(device->CreateGraphicsPipelineState(
          &pipeline_desc, IID_PPV_ARGS(&pipeline_line_)))) {
    XELOGE(
        "Failed to create the Direct3D 12 immediate drawer line pipeline "
        "state");
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
    XELOGE(
        "Failed to create the Direct3D 12 immediate drawer sampler descriptor "
        "heap");
    Shutdown();
    return false;
  }
  sampler_heap_cpu_start_ = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
  sampler_heap_gpu_start_ = sampler_heap_->GetGPUDescriptorHandleForHeapStart();
  uint32_t sampler_size = provider.GetSamplerDescriptorSize();
  // Nearest neighbor, clamp.
  D3D12_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.MaxAnisotropy = 1;
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_handle;
  sampler_handle.ptr = sampler_heap_cpu_start_.ptr +
                       uint32_t(SamplerIndex::kNearestClamp) * sampler_size;
  device->CreateSampler(&sampler_desc, sampler_handle);
  // Bilinear, clamp.
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_handle.ptr = sampler_heap_cpu_start_.ptr +
                       uint32_t(SamplerIndex::kLinearClamp) * sampler_size;
  device->CreateSampler(&sampler_desc, sampler_handle);
  // Bilinear, repeat.
  sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler_handle.ptr = sampler_heap_cpu_start_.ptr +
                       uint32_t(SamplerIndex::kLinearRepeat) * sampler_size;
  device->CreateSampler(&sampler_desc, sampler_handle);
  // Nearest neighbor, repeat.
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler_handle.ptr = sampler_heap_cpu_start_.ptr +
                       uint32_t(SamplerIndex::kNearestRepeat) * sampler_size;
  device->CreateSampler(&sampler_desc, sampler_handle);

  // Create pools for draws.
  vertex_buffer_pool_ = std::make_unique<D3D12UploadBufferPool>(provider);
  texture_descriptor_pool_ = std::make_unique<D3D12DescriptorHeapPool>(
      device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048);

  // Reset the current state.
  current_command_list_ = nullptr;
  batch_open_ = false;

  return true;
}

void D3D12ImmediateDrawer::Shutdown() {
  for (auto& deleted_texture : textures_deleted_) {
    deleted_texture.first->Release();
  }
  textures_deleted_.clear();

  for (auto& texture_upload : texture_uploads_submitted_) {
    texture_upload.buffer->Release();
    texture_upload.texture->Release();
  }
  texture_uploads_submitted_.clear();

  for (auto& texture_upload : texture_uploads_pending_) {
    texture_upload.buffer->Release();
    texture_upload.texture->Release();
  }
  texture_uploads_pending_.clear();

  for (D3D12ImmediateTexture* texture : textures_) {
    texture->OnImmediateDrawerShutdown();
  }
  textures_.clear();

  texture_descriptor_pool_.reset();
  vertex_buffer_pool_.reset();

  util::ReleaseAndNull(sampler_heap_);

  util::ReleaseAndNull(pipeline_line_);
  util::ReleaseAndNull(pipeline_triangle_);

  util::ReleaseAndNull(root_signature_);
}

std::unique_ptr<ImmediateTexture> D3D12ImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter,
    bool is_repeated, const uint8_t* data) {
  const D3D12Provider& provider = context_.GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  D3D12_HEAP_FLAGS heap_flag_create_not_zeroed =
      provider.GetHeapFlagCreateNotZeroed();

  D3D12_RESOURCE_DESC resource_desc;
  resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Alignment = 0;
  resource_desc.Width = width;
  resource_desc.Height = height;
  resource_desc.DepthOrArraySize = 1;
  resource_desc.MipLevels = 1;
  resource_desc.Format = D3D12ImmediateTexture::kFormat;
  resource_desc.SampleDesc.Count = 1;
  resource_desc.SampleDesc.Quality = 0;
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  ID3D12Resource* resource;
  if (SUCCEEDED(provider.GetDevice()->CreateCommittedResource(
          &util::kHeapPropertiesDefault, heap_flag_create_not_zeroed,
          &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&resource)))) {
    // Create and fill the upload buffer.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT upload_footprint;
    UINT64 upload_size;
    device->GetCopyableFootprints(&resource_desc, 0, 1, 0, &upload_footprint,
                                  nullptr, nullptr, &upload_size);
    D3D12_RESOURCE_DESC upload_buffer_desc;
    util::FillBufferResourceDesc(upload_buffer_desc, upload_size,
                                 D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* upload_buffer;
    if (SUCCEEDED(device->CreateCommittedResource(
            &util::kHeapPropertiesUpload, heap_flag_create_not_zeroed,
            &upload_buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload_buffer)))) {
      D3D12_RANGE upload_buffer_read_range;
      upload_buffer_read_range.Begin = 0;
      upload_buffer_read_range.End = 0;
      void* upload_buffer_mapping;
      if (SUCCEEDED(upload_buffer->Map(0, &upload_buffer_read_range,
                                       &upload_buffer_mapping))) {
        size_t data_row_length = sizeof(uint32_t) * width;
        if (data_row_length == upload_footprint.Footprint.RowPitch) {
          std::memcpy(upload_buffer_mapping, data, data_row_length * height);
        } else {
          uint8_t* upload_buffer_row =
              reinterpret_cast<uint8_t*>(upload_buffer_mapping) +
              upload_footprint.Offset;
          const uint8_t* data_row = data;
          for (uint32_t i = 0; i < height; ++i) {
            std::memcpy(upload_buffer_row, data_row, data_row_length);
            data_row += data_row_length;
            upload_buffer_row += upload_footprint.Footprint.RowPitch;
          }
        }
        upload_buffer->Unmap(0, nullptr);
        // Defer uploading and transition to the next draw.
        PendingTextureUpload& pending_upload =
            texture_uploads_pending_.emplace_back();
        // While the upload has not been yet completed, keep a reference to the
        // resource because its lifetime is not tied to that of the
        // ImmediateTexture (and thus to context's submissions) now.
        resource->AddRef();
        pending_upload.texture = resource;
        pending_upload.buffer = upload_buffer;
      } else {
        XELOGE(
            "Failed to map a Direct3D 12 upload buffer for a {}x{} texture for "
            "immediate drawing",
            width, height);
        upload_buffer->Release();
        resource->Release();
        resource = nullptr;
      }
    } else {
      XELOGE(
          "Failed to create a Direct3D 12 upload buffer for a {}x{} texture "
          "for immediate drawing",
          width, height);
      resource->Release();
      resource = nullptr;
    }
  } else {
    XELOGE("Failed to create a {}x{} Direct3D 12 texture for immediate drawing",
           width, height);
    resource = nullptr;
  }

  SamplerIndex sampler_index;
  if (filter == ImmediateTextureFilter::kLinear) {
    sampler_index =
        is_repeated ? SamplerIndex::kLinearRepeat : SamplerIndex::kLinearClamp;
  } else {
    sampler_index = is_repeated ? SamplerIndex::kNearestRepeat
                                : SamplerIndex::kNearestClamp;
  }

  // Manage by this immediate drawer if successfully created a resource.
  std::unique_ptr<D3D12ImmediateTexture> texture =
      std::make_unique<D3D12ImmediateTexture>(
          width, height, resource, sampler_index, resource ? this : nullptr,
          textures_.size());
  if (resource) {
    textures_.push_back(texture.get());
    // D3D12ImmediateTexture now holds a reference.
    resource->Release();
  }
  return std::move(texture);
}

void D3D12ImmediateDrawer::Begin(int render_target_width,
                                 int render_target_height) {
  assert_null(current_command_list_);
  assert_false(batch_open_);

  ID3D12Device* device = context_.GetD3D12Provider().GetDevice();

  // Use the compositing command list.
  current_command_list_ = context_.GetSwapCommandList();

  uint64_t completed_fence_value = context_.GetSwapCompletedFenceValue();

  // Release deleted textures.
  for (auto it = textures_deleted_.begin(); it != textures_deleted_.end();) {
    if (it->second > completed_fence_value) {
      ++it;
      continue;
    }
    it->first->Release();
    if (std::next(it) != textures_deleted_.end()) {
      *it = textures_deleted_.back();
    }
    textures_deleted_.pop_back();
  }

  // Release upload buffers for completed texture uploads.
  auto erase_uploads_end = texture_uploads_submitted_.begin();
  while (erase_uploads_end != texture_uploads_submitted_.end()) {
    if (erase_uploads_end->fence_value > completed_fence_value) {
      break;
    }
    erase_uploads_end->buffer->Release();
    // Release the texture reference held for uploading.
    erase_uploads_end->texture->Release();
    ++erase_uploads_end;
  }
  texture_uploads_submitted_.erase(texture_uploads_submitted_.begin(),
                                   erase_uploads_end);

  vertex_buffer_pool_->Reclaim(completed_fence_value);
  texture_descriptor_pool_->Reclaim(completed_fence_value);

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
  float viewport_inv_size[2];
  viewport_inv_size[0] = 1.0f / viewport.Width;
  viewport_inv_size[1] = 1.0f / viewport.Height;
  current_command_list_->SetGraphicsRoot32BitConstants(
      UINT(RootParameter::kViewportSizeInv), 2, viewport_inv_size, 0);

  current_scissor_.left = 0;
  current_scissor_.top = 0;
  current_scissor_.right = 0;
  current_scissor_.bottom = 0;

  current_primitive_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  current_texture_ = nullptr;
  current_texture_descriptor_heap_index_ =
      D3D12DescriptorHeapPool::kHeapIndexInvalid;
  current_sampler_index_ = SamplerIndex::kInvalid;
}

void D3D12ImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  assert_false(batch_open_);
  assert_not_null(current_command_list_);

  uint64_t current_fence_value = context_.GetSwapCurrentFenceValue();

  // Bind the vertices.
  D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
  vertex_buffer_view.StrideInBytes = UINT(sizeof(ImmediateVertex));
  vertex_buffer_view.SizeInBytes =
      UINT(sizeof(ImmediateVertex)) * batch.vertex_count;
  void* vertex_buffer_mapping = vertex_buffer_pool_->Request(
      current_fence_value, vertex_buffer_view.SizeInBytes, sizeof(float),
      nullptr, nullptr, &vertex_buffer_view.BufferLocation);
  if (vertex_buffer_mapping == nullptr) {
    XELOGE("Failed to get a buffer for {} vertices in the immediate drawer",
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
    index_buffer_view.SizeInBytes = UINT(sizeof(uint16_t)) * batch.index_count;
    index_buffer_view.Format = DXGI_FORMAT_R16_UINT;
    void* index_buffer_mapping = vertex_buffer_pool_->Request(
        current_fence_value, index_buffer_view.SizeInBytes, sizeof(uint16_t),
        nullptr, nullptr, &index_buffer_view.BufferLocation);
    if (index_buffer_mapping == nullptr) {
      XELOGE("Failed to get a buffer for {} indices in the immediate drawer",
             batch.index_count);
      return;
    }
    std::memcpy(index_buffer_mapping, batch.indices,
                index_buffer_view.SizeInBytes);
    current_command_list_->IASetIndexBuffer(&index_buffer_view);
  }

  batch_open_ = true;
}

void D3D12ImmediateDrawer::Draw(const ImmediateDraw& draw) {
  if (!batch_open_) {
    // Could be an error while obtaining the vertex and index buffers.
    return;
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
  if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) {
    // Nothing is visible (used as the default current_scissor_ value also).
    return;
  }
  if (current_scissor_.left != scissor.left ||
      current_scissor_.top != scissor.top ||
      current_scissor_.right != scissor.right ||
      current_scissor_.bottom != scissor.bottom) {
    current_scissor_ = scissor;
    current_command_list_->RSSetScissorRects(1, &scissor);
  }

  // Ensure texture data is available if any texture is loaded, upload all in a
  // batch, then transition all at once.
  UploadTextures();

  // Bind the texture. If this is the first draw in a frame, the descriptor heap
  // index will be invalid initially, and the texture will be bound regardless
  // of what's in current_texture_.
  uint64_t current_fence_value = context_.GetSwapCurrentFenceValue();
  auto texture = static_cast<D3D12ImmediateTexture*>(draw.texture);
  ID3D12Resource* texture_resource = texture ? texture->resource() : nullptr;
  bool bind_texture = current_texture_ != texture_resource;
  uint32_t texture_descriptor_index;
  uint64_t texture_heap_index = texture_descriptor_pool_->Request(
      current_fence_value, current_texture_descriptor_heap_index_,
      bind_texture ? 1 : 0, 1, texture_descriptor_index);
  if (texture_heap_index == D3D12DescriptorHeapPool::kHeapIndexInvalid) {
    return;
  }
  if (texture_resource) {
    texture->SetLastUsageFenceValue(current_fence_value);
  }
  if (current_texture_descriptor_heap_index_ != texture_heap_index) {
    current_texture_descriptor_heap_index_ = texture_heap_index;
    bind_texture = true;
    ID3D12DescriptorHeap* descriptor_heaps[] = {
        texture_descriptor_pool_->GetLastRequestHeap(), sampler_heap_};
    current_command_list_->SetDescriptorHeaps(2, descriptor_heaps);
  }

  const D3D12Provider& provider = context_.GetD3D12Provider();

  if (bind_texture) {
    current_texture_ = texture_resource;
    D3D12_SHADER_RESOURCE_VIEW_DESC texture_view_desc;
    texture_view_desc.Format = D3D12ImmediateTexture::kFormat;
    texture_view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    if (texture_resource) {
      texture_view_desc.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    } else {
      // No texture, solid color.
      texture_view_desc.Shader4ComponentMapping =
          D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
              D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
              D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
              D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
              D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
    }
    texture_view_desc.Texture2D.MostDetailedMip = 0;
    texture_view_desc.Texture2D.MipLevels = 1;
    texture_view_desc.Texture2D.PlaneSlice = 0;
    texture_view_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    provider.GetDevice()->CreateShaderResourceView(
        texture_resource, &texture_view_desc,
        provider.OffsetViewDescriptor(
            texture_descriptor_pool_->GetLastRequestHeapCPUStart(),
            texture_descriptor_index));
    current_command_list_->SetGraphicsRootDescriptorTable(
        UINT(RootParameter::kTexture),
        provider.OffsetViewDescriptor(
            texture_descriptor_pool_->GetLastRequestHeapGPUStart(),
            texture_descriptor_index));
  }

  // Bind the sampler. If the resource doesn't exist (solid color drawing), use
  // nearest-neighbor and clamp so fetching is simpler.
  SamplerIndex sampler_index =
      texture_resource ? texture->sampler_index() : SamplerIndex::kNearestClamp;
  if (current_sampler_index_ != sampler_index) {
    current_sampler_index_ = sampler_index;
    current_command_list_->SetGraphicsRootDescriptorTable(
        UINT(RootParameter::kSampler),
        provider.OffsetSamplerDescriptor(sampler_heap_gpu_start_,
                                         uint32_t(sampler_index)));
  }

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
    current_primitive_topology_ = primitive_topology;
    current_command_list_->IASetPrimitiveTopology(primitive_topology);
    current_command_list_->SetPipelineState(pipeline);
  }

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
  assert_false(batch_open_);
  if (current_command_list_) {
    // Don't keep upload buffers forever if nothing was drawn in this frame.
    UploadTextures();
    current_command_list_ = nullptr;
  }
}

void D3D12ImmediateDrawer::OnImmediateTextureDestroyed(
    D3D12ImmediateTexture& texture) {
  // Remove from the texture list.
  size_t texture_index = texture.immediate_drawer_index();
  assert_true(texture_index != SIZE_MAX);
  D3D12ImmediateTexture*& texture_at_index = textures_[texture_index];
  texture_at_index = textures_.back();
  texture_at_index->SetImmediateDrawerIndex(texture_index);
  textures_.pop_back();

  // Queue for delayed release.
  ID3D12Resource* resource = texture.resource();
  uint64_t last_usage_fence_value = texture.last_usage_fence_value();
  if (resource &&
      last_usage_fence_value > context_.GetSwapCompletedFenceValue()) {
    resource->AddRef();
    textures_deleted_.push_back(
        std::make_pair(resource, last_usage_fence_value));
  }
}

void D3D12ImmediateDrawer::UploadTextures() {
  assert_not_null(current_command_list_);
  if (texture_uploads_pending_.empty()) {
    // Called often - don't initialize anything.
    return;
  }

  ID3D12Device* device = context_.GetD3D12Provider().GetDevice();
  uint64_t current_fence_value = context_.GetSwapCurrentFenceValue();

  // Copy all at once, then transition all at once (not interleaving copying and
  // pipeline barriers).
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  barriers.reserve(texture_uploads_pending_.size());
  for (const PendingTextureUpload& pending_upload : texture_uploads_pending_) {
    ID3D12Resource* texture = pending_upload.texture;

    D3D12_RESOURCE_DESC texture_desc = texture->GetDesc();
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = pending_upload.buffer;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    device->GetCopyableFootprints(&texture_desc, 0, 1, 0,
                                  &location_source.PlacedFootprint, nullptr,
                                  nullptr, nullptr);
    location_dest.pResource = texture;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_dest.SubresourceIndex = 0;
    current_command_list_->CopyTextureRegion(&location_dest, 0, 0, 0,
                                             &location_source, nullptr);

    D3D12_RESOURCE_BARRIER& barrier = barriers.emplace_back();
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    SubmittedTextureUpload& submitted_upload =
        texture_uploads_submitted_.emplace_back();
    // Transfer the reference to the texture - need to keep it until the upload
    // is completed.
    submitted_upload.texture = texture;
    submitted_upload.buffer = pending_upload.buffer;
    submitted_upload.fence_value = current_fence_value;
  }
  texture_uploads_pending_.clear();
  assert_false(barriers.empty());
  current_command_list_->ResourceBarrier(UINT(barriers.size()),
                                         barriers.data());
}

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_command_processor.h"

#include <gflags/gflags.h>

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#include "xenia/gpu/d3d12/d3d12_shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/d3d12/d3d12_util.h"

// Some games (such as Banjo-Kazooie) are not aware of the half-pixel offset and
// may be blurry or have texture sampling artifacts, in this case the user may
// disable half-pixel offset by setting this to false.
DEFINE_bool(d3d12_half_pixel_offset, true,
            "Enable half-pixel vertex and VPOS offset");
// Disabled because the current positions look worse than sampling at centers.
DEFINE_bool(d3d12_programmable_sample_positions, false,
            "Enable custom SSAA sample positions where available");

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

ID3D12GraphicsCommandList* D3D12CommandProcessor::GetCurrentCommandList()
    const {
  assert_true(current_queue_frame_ != UINT_MAX);
  if (current_queue_frame_ == UINT_MAX) {
    return nullptr;
  }
  return command_lists_[current_queue_frame_]->GetCommandList();
}

ID3D12GraphicsCommandList1* D3D12CommandProcessor::GetCurrentCommandList1()
    const {
  assert_true(current_queue_frame_ != UINT_MAX);
  if (current_queue_frame_ == UINT_MAX) {
    return nullptr;
  }
  return command_lists_[current_queue_frame_]->GetCommandList1();
}

uint32_t D3D12CommandProcessor::GetCurrentColorMask(
    const D3D12Shader* pixel_shader) const {
  if (pixel_shader == nullptr) {
    return 0;
  }
  auto& regs = *register_file_;
  uint32_t color_mask = regs[XE_GPU_REG_RB_COLOR_MASK].u32 & 0xFFFF;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!pixel_shader->writes_color_target(i)) {
      color_mask &= ~(0xF << (i * 4));
    }
  }
  return color_mask;
}

void D3D12CommandProcessor::PushTransitionBarrier(
    ID3D12Resource* resource, D3D12_RESOURCE_STATES old_state,
    D3D12_RESOURCE_STATES new_state, UINT subresource) {
  if (old_state == new_state) {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = subresource;
  barrier.Transition.StateBefore = old_state;
  barrier.Transition.StateAfter = new_state;
  barriers_.push_back(barrier);
}

void D3D12CommandProcessor::PushAliasingBarrier(ID3D12Resource* old_resource,
                                                ID3D12Resource* new_resource) {
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Aliasing.pResourceBefore = old_resource;
  barrier.Aliasing.pResourceAfter = new_resource;
  barriers_.push_back(barrier);
}

void D3D12CommandProcessor::PushUAVBarrier(ID3D12Resource* resource) {
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.UAV.pResource = resource;
  barriers_.push_back(barrier);
}

void D3D12CommandProcessor::SubmitBarriers() {
  UINT barrier_count = UINT(barriers_.size());
  if (barrier_count != 0) {
    GetCurrentCommandList()->ResourceBarrier(barrier_count, barriers_.data());
    barriers_.clear();
  }
}

ID3D12RootSignature* D3D12CommandProcessor::GetRootSignature(
    const D3D12Shader* vertex_shader, const D3D12Shader* pixel_shader) {
  assert_true(vertex_shader->is_translated());
  assert_true(pixel_shader == nullptr || pixel_shader->is_translated());

  uint32_t pixel_texture_count = 0, pixel_sampler_count = 0;
  if (pixel_shader != nullptr) {
    pixel_shader->GetTextureSRVs(pixel_texture_count);
    pixel_shader->GetSamplerBindings(pixel_sampler_count);
  }
  uint32_t vertex_texture_count, vertex_sampler_count;
  vertex_shader->GetTextureSRVs(vertex_texture_count);
  vertex_shader->GetSamplerBindings(vertex_sampler_count);

  uint32_t index = 0;
  uint32_t index_offset = 0;
  index |= pixel_texture_count << index_offset;
  index_offset += D3D12Shader::kMaxTextureSRVIndexBits;
  index |= pixel_sampler_count << index_offset;
  index_offset += D3D12Shader::kMaxSamplerBindingIndexBits;
  index |= vertex_texture_count << index_offset;
  index_offset += D3D12Shader::kMaxTextureSRVIndexBits;
  index |= vertex_sampler_count << index_offset;
  index_offset += D3D12Shader::kMaxSamplerBindingIndexBits;
  assert_true(index_offset <= 32);

  // Try an existing root signature.
  auto it = root_signatures_.find(index);
  if (it != root_signatures_.end()) {
    return it->second;
  }

  // Create a new one.
  D3D12_ROOT_SIGNATURE_DESC desc;
  D3D12_ROOT_PARAMETER parameters[kRootParameter_Count_Max];
  D3D12_DESCRIPTOR_RANGE ranges[kRootParameter_Count_Max];
  desc.NumParameters = kRootParameter_Count_Base;
  desc.pParameters = parameters;
  desc.NumStaticSamplers = 0;
  desc.pStaticSamplers = nullptr;
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  // Base parameters.

  // Fetch constants.
  {
    auto& parameter = parameters[kRootParameter_FetchConstants];
    auto& range = ranges[kRootParameter_FetchConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 2;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  // Vertex float constants.
  {
    auto& parameter = parameters[kRootParameter_VertexFloatConstants];
    auto& range = ranges[kRootParameter_VertexFloatConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 8;
    range.BaseShaderRegister = 3;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  // Pixel float constants.
  {
    auto& parameter = parameters[kRootParameter_PixelFloatConstants];
    auto& range = ranges[kRootParameter_PixelFloatConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 8;
    range.BaseShaderRegister = 3;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  // Common constants - system and loop/bool.
  {
    auto& parameter = parameters[kRootParameter_CommonConstants];
    auto& range = ranges[kRootParameter_CommonConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 2;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  // Shared memory.
  {
    auto& parameter = parameters[kRootParameter_SharedMemory];
    auto& range = ranges[kRootParameter_SharedMemory];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  // Extra parameters.

  // Pixel textures.
  if (pixel_texture_count > 0) {
    auto& parameter = parameters[desc.NumParameters];
    auto& range = ranges[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = pixel_texture_count;
    range.BaseShaderRegister = 1;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Pixel samplers.
  if (pixel_sampler_count > 0) {
    auto& parameter = parameters[desc.NumParameters];
    auto& range = ranges[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    range.NumDescriptors = pixel_sampler_count;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Vertex textures.
  if (vertex_texture_count > 0) {
    auto& parameter = parameters[desc.NumParameters];
    auto& range = ranges[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = vertex_texture_count;
    range.BaseShaderRegister = 1;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Vertex samplers.
  if (vertex_sampler_count > 0) {
    auto& parameter = parameters[desc.NumParameters];
    auto& range = ranges[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    range.NumDescriptors = vertex_sampler_count;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  ID3D12RootSignature* root_signature = ui::d3d12::util::CreateRootSignature(
      GetD3D12Context()->GetD3D12Provider()->GetDevice(), desc);
  if (root_signature == nullptr) {
    XELOGE(
        "Failed to create a root signature with %u pixel textures, %u pixel "
        "samplers, %u vertex textures and %u vertex samplers",
        pixel_texture_count, pixel_sampler_count, vertex_texture_count,
        vertex_sampler_count);
    return nullptr;
  }
  root_signatures_.insert({index, root_signature});
  return root_signature;
}

uint32_t D3D12CommandProcessor::GetRootExtraParameterIndices(
    const D3D12Shader* vertex_shader, const D3D12Shader* pixel_shader,
    RootExtraParameterIndices& indices_out) {
  uint32_t pixel_texture_count = 0, pixel_sampler_count = 0;
  if (pixel_shader != nullptr) {
    pixel_shader->GetTextureSRVs(pixel_texture_count);
    pixel_shader->GetSamplerBindings(pixel_sampler_count);
  }
  uint32_t vertex_texture_count, vertex_sampler_count;
  vertex_shader->GetTextureSRVs(vertex_texture_count);
  vertex_shader->GetSamplerBindings(vertex_sampler_count);

  uint32_t index = kRootParameter_Count_Base;
  if (pixel_texture_count != 0) {
    indices_out.pixel_textures = index++;
  } else {
    indices_out.pixel_textures = RootExtraParameterIndices::kUnavailable;
  }
  if (pixel_sampler_count != 0) {
    indices_out.pixel_samplers = index++;
  } else {
    indices_out.pixel_samplers = RootExtraParameterIndices::kUnavailable;
  }
  if (vertex_texture_count != 0) {
    indices_out.vertex_textures = index++;
  } else {
    indices_out.vertex_textures = RootExtraParameterIndices::kUnavailable;
  }
  if (vertex_sampler_count != 0) {
    indices_out.vertex_samplers = index++;
  } else {
    indices_out.vertex_samplers = RootExtraParameterIndices::kUnavailable;
  }
  return index;
}

uint64_t D3D12CommandProcessor::RequestViewDescriptors(
    uint64_t previous_full_update, uint32_t count_for_partial_update,
    uint32_t count_for_full_update, D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle_out,
    D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle_out) {
  uint32_t descriptor_index;
  uint64_t current_full_update =
      view_heap_pool_->Request(previous_full_update, count_for_partial_update,
                               count_for_full_update, descriptor_index);
  if (current_full_update == 0) {
    // There was an error.
    return 0;
  }
  ID3D12DescriptorHeap* heap = view_heap_pool_->GetLastRequestHeap();
  if (current_view_heap_ != heap) {
    // Bind the new descriptor heaps if needed.
    current_view_heap_ = heap;
    ID3D12DescriptorHeap* heaps[2];
    uint32_t heap_count = 0;
    heaps[heap_count++] = heap;
    if (current_sampler_heap_ != nullptr) {
      heaps[heap_count++] = current_sampler_heap_;
    }
    GetCurrentCommandList()->SetDescriptorHeaps(heap_count, heaps);
  }
  auto provider = GetD3D12Context()->GetD3D12Provider();
  cpu_handle_out = provider->OffsetViewDescriptor(
      view_heap_pool_->GetLastRequestHeapCPUStart(), descriptor_index);
  gpu_handle_out = provider->OffsetViewDescriptor(
      view_heap_pool_->GetLastRequestHeapGPUStart(), descriptor_index);
  return current_full_update;
}

uint64_t D3D12CommandProcessor::RequestSamplerDescriptors(
    uint64_t previous_full_update, uint32_t count_for_partial_update,
    uint32_t count_for_full_update, D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle_out,
    D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle_out) {
  uint32_t descriptor_index;
  uint64_t current_full_update = sampler_heap_pool_->Request(
      previous_full_update, count_for_partial_update, count_for_full_update,
      descriptor_index);
  if (current_full_update == 0) {
    // There was an error.
    return 0;
  }
  ID3D12DescriptorHeap* heap = sampler_heap_pool_->GetLastRequestHeap();
  if (current_sampler_heap_ != heap) {
    // Bind the new descriptor heaps if needed.
    current_sampler_heap_ = heap;
    ID3D12DescriptorHeap* heaps[2];
    uint32_t heap_count = 0;
    heaps[heap_count++] = heap;
    if (current_view_heap_ != nullptr) {
      heaps[heap_count++] = current_view_heap_;
    }
    GetCurrentCommandList()->SetDescriptorHeaps(heap_count, heaps);
  }
  uint32_t descriptor_offset =
      descriptor_index *
      GetD3D12Context()->GetD3D12Provider()->GetSamplerDescriptorSize();
  cpu_handle_out.ptr =
      sampler_heap_pool_->GetLastRequestHeapCPUStart().ptr + descriptor_offset;
  gpu_handle_out.ptr =
      sampler_heap_pool_->GetLastRequestHeapGPUStart().ptr + descriptor_offset;
  return current_full_update;
}

ID3D12Resource* D3D12CommandProcessor::RequestScratchGPUBuffer(
    uint32_t size, D3D12_RESOURCE_STATES state) {
  assert_true(current_queue_frame_ != UINT_MAX);
  assert_false(scratch_buffer_used_);
  if (current_queue_frame_ == UINT_MAX || scratch_buffer_used_ || size == 0) {
    return nullptr;
  }

  if (size <= scratch_buffer_size_) {
    PushTransitionBarrier(scratch_buffer_, scratch_buffer_state_, state);
    scratch_buffer_state_ = state;
    scratch_buffer_used_ = true;
    return scratch_buffer_;
  }

  size = xe::align(size, kScratchBufferSizeIncrement);

  auto context = GetD3D12Context();
  auto device = context->GetD3D12Provider()->GetDevice();
  D3D12_RESOURCE_DESC buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(
      buffer_desc, size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ID3D12Resource* buffer;
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE,
          &buffer_desc, state, nullptr, IID_PPV_ARGS(&buffer)))) {
    XELOGE("Failed to create a %u MB scratch GPU buffer", size >> 20);
    return nullptr;
  }
  if (scratch_buffer_ != nullptr) {
    BufferForDeletion buffer_for_deletion;
    buffer_for_deletion.buffer = scratch_buffer_;
    buffer_for_deletion.last_usage_frame = GetD3D12Context()->GetCurrentFrame();
    buffers_for_deletion_.push_back(buffer_for_deletion);
  }
  scratch_buffer_ = buffer;
  scratch_buffer_size_ = size;
  scratch_buffer_state_ = state;
  scratch_buffer_used_ = true;
  return scratch_buffer_;
}

void D3D12CommandProcessor::ReleaseScratchGPUBuffer(
    ID3D12Resource* buffer, D3D12_RESOURCE_STATES new_state) {
  assert_true(current_queue_frame_ != UINT_MAX);
  assert_true(scratch_buffer_used_);
  scratch_buffer_used_ = false;
  if (buffer == scratch_buffer_) {
    scratch_buffer_state_ = new_state;
  }
}

void D3D12CommandProcessor::SetSamplePositions(MsaaSamples sample_positions) {
  if (current_sample_positions_ == sample_positions) {
    return;
  }
  if (FLAGS_d3d12_programmable_sample_positions) {
    auto provider = GetD3D12Context()->GetD3D12Provider();
    auto tier = provider->GetProgrammableSamplePositionsTier();
    auto command_list = GetCurrentCommandList1();
    if (tier >= 2 && command_list != nullptr) {
      // Depth buffer transitions are affected by sample positions.
      SubmitBarriers();
      // Standard sample positions in Direct3D 10.1, but adjusted to take the
      // fact that SSAA samples are already shifted by 1/4 of a pixel.
      // TODO(Triang3l): Find what sample positions are used by Xenos, though
      // they are not necessarily better. The purpose is just to make 2x SSAA
      // work a little bit better for tall stairs.
      // FIXME(Triang3l): This is currently even uglier than without custom
      // sample positions.
      if (sample_positions >= MsaaSamples::k2X) {
        // Sample 1 is lower-left on Xenos, but upper-right in Direct3D 12.
        D3D12_SAMPLE_POSITION d3d_sample_positions[4];
        if (sample_positions >= MsaaSamples::k4X) {
          // Upper-left.
          d3d_sample_positions[0].X = -2 + 4;
          d3d_sample_positions[0].Y = -6 + 4;
          // Upper-right.
          d3d_sample_positions[1].X = 6 - 4;
          d3d_sample_positions[1].Y = -2 + 4;
          // Lower-left.
          d3d_sample_positions[2].X = -6 + 4;
          d3d_sample_positions[2].Y = 2 - 4;
          // Lower-right.
          d3d_sample_positions[3].X = 2 - 4;
          d3d_sample_positions[3].Y = 6 - 4;
        } else {
          // Upper.
          d3d_sample_positions[0].X = -4;
          d3d_sample_positions[0].Y = -4 + 4;
          d3d_sample_positions[1].X = -4;
          d3d_sample_positions[1].Y = -4 + 4;
          // Lower.
          d3d_sample_positions[2].X = 4;
          d3d_sample_positions[2].Y = 4 - 4;
          d3d_sample_positions[3].X = 4;
          d3d_sample_positions[3].Y = 4 - 4;
        }
        command_list->SetSamplePositions(1, 4, d3d_sample_positions);
      } else {
        command_list->SetSamplePositions(0, 0, nullptr);
      }
    }
  }
  current_sample_positions_ = sample_positions;
}

void D3D12CommandProcessor::SetComputePipeline(ID3D12PipelineState* pipeline) {
  if (current_pipeline_ != pipeline) {
    GetCurrentCommandList()->SetPipelineState(pipeline);
    current_pipeline_ = pipeline;
  }
}

void D3D12CommandProcessor::UnbindRenderTargets() {
  render_target_cache_->UnbindRenderTargets();
}

void D3D12CommandProcessor::SetExternalGraphicsPipeline(
    ID3D12PipelineState* pipeline, bool reset_viewport, bool reset_blend_factor,
    bool reset_stencil_ref) {
  if (current_pipeline_ != pipeline) {
    GetCurrentCommandList()->SetPipelineState(pipeline);
    current_pipeline_ = pipeline;
  }
  current_graphics_root_signature_ = nullptr;
  current_graphics_root_up_to_date_ = 0;
  primitive_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  if (reset_viewport) {
    ff_viewport_update_needed_ = true;
    ff_scissor_update_needed_ = true;
  }
  if (reset_blend_factor) {
    ff_blend_factor_update_needed_ = true;
  }
  if (reset_stencil_ref) {
    ff_stencil_ref_update_needed_ = true;
  }
}

bool D3D12CommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Failed to initialize base command processor context");
    return false;
  }

  auto context = GetD3D12Context();
  auto provider = context->GetD3D12Provider();
  auto device = provider->GetDevice();
  auto direct_queue = provider->GetDirectQueue();

  for (uint32_t i = 0; i < ui::d3d12::D3D12Context::kQueuedFrames; ++i) {
    command_lists_[i] = ui::d3d12::CommandList::Create(
        device, direct_queue, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (command_lists_[i] == nullptr) {
      XELOGE("Failed to create the command lists");
      return false;
    }
  }

  constant_buffer_pool_ =
      std::make_unique<ui::d3d12::UploadBufferPool>(context, 1024 * 1024);
  view_heap_pool_ = std::make_unique<ui::d3d12::DescriptorHeapPool>(
      context, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 32768);
  // Can't create a shader-visible heap with more than 2048 samplers.
  sampler_heap_pool_ = std::make_unique<ui::d3d12::DescriptorHeapPool>(
      context, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048);

  shared_memory_ = std::make_unique<SharedMemory>(this, memory_);
  if (!shared_memory_->Initialize()) {
    XELOGE("Failed to initialize shared memory");
    return false;
  }

  pipeline_cache_ = std::make_unique<PipelineCache>(this, register_file_);

  texture_cache_ = std::make_unique<TextureCache>(this, register_file_,
                                                  shared_memory_.get());
  if (!texture_cache_->Initialize()) {
    XELOGE("Failed to initialize the texture cache");
    return false;
  }

  render_target_cache_ =
      std::make_unique<RenderTargetCache>(this, register_file_);
  if (!render_target_cache_->Initialize()) {
    XELOGE("Failed to initialize the render target cache");
    return false;
  }

  primitive_converter_ = std::make_unique<PrimitiveConverter>(
      this, register_file_, memory_, shared_memory_.get());
  if (!primitive_converter_->Initialize()) {
    XELOGE("Failed to initialize the geometric primitive converter");
    return false;
  }

  D3D12_RESOURCE_DESC swap_texture_desc;
  swap_texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  swap_texture_desc.Alignment = 0;
  swap_texture_desc.Width = kSwapTextureWidth;
  swap_texture_desc.Height = kSwapTextureHeight;
  swap_texture_desc.DepthOrArraySize = 1;
  swap_texture_desc.MipLevels = 1;
  swap_texture_desc.Format = ui::d3d12::D3D12Context::kSwapChainFormat;
  swap_texture_desc.SampleDesc.Count = 1;
  swap_texture_desc.SampleDesc.Quality = 0;
  swap_texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  swap_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  // Can be sampled at any time, switch to render target when needed, then back.
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE,
          &swap_texture_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
          nullptr, IID_PPV_ARGS(&swap_texture_)))) {
    XELOGE("Failed to create the command processor front buffer");
    return false;
  }
  D3D12_DESCRIPTOR_HEAP_DESC swap_descriptor_heap_desc;
  swap_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  swap_descriptor_heap_desc.NumDescriptors = 1;
  swap_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  swap_descriptor_heap_desc.NodeMask = 0;
  if (FAILED(device->CreateDescriptorHeap(
          &swap_descriptor_heap_desc,
          IID_PPV_ARGS(&swap_texture_rtv_descriptor_heap_)))) {
    XELOGE("Failed to create the command processor front buffer RTV heap");
    return false;
  }
  swap_texture_rtv_ =
      swap_texture_rtv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
  D3D12_RENDER_TARGET_VIEW_DESC swap_rtv_desc;
  swap_rtv_desc.Format = ui::d3d12::D3D12Context::kSwapChainFormat;
  swap_rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  swap_rtv_desc.Texture2D.MipSlice = 0;
  swap_rtv_desc.Texture2D.PlaneSlice = 0;
  device->CreateRenderTargetView(swap_texture_, &swap_rtv_desc,
                                 swap_texture_rtv_);
  swap_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  swap_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device->CreateDescriptorHeap(
          &swap_descriptor_heap_desc,
          IID_PPV_ARGS(&swap_texture_srv_descriptor_heap_)))) {
    XELOGE("Failed to create the command processor front buffer SRV heap");
    return false;
  }
  D3D12_SHADER_RESOURCE_VIEW_DESC swap_srv_desc;
  swap_srv_desc.Format = ui::d3d12::D3D12Context::kSwapChainFormat;
  swap_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  swap_srv_desc.Shader4ComponentMapping =
      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  swap_srv_desc.Texture2D.MostDetailedMip = 0;
  swap_srv_desc.Texture2D.MipLevels = 1;
  swap_srv_desc.Texture2D.PlaneSlice = 0;
  swap_srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(
      swap_texture_, &swap_srv_desc,
      swap_texture_srv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart());

  return true;
}

void D3D12CommandProcessor::ShutdownContext() {
  auto context = GetD3D12Context();
  context->AwaitAllFramesCompletion();

  ui::d3d12::util::ReleaseAndNull(scratch_buffer_);
  scratch_buffer_size_ = 0;

  for (auto& buffer_for_deletion : buffers_for_deletion_) {
    buffer_for_deletion.buffer->Release();
  }
  buffers_for_deletion_.clear();

  if (swap_texture_srv_descriptor_heap_ != nullptr) {
    {
      std::lock_guard<std::mutex> lock(swap_state_.mutex);
      swap_state_.pending = false;
      swap_state_.front_buffer_texture = 0;
    }
    auto graphics_system = static_cast<D3D12GraphicsSystem*>(graphics_system_);
    graphics_system->AwaitFrontBufferUnused();
    swap_texture_srv_descriptor_heap_->Release();
    swap_texture_srv_descriptor_heap_ = nullptr;
  }
  ui::d3d12::util::ReleaseAndNull(swap_texture_rtv_descriptor_heap_);
  ui::d3d12::util::ReleaseAndNull(swap_texture_);

  sampler_heap_pool_.reset();
  view_heap_pool_.reset();
  constant_buffer_pool_.reset();

  primitive_converter_.reset();

  render_target_cache_.reset();

  texture_cache_.reset();

  pipeline_cache_.reset();

  // Root signatured are used by pipelines, thus freed after the pipelines.
  for (auto it : root_signatures_) {
    it.second->Release();
  }
  root_signatures_.clear();

  shared_memory_.reset();

  for (uint32_t i = 0; i < ui::d3d12::D3D12Context::kQueuedFrames; ++i) {
    command_lists_[i].reset();
  }

  CommandProcessor::ShutdownContext();
}

void D3D12CommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  CommandProcessor::WriteRegister(index, value);

  if (index >= XE_GPU_REG_SHADER_CONSTANT_000_X &&
      index <= XE_GPU_REG_SHADER_CONSTANT_511_W) {
    uint32_t component_index = index - XE_GPU_REG_SHADER_CONSTANT_000_X;
    cbuffer_bindings_float_[component_index >> 7].up_to_date = false;
  } else if (index >= XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 &&
             index <= XE_GPU_REG_SHADER_CONSTANT_LOOP_31) {
    cbuffer_bindings_bool_loop_.up_to_date = false;
  } else if (index >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 &&
             index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {
    cbuffer_bindings_fetch_.up_to_date = false;
    if (texture_cache_ != nullptr) {
      texture_cache_->TextureFetchConstantWritten(
          (index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) / 6);
    }
  }
}

void D3D12CommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                        uint32_t frontbuffer_width,
                                        uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");

  // In case the swap command is the only one in the frame.
  BeginFrame();

  D3D12_CPU_DESCRIPTOR_HANDLE frontbuffer_cpu_handle;
  D3D12_GPU_DESCRIPTOR_HANDLE frontbuffer_gpu_handle;
  if (RequestViewDescriptors(0, 1, 1, frontbuffer_cpu_handle,
                             frontbuffer_gpu_handle) != 0) {
    if (texture_cache_->RequestSwapTexture(frontbuffer_cpu_handle)) {
      auto command_list = GetCurrentCommandList();
      render_target_cache_->UnbindRenderTargets();
      // The swap texture is kept as an SRV because the graphics system may draw
      // with it at any time. It's switched to RTV and back when needed.
      PushTransitionBarrier(swap_texture_,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_RENDER_TARGET);
      SubmitBarriers();
      command_list->OMSetRenderTargets(1, &swap_texture_rtv_, TRUE, nullptr);
      D3D12_VIEWPORT viewport;
      viewport.TopLeftX = 0.0f;
      viewport.TopLeftY = 0.0f;
      viewport.Width = float(kSwapTextureWidth);
      viewport.Height = float(kSwapTextureHeight);
      viewport.MinDepth = 0.0f;
      viewport.MaxDepth = 0.0f;
      command_list->RSSetViewports(1, &viewport);
      D3D12_RECT scissor;
      scissor.left = 0;
      scissor.top = 0;
      scissor.right = kSwapTextureWidth;
      scissor.bottom = kSwapTextureHeight;
      command_list->RSSetScissorRects(1, &scissor);
      D3D12GraphicsSystem* graphics_system =
          static_cast<D3D12GraphicsSystem*>(graphics_system_);
      graphics_system->StretchTextureToFrontBuffer(frontbuffer_gpu_handle,
                                                   command_list);
      PushTransitionBarrier(swap_texture_, D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      // Don't care about graphics state because the frame is ending anyway.
      {
        std::lock_guard<std::mutex> lock(swap_state_.mutex);
        swap_state_.width = kSwapTextureWidth;
        swap_state_.height = kSwapTextureHeight;
        swap_state_.front_buffer_texture =
            reinterpret_cast<uintptr_t>(swap_texture_srv_descriptor_heap_);
      }
    }
  }

  EndFrame();

  if (cache_clear_requested_) {
    cache_clear_requested_ = false;
    GetD3D12Context()->AwaitAllFramesCompletion();

    ui::d3d12::util::ReleaseAndNull(scratch_buffer_);
    scratch_buffer_size_ = 0;

    sampler_heap_pool_->ClearCache();
    view_heap_pool_->ClearCache();
    constant_buffer_pool_->ClearCache();

    primitive_converter_->ClearCache();

    render_target_cache_->ClearCache();

    texture_cache_->ClearCache();

    pipeline_cache_->ClearCache();

    for (auto it : root_signatures_) {
      it.second->Release();
    }
    root_signatures_.clear();

    // TODO(Triang3l): Shared memory cache clear.
    // shared_memory_->ClearCache();
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
  auto device = GetD3D12Context()->GetD3D12Provider()->GetDevice();
  auto& regs = *register_file_;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto enable_mode = static_cast<xenos::ModeControl>(
      regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7);
  if (enable_mode == xenos::ModeControl::kIgnore) {
    // Ignored.
    return true;
  }
  if (enable_mode == xenos::ModeControl::kCopy) {
    // Special copy handling.
    return IssueCopy();
  }

  if ((regs[XE_GPU_REG_RB_SURFACE_INFO].u32 & 0x3FFF) == 0) {
    // Doesn't actually draw.
    return true;
  }
  if ((regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32 & 0x3) == 0x3 &&
      primitive_type != PrimitiveType::kPointList &&
      primitive_type != PrimitiveType::kRectangleList) {
    // Both sides are culled - can't reproduce this with rasterizer state.
    return true;
  }

  bool indexed = index_buffer_info != nullptr && index_buffer_info->guest_base;
  if (indexed && regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32 & (1 << 21)) {
    uint32_t reset_index = regs[XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX].u32;
    uint32_t reset_index_expected;
    if (index_buffer_info->format == IndexFormat::kInt32) {
      reset_index_expected = 0xFFFFFFFFu;
    } else {
      reset_index_expected = 0xFFFFu;
    }
    if (reset_index != reset_index_expected) {
      // Only 0xFFFF and 0xFFFFFFFF primitive restart indices are supported by
      // Direct3D 12 (endianness doesn't matter for them). With shared memory,
      // it's impossible to replace the cut index in the buffer without
      // affecting the game memory.
      XELOGE(
          "The game uses the primitive restart index 0x%X that isn't 0xFFFF or "
          "0xFFFFFFFF. Report the game to Xenia developers so geometry shaders "
          "will be added to handle this!",
          reset_index);
      assert_always();
      return false;
    }
  }

  // Shaders will have already been defined by previous loads.
  // We need them to do just about anything so validate here.
  auto vertex_shader = static_cast<D3D12Shader*>(active_vertex_shader());
  auto pixel_shader = static_cast<D3D12Shader*>(active_pixel_shader());
  if (!vertex_shader) {
    // Always need a vertex shader.
    return false;
  }
  // Depth-only mode doesn't need a pixel shader.
  if (enable_mode == xenos::ModeControl::kDepth) {
    pixel_shader = nullptr;
  } else if (!pixel_shader) {
    // Need a pixel shader in normal color mode.
    return false;
  }
  // Translate shaders now because to get the color mask, which is needed by the
  // render target cache.
  if (!pipeline_cache_->EnsureShadersTranslated(vertex_shader, pixel_shader)) {
    return false;
  }

  uint32_t color_mask = GetCurrentColorMask(pixel_shader);
  if (!color_mask && !(regs[XE_GPU_REG_RB_DEPTHCONTROL].u32 & (0x1 | 0x4))) {
    // Not writing to color, depth or doing stencil test, so doesn't draw.
    return true;
  }

  bool new_frame = BeginFrame();
  auto command_list = GetCurrentCommandList();

  // Set up the render targets - this may bind pipelines.
  if (!render_target_cache_->UpdateRenderTargets(pixel_shader)) {
    // Doesn't actually draw.
    return true;
  }
  const RenderTargetCache::PipelineRenderTarget* pipeline_render_targets =
      render_target_cache_->GetCurrentPipelineRenderTargets();

  // Set the primitive topology.
  PrimitiveType primitive_type_converted =
      PrimitiveConverter::GetReplacementPrimitiveType(primitive_type);
  D3D_PRIMITIVE_TOPOLOGY primitive_topology;
  switch (primitive_type_converted) {
    case PrimitiveType::kPointList:
      primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
      break;
    case PrimitiveType::kLineList:
      primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
      break;
    case PrimitiveType::kLineStrip:
      primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
      break;
    case PrimitiveType::kTriangleList:
    case PrimitiveType::kRectangleList:
      primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      break;
    case PrimitiveType::kTriangleStrip:
      primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
      break;
    case PrimitiveType::kQuadList:
      primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
      break;
    default:
      return false;
  }
  if (primitive_topology_ != primitive_topology) {
    primitive_topology_ = primitive_topology;
    command_list->IASetPrimitiveTopology(primitive_topology);
  }

  // Get the pipeline and translate the shaders so used textures are known.
  ID3D12PipelineState* pipeline;
  ID3D12RootSignature* root_signature;
  auto pipeline_status = pipeline_cache_->ConfigurePipeline(
      vertex_shader, pixel_shader, primitive_type_converted,
      indexed ? index_buffer_info->format : IndexFormat::kInt16,
      pipeline_render_targets, &pipeline, &root_signature);
  if (pipeline_status == PipelineCache::UpdateStatus::kError) {
    return false;
  }

  // Update the textures - this may bind pipelines.
  texture_cache_->RequestTextures(
      vertex_shader->GetUsedTextureMask(),
      pixel_shader != nullptr ? pixel_shader->GetUsedTextureMask() : 0);

  // Update viewport, scissor, blend factor and stencil reference.
  UpdateFixedFunctionState(command_list);

  // Bind the pipeline.
  if (current_pipeline_ != pipeline) {
    GetCurrentCommandList()->SetPipelineState(pipeline);
    current_pipeline_ = pipeline;
  }

  // Update system constants before uploading them.
  UpdateSystemConstantValues(
      indexed ? index_buffer_info->endianness : Endian::kUnspecified,
      pipeline_render_targets);

  // Update constant buffers, descriptors and root parameters.
  if (!UpdateBindings(command_list, vertex_shader, pixel_shader,
                      root_signature)) {
    return false;
  }

  // Ensure vertex and index buffers are resident and draw.
  // TODO(Triang3l): Cache residency for ranges in a way similar to how texture
  // validity will be tracked.
  uint64_t vertex_buffers_resident[2] = {};
  for (const auto& vertex_binding : vertex_shader->vertex_bindings()) {
    uint32_t vfetch_index = vertex_binding.fetch_constant;
    if (vertex_buffers_resident[vfetch_index >> 6] &
        (1ull << (vfetch_index & 63))) {
      continue;
    }
    uint32_t vfetch_constant_index =
        XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + vfetch_index * 2;
    if ((regs[vfetch_constant_index].u32 & 0x3) != 3) {
      XELOGGPU("Vertex fetch type is not 3!");
      return false;
    }
    shared_memory_->RequestRange(
        regs[vfetch_constant_index].u32 & 0x1FFFFFFC,
        regs[vfetch_constant_index + 1].u32 & 0x3FFFFFC);
    vertex_buffers_resident[vfetch_index >> 6] |= 1ull << (vfetch_index & 63);
  }

  if (indexed) {
    uint32_t index_size = index_buffer_info->format == IndexFormat::kInt32
                              ? sizeof(uint32_t)
                              : sizeof(uint16_t);
    assert_false(index_buffer_info->guest_base & (index_size - 1));
    uint32_t index_base = index_buffer_info->guest_base & ~(index_size - 1);
    D3D12_INDEX_BUFFER_VIEW index_buffer_view;
    index_buffer_view.Format = index_buffer_info->format == IndexFormat::kInt32
                                   ? DXGI_FORMAT_R32_UINT
                                   : DXGI_FORMAT_R16_UINT;
    uint32_t converted_index_count;
    PrimitiveConverter::ConversionResult conversion_result =
        primitive_converter_->ConvertPrimitives(
            primitive_type, index_buffer_info->guest_base, index_count,
            index_buffer_info->format, index_buffer_info->endianness,
            index_buffer_view.BufferLocation, converted_index_count);
    if (conversion_result == PrimitiveConverter::ConversionResult::kFailed) {
      return false;
    }
    if (conversion_result ==
        PrimitiveConverter::ConversionResult::kPrimitiveEmpty) {
      return true;
    }
    if (conversion_result == PrimitiveConverter::ConversionResult::kConverted) {
      index_buffer_view.SizeInBytes = converted_index_count * index_size;
      index_count = converted_index_count;
    } else {
      uint32_t index_buffer_size = index_buffer_info->count * index_size;
      shared_memory_->RequestRange(index_base, index_buffer_size);
      index_buffer_view.BufferLocation =
          shared_memory_->GetGPUAddress() + index_base;
      index_buffer_view.SizeInBytes = index_buffer_size;
    }
    shared_memory_->UseForReading();
    command_list->IASetIndexBuffer(&index_buffer_view);
    SubmitBarriers();
    command_list->DrawIndexedInstanced(index_count, 1, 0, 0, 0);
  } else {
    // Check if need to draw using a conversion index buffer.
    uint32_t converted_index_count;
    D3D12_GPU_VIRTUAL_ADDRESS conversion_gpu_address =
        primitive_converter_->GetStaticIndexBuffer(primitive_type, index_count,
                                                   converted_index_count);
    shared_memory_->UseForReading();
    SubmitBarriers();
    if (conversion_gpu_address) {
      D3D12_INDEX_BUFFER_VIEW index_buffer_view;
      index_buffer_view.BufferLocation = conversion_gpu_address;
      index_buffer_view.SizeInBytes = converted_index_count * sizeof(uint16_t);
      index_buffer_view.Format = DXGI_FORMAT_R16_UINT;
      command_list->IASetIndexBuffer(&index_buffer_view);
      command_list->DrawIndexedInstanced(converted_index_count, 1, 0, 0, 0);
    } else {
      command_list->DrawInstanced(index_count, 1, 0, 0);
    }
  }

  return true;
}

bool D3D12CommandProcessor::IssueCopy() {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES
  BeginFrame();
  return render_target_cache_->Resolve(shared_memory_.get(),
                                       texture_cache_.get(), memory_);
}

bool D3D12CommandProcessor::BeginFrame() {
  if (current_queue_frame_ != UINT32_MAX) {
    return false;
  }

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto context = GetD3D12Context();
  context->BeginSwap();
  current_queue_frame_ = context->GetCurrentQueueFrame();

  // Remove outdated temporary buffers.
  uint64_t last_completed_frame = context->GetLastCompletedFrame();
  auto erase_buffers_end = buffers_for_deletion_.begin();
  while (erase_buffers_end != buffers_for_deletion_.end()) {
    uint64_t upload_frame = erase_buffers_end->last_usage_frame;
    if (upload_frame > last_completed_frame) {
      ++erase_buffers_end;
      break;
    }
    erase_buffers_end->buffer->Release();
    ++erase_buffers_end;
  }
  buffers_for_deletion_.erase(buffers_for_deletion_.begin(), erase_buffers_end);

  // Reset fixed-function state.
  ff_viewport_update_needed_ = true;
  ff_scissor_update_needed_ = true;
  ff_blend_factor_update_needed_ = true;
  ff_stencil_ref_update_needed_ = true;

  // Since a new command list is being started, sample positions are reset to
  // centers.
  current_sample_positions_ = MsaaSamples::k1X;

  // Reset bindings, particularly because the buffers backing them are recycled.
  current_pipeline_ = nullptr;
  current_graphics_root_signature_ = nullptr;
  current_graphics_root_up_to_date_ = 0;
  current_view_heap_ = nullptr;
  current_sampler_heap_ = nullptr;
  cbuffer_bindings_system_.up_to_date = false;
  for (uint32_t i = 0; i < xe::countof(cbuffer_bindings_float_); ++i) {
    cbuffer_bindings_float_[i].up_to_date = false;
  }
  cbuffer_bindings_bool_loop_.up_to_date = false;
  cbuffer_bindings_fetch_.up_to_date = false;
  draw_view_full_update_ = 0;
  draw_sampler_full_update_ = 0;
  primitive_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

  command_lists_[current_queue_frame_]->BeginRecording();

  constant_buffer_pool_->BeginFrame();
  view_heap_pool_->BeginFrame();
  sampler_heap_pool_->BeginFrame();

  shared_memory_->BeginFrame();

  texture_cache_->BeginFrame();

  render_target_cache_->BeginFrame();

  primitive_converter_->BeginFrame();

  return true;
}

bool D3D12CommandProcessor::EndFrame() {
  if (current_queue_frame_ == UINT32_MAX) {
    return false;
  }

  assert_false(scratch_buffer_used_);

  primitive_converter_->EndFrame();

  render_target_cache_->EndFrame();

  shared_memory_->EndFrame();

  // Submit barriers now because resources the queued barriers are for may be
  // destroyed between frames.
  SubmitBarriers();
  command_lists_[current_queue_frame_]->Execute();

  sampler_heap_pool_->EndFrame();
  view_heap_pool_->EndFrame();
  constant_buffer_pool_->EndFrame();

  auto context = GetD3D12Context();
  context->EndSwap();
  current_queue_frame_ = UINT32_MAX;

  return true;
}

void D3D12CommandProcessor::UpdateFixedFunctionState(
    ID3D12GraphicsCommandList* command_list) {
  auto& regs = *register_file_;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  // Window parameters.
  // http://ftp.tku.edu.tw/NetBSD/NetBSD-current/xsrc/external/mit/xf86-video-ati/dist/src/r600_reg_auto_r6xx.h
  // See r200UpdateWindow:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  uint32_t pa_sc_window_offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32;
  int16_t window_offset_x = pa_sc_window_offset & 0x7FFF;
  int16_t window_offset_y = (pa_sc_window_offset >> 16) & 0x7FFF;
  if (window_offset_x & 0x4000) {
    window_offset_x |= 0x8000;
  }
  if (window_offset_y & 0x4000) {
    window_offset_y |= 0x8000;
  }

  // Supersampling replacing multisampling due to difficulties of emulating
  // EDRAM with multisampling.
  MsaaSamples msaa_samples =
      MsaaSamples((regs[XE_GPU_REG_RB_SURFACE_INFO].u32 >> 16) & 0x3);
  uint32_t ssaa_scale_x = msaa_samples >= MsaaSamples::k4X ? 2 : 1;
  uint32_t ssaa_scale_y = msaa_samples >= MsaaSamples::k2X ? 2 : 1;

  // Viewport.
  // PA_CL_VTE_CNTL contains whether offsets and scales are enabled.
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  // In games, either all are enabled (for regular drawing) or none are (for
  // rectangle lists usually).
  //
  // If scale/offset is enabled, the Xenos shader is writing (neglecting W
  // division) position in the NDC (-1, -1, dx_clip_space_def - 1) -> (1, 1, 1)
  // box. If it's not, the position is in screen space. Since we can only use
  // the NDC in PC APIs, we use a viewport of the largest possible size, and
  // divide the position by it in translated shaders.
  uint32_t pa_cl_vte_cntl = regs[XE_GPU_REG_PA_CL_VTE_CNTL].u32;
  float viewport_scale_x =
      (pa_cl_vte_cntl & (1 << 0))
          ? std::abs(regs[XE_GPU_REG_PA_CL_VPORT_XSCALE].f32)
          : 1280.0f;
  float viewport_scale_y =
      (pa_cl_vte_cntl & (1 << 2))
          ? std::abs(regs[XE_GPU_REG_PA_CL_VPORT_YSCALE].f32)
          : 1280.0f;
  float viewport_scale_z = (pa_cl_vte_cntl & (1 << 4))
                               ? regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE].f32
                               : 1.0f;
  float viewport_offset_x = (pa_cl_vte_cntl & (1 << 1))
                                ? regs[XE_GPU_REG_PA_CL_VPORT_XOFFSET].f32
                                : std::abs(viewport_scale_x);
  float viewport_offset_y = (pa_cl_vte_cntl & (1 << 3))
                                ? regs[XE_GPU_REG_PA_CL_VPORT_YOFFSET].f32
                                : std::abs(viewport_scale_y);
  float viewport_offset_z = (pa_cl_vte_cntl & (1 << 5))
                                ? regs[XE_GPU_REG_PA_CL_VPORT_ZOFFSET].f32
                                : 0.0f;
  if (regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32 & (1 << 16)) {
    viewport_offset_x += float(window_offset_x);
    viewport_offset_y += float(window_offset_y);
  }
  D3D12_VIEWPORT viewport;
  viewport.TopLeftX =
      (viewport_offset_x - viewport_scale_x) * float(ssaa_scale_x);
  viewport.TopLeftY =
      (viewport_offset_y - viewport_scale_y) * float(ssaa_scale_y);
  viewport.Width = viewport_scale_x * 2.0f * float(ssaa_scale_x);
  viewport.Height = viewport_scale_y * 2.0f * float(ssaa_scale_y);
  viewport.MinDepth = viewport_offset_z;
  viewport.MaxDepth = viewport_offset_z + viewport_scale_z;
  ff_viewport_update_needed_ |= ff_viewport_.TopLeftX != viewport.TopLeftX;
  ff_viewport_update_needed_ |= ff_viewport_.TopLeftY != viewport.TopLeftY;
  ff_viewport_update_needed_ |= ff_viewport_.Width != viewport.Width;
  ff_viewport_update_needed_ |= ff_viewport_.Height != viewport.Height;
  ff_viewport_update_needed_ |= ff_viewport_.MinDepth != viewport.MinDepth;
  ff_viewport_update_needed_ |= ff_viewport_.MaxDepth != viewport.MaxDepth;
  if (ff_viewport_update_needed_) {
    ff_viewport_ = viewport;
    command_list->RSSetViewports(1, &viewport);
    ff_viewport_update_needed_ = false;
  }

  // Scissor.
  uint32_t pa_sc_window_scissor_tl =
      regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL].u32;
  uint32_t pa_sc_window_scissor_br =
      regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR].u32;
  D3D12_RECT scissor;
  scissor.left = pa_sc_window_scissor_tl & 0x7FFF;
  scissor.top = (pa_sc_window_scissor_tl >> 16) & 0x7FFF;
  scissor.right = pa_sc_window_scissor_br & 0x7FFF;
  scissor.bottom = (pa_sc_window_scissor_br >> 16) & 0x7FFF;
  if (!(pa_sc_window_scissor_tl & (1u << 31))) {
    // !WINDOW_OFFSET_DISABLE.
    scissor.left = std::max(scissor.left + window_offset_x, LONG(0));
    scissor.top = std::max(scissor.top + window_offset_y, LONG(0));
    scissor.right = std::max(scissor.right + window_offset_x, LONG(0));
    scissor.bottom = std::max(scissor.bottom + window_offset_y, LONG(0));
  }
  scissor.left *= ssaa_scale_x;
  scissor.top *= ssaa_scale_y;
  scissor.right *= ssaa_scale_x;
  scissor.bottom *= ssaa_scale_y;
  ff_scissor_update_needed_ |= ff_scissor_.left != scissor.left;
  ff_scissor_update_needed_ |= ff_scissor_.top != scissor.top;
  ff_scissor_update_needed_ |= ff_scissor_.right != scissor.right;
  ff_scissor_update_needed_ |= ff_scissor_.bottom != scissor.bottom;
  if (ff_scissor_update_needed_) {
    ff_scissor_ = scissor;
    command_list->RSSetScissorRects(1, &scissor);
    ff_scissor_update_needed_ = false;
  }

  // Blend factor.
  ff_blend_factor_update_needed_ |=
      ff_blend_factor_[0] != regs[XE_GPU_REG_RB_BLEND_RED].f32;
  ff_blend_factor_update_needed_ |=
      ff_blend_factor_[1] != regs[XE_GPU_REG_RB_BLEND_GREEN].f32;
  ff_blend_factor_update_needed_ |=
      ff_blend_factor_[2] != regs[XE_GPU_REG_RB_BLEND_BLUE].f32;
  ff_blend_factor_update_needed_ |=
      ff_blend_factor_[3] != regs[XE_GPU_REG_RB_BLEND_ALPHA].f32;
  if (ff_blend_factor_update_needed_) {
    ff_blend_factor_[0] = regs[XE_GPU_REG_RB_BLEND_RED].f32;
    ff_blend_factor_[1] = regs[XE_GPU_REG_RB_BLEND_GREEN].f32;
    ff_blend_factor_[2] = regs[XE_GPU_REG_RB_BLEND_BLUE].f32;
    ff_blend_factor_[3] = regs[XE_GPU_REG_RB_BLEND_ALPHA].f32;
    command_list->OMSetBlendFactor(ff_blend_factor_);
    ff_blend_factor_update_needed_ = false;
  }

  // Stencil reference value.
  uint32_t stencil_ref = regs[XE_GPU_REG_RB_STENCILREFMASK].u32 & 0xFF;
  ff_stencil_ref_update_needed_ |= ff_stencil_ref_ != stencil_ref;
  if (ff_stencil_ref_update_needed_) {
    ff_stencil_ref_ = stencil_ref;
    command_list->OMSetStencilRef(stencil_ref);
    ff_stencil_ref_update_needed_ = false;
  }
}

void D3D12CommandProcessor::UpdateSystemConstantValues(
    Endian index_endian,
    const RenderTargetCache::PipelineRenderTarget render_targets[4]) {
  auto& regs = *register_file_;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  uint32_t vgt_indx_offset = regs[XE_GPU_REG_VGT_INDX_OFFSET].u32;
  uint32_t pa_cl_vte_cntl = regs[XE_GPU_REG_PA_CL_VTE_CNTL].u32;
  uint32_t pa_cl_clip_cntl = regs[XE_GPU_REG_PA_CL_CLIP_CNTL].u32;
  uint32_t pa_su_vtx_cntl = regs[XE_GPU_REG_PA_SU_VTX_CNTL].u32;
  uint32_t pa_su_point_size = regs[XE_GPU_REG_PA_SU_POINT_SIZE].u32;
  uint32_t sq_program_cntl = regs[XE_GPU_REG_SQ_PROGRAM_CNTL].u32;
  uint32_t sq_context_misc = regs[XE_GPU_REG_SQ_CONTEXT_MISC].u32;
  uint32_t rb_surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t rb_colorcontrol = regs[XE_GPU_REG_RB_COLORCONTROL].u32;
  uint32_t rb_alpha_ref = regs[XE_GPU_REG_RB_ALPHA_REF].u32;

  bool dirty = false;

  // Vertex index offset.
  dirty |= system_constants_.vertex_base_index != vgt_indx_offset;
  system_constants_.vertex_base_index = vgt_indx_offset;

  // Index buffer endianness.
  dirty |= system_constants_.vertex_index_endian != uint32_t(index_endian);
  system_constants_.vertex_index_endian = uint32_t(index_endian);

  // W0 division control.
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  // VTX_XY_FMT = true: the incoming XY have already been multiplied by 1/W0.
  //            = false: multiply the X, Y coordinates by 1/W0.
  // VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
  //           = false: multiply the Z coordinate by 1/W0.
  // VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal to
  //                    get 1/W0.
  uint32_t vtx_xy_fmt = (pa_cl_vte_cntl >> 8) & 1;
  uint32_t vtx_z_fmt = (pa_cl_vte_cntl >> 9) & 1;
  uint32_t vtx_w0_fmt = (pa_cl_vte_cntl >> 10) & 1;
  dirty |= system_constants_.vertex_w_format[0] != vtx_xy_fmt;
  dirty |= system_constants_.vertex_w_format[1] != vtx_z_fmt;
  dirty |= system_constants_.vertex_w_format[2] != vtx_w0_fmt;
  system_constants_.vertex_w_format[0] = vtx_xy_fmt;
  system_constants_.vertex_w_format[1] = vtx_z_fmt;
  system_constants_.vertex_w_format[2] = vtx_w0_fmt;

  // Conversion to Direct3D 12 normalized device coordinates.
  // See viewport configuration in UpdateFixedFunctionState for explanations.
  // X and Y scale/offset is to convert unnormalized coordinates generated by
  // shaders (for rectangle list drawing, for instance) to the 2560x2560
  // viewport that is used to emulate unnormalized coordinates.
  // Z scale/offset is to convert from OpenGL NDC to Direct3D NDC if needed.
  // Also apply half-pixel offset to reproduce Direct3D 9 rasterization rules.
  // TODO(Triang3l): Check if pixel coordinates need to be offset depending on a
  // different register (and if there's such register at all).
  float viewport_scale_x = regs[XE_GPU_REG_PA_CL_VPORT_XSCALE].f32;
  float viewport_scale_y = regs[XE_GPU_REG_PA_CL_VPORT_YSCALE].f32;
  bool gl_clip_space_def =
      !(pa_cl_clip_cntl & (1 << 19)) && (pa_cl_vte_cntl & (1 << 4));
  float ndc_scale_x, ndc_scale_y;
  if (pa_cl_vte_cntl & (1 << 0)) {
    ndc_scale_x = viewport_scale_x >= 0.0f ? 1.0f : -1.0f;
  } else {
    ndc_scale_x = 1.0f / 1280.0f;
  }
  if (pa_cl_vte_cntl & (1 << 2)) {
    ndc_scale_y = viewport_scale_y >= 0.0f ? -1.0f : 1.0f;
  } else {
    ndc_scale_y = -1.0f / 1280.0f;
  }
  float ndc_scale_z = gl_clip_space_def ? 0.5f : 1.0f;
  float ndc_offset_x = (pa_cl_vte_cntl & (1 << 1)) ? 0.0f : -1.0f;
  float ndc_offset_y = (pa_cl_vte_cntl & (1 << 3)) ? 0.0f : 1.0f;
  float ndc_offset_z = gl_clip_space_def ? 0.5f : 0.0f;
  float pixel_half_pixel_offset = 0.0f;
  if (FLAGS_d3d12_half_pixel_offset && !(pa_su_vtx_cntl & (1 << 0))) {
    // Signs are hopefully correct here, tested in GTA IV on both clearing
    // (without a viewport) and drawing things near the edges of the screen.
    if (pa_cl_vte_cntl & (1 << 0)) {
      if (viewport_scale_x != 0.0f) {
        ndc_offset_x += 0.5f / viewport_scale_x;
      }
    } else {
      ndc_offset_x += 1.0f / 2560.0f;
    }
    if (pa_cl_vte_cntl & (1 << 2)) {
      if (viewport_scale_y != 0.0f) {
        ndc_offset_y += 0.5f / viewport_scale_y;
      }
    } else {
      ndc_offset_y -= 1.0f / 2560.0f;
    }
    pixel_half_pixel_offset = -0.5f;
  }
  dirty |= system_constants_.ndc_scale[0] != ndc_scale_x;
  dirty |= system_constants_.ndc_scale[1] != ndc_scale_y;
  dirty |= system_constants_.ndc_scale[2] != ndc_scale_z;
  dirty |= system_constants_.ndc_offset[0] != ndc_offset_x;
  dirty |= system_constants_.ndc_offset[1] != ndc_offset_y;
  dirty |= system_constants_.ndc_offset[2] != ndc_offset_z;
  dirty |= system_constants_.pixel_half_pixel_offset != pixel_half_pixel_offset;
  system_constants_.ndc_scale[0] = ndc_scale_x;
  system_constants_.ndc_scale[1] = ndc_scale_y;
  system_constants_.ndc_scale[2] = ndc_scale_z;
  system_constants_.ndc_offset[0] = ndc_offset_x;
  system_constants_.ndc_offset[1] = ndc_offset_y;
  system_constants_.ndc_offset[2] = ndc_offset_z;
  system_constants_.pixel_half_pixel_offset = pixel_half_pixel_offset;

  // Point size.
  float point_size[2];
  point_size[0] = float(pa_su_point_size >> 16) * 0.125f;
  point_size[1] = float(pa_su_point_size & 0xFFFF) * 0.125f;
  dirty |= system_constants_.point_size[0] != point_size[0];
  dirty |= system_constants_.point_size[1] != point_size[1];
  system_constants_.point_size[0] = point_size[0];
  system_constants_.point_size[1] = point_size[1];

  // Pixel position register.
  uint32_t pixel_pos_reg =
      (sq_program_cntl & (1 << 18)) ? (sq_context_misc >> 8) & 0xFF : UINT_MAX;
  dirty |= system_constants_.pixel_pos_reg != pixel_pos_reg;
  system_constants_.pixel_pos_reg = pixel_pos_reg;

  // Supersampling anti-aliasing pixel scale inverse for pixel positions.
  MsaaSamples msaa_samples = MsaaSamples((rb_surface_info >> 16) & 0x3);
  float ssaa_inv_scale_x = msaa_samples >= MsaaSamples::k4X ? 0.5f : 1.0f;
  float ssaa_inv_scale_y = msaa_samples >= MsaaSamples::k2X ? 0.5f : 1.0f;
  dirty |= system_constants_.ssaa_inv_scale[0] != ssaa_inv_scale_x;
  dirty |= system_constants_.ssaa_inv_scale[1] != ssaa_inv_scale_y;
  system_constants_.ssaa_inv_scale[0] = ssaa_inv_scale_x;
  system_constants_.ssaa_inv_scale[1] = ssaa_inv_scale_y;

  // Alpha test.
  int32_t alpha_test;
  if (rb_colorcontrol & 0x8) {
    uint32_t alpha_test_function = rb_colorcontrol & 0x7;
    // 0: Never - fail in [-inf, +inf].
    // 1: Less - fail in [ref, +inf].
    // 2: Equal - pass in [ref, ref].
    // 3: Less or equal - pass in [-inf, ref].
    // 4: Greater - fail in [-inf, ref].
    // 5: Not equal - fail in [ref, ref].
    // 6: Greater or equal - pass in [ref, +inf].
    // 7: Always - pass in [-inf, +inf].
    alpha_test = (alpha_test_function & 0x2) ? 1 : -1;
    uint32_t alpha_test_range_start =
        (alpha_test_function == 1 || alpha_test_function == 2 ||
         alpha_test_function == 5 || alpha_test_function == 6)
            ? rb_alpha_ref
            : 0xFF800000u;
    uint32_t alpha_test_range_end =
        (alpha_test_function == 2 || alpha_test_function == 3 ||
         alpha_test_function == 4 || alpha_test_function == 5)
            ? rb_alpha_ref
            : 0x7F800000u;
    dirty |= system_constants_.alpha_test_range[0] != alpha_test_range_start;
    dirty |= system_constants_.alpha_test_range[1] != alpha_test_range_end;
    system_constants_.alpha_test_range[0] = alpha_test_range_start;
    system_constants_.alpha_test_range[1] = alpha_test_range_end;
  } else {
    alpha_test = 0;
  }
  dirty |= system_constants_.alpha_test != alpha_test;
  system_constants_.alpha_test = alpha_test;

  // Color exponent bias and output index mapping.
  for (uint32_t i = 0; i < 4; ++i) {
    uint32_t color_info;
    switch (i) {
      case 1:
        color_info = regs[XE_GPU_REG_RB_COLOR1_INFO].u32;
        break;
      case 2:
        color_info = regs[XE_GPU_REG_RB_COLOR2_INFO].u32;
        break;
      case 3:
        color_info = regs[XE_GPU_REG_RB_COLOR3_INFO].u32;
        break;
      default:
        color_info = regs[XE_GPU_REG_RB_COLOR_INFO].u32;
    }
    float color_exp_bias;
    // Exponent bias is in bits 20:25 of RB_COLOR_INFO.
    *reinterpret_cast<int32_t*>(&color_exp_bias) =
        0x3F800000 + (int32_t((color_info & (0x3F << 20)) << 6) >> 3);
    dirty |= system_constants_.color_exp_bias[i] != color_exp_bias;
    system_constants_.color_exp_bias[i] = color_exp_bias;
    dirty |= system_constants_.color_output_map[i] !=
             render_targets[i].guest_render_target;
    system_constants_.color_output_map[i] =
        render_targets[i].guest_render_target;
  }

  cbuffer_bindings_system_.up_to_date &= !dirty;
}

bool D3D12CommandProcessor::UpdateBindings(
    ID3D12GraphicsCommandList* command_list, const D3D12Shader* vertex_shader,
    const D3D12Shader* pixel_shader, ID3D12RootSignature* root_signature) {
  auto provider = GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  auto& regs = *register_file_;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  // Bind the new root signature.
  if (current_graphics_root_signature_ != root_signature) {
    current_graphics_root_signature_ = root_signature;
    GetRootExtraParameterIndices(vertex_shader, pixel_shader,
                                 current_graphics_root_extras_);
    // We don't know which root parameters are up to date anymore.
    current_graphics_root_up_to_date_ = 0;
    command_list->SetGraphicsRootSignature(root_signature);
  }

  // Get used textures and samplers.
  uint32_t pixel_texture_count, pixel_sampler_count;
  const D3D12Shader::TextureSRV* pixel_textures;
  const D3D12Shader::SamplerBinding* pixel_samplers;
  if (pixel_shader != nullptr) {
    pixel_textures = pixel_shader->GetTextureSRVs(pixel_texture_count);
    pixel_samplers = pixel_shader->GetSamplerBindings(pixel_sampler_count);
  } else {
    pixel_textures = nullptr;
    pixel_texture_count = 0;
    pixel_samplers = nullptr;
    pixel_sampler_count = 0;
  }
  uint32_t vertex_texture_count, vertex_sampler_count;
  const D3D12Shader::TextureSRV* vertex_textures =
      vertex_shader->GetTextureSRVs(vertex_texture_count);
  const D3D12Shader::SamplerBinding* vertex_samplers =
      vertex_shader->GetSamplerBindings(vertex_sampler_count);
  uint32_t texture_count = pixel_texture_count + vertex_texture_count;
  uint32_t sampler_count = pixel_sampler_count + vertex_sampler_count;

  // Begin updating descriptors.
  bool write_common_constant_views = false;
  bool write_fetch_constant_view = false;
  bool write_vertex_float_constant_views = false;
  bool write_pixel_float_constant_views = false;
  // TODO(Triang3l): Update textures and samplers only if shaders or binding
  // hash change.
  bool write_textures = texture_count != 0;
  bool write_samplers = sampler_count != 0;

  // Update constant buffers.
  if (!cbuffer_bindings_system_.up_to_date) {
    uint8_t* system_constants = constant_buffer_pool_->RequestFull(
        xe::align(uint32_t(sizeof(system_constants_)), 256u), nullptr, nullptr,
        &cbuffer_bindings_system_.buffer_address);
    if (system_constants == nullptr) {
      return false;
    }
    std::memcpy(system_constants, &system_constants_,
                sizeof(system_constants_));
    cbuffer_bindings_system_.up_to_date = true;
    write_common_constant_views = true;
  }
  if (!cbuffer_bindings_bool_loop_.up_to_date) {
    uint32_t* bool_loop_constants =
        reinterpret_cast<uint32_t*>(constant_buffer_pool_->RequestFull(
            768, nullptr, nullptr,
            &cbuffer_bindings_bool_loop_.buffer_address));
    if (bool_loop_constants == nullptr) {
      return false;
    }
    // Bool and loop constants are quadrupled to allow dynamic indexing.
    for (uint32_t i = 0; i < 40; ++i) {
      uint32_t bool_loop_constant =
          regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 + i].u32;
      uint32_t* bool_loop_constant_vector = bool_loop_constants + (i << 2);
      bool_loop_constant_vector[0] = bool_loop_constant;
      bool_loop_constant_vector[1] = bool_loop_constant;
      bool_loop_constant_vector[2] = bool_loop_constant;
      bool_loop_constant_vector[3] = bool_loop_constant;
    }
    cbuffer_bindings_bool_loop_.up_to_date = true;
    write_common_constant_views = true;
  }
  if (!cbuffer_bindings_fetch_.up_to_date) {
    uint8_t* fetch_constants = constant_buffer_pool_->RequestFull(
        768, nullptr, nullptr, &cbuffer_bindings_fetch_.buffer_address);
    if (fetch_constants == nullptr) {
      return false;
    }
    std::memcpy(fetch_constants,
                &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0].u32,
                32 * 6 * sizeof(uint32_t));
    cbuffer_bindings_fetch_.up_to_date = true;
    write_fetch_constant_view = true;
  }
  for (uint32_t i = 0; i < 16; ++i) {
    ConstantBufferBinding& float_binding = cbuffer_bindings_float_[i];
    if (float_binding.up_to_date) {
      continue;
    }
    uint8_t* float_constants = constant_buffer_pool_->RequestFull(
        512, nullptr, nullptr, &float_binding.buffer_address);
    if (float_constants == nullptr) {
      return false;
    }
    std::memcpy(float_constants,
                &regs[XE_GPU_REG_SHADER_CONSTANT_000_X + (i << 7)].f32,
                32 * 4 * sizeof(uint32_t));
    float_binding.up_to_date = true;
    if (i < 8) {
      write_vertex_float_constant_views = true;
    } else {
      write_pixel_float_constant_views = true;
    }
  }

  // Allocate the descriptors.
  uint32_t view_count_partial_update = 0;
  if (write_common_constant_views) {
    // System and bool/loop constants.
    view_count_partial_update += 2;
  }
  if (write_fetch_constant_view) {
    // Fetch constants.
    ++view_count_partial_update;
  }
  if (write_vertex_float_constant_views) {
    // Vertex float constants.
    view_count_partial_update += 8;
  }
  if (write_pixel_float_constant_views) {
    // Pixel float constants.
    view_count_partial_update += 8;
  }
  if (write_textures) {
    view_count_partial_update += texture_count;
  }
  // All the constants + shared memory + textures.
  uint32_t view_count_full_update = 20 + texture_count;
  D3D12_CPU_DESCRIPTOR_HANDLE view_cpu_handle;
  D3D12_GPU_DESCRIPTOR_HANDLE view_gpu_handle;
  uint32_t descriptor_size_view = provider->GetViewDescriptorSize();
  uint64_t view_full_update_index = RequestViewDescriptors(
      draw_view_full_update_, view_count_partial_update, view_count_full_update,
      view_cpu_handle, view_gpu_handle);
  if (view_full_update_index == 0) {
    XELOGE("Failed to allocate view descriptors!");
    return false;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_handle = {};
  D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_handle = {};
  uint32_t descriptor_size_sampler = provider->GetSamplerDescriptorSize();
  uint64_t sampler_full_update_index = 0;
  if (sampler_count != 0) {
    sampler_full_update_index = RequestSamplerDescriptors(
        draw_sampler_full_update_, write_samplers ? sampler_count : 0,
        sampler_count, sampler_cpu_handle, sampler_gpu_handle);
    if (sampler_full_update_index == 0) {
      XELOGE("Failed to allocate sampler descriptors!");
      return false;
    }
  }
  if (draw_view_full_update_ != view_full_update_index) {
    // Need to update all view descriptors.
    draw_view_full_update_ = view_full_update_index;
    write_common_constant_views = true;
    write_fetch_constant_view = true;
    write_vertex_float_constant_views = true;
    write_pixel_float_constant_views = true;
    write_textures = texture_count != 0;
    // If updating fully, write the shared memory descriptor (t0, space1).
    shared_memory_->CreateSRV(view_cpu_handle);
    gpu_handle_shared_memory_ = view_gpu_handle;
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    current_graphics_root_up_to_date_ &= ~(1u << kRootParameter_SharedMemory);
  }
  if (sampler_count != 0 &&
      draw_sampler_full_update_ != sampler_full_update_index) {
    draw_sampler_full_update_ = sampler_full_update_index;
    write_samplers = true;
  }

  // Write the descriptors.
  D3D12_CONSTANT_BUFFER_VIEW_DESC constant_buffer_desc;
  if (write_common_constant_views) {
    gpu_handle_common_constants_ = view_gpu_handle;
    // System constants (b0).
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_system_.buffer_address;
    constant_buffer_desc.SizeInBytes =
        xe::align(uint32_t(sizeof(system_constants_)), 256u);
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    // Bool/loop constants (b1).
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_bool_loop_.buffer_address;
    constant_buffer_desc.SizeInBytes = 768;
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_CommonConstants);
  }
  if (write_fetch_constant_view) {
    gpu_handle_fetch_constants_ = view_gpu_handle;
    // Fetch constants (b2).
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_fetch_.buffer_address;
    constant_buffer_desc.SizeInBytes = 768;
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    current_graphics_root_up_to_date_ &= ~(1u << kRootParameter_FetchConstants);
  }
  if (write_vertex_float_constant_views) {
    gpu_handle_vertex_float_constants_ = view_gpu_handle;
    // Vertex float constants (b3-b10).
    for (uint32_t i = 0; i < 8; ++i) {
      constant_buffer_desc.BufferLocation =
          cbuffer_bindings_float_[i].buffer_address;
      constant_buffer_desc.SizeInBytes = 512;
      device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
    }
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_VertexFloatConstants);
  }
  if (write_pixel_float_constant_views) {
    gpu_handle_pixel_float_constants_ = view_gpu_handle;
    // Pixel float constants (b3-b10).
    for (uint32_t i = 0; i < 8; ++i) {
      constant_buffer_desc.BufferLocation =
          cbuffer_bindings_float_[8 + i].buffer_address;
      constant_buffer_desc.SizeInBytes = 512;
      device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
    }
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_PixelFloatConstants);
  }
  if (write_textures) {
    if (pixel_texture_count != 0) {
      assert_true(current_graphics_root_extras_.pixel_textures !=
                  RootExtraParameterIndices::kUnavailable);
      gpu_handle_pixel_textures_ = view_gpu_handle;
      for (uint32_t i = 0; i < pixel_texture_count; ++i) {
        const D3D12Shader::TextureSRV& srv = pixel_textures[i];
        texture_cache_->WriteTextureSRV(srv.fetch_constant, srv.dimension,
                                        view_cpu_handle);
        view_cpu_handle.ptr += descriptor_size_view;
        view_gpu_handle.ptr += descriptor_size_view;
      }
      current_graphics_root_up_to_date_ &=
          ~(1u << current_graphics_root_extras_.pixel_textures);
    }
    if (vertex_texture_count != 0) {
      assert_true(current_graphics_root_extras_.vertex_textures !=
                  RootExtraParameterIndices::kUnavailable);
      gpu_handle_vertex_textures_ = view_gpu_handle;
      for (uint32_t i = 0; i < vertex_texture_count; ++i) {
        const D3D12Shader::TextureSRV& srv = vertex_textures[i];
        texture_cache_->WriteTextureSRV(srv.fetch_constant, srv.dimension,
                                        view_cpu_handle);
        view_cpu_handle.ptr += descriptor_size_view;
        view_gpu_handle.ptr += descriptor_size_view;
      }
      current_graphics_root_up_to_date_ &=
          ~(1u << current_graphics_root_extras_.vertex_textures);
    }
  }
  if (write_samplers) {
    if (pixel_sampler_count != 0) {
      assert_true(current_graphics_root_extras_.pixel_samplers !=
                  RootExtraParameterIndices::kUnavailable);
      gpu_handle_pixel_samplers_ = sampler_gpu_handle;
      for (uint32_t i = 0; i < pixel_sampler_count; ++i) {
        const D3D12Shader::SamplerBinding& sampler = pixel_samplers[i];
        texture_cache_->WriteSampler(sampler.fetch_constant, sampler.mag_filter,
                                     sampler.min_filter, sampler.mip_filter,
                                     sampler.aniso_filter, sampler_cpu_handle);
        sampler_cpu_handle.ptr += descriptor_size_sampler;
        sampler_gpu_handle.ptr += descriptor_size_sampler;
      }
      current_graphics_root_up_to_date_ &=
          ~(1u << current_graphics_root_extras_.pixel_samplers);
    }
    if (vertex_sampler_count != 0) {
      assert_true(current_graphics_root_extras_.vertex_samplers !=
                  RootExtraParameterIndices::kUnavailable);
      gpu_handle_vertex_samplers_ = sampler_gpu_handle;
      for (uint32_t i = 0; i < vertex_sampler_count; ++i) {
        const D3D12Shader::SamplerBinding& sampler = vertex_samplers[i];
        texture_cache_->WriteSampler(sampler.fetch_constant, sampler.mag_filter,
                                     sampler.min_filter, sampler.mip_filter,
                                     sampler.aniso_filter, sampler_cpu_handle);
        sampler_cpu_handle.ptr += descriptor_size_sampler;
        sampler_gpu_handle.ptr += descriptor_size_sampler;
      }
      current_graphics_root_up_to_date_ &=
          ~(1u << current_graphics_root_extras_.vertex_samplers);
    }
  }

  // Update the root parameters.
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_FetchConstants))) {
    command_list->SetGraphicsRootDescriptorTable(kRootParameter_FetchConstants,
                                                 gpu_handle_fetch_constants_);
    current_graphics_root_up_to_date_ |= 1u << kRootParameter_FetchConstants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_VertexFloatConstants))) {
    command_list->SetGraphicsRootDescriptorTable(
        kRootParameter_VertexFloatConstants,
        gpu_handle_vertex_float_constants_);
    current_graphics_root_up_to_date_ |= 1u
                                         << kRootParameter_VertexFloatConstants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_PixelFloatConstants))) {
    command_list->SetGraphicsRootDescriptorTable(
        kRootParameter_PixelFloatConstants, gpu_handle_pixel_float_constants_);
    current_graphics_root_up_to_date_ |= 1u
                                         << kRootParameter_PixelFloatConstants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_CommonConstants))) {
    command_list->SetGraphicsRootDescriptorTable(kRootParameter_CommonConstants,
                                                 gpu_handle_common_constants_);
    current_graphics_root_up_to_date_ |= 1u << kRootParameter_CommonConstants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_SharedMemory))) {
    command_list->SetGraphicsRootDescriptorTable(kRootParameter_SharedMemory,
                                                 gpu_handle_shared_memory_);
    current_graphics_root_up_to_date_ |= 1u << kRootParameter_SharedMemory;
  }
  uint32_t extra_index;
  extra_index = current_graphics_root_extras_.pixel_textures;
  if (extra_index != RootExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    command_list->SetGraphicsRootDescriptorTable(extra_index,
                                                 gpu_handle_pixel_textures_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }
  extra_index = current_graphics_root_extras_.pixel_samplers;
  if (extra_index != RootExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    command_list->SetGraphicsRootDescriptorTable(extra_index,
                                                 gpu_handle_pixel_samplers_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }
  extra_index = current_graphics_root_extras_.vertex_textures;
  if (extra_index != RootExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    command_list->SetGraphicsRootDescriptorTable(extra_index,
                                                 gpu_handle_vertex_textures_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }
  extra_index = current_graphics_root_extras_.vertex_samplers;
  if (extra_index != RootExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    command_list->SetGraphicsRootDescriptorTable(extra_index,
                                                 gpu_handle_vertex_samplers_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }

  return true;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

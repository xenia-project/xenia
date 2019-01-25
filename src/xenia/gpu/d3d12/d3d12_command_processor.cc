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
#include "third_party/xxhash/xxhash.h"

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

DEFINE_bool(d3d12_edram_rov, true,
            "Use rasterizer-ordered views for render target emulation where "
            "available.");
// Some games (such as Banjo-Kazooie) are not aware of the half-pixel offset and
// may be blurry or have texture sampling artifacts, in this case the user may
// disable half-pixel offset by setting this to false.
DEFINE_bool(d3d12_half_pixel_offset, true,
            "Enable half-pixel vertex and VPOS offset.");
DEFINE_bool(d3d12_memexport_readback, false,
            "Read data written by memory export in shaders on the CPU. This "
            "may be needed in some games (but many only access exported data "
            "on the GPU, and this flag isn't needed to handle such behavior), "
            "but causes mid-frame synchronization, so it has a huge "
            "performance impact.");
DEFINE_bool(d3d12_ssaa_custom_sample_positions, false,
            "Enable custom SSAA sample positions for the RTV/DSV rendering "
            "path where available instead of centers (experimental, not very "
            "high-quality).");

namespace xe {
namespace gpu {
namespace d3d12 {

constexpr uint32_t
    D3D12CommandProcessor::RootExtraParameterIndices::kUnavailable;
constexpr uint32_t D3D12CommandProcessor::kSwapTextureWidth;
constexpr uint32_t D3D12CommandProcessor::kSwapTextureHeight;
constexpr uint32_t D3D12CommandProcessor::kScratchBufferSizeIncrement;

D3D12CommandProcessor::D3D12CommandProcessor(
    D3D12GraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}
D3D12CommandProcessor::~D3D12CommandProcessor() = default;

void D3D12CommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();
  cache_clear_requested_ = true;
}

void D3D12CommandProcessor::RequestFrameTrace(const std::wstring& root_path) {
  // Capture with PIX if attached.
  if (GetD3D12Context()->GetD3D12Provider()->GetGraphicsAnalysis() != nullptr) {
    pix_capture_requested_.store(true, std::memory_order_relaxed);
    return;
  }
  CommandProcessor::RequestFrameTrace(root_path);
}

bool D3D12CommandProcessor::IsROVUsedForEDRAM() const {
  if (!FLAGS_d3d12_edram_rov) {
    return false;
  }
  auto provider = GetD3D12Context()->GetD3D12Provider();
  return provider->AreRasterizerOrderedViewsSupported();
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
    deferred_command_list_->D3DResourceBarrier(barrier_count, barriers_.data());
    barriers_.clear();
  }
}

ID3D12RootSignature* D3D12CommandProcessor::GetRootSignature(
    const D3D12Shader* vertex_shader, const D3D12Shader* pixel_shader,
    PrimitiveType primitive_type) {
  assert_true(vertex_shader->is_translated());
  assert_true(pixel_shader == nullptr || pixel_shader->is_translated());

  D3D12_SHADER_VISIBILITY vertex_visibility;
  if (primitive_type == PrimitiveType::kTrianglePatch ||
      primitive_type == PrimitiveType::kQuadPatch) {
    vertex_visibility = D3D12_SHADER_VISIBILITY_DOMAIN;
  } else {
    vertex_visibility = D3D12_SHADER_VISIBILITY_VERTEX;
  }

  uint32_t texture_count_vertex, sampler_count_vertex;
  vertex_shader->GetTextureSRVs(texture_count_vertex);
  vertex_shader->GetSamplerBindings(sampler_count_vertex);
  uint32_t texture_count_pixel = 0, sampler_count_pixel = 0;
  if (pixel_shader != nullptr) {
    pixel_shader->GetTextureSRVs(texture_count_pixel);
    pixel_shader->GetSamplerBindings(sampler_count_pixel);
  }

  // Better put the pixel texture/sampler in the lower bits probably because it
  // changes often.
  uint32_t index = 0;
  uint32_t index_offset = 0;
  index |= texture_count_pixel << index_offset;
  index_offset += D3D12Shader::kMaxTextureSRVIndexBits;
  index |= sampler_count_pixel << index_offset;
  index_offset += D3D12Shader::kMaxSamplerBindingIndexBits;
  index |= texture_count_vertex << index_offset;
  index_offset += D3D12Shader::kMaxTextureSRVIndexBits;
  index |= sampler_count_vertex << index_offset;
  index_offset += D3D12Shader::kMaxSamplerBindingIndexBits;
  index |= uint32_t(vertex_visibility == D3D12_SHADER_VISIBILITY_DOMAIN)
           << index_offset;
  ++index_offset;
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
    range.BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kFetchConstants);
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  // Vertex float constants.
  {
    auto& parameter = parameters[kRootParameter_FloatConstantsVertex];
    auto& range = ranges[kRootParameter_FloatConstantsVertex];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = vertex_visibility;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kFloatConstants);
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  // Pixel float constants.
  {
    auto& parameter = parameters[kRootParameter_FloatConstantsPixel];
    auto& range = ranges[kRootParameter_FloatConstantsPixel];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kFloatConstants);
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  // System constants.
  {
    auto& parameter = parameters[kRootParameter_SystemConstants];
    auto& range = ranges[kRootParameter_SystemConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kSystemConstants);
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  // Bool and loop constants.
  {
    auto& parameter = parameters[kRootParameter_BoolLoopConstants];
    auto& range = ranges[kRootParameter_BoolLoopConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kBoolLoopConstants);
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  // Shared memory and, if ROVs are used, EDRAM.
  D3D12_DESCRIPTOR_RANGE shared_memory_and_edram_ranges[3];
  {
    auto& parameter = parameters[kRootParameter_SharedMemoryAndEDRAM];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges =
        shared_memory_and_edram_ranges;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    shared_memory_and_edram_ranges[0].RangeType =
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shared_memory_and_edram_ranges[0].NumDescriptors = 1;
    shared_memory_and_edram_ranges[0].BaseShaderRegister = 0;
    shared_memory_and_edram_ranges[0].RegisterSpace = 0;
    shared_memory_and_edram_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    shared_memory_and_edram_ranges[1].RangeType =
        D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    shared_memory_and_edram_ranges[1].NumDescriptors = 1;
    shared_memory_and_edram_ranges[1].BaseShaderRegister =
        UINT(DxbcShaderTranslator::UAVRegister::kSharedMemory);
    shared_memory_and_edram_ranges[1].RegisterSpace = 0;
    shared_memory_and_edram_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    if (IsROVUsedForEDRAM()) {
      ++parameter.DescriptorTable.NumDescriptorRanges;
      shared_memory_and_edram_ranges[2].RangeType =
          D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
      shared_memory_and_edram_ranges[2].NumDescriptors = 1;
      shared_memory_and_edram_ranges[2].BaseShaderRegister =
          UINT(DxbcShaderTranslator::UAVRegister::kEDRAM);
      shared_memory_and_edram_ranges[2].RegisterSpace = 0;
      shared_memory_and_edram_ranges[2].OffsetInDescriptorsFromTableStart = 2;
    }
  }

  // Extra parameters.

  // Pixel textures.
  if (texture_count_pixel > 0) {
    auto& parameter = parameters[desc.NumParameters];
    auto& range = ranges[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = texture_count_pixel;
    range.BaseShaderRegister = 1;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Pixel samplers.
  if (sampler_count_pixel > 0) {
    auto& parameter = parameters[desc.NumParameters];
    auto& range = ranges[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    range.NumDescriptors = sampler_count_pixel;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Vertex textures.
  if (texture_count_vertex > 0) {
    auto& parameter = parameters[desc.NumParameters];
    auto& range = ranges[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = vertex_visibility;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = texture_count_vertex;
    range.BaseShaderRegister = 1;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Vertex samplers.
  if (sampler_count_vertex > 0) {
    auto& parameter = parameters[desc.NumParameters];
    auto& range = ranges[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = vertex_visibility;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    range.NumDescriptors = sampler_count_vertex;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  ID3D12RootSignature* root_signature = ui::d3d12::util::CreateRootSignature(
      GetD3D12Context()->GetD3D12Provider(), desc);
  if (root_signature == nullptr) {
    XELOGE(
        "Failed to create a root signature with %u pixel textures, %u pixel "
        "samplers, %u vertex textures and %u vertex samplers",
        texture_count_pixel, sampler_count_pixel, texture_count_vertex,
        sampler_count_vertex);
    return nullptr;
  }
  root_signatures_.insert({index, root_signature});
  return root_signature;
}

uint32_t D3D12CommandProcessor::GetRootExtraParameterIndices(
    const D3D12Shader* vertex_shader, const D3D12Shader* pixel_shader,
    RootExtraParameterIndices& indices_out) {
  uint32_t texture_count_pixel = 0, sampler_count_pixel = 0;
  if (pixel_shader != nullptr) {
    pixel_shader->GetTextureSRVs(texture_count_pixel);
    pixel_shader->GetSamplerBindings(sampler_count_pixel);
  }
  uint32_t texture_count_vertex, sampler_count_vertex;
  vertex_shader->GetTextureSRVs(texture_count_vertex);
  vertex_shader->GetSamplerBindings(sampler_count_vertex);

  uint32_t index = kRootParameter_Count_Base;
  if (texture_count_pixel != 0) {
    indices_out.textures_pixel = index++;
  } else {
    indices_out.textures_pixel = RootExtraParameterIndices::kUnavailable;
  }
  if (sampler_count_pixel != 0) {
    indices_out.samplers_pixel = index++;
  } else {
    indices_out.samplers_pixel = RootExtraParameterIndices::kUnavailable;
  }
  if (texture_count_vertex != 0) {
    indices_out.textures_vertex = index++;
  } else {
    indices_out.textures_vertex = RootExtraParameterIndices::kUnavailable;
  }
  if (sampler_count_vertex != 0) {
    indices_out.samplers_vertex = index++;
  } else {
    indices_out.samplers_vertex = RootExtraParameterIndices::kUnavailable;
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
    current_view_heap_ = heap;
    deferred_command_list_->SetDescriptorHeaps(current_view_heap_,
                                               current_sampler_heap_);
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
    current_sampler_heap_ = heap;
    deferred_command_list_->SetDescriptorHeaps(current_view_heap_,
                                               current_sampler_heap_);
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
  // Evaluating attributes by sample index - which is done for per-sample
  // depth - is undefined with programmable sample positions, so can't use them
  // for ROV output. There's hardly any difference between 2,6 (of 0 and 3 with
  // 4x MSAA) and 4,4 anyway.
  // https://docs.microsoft.com/en-us/windows/desktop/api/d3d12/nf-d3d12-id3d12graphicscommandlist1-setsamplepositions
  if (FLAGS_d3d12_ssaa_custom_sample_positions && !IsROVUsedForEDRAM()) {
    auto provider = GetD3D12Context()->GetD3D12Provider();
    auto tier = provider->GetProgrammableSamplePositionsTier();
    if (tier >= 2 &&
        command_lists_[current_queue_frame_]->GetCommandList1() != nullptr) {
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
        deferred_command_list_->D3DSetSamplePositions(1, 4,
                                                      d3d_sample_positions);
      } else {
        deferred_command_list_->D3DSetSamplePositions(0, 0, nullptr);
      }
    }
  }
  current_sample_positions_ = sample_positions;
}

void D3D12CommandProcessor::SetComputePipeline(ID3D12PipelineState* pipeline) {
  if (current_external_pipeline_ != pipeline) {
    deferred_command_list_->D3DSetPipelineState(pipeline);
    current_external_pipeline_ = pipeline;
    current_cached_pipeline_ = nullptr;
  }
}

void D3D12CommandProcessor::UnbindRenderTargets() {
  render_target_cache_->UnbindRenderTargets();
}

void D3D12CommandProcessor::SetExternalGraphicsPipeline(
    ID3D12PipelineState* pipeline, bool reset_viewport, bool reset_blend_factor,
    bool reset_stencil_ref) {
  if (current_external_pipeline_ != pipeline) {
    deferred_command_list_->D3DSetPipelineState(pipeline);
    current_external_pipeline_ = pipeline;
    current_cached_pipeline_ = nullptr;
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

std::wstring D3D12CommandProcessor::GetWindowTitleText() const {
  if (IsROVUsedForEDRAM()) {
    // Currently scaling is only supported with ROV.
    if (texture_cache_ != nullptr && texture_cache_->IsResolutionScale2X()) {
      return L"Direct3D 12 - ROV 2x";
    } else {
      return L"Direct3D 12 - ROV";
    }
  } else {
    return L"Direct3D 12 - RTV/DSV";
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
  deferred_command_list_ = std::make_unique<DeferredCommandList>(this);

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

  texture_cache_ = std::make_unique<TextureCache>(this, register_file_,
                                                  shared_memory_.get());
  if (!texture_cache_->Initialize()) {
    XELOGE("Failed to initialize the texture cache");
    return false;
  }

  render_target_cache_ =
      std::make_unique<RenderTargetCache>(this, register_file_);
  if (!render_target_cache_->Initialize(texture_cache_.get())) {
    XELOGE("Failed to initialize the render target cache");
    return false;
  }

  pipeline_cache_ = std::make_unique<PipelineCache>(this, register_file_,
                                                    IsROVUsedForEDRAM());
  if (!pipeline_cache_->Initialize()) {
    XELOGE("Failed to initialize the graphics pipeline state cache");
    return false;
  }

  primitive_converter_ =
      std::make_unique<PrimitiveConverter>(this, register_file_, memory_);
  if (!primitive_converter_->Initialize()) {
    XELOGE("Failed to initialize the geometric primitive converter");
    return false;
  }

  // Create gamma ramp resources. The PWL gamma ramp is 16-bit, but 6 bits are
  // hardwired to zero, so DXGI_FORMAT_R10G10B10A2_UNORM can be used for it too.
  // https://www.x.org/docs/AMD/old/42590_m76_rrg_1.01o.pdf
  dirty_gamma_ramp_normal_ = true;
  dirty_gamma_ramp_pwl_ = true;
  D3D12_RESOURCE_DESC gamma_ramp_desc;
  gamma_ramp_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
  gamma_ramp_desc.Alignment = 0;
  gamma_ramp_desc.Width = 256;
  gamma_ramp_desc.Height = 1;
  gamma_ramp_desc.DepthOrArraySize = 1;
  // Normal gamma is 256x1, PWL gamma is 128x1.
  gamma_ramp_desc.MipLevels = 2;
  gamma_ramp_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  gamma_ramp_desc.SampleDesc.Count = 1;
  gamma_ramp_desc.SampleDesc.Quality = 0;
  gamma_ramp_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  gamma_ramp_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  // The first action will be uploading.
  gamma_ramp_texture_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE,
          &gamma_ramp_desc, gamma_ramp_texture_state_, nullptr,
          IID_PPV_ARGS(&gamma_ramp_texture_)))) {
    XELOGE("Failed to create the gamma ramp texture");
    return false;
  }
  // Get the layout for the upload buffer.
  gamma_ramp_desc.DepthOrArraySize = ui::d3d12::D3D12Context::kQueuedFrames;
  UINT64 gamma_ramp_upload_size;
  device->GetCopyableFootprints(
      &gamma_ramp_desc, 0, ui::d3d12::D3D12Context::kQueuedFrames * 2, 0,
      gamma_ramp_footprints_, nullptr, nullptr, &gamma_ramp_upload_size);
  // Create the upload buffer for the gamma ramp.
  ui::d3d12::util::FillBufferResourceDesc(
      gamma_ramp_desc, gamma_ramp_upload_size, D3D12_RESOURCE_FLAG_NONE);
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesUpload, D3D12_HEAP_FLAG_NONE,
          &gamma_ramp_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&gamma_ramp_upload_)))) {
    XELOGE("Failed to create the gamma ramp upload buffer");
    return false;
  }
  if (FAILED(gamma_ramp_upload_->Map(
          0, nullptr, reinterpret_cast<void**>(&gamma_ramp_upload_mapping_)))) {
    XELOGE("Failed to map the gamma ramp upload buffer");
    return false;
  }

  D3D12_RESOURCE_DESC swap_texture_desc;
  swap_texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  swap_texture_desc.Alignment = 0;
  swap_texture_desc.Width = kSwapTextureWidth;
  swap_texture_desc.Height = kSwapTextureHeight;
  if (texture_cache_->IsResolutionScale2X()) {
    swap_texture_desc.Width *= 2;
    swap_texture_desc.Height *= 2;
  }
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

  pix_capture_requested_.store(false, std::memory_order_relaxed);
  pix_capturing_ = false;

  // Just not to expose uninitialized memory.
  std::memset(&system_constants_, 0, sizeof(system_constants_));
  // Force writing of new format data.
  std::memset(system_constants_color_formats_, 0xFF,
              sizeof(system_constants_color_formats_));

  return true;
}

void D3D12CommandProcessor::ShutdownContext() {
  auto context = GetD3D12Context();
  context->AwaitAllFramesCompletion();

  ui::d3d12::util::ReleaseAndNull(readback_buffer_);
  readback_buffer_size_ = 0;

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

  // Don't need the data anymore, so zero range.
  if (gamma_ramp_upload_mapping_ != nullptr) {
    D3D12_RANGE gamma_ramp_written_range;
    gamma_ramp_written_range.Begin = 0;
    gamma_ramp_written_range.End = 0;
    gamma_ramp_upload_->Unmap(0, &gamma_ramp_written_range);
    gamma_ramp_upload_mapping_ = nullptr;
  }
  ui::d3d12::util::ReleaseAndNull(gamma_ramp_upload_);
  ui::d3d12::util::ReleaseAndNull(gamma_ramp_texture_);

  sampler_heap_pool_.reset();
  view_heap_pool_.reset();
  constant_buffer_pool_.reset();

  primitive_converter_.reset();

  pipeline_cache_.reset();

  render_target_cache_.reset();

  texture_cache_.reset();

  // Root signatured are used by pipelines, thus freed after the pipelines.
  for (auto it : root_signatures_) {
    it.second->Release();
  }
  root_signatures_.clear();

  shared_memory_.reset();

  deferred_command_list_.reset();
  for (uint32_t i = 0; i < ui::d3d12::D3D12Context::kQueuedFrames; ++i) {
    command_lists_[i].reset();
  }

  CommandProcessor::ShutdownContext();
}

void D3D12CommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  CommandProcessor::WriteRegister(index, value);

  if (index >= XE_GPU_REG_SHADER_CONSTANT_000_X &&
      index <= XE_GPU_REG_SHADER_CONSTANT_511_W) {
    if (current_queue_frame_ != UINT32_MAX) {
      uint32_t float_constant_index =
          (index - XE_GPU_REG_SHADER_CONSTANT_000_X) >> 2;
      if (float_constant_index >= 256) {
        float_constant_index -= 256;
        if (current_float_constant_map_pixel_[float_constant_index >> 6] &
            (1ull << (float_constant_index & 63))) {
          cbuffer_bindings_float_pixel_.up_to_date = false;
        }
      } else {
        if (current_float_constant_map_vertex_[float_constant_index >> 6] &
            (1ull << (float_constant_index & 63))) {
          cbuffer_bindings_float_vertex_.up_to_date = false;
        }
      }
    }
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
  } else if (index == XE_GPU_REG_DC_LUT_PWL_DATA) {
    UpdateGammaRampValue(GammaRampType::kPWL, value);
  } else if (index == XE_GPU_REG_DC_LUT_30_COLOR) {
    UpdateGammaRampValue(GammaRampType::kNormal, value);
  } else if (index == XE_GPU_REG_DC_LUT_RW_MODE) {
    gamma_ramp_rw_subindex_ = 0;
  }
}

void D3D12CommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                        uint32_t frontbuffer_width,
                                        uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");

  // In case the swap command is the only one in the frame.
  BeginFrame();

  auto provider = GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();

  // Upload the new gamma ramps.
  if (dirty_gamma_ramp_normal_) {
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& gamma_ramp_footprint =
        gamma_ramp_footprints_[current_queue_frame_ * 2];
    volatile uint32_t* mapping = reinterpret_cast<uint32_t*>(
        gamma_ramp_upload_mapping_ + gamma_ramp_footprint.Offset);
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t value = gamma_ramp_.normal[i].value;
      // Swap red and blue (Project Sylpheed has settings allowing separate
      // configuration).
      mapping[i] = ((value & 1023) << 20) | (value & (1023 << 10)) |
                   ((value >> 20) & 1023);
    }
    PushTransitionBarrier(gamma_ramp_texture_, gamma_ramp_texture_state_,
                          D3D12_RESOURCE_STATE_COPY_DEST);
    gamma_ramp_texture_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    SubmitBarriers();
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = gamma_ramp_upload_;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_source.PlacedFootprint = gamma_ramp_footprint;
    location_dest.pResource = gamma_ramp_texture_;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_dest.SubresourceIndex = 0;
    deferred_command_list_->CopyTexture(location_dest, location_source);
    dirty_gamma_ramp_normal_ = false;
  }
  if (dirty_gamma_ramp_pwl_) {
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& gamma_ramp_footprint =
        gamma_ramp_footprints_[current_queue_frame_ * 2 + 1];
    volatile uint32_t* mapping = reinterpret_cast<uint32_t*>(
        gamma_ramp_upload_mapping_ + gamma_ramp_footprint.Offset);
    for (uint32_t i = 0; i < 128; ++i) {
      // TODO(Triang3l): Find a game to test if red and blue need to be swapped.
      mapping[i] = (gamma_ramp_.pwl[i].values[0].base >> 6) |
                   (uint32_t(gamma_ramp_.pwl[i].values[1].base >> 6) << 10) |
                   (uint32_t(gamma_ramp_.pwl[i].values[2].base >> 6) << 20);
    }
    PushTransitionBarrier(gamma_ramp_texture_, gamma_ramp_texture_state_,
                          D3D12_RESOURCE_STATE_COPY_DEST);
    gamma_ramp_texture_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    SubmitBarriers();
    D3D12_TEXTURE_COPY_LOCATION location_source, location_dest;
    location_source.pResource = gamma_ramp_upload_;
    location_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location_source.PlacedFootprint = gamma_ramp_footprint;
    location_dest.pResource = gamma_ramp_texture_;
    location_dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location_dest.SubresourceIndex = 1;
    deferred_command_list_->CopyTexture(location_dest, location_source);
    dirty_gamma_ramp_pwl_ = false;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_start;
  D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu_start;
  if (RequestViewDescriptors(0, 2, 2, descriptor_cpu_start,
                             descriptor_gpu_start) != 0) {
    TextureFormat frontbuffer_format;
    if (texture_cache_->RequestSwapTexture(descriptor_cpu_start,
                                           frontbuffer_format)) {
      render_target_cache_->UnbindRenderTargets();

      // Create the gamma ramp texture descriptor.
      // This is according to D3D::InitializePresentationParameters from a game
      // executable, which initializes the normal gamma ramp for 8_8_8_8 output
      // and the PWL gamma ramp for 2_10_10_10.
      bool use_pwl_gamma_ramp =
          frontbuffer_format == TextureFormat::k_2_10_10_10 ||
          frontbuffer_format == TextureFormat::k_2_10_10_10_AS_16_16_16_16;
      D3D12_SHADER_RESOURCE_VIEW_DESC gamma_ramp_srv_desc;
      gamma_ramp_srv_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
      gamma_ramp_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
      gamma_ramp_srv_desc.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      gamma_ramp_srv_desc.Texture1D.MostDetailedMip =
          use_pwl_gamma_ramp ? 1 : 0;
      gamma_ramp_srv_desc.Texture1D.MipLevels = 1;
      gamma_ramp_srv_desc.Texture1D.ResourceMinLODClamp = 0.0f;
      device->CreateShaderResourceView(
          gamma_ramp_texture_, &gamma_ramp_srv_desc,
          provider->OffsetViewDescriptor(descriptor_cpu_start, 1));

      // The swap texture is kept as an SRV because the graphics system may draw
      // with it at any time. It's switched to RTV and back when needed.
      PushTransitionBarrier(swap_texture_,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_RENDER_TARGET);
      PushTransitionBarrier(gamma_ramp_texture_, gamma_ramp_texture_state_,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      gamma_ramp_texture_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      SubmitBarriers();

      uint32_t swap_texture_width = kSwapTextureWidth;
      uint32_t swap_texture_height = kSwapTextureHeight;
      if (texture_cache_->IsResolutionScale2X()) {
        swap_texture_width *= 2;
        swap_texture_height *= 2;
      }

      // Draw the stretching rectangle.
      deferred_command_list_->D3DOMSetRenderTargets(1, &swap_texture_rtv_, TRUE,
                                                    nullptr);
      D3D12_VIEWPORT viewport;
      viewport.TopLeftX = 0.0f;
      viewport.TopLeftY = 0.0f;
      viewport.Width = float(swap_texture_width);
      viewport.Height = float(swap_texture_height);
      viewport.MinDepth = 0.0f;
      viewport.MaxDepth = 0.0f;
      deferred_command_list_->RSSetViewport(viewport);
      D3D12_RECT scissor;
      scissor.left = 0;
      scissor.top = 0;
      scissor.right = swap_texture_width;
      scissor.bottom = swap_texture_height;
      deferred_command_list_->RSSetScissorRect(scissor);
      D3D12GraphicsSystem* graphics_system =
          static_cast<D3D12GraphicsSystem*>(graphics_system_);
      D3D12_GPU_DESCRIPTOR_HANDLE gamma_ramp_gpu_handle =
          provider->OffsetViewDescriptor(descriptor_gpu_start, 1);
      graphics_system->StretchTextureToFrontBuffer(
          descriptor_gpu_start, &gamma_ramp_gpu_handle,
          use_pwl_gamma_ramp ? (1.0f / 128.0f) : (1.0f / 256.0f),
          *deferred_command_list_);
      // Ending the current frame's command list anyway, so no need to unbind
      // the render targets when using ROV.

      PushTransitionBarrier(swap_texture_, D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      // Don't care about graphics state because the frame is ending anyway.
      {
        std::lock_guard<std::mutex> lock(swap_state_.mutex);
        swap_state_.width = swap_texture_width;
        swap_state_.height = swap_texture_height;
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

    pipeline_cache_->ClearCache();

    render_target_cache_->ClearCache();

    texture_cache_->ClearCache();

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
  auto context = GetD3D12Context();
  auto device = context->GetD3D12Provider()->GetDevice();
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
    // TODO(Triang3l): Do something so memexport still works in this case maybe?
    // Unlikely that zero would even really be legal though.
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
  // Depth-only mode doesn't need a pixel shader.
  if (enable_mode == xenos::ModeControl::kDepth) {
    pixel_shader = nullptr;
  } else if (!pixel_shader) {
    // Need a pixel shader in normal color mode.
    return false;
  }
  // Translate the shaders now to get memexport configuration and color mask,
  // which is needed by the render target cache, and also to get used textures
  // and samplers.
  if (!pipeline_cache_->EnsureShadersTranslated(vertex_shader, pixel_shader,
                                                primitive_type)) {
    return false;
  }

  // Check if memexport is used. If it is, we can't skip draw calls that have no
  // visual effect.
  bool memexport_used_vertex =
      !vertex_shader->memexport_stream_constants().empty();
  bool memexport_used_pixel =
      pixel_shader != nullptr &&
      !pixel_shader->memexport_stream_constants().empty();
  bool memexport_used = memexport_used_vertex || memexport_used_pixel;

  if (!memexport_used_vertex &&
      (regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32 & 0x3) == 0x3 &&
      primitive_type != PrimitiveType::kPointList &&
      primitive_type != PrimitiveType::kRectangleList) {
    // Both sides are culled - can't reproduce this with rasterizer state.
    return true;
  }

  uint32_t color_mask = GetCurrentColorMask(pixel_shader);
  uint32_t rb_depthcontrol = regs[XE_GPU_REG_RB_DEPTHCONTROL].u32;
  uint32_t rb_stencilrefmask = regs[XE_GPU_REG_RB_STENCILREFMASK].u32;
  if (!memexport_used && !color_mask &&
      ((rb_depthcontrol & (0x2 | 0x4)) != (0x2 | 0x4)) &&
      (!(rb_depthcontrol & 0x1) || !(rb_stencilrefmask & (0xFF << 16)))) {
    // Not writing to color, depth or stencil, so doesn't draw.
    return true;
  }

  bool new_frame = BeginFrame();

  // Set up the render targets - this may bind pipelines.
  if (!render_target_cache_->UpdateRenderTargets(pixel_shader)) {
    // Doesn't actually draw.
    // TODO(Triang3l): Do something so memexport still works in this case maybe?
    // Not distingushing between no operation and a true failure.
    return true;
  }
  const RenderTargetCache::PipelineRenderTarget* pipeline_render_targets =
      render_target_cache_->GetCurrentPipelineRenderTargets();

  // Set up primitive topology.
  bool indexed = index_buffer_info != nullptr && index_buffer_info->guest_base;
  // Adaptive tessellation requires an index buffer, but it contains per-edge
  // tessellation factors (as floats) instead of control point indices.
  bool adaptive_tessellation;
  if (primitive_type == PrimitiveType::kTrianglePatch ||
      primitive_type == PrimitiveType::kQuadPatch) {
    TessellationMode tessellation_mode =
        TessellationMode(regs[XE_GPU_REG_VGT_HOS_CNTL].u32 & 0x3);
    adaptive_tessellation = tessellation_mode == TessellationMode::kAdaptive;
    if (adaptive_tessellation &&
        (!indexed || index_buffer_info->format != IndexFormat::kInt32)) {
      return false;
    }
    // TODO(Triang3l): Implement all tessellation modes if games using any other
    // than adaptive are found. The biggest question about them is what is being
    // passed to vertex shader registers, especially if patches are drawn with
    // an index buffer.
    // https://www.slideshare.net/blackdevilvikas/next-generation-graphics-programming-on-xbox-360
    if (tessellation_mode != TessellationMode::kAdaptive) {
      XELOGE(
          "Tessellation mode %u is not implemented yet, only adaptive is "
          "partially available now - report the game to Xenia developers!",
          uint32_t(tessellation_mode));
      return false;
    }
  } else {
    adaptive_tessellation = false;
  }
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
    case PrimitiveType::kTrianglePatch:
      primitive_topology = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
      break;
    case PrimitiveType::kQuadPatch:
      primitive_topology = D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
      break;
    default:
      return false;
  }
  if (primitive_topology_ != primitive_topology) {
    primitive_topology_ = primitive_topology;
    deferred_command_list_->D3DIASetPrimitiveTopology(primitive_topology);
  }
  uint32_t line_loop_closing_index;
  if (primitive_type == PrimitiveType::kLineLoop && !indexed &&
      index_count >= 3) {
    // Add a vertex to close the loop, and make the vertex shader replace its
    // index (before adding the offset) with 0 to fetch the first vertex again.
    // For indexed line loops, the primitive converter will add the vertex.
    line_loop_closing_index = index_count;
    ++index_count;
  } else {
    // Replace index 0 with 0 (do nothing) otherwise.
    line_loop_closing_index = 0;
  }

  // Update the textures - this may bind pipelines.
  texture_cache_->RequestTextures(
      vertex_shader->GetUsedTextureMask(),
      pixel_shader != nullptr ? pixel_shader->GetUsedTextureMask() : 0);

  // Create the pipeline if needed and bind it.
  void* pipeline_handle;
  ID3D12RootSignature* root_signature;
  if (!pipeline_cache_->ConfigurePipeline(
          vertex_shader, pixel_shader, primitive_type_converted,
          indexed ? index_buffer_info->format : IndexFormat::kInt16,
          pipeline_render_targets, &pipeline_handle, &root_signature)) {
    return false;
  }
  if (current_cached_pipeline_ != pipeline_handle) {
    deferred_command_list_->SetPipelineStateHandle(
        reinterpret_cast<void*>(pipeline_handle));
    current_cached_pipeline_ = pipeline_handle;
    current_external_pipeline_ = nullptr;
  }

  // Update viewport, scissor, blend factor and stencil reference.
  UpdateFixedFunctionState();

  // Update system constants before uploading them.
  UpdateSystemConstantValues(
      memexport_used, primitive_type, line_loop_closing_index,
      indexed ? index_buffer_info->endianness : Endian::kUnspecified,
      adaptive_tessellation ? (index_buffer_info->guest_base & 0x1FFFFFFC) : 0,
      color_mask, pipeline_render_targets);

  // Update constant buffers, descriptors and root parameters.
  if (!UpdateBindings(vertex_shader, pixel_shader, root_signature)) {
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
      XELOGW("Vertex fetch type is not 3 (fetch constant %u is %.8X %.8X)!",
             vfetch_index, regs[vfetch_constant_index].u32,
             regs[vfetch_constant_index + 1].u32);
      return false;
    }
    if (!shared_memory_->RequestRange(
            regs[vfetch_constant_index].u32 & 0x1FFFFFFC,
            regs[vfetch_constant_index + 1].u32 & 0x3FFFFFC)) {
      XELOGE(
          "Failed to request vertex buffer at 0x%.8X (size %u) in the shared "
          "memory",
          regs[vfetch_constant_index].u32 & 0x1FFFFFFC,
          regs[vfetch_constant_index + 1].u32 & 0x3FFFFFC);
      return false;
    }
    vertex_buffers_resident[vfetch_index >> 6] |= 1ull << (vfetch_index & 63);
  }

  // Gather memexport ranges and ensure the heaps for them are resident, and
  // also load the data surrounding the export and to fill the regions that
  // won't be modified by the shaders.
  struct MemExportRange {
    uint32_t base_address_dwords;
    uint32_t size_dwords;
  };
  MemExportRange memexport_ranges[512];
  uint32_t memexport_range_count = 0;
  if (memexport_used_vertex) {
    const std::vector<uint32_t>& memexport_stream_constants_vertex =
        vertex_shader->memexport_stream_constants();
    for (uint32_t constant_index : memexport_stream_constants_vertex) {
      const xenos::xe_gpu_memexport_stream_t* memexport_stream =
          reinterpret_cast<const xenos::xe_gpu_memexport_stream_t*>(
              &regs[XE_GPU_REG_SHADER_CONSTANT_000_X + constant_index * 4]);
      if (memexport_stream->index_count == 0) {
        continue;
      }
      uint32_t memexport_format_size =
          GetSupportedMemExportFormatSize(memexport_stream->format);
      if (memexport_format_size == 0) {
        XELOGE(
            "Unsupported memexport format %s",
            FormatInfo::Get(TextureFormat(uint32_t(memexport_stream->format)))
                ->name);
        return false;
      }
      uint32_t memexport_base_address = memexport_stream->base_address;
      uint32_t memexport_size_dwords =
          memexport_stream->index_count * memexport_format_size;
      // Try to reduce the number of shared memory operations when writing
      // different elements into the same buffer through different exports
      // (happens in Halo 3).
      bool memexport_range_reused = false;
      for (uint32_t i = 0; i < memexport_range_count; ++i) {
        MemExportRange& memexport_range = memexport_ranges[i];
        if (memexport_range.base_address_dwords == memexport_base_address) {
          memexport_range.size_dwords =
              std::max(memexport_range.size_dwords, memexport_size_dwords);
          memexport_range_reused = true;
          break;
        }
      }
      // Add a new range if haven't expanded an existing one.
      if (!memexport_range_reused) {
        MemExportRange& memexport_range =
            memexport_ranges[memexport_range_count++];
        memexport_range.base_address_dwords = memexport_base_address;
        memexport_range.size_dwords = memexport_size_dwords;
      }
    }
  }
  if (memexport_used_pixel) {
    const std::vector<uint32_t>& memexport_stream_constants_pixel =
        pixel_shader->memexport_stream_constants();
    for (uint32_t constant_index : memexport_stream_constants_pixel) {
      const xenos::xe_gpu_memexport_stream_t* memexport_stream =
          reinterpret_cast<const xenos::xe_gpu_memexport_stream_t*>(
              &regs[XE_GPU_REG_SHADER_CONSTANT_256_X + constant_index * 4]);
      if (memexport_stream->index_count == 0) {
        continue;
      }
      uint32_t memexport_format_size =
          GetSupportedMemExportFormatSize(memexport_stream->format);
      if (memexport_format_size == 0) {
        XELOGE(
            "Unsupported memexport format %s",
            FormatInfo::Get(TextureFormat(uint32_t(memexport_stream->format)))
                ->name);
        return false;
      }
      uint32_t memexport_base_address = memexport_stream->base_address;
      uint32_t memexport_size_dwords =
          memexport_stream->index_count * memexport_format_size;
      bool memexport_range_reused = false;
      for (uint32_t i = 0; i < memexport_range_count; ++i) {
        MemExportRange& memexport_range = memexport_ranges[i];
        if (memexport_range.base_address_dwords == memexport_base_address) {
          memexport_range.size_dwords =
              std::max(memexport_range.size_dwords, memexport_size_dwords);
          memexport_range_reused = true;
          break;
        }
      }
      if (!memexport_range_reused) {
        MemExportRange& memexport_range =
            memexport_ranges[memexport_range_count++];
        memexport_range.base_address_dwords = memexport_base_address;
        memexport_range.size_dwords = memexport_size_dwords;
      }
    }
  }
  for (uint32_t i = 0; i < memexport_range_count; ++i) {
    const MemExportRange& memexport_range = memexport_ranges[i];
    if (!shared_memory_->RequestRange(memexport_range.base_address_dwords << 2,
                                      memexport_range.size_dwords << 2)) {
      XELOGE(
          "Failed to request memexport stream at 0x%.8X (size %u) in the "
          "shared memory",
          memexport_range.base_address_dwords << 2,
          memexport_range.size_dwords << 2);
      return false;
    }
  }

  if (IsROVUsedForEDRAM()) {
    render_target_cache_->UseEDRAMAsUAV();
  }

  // Actually draw.
  if (indexed) {
    uint32_t index_size = index_buffer_info->format == IndexFormat::kInt32
                              ? sizeof(uint32_t)
                              : sizeof(uint16_t);
    assert_false(index_buffer_info->guest_base & (index_size - 1));
    uint32_t index_base =
        index_buffer_info->guest_base & 0x1FFFFFFF & ~(index_size - 1);
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
    ID3D12Resource* scratch_index_buffer = nullptr;
    if (conversion_result == PrimitiveConverter::ConversionResult::kConverted) {
      index_buffer_view.SizeInBytes = converted_index_count * index_size;
      index_count = converted_index_count;
    } else {
      uint32_t index_buffer_size = index_buffer_info->count * index_size;
      if (!shared_memory_->RequestRange(index_base, index_buffer_size)) {
        XELOGE(
            "Failed to request index buffer at 0x%.8X (size %u) in the shared "
            "memory",
            index_base, index_buffer_size);
        return false;
      }
      if (memexport_used && !adaptive_tessellation) {
        // If the shared memory is a UAV, it can't be used as an index buffer
        // (UAV is a read/write state, index buffer is a read-only state). Need
        // to copy the indices to a buffer in the index buffer state.
        scratch_index_buffer = RequestScratchGPUBuffer(
            index_buffer_size, D3D12_RESOURCE_STATE_COPY_DEST);
        if (scratch_index_buffer == nullptr) {
          return false;
        }
        shared_memory_->UseAsCopySource();
        SubmitBarriers();
        deferred_command_list_->D3DCopyBufferRegion(
            scratch_index_buffer, 0, shared_memory_->GetBuffer(), index_base,
            index_buffer_size);
        PushTransitionBarrier(scratch_index_buffer,
                              D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_STATE_INDEX_BUFFER);
        index_buffer_view.BufferLocation =
            scratch_index_buffer->GetGPUVirtualAddress();
      } else {
        index_buffer_view.BufferLocation =
            shared_memory_->GetGPUAddress() + index_base;
      }
      index_buffer_view.SizeInBytes = index_buffer_size;
    }
    if (memexport_used) {
      shared_memory_->UseForWriting();
    } else {
      shared_memory_->UseForReading();
    }
    SubmitBarriers();
    if (adaptive_tessellation) {
      // Index buffer used for per-edge factors.
      deferred_command_list_->D3DDrawInstanced(index_count, 1, 0, 0);
    } else {
      deferred_command_list_->D3DIASetIndexBuffer(&index_buffer_view);
      deferred_command_list_->D3DDrawIndexedInstanced(index_count, 1, 0, 0, 0);
    }
    if (scratch_index_buffer != nullptr) {
      ReleaseScratchGPUBuffer(scratch_index_buffer,
                              D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
  } else {
    // Check if need to draw using a conversion index buffer.
    uint32_t converted_index_count;
    D3D12_GPU_VIRTUAL_ADDRESS conversion_gpu_address =
        primitive_converter_->GetStaticIndexBuffer(primitive_type, index_count,
                                                   converted_index_count);
    if (memexport_used) {
      shared_memory_->UseForWriting();
    } else {
      shared_memory_->UseForReading();
    }
    SubmitBarriers();
    if (conversion_gpu_address) {
      D3D12_INDEX_BUFFER_VIEW index_buffer_view;
      index_buffer_view.BufferLocation = conversion_gpu_address;
      index_buffer_view.SizeInBytes = converted_index_count * sizeof(uint16_t);
      index_buffer_view.Format = DXGI_FORMAT_R16_UINT;
      deferred_command_list_->D3DIASetIndexBuffer(&index_buffer_view);
      deferred_command_list_->D3DDrawIndexedInstanced(converted_index_count, 1,
                                                      0, 0, 0);
    } else {
      deferred_command_list_->D3DDrawInstanced(index_count, 1, 0, 0);
    }
  }

  if (memexport_used) {
    // Commit shared memory writing.
    PushUAVBarrier(shared_memory_->GetBuffer());
    // Invalidate textures in memexported memory and watch for changes.
    for (uint32_t i = 0; i < memexport_range_count; ++i) {
      const MemExportRange& memexport_range = memexport_ranges[i];
      shared_memory_->RangeWrittenByGPU(
          memexport_range.base_address_dwords << 2,
          memexport_range.size_dwords << 2);
    }
    if (FLAGS_d3d12_memexport_readback) {
      // Read the exported data on the CPU.
      uint32_t memexport_total_size = 0;
      for (uint32_t i = 0; i < memexport_range_count; ++i) {
        memexport_total_size += memexport_ranges[i].size_dwords << 2;
      }
      if (memexport_total_size != 0) {
        ID3D12Resource* readback_buffer =
            RequestReadbackBuffer(memexport_total_size);
        if (readback_buffer != nullptr) {
          shared_memory_->UseAsCopySource();
          SubmitBarriers();
          ID3D12Resource* shared_memory_buffer = shared_memory_->GetBuffer();
          uint32_t readback_buffer_offset = 0;
          for (uint32_t i = 0; i < memexport_range_count; ++i) {
            const MemExportRange& memexport_range = memexport_ranges[i];
            uint32_t memexport_range_size = memexport_range.size_dwords << 2;
            deferred_command_list_->D3DCopyBufferRegion(
                readback_buffer, readback_buffer_offset, shared_memory_buffer,
                memexport_range.base_address_dwords << 2, memexport_range_size);
            readback_buffer_offset += memexport_range_size;
          }
          EndFrame();
          context->AwaitAllFramesCompletion();
          D3D12_RANGE readback_range;
          readback_range.Begin = 0;
          readback_range.End = memexport_total_size;
          void* readback_mapping;
          if (SUCCEEDED(readback_buffer->Map(0, &readback_range,
                                             &readback_mapping))) {
            const uint32_t* readback_dwords =
                reinterpret_cast<const uint32_t*>(readback_mapping);
            for (uint32_t i = 0; i < memexport_range_count; ++i) {
              const MemExportRange& memexport_range = memexport_ranges[i];
              std::memcpy(memory_->TranslatePhysical(
                              memexport_range.base_address_dwords << 2),
                          readback_dwords, memexport_range.size_dwords << 2);
              readback_dwords += memexport_range.size_dwords;
            }
            D3D12_RANGE readback_write_range = {};
            readback_buffer->Unmap(0, &readback_write_range);
          }
        }
      }
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
  auto provider = context->GetD3D12Provider();
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
  current_cached_pipeline_ = nullptr;
  current_external_pipeline_ = nullptr;
  current_graphics_root_signature_ = nullptr;
  current_graphics_root_up_to_date_ = 0;
  current_view_heap_ = nullptr;
  current_sampler_heap_ = nullptr;
  std::memset(current_float_constant_map_vertex_, 0,
              sizeof(current_float_constant_map_vertex_));
  std::memset(current_float_constant_map_pixel_, 0,
              sizeof(current_float_constant_map_pixel_));
  cbuffer_bindings_system_.up_to_date = false;
  cbuffer_bindings_float_vertex_.up_to_date = false;
  cbuffer_bindings_float_pixel_.up_to_date = false;
  cbuffer_bindings_bool_loop_.up_to_date = false;
  cbuffer_bindings_fetch_.up_to_date = false;
  draw_view_full_update_ = 0;
  draw_sampler_full_update_ = 0;
  texture_bindings_written_vertex_ = false;
  texture_bindings_written_pixel_ = false;
  samplers_written_vertex_ = false;
  samplers_written_pixel_ = false;
  primitive_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

  pix_capturing_ =
      pix_capture_requested_.exchange(false, std::memory_order_relaxed);
  if (pix_capturing_) {
    IDXGraphicsAnalysis* graphics_analysis = provider->GetGraphicsAnalysis();
    if (graphics_analysis != nullptr) {
      graphics_analysis->BeginCapture();
    }
  }
  deferred_command_list_->Reset();

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

  pipeline_cache_->EndFrame();

  render_target_cache_->EndFrame();

  texture_cache_->EndFrame();

  shared_memory_->EndFrame();

  // Submit barriers now because resources the queued barriers are for may be
  // destroyed between frames.
  SubmitBarriers();

  // Submit the command list.
  auto current_command_list = command_lists_[current_queue_frame_].get();
  current_command_list->BeginRecording();
  deferred_command_list_->Execute(current_command_list->GetCommandList(),
                                  current_command_list->GetCommandList1());
  current_command_list->Execute();

  if (pix_capturing_) {
    IDXGraphicsAnalysis* graphics_analysis =
        GetD3D12Context()->GetD3D12Provider()->GetGraphicsAnalysis();
    if (graphics_analysis != nullptr) {
      graphics_analysis->EndCapture();
    }
    pix_capturing_ = false;
  }

  sampler_heap_pool_->EndFrame();
  view_heap_pool_->EndFrame();
  constant_buffer_pool_->EndFrame();

  auto context = GetD3D12Context();
  context->EndSwap();
  current_queue_frame_ = UINT32_MAX;

  return true;
}

void D3D12CommandProcessor::UpdateFixedFunctionState() {
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
  // EDRAM with multisampling with RTV/DSV (with ROV, there's MSAA), and also
  // resolution scale.
  uint32_t pixel_size_x, pixel_size_y;
  if (IsROVUsedForEDRAM()) {
    pixel_size_x = 1;
    pixel_size_y = 1;
  } else {
    MsaaSamples msaa_samples =
        MsaaSamples((regs[XE_GPU_REG_RB_SURFACE_INFO].u32 >> 16) & 0x3);
    pixel_size_x = msaa_samples >= MsaaSamples::k4X ? 2 : 1;
    pixel_size_y = msaa_samples >= MsaaSamples::k2X ? 2 : 1;
  }
  if (texture_cache_->IsResolutionScale2X()) {
    pixel_size_x *= 2;
    pixel_size_y *= 2;
  }

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
      (viewport_offset_x - viewport_scale_x) * float(pixel_size_x);
  viewport.TopLeftY =
      (viewport_offset_y - viewport_scale_y) * float(pixel_size_y);
  viewport.Width = viewport_scale_x * 2.0f * float(pixel_size_x);
  viewport.Height = viewport_scale_y * 2.0f * float(pixel_size_y);
  viewport.MinDepth = viewport_offset_z;
  viewport.MaxDepth = viewport_offset_z + viewport_scale_z;
  if (viewport_scale_z < 0.0f) {
    // MinDepth > MaxDepth doesn't work on Nvidia, emulating it in vertex
    // shaders and when applying polygon offset.
    std::swap(viewport.MinDepth, viewport.MaxDepth);
  }
  ff_viewport_update_needed_ |= ff_viewport_.TopLeftX != viewport.TopLeftX;
  ff_viewport_update_needed_ |= ff_viewport_.TopLeftY != viewport.TopLeftY;
  ff_viewport_update_needed_ |= ff_viewport_.Width != viewport.Width;
  ff_viewport_update_needed_ |= ff_viewport_.Height != viewport.Height;
  ff_viewport_update_needed_ |= ff_viewport_.MinDepth != viewport.MinDepth;
  ff_viewport_update_needed_ |= ff_viewport_.MaxDepth != viewport.MaxDepth;
  if (ff_viewport_update_needed_) {
    ff_viewport_ = viewport;
    deferred_command_list_->RSSetViewport(viewport);
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
  scissor.left *= pixel_size_x;
  scissor.top *= pixel_size_y;
  scissor.right *= pixel_size_x;
  scissor.bottom *= pixel_size_y;
  ff_scissor_update_needed_ |= ff_scissor_.left != scissor.left;
  ff_scissor_update_needed_ |= ff_scissor_.top != scissor.top;
  ff_scissor_update_needed_ |= ff_scissor_.right != scissor.right;
  ff_scissor_update_needed_ |= ff_scissor_.bottom != scissor.bottom;
  if (ff_scissor_update_needed_) {
    ff_scissor_ = scissor;
    deferred_command_list_->RSSetScissorRect(scissor);
    ff_scissor_update_needed_ = false;
  }

  if (!IsROVUsedForEDRAM()) {
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
      deferred_command_list_->D3DOMSetBlendFactor(ff_blend_factor_);
      ff_blend_factor_update_needed_ = false;
    }

    // Stencil reference value.
    uint32_t stencil_ref = regs[XE_GPU_REG_RB_STENCILREFMASK].u32 & 0xFF;
    ff_stencil_ref_update_needed_ |= ff_stencil_ref_ != stencil_ref;
    if (ff_stencil_ref_update_needed_) {
      ff_stencil_ref_ = stencil_ref;
      deferred_command_list_->D3DOMSetStencilRef(stencil_ref);
      ff_stencil_ref_update_needed_ = false;
    }
  }
}

void D3D12CommandProcessor::UpdateSystemConstantValues(
    bool shared_memory_is_uav, PrimitiveType primitive_type,
    uint32_t line_loop_closing_index, Endian index_endian,
    uint32_t edge_factor_base, uint32_t color_mask,
    const RenderTargetCache::PipelineRenderTarget render_targets[4]) {
  auto& regs = *register_file_;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  int32_t vgt_indx_offset = int32_t(regs[XE_GPU_REG_VGT_INDX_OFFSET].u32);
  uint32_t pa_cl_vte_cntl = regs[XE_GPU_REG_PA_CL_VTE_CNTL].u32;
  uint32_t rb_depthcontrol = regs[XE_GPU_REG_RB_DEPTHCONTROL].u32;
  uint32_t rb_stencilrefmask = regs[XE_GPU_REG_RB_STENCILREFMASK].u32;
  uint32_t rb_depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
  uint32_t pa_cl_clip_cntl = regs[XE_GPU_REG_PA_CL_CLIP_CNTL].u32;
  uint32_t pa_su_vtx_cntl = regs[XE_GPU_REG_PA_SU_VTX_CNTL].u32;
  uint32_t pa_su_point_size = regs[XE_GPU_REG_PA_SU_POINT_SIZE].u32;
  uint32_t pa_su_point_minmax = regs[XE_GPU_REG_PA_SU_POINT_MINMAX].u32;
  uint32_t sq_program_cntl = regs[XE_GPU_REG_SQ_PROGRAM_CNTL].u32;
  uint32_t sq_context_misc = regs[XE_GPU_REG_SQ_CONTEXT_MISC].u32;
  uint32_t rb_surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t rb_colorcontrol = regs[XE_GPU_REG_RB_COLORCONTROL].u32;
  float rb_alpha_ref = regs[XE_GPU_REG_RB_ALPHA_REF].f32;
  uint32_t pa_su_sc_mode_cntl = regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32;

  // Get the color info register values for each render target, and also put
  // some safety measures for the ROV path - disable fully aliased render
  // targets. Also, for ROV, exclude components that don't exist in the format
  // from the write mask.
  uint32_t color_infos[4], rov_color_format_rt_flags[4];
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
    color_infos[i] = color_info;

    if (IsROVUsedForEDRAM()) {
      ColorRenderTargetFormat color_format =
          RenderTargetCache::GetBaseColorFormat(
              ColorRenderTargetFormat((color_info >> 16) & 0xF));
      uint32_t rt_flags =
          DxbcShaderTranslator::GetColorFormatRTFlags(color_format);
      rov_color_format_rt_flags[i] = rt_flags;

      // Exclude unused components from the write mask.
      color_mask &=
          ~(((rt_flags >> DxbcShaderTranslator::kRTFlag_FormatUnusedR_Shift) &
             0xF)
            << (i * 4));

      // Disable the render target if it has the same EDRAM base as another one
      // (with a smaller index - assume it's more important).
      if (color_mask & (0xF << (i * 4))) {
        uint32_t edram_base = color_info & 0xFFF;
        for (uint32_t j = 0; j < i; ++j) {
          if ((color_mask & (0xF << (j * 4))) &&
              edram_base == (color_infos[j] & 0xFFF)) {
            color_mask &= ~(uint32_t(0xF << (i * 4)));
            break;
          }
        }
      }
    }
  }

  // Disable depth and stencil if it aliases a color render target (for
  // instance, during the XBLA logo in Banjo-Kazooie, though depth writing is
  // already disabled there).
  if (IsROVUsedForEDRAM() && (rb_depthcontrol & (0x1 | 0x2))) {
    uint32_t edram_base_depth = rb_depth_info & 0xFFF;
    for (uint32_t i = 0; i < 4; ++i) {
      if ((color_mask & (0xF << (i * 4))) &&
          edram_base_depth == (color_infos[i] & 0xFFF)) {
        rb_depthcontrol &= ~(uint32_t(0x1 | 0x2));
        break;
      }
    }
  }

  // Get viewport Z scale - needed for flags and ROV output.
  float viewport_scale_z = (pa_cl_vte_cntl & (1 << 4))
                               ? regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE].f32
                               : 1.0f;

  bool dirty = false;

  // Flags.
  uint32_t flags = 0;
  // Whether shared memory is an SRV or a UAV. Because a resource can't be in a
  // read-write (UAV) and a read-only (SRV, IBV) state at once, if any shader in
  // the pipeline uses memexport, the shared memory buffer must be a UAV.
  if (shared_memory_is_uav) {
    flags |= DxbcShaderTranslator::kSysFlag_SharedMemoryIsUAV;
  }
  // W0 division control.
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  // 8: VTX_XY_FMT = true: the incoming XY have already been multiplied by 1/W0.
  //               = false: multiply the X, Y coordinates by 1/W0.
  // 9: VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
  //              = false: multiply the Z coordinate by 1/W0.
  // 10: VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal
  //                        to get 1/W0.
  if (pa_cl_vte_cntl & (1 << 8)) {
    flags |= DxbcShaderTranslator::kSysFlag_XYDividedByW;
  }
  if (pa_cl_vte_cntl & (1 << 9)) {
    flags |= DxbcShaderTranslator::kSysFlag_ZDividedByW;
  }
  if (pa_cl_vte_cntl & (1 << 10)) {
    flags |= DxbcShaderTranslator::kSysFlag_WNotReciprocal;
  }
  // Reversed depth.
  if (viewport_scale_z < 0.0f) {
    flags |= DxbcShaderTranslator::kSysFlag_ReverseZ;
  }
  // Alpha test.
  if (rb_colorcontrol & 0x8) {
    flags |= (rb_colorcontrol & 0x7)
             << DxbcShaderTranslator::kSysFlag_AlphaPassIfLess_Shift;
  } else {
    flags |= DxbcShaderTranslator::kSysFlag_AlphaPassIfLess |
             DxbcShaderTranslator::kSysFlag_AlphaPassIfEqual |
             DxbcShaderTranslator::kSysFlag_AlphaPassIfGreater;
  }
  // Alpha to coverage.
  if (rb_colorcontrol & 0x10) {
    flags |= DxbcShaderTranslator::kSysFlag_AlphaToCoverage;
  }
  // Gamma writing.
  if (((regs[XE_GPU_REG_RB_COLOR_INFO].u32 >> 16) & 0xF) ==
      uint32_t(ColorRenderTargetFormat::k_8_8_8_8_GAMMA)) {
    flags |= DxbcShaderTranslator::kSysFlag_Color0Gamma;
  }
  if (((regs[XE_GPU_REG_RB_COLOR1_INFO].u32 >> 16) & 0xF) ==
      uint32_t(ColorRenderTargetFormat::k_8_8_8_8_GAMMA)) {
    flags |= DxbcShaderTranslator::kSysFlag_Color1Gamma;
  }
  if (((regs[XE_GPU_REG_RB_COLOR2_INFO].u32 >> 16) & 0xF) ==
      uint32_t(ColorRenderTargetFormat::k_8_8_8_8_GAMMA)) {
    flags |= DxbcShaderTranslator::kSysFlag_Color2Gamma;
  }
  if (((regs[XE_GPU_REG_RB_COLOR3_INFO].u32 >> 16) & 0xF) ==
      uint32_t(ColorRenderTargetFormat::k_8_8_8_8_GAMMA)) {
    flags |= DxbcShaderTranslator::kSysFlag_Color3Gamma;
  }
  if (IsROVUsedForEDRAM() && (rb_depthcontrol & (0x1 | 0x2))) {
    flags |= DxbcShaderTranslator::kSysFlag_DepthStencil;
    if (DepthRenderTargetFormat((rb_depth_info >> 16) & 0x1) ==
        DepthRenderTargetFormat::kD24FS8) {
      flags |= DxbcShaderTranslator::kSysFlag_DepthFloat24;
    }
    if (rb_depthcontrol & 0x2) {
      flags |= ((rb_depthcontrol >> 4) & 0x7)
               << DxbcShaderTranslator::kSysFlag_DepthPassIfLess_Shift;
      if (rb_depthcontrol & 0x4) {
        flags |= DxbcShaderTranslator::kSysFlag_DepthWriteMask |
                 DxbcShaderTranslator::kSysFlag_DepthStencilWrite;
      }
    } else {
      // In case stencil is used without depth testing - always pass, and
      // don't modify the stored depth.
      flags |= DxbcShaderTranslator::kSysFlag_DepthPassIfLess |
               DxbcShaderTranslator::kSysFlag_DepthPassIfEqual |
               DxbcShaderTranslator::kSysFlag_DepthPassIfGreater;
    }
    if (rb_depthcontrol & 0x1) {
      flags |= DxbcShaderTranslator::kSysFlag_StencilTest;
      if (rb_stencilrefmask & (0xFF << 16)) {
        flags |= DxbcShaderTranslator::kSysFlag_DepthStencilWrite;
      }
    }
  }
  dirty |= system_constants_.flags != flags;
  system_constants_.flags = flags;

  // Tessellation factor range, plus 1.0 according to the images in
  // https://www.slideshare.net/blackdevilvikas/next-generation-graphics-programming-on-xbox-360
  float tessellation_factor_min =
      regs[XE_GPU_REG_VGT_HOS_MIN_TESS_LEVEL].f32 + 1.0f;
  float tessellation_factor_max =
      regs[XE_GPU_REG_VGT_HOS_MAX_TESS_LEVEL].f32 + 1.0f;
  dirty |= system_constants_.tessellation_factor_range_min !=
           tessellation_factor_min;
  system_constants_.tessellation_factor_range_min = tessellation_factor_min;
  dirty |= system_constants_.tessellation_factor_range_max !=
           tessellation_factor_max;
  system_constants_.tessellation_factor_range_max = tessellation_factor_max;

  // Line loop closing index (or 0 when drawing other primitives or using an
  // index buffer).
  dirty |= system_constants_.line_loop_closing_index != line_loop_closing_index;
  system_constants_.line_loop_closing_index = line_loop_closing_index;

  // Vertex index offset.
  dirty |= system_constants_.vertex_base_index != vgt_indx_offset;
  system_constants_.vertex_base_index = vgt_indx_offset;

  // Index buffer endianness and adaptive tessellation factors.
  uint32_t index_endian_and_edge_factors =
      uint32_t(index_endian) | edge_factor_base;
  dirty |= system_constants_.vertex_index_endian_and_edge_factors !=
           index_endian_and_edge_factors;
  system_constants_.vertex_index_endian_and_edge_factors =
      index_endian_and_edge_factors;

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
  // Like in OpenGL - VPOS giving pixel centers.
  // TODO(Triang3l): Check if ps_param_gen should give center positions in
  // OpenGL mode on the Xbox 360.
  float pixel_half_pixel_offset = 0.5f;
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
    // Like in Direct3D 9 - VPOS giving the top-left corner.
    pixel_half_pixel_offset = 0.0f;
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
  float point_size_x = float(pa_su_point_size >> 16) * 0.125f;
  float point_size_y = float(pa_su_point_size & 0xFFFF) * 0.125f;
  float point_size_min = float(pa_su_point_minmax & 0xFFFF) * 0.125f;
  float point_size_max = float(pa_su_point_minmax >> 16) * 0.125f;
  dirty |= system_constants_.point_size[0] != point_size_x;
  dirty |= system_constants_.point_size[1] != point_size_y;
  dirty |= system_constants_.point_size_min_max[0] != point_size_min;
  dirty |= system_constants_.point_size_min_max[1] != point_size_max;
  system_constants_.point_size[0] = point_size_x;
  system_constants_.point_size[1] = point_size_y;
  system_constants_.point_size_min_max[0] = point_size_min;
  system_constants_.point_size_min_max[1] = point_size_max;
  float point_screen_to_ndc_x, point_screen_to_ndc_y;
  if (pa_cl_vte_cntl & (1 << 0)) {
    point_screen_to_ndc_x =
        (viewport_scale_x != 0.0f) ? (0.5f / viewport_scale_x) : 0.0f;
  } else {
    point_screen_to_ndc_x = 1.0f / 2560.0f;
  }
  if (pa_cl_vte_cntl & (1 << 2)) {
    point_screen_to_ndc_y =
        (viewport_scale_y != 0.0f) ? (-0.5f / viewport_scale_y) : 0.0f;
  } else {
    point_screen_to_ndc_y = -1.0f / 2560.0f;
  }
  dirty |= system_constants_.point_screen_to_ndc[0] != point_screen_to_ndc_x;
  dirty |= system_constants_.point_screen_to_ndc[1] != point_screen_to_ndc_y;
  system_constants_.point_screen_to_ndc[0] = point_screen_to_ndc_x;
  system_constants_.point_screen_to_ndc[1] = point_screen_to_ndc_y;

  // Pixel position register.
  uint32_t pixel_pos_reg =
      (sq_program_cntl & (1 << 18)) ? (sq_context_misc >> 8) & 0xFF : UINT_MAX;
  dirty |= system_constants_.pixel_pos_reg != pixel_pos_reg;
  system_constants_.pixel_pos_reg = pixel_pos_reg;

  // Log2 of sample count, for scaling VPOS with SSAA (without ROV) and for
  // EDRAM address calculation with MSAA (with ROV).
  MsaaSamples msaa_samples = MsaaSamples((rb_surface_info >> 16) & 0x3);
  uint32_t sample_count_log2_x = msaa_samples >= MsaaSamples::k4X ? 1 : 0;
  uint32_t sample_count_log2_y = msaa_samples >= MsaaSamples::k2X ? 1 : 0;
  dirty |= system_constants_.sample_count_log2[0] != sample_count_log2_x;
  dirty |= system_constants_.sample_count_log2[1] != sample_count_log2_y;
  system_constants_.sample_count_log2[0] = sample_count_log2_x;
  system_constants_.sample_count_log2[1] = sample_count_log2_y;

  // Alpha test.
  dirty |= system_constants_.alpha_test_reference != rb_alpha_ref;
  system_constants_.alpha_test_reference = rb_alpha_ref;

  // EDRAM pitch for ROV writing.
  if (IsROVUsedForEDRAM()) {
    uint32_t edram_pitch_tiles = ((std::min(rb_surface_info & 0x3FFFu, 2560u) *
                                   (msaa_samples >= MsaaSamples::k4X ? 2 : 1)) +
                                  79) /
                                 80;
    dirty |= system_constants_.edram_pitch_tiles != edram_pitch_tiles;
    system_constants_.edram_pitch_tiles = edram_pitch_tiles;
  }

  // Color exponent bias and output index mapping or ROV render target writing.
  bool colorcontrol_blend_enable = (rb_colorcontrol & 0x20) == 0;
  for (uint32_t i = 0; i < 4; ++i) {
    uint32_t color_info = color_infos[i];
    uint32_t blend_control;
    switch (i) {
      case 1:
        blend_control = regs[XE_GPU_REG_RB_BLENDCONTROL_1].u32;
        break;
      case 2:
        blend_control = regs[XE_GPU_REG_RB_BLENDCONTROL_2].u32;
        break;
      case 3:
        blend_control = regs[XE_GPU_REG_RB_BLENDCONTROL_3].u32;
        break;
      default:
        blend_control = regs[XE_GPU_REG_RB_BLENDCONTROL_0].u32;
    }
    // Exponent bias is in bits 20:25 of RB_COLOR_INFO.
    int32_t color_exp_bias = int32_t(color_info << 6) >> 26;
    ColorRenderTargetFormat color_format =
        RenderTargetCache::GetBaseColorFormat(
            ColorRenderTargetFormat((color_info >> 16) & 0xF));
    if (color_format == ColorRenderTargetFormat::k_16_16 ||
        color_format == ColorRenderTargetFormat::k_16_16_16_16) {
      // On the Xbox 360, k_16_16_EDRAM and k_16_16_16_16_EDRAM internally have
      // -32...32 range and expect shaders to give -32...32 values, but they're
      // emulated using normalized RG16/RGBA16 when not using the ROV, so the
      // value returned from the shader needs to be divided by 32 (blending will
      // be incorrect in this case, but there's no other way without using ROV).
      // http://www.students.science.uu.nl/~3220516/advancedgraphics/papers/inferred_lighting.pdf
      if (!IsROVUsedForEDRAM()) {
        color_exp_bias -= 5;
      }
    }
    float color_exp_bias_scale;
    *reinterpret_cast<int32_t*>(&color_exp_bias_scale) =
        0x3F800000 + (color_exp_bias << 23);
    dirty |= system_constants_.color_exp_bias[i] != color_exp_bias_scale;
    system_constants_.color_exp_bias[i] = color_exp_bias_scale;
    if (IsROVUsedForEDRAM()) {
      uint32_t edram_base_dwords = (color_info & 0xFFF) * 1280;
      dirty |= system_constants_.edram_base_dwords[i] != edram_base_dwords;
      system_constants_.edram_base_dwords[i] = edram_base_dwords;
      uint32_t rt_flags = rov_color_format_rt_flags[i];
      // Unused components already excluded from the write mask when color infos
      // were obtained, and fully aliased render targets were already skipped.
      uint32_t rt_mask = (color_mask >> (i * 4)) & 0xF;
      if (rt_mask != 0) {
        rt_flags |= rt_mask << DxbcShaderTranslator::kRTFlag_WriteR_Shift;
        uint32_t blend_x, blend_y;
        if (colorcontrol_blend_enable &&
            DxbcShaderTranslator::GetBlendConstants(blend_control, blend_x,
                                                    blend_y)) {
          rt_flags |= DxbcShaderTranslator::kRTFlag_Blend;
          uint32_t rt_pair_index = i >> 1;
          uint32_t rt_pair_comp = (i & 1) << 1;
          if (system_constants_
                  .edram_blend_rt01_rt23[rt_pair_index][rt_pair_comp] !=
              blend_x) {
            dirty = true;
            system_constants_
                .edram_blend_rt01_rt23[rt_pair_index][rt_pair_comp] = blend_x;
          }
          if (system_constants_
                  .edram_blend_rt01_rt23[rt_pair_index][rt_pair_comp + 1] !=
              blend_y) {
            dirty = true;
            system_constants_
                .edram_blend_rt01_rt23[rt_pair_index][rt_pair_comp + 1] =
                blend_y;
          }
        }
      }
      dirty |= system_constants_.edram_rt_flags[i] != rt_flags;
      system_constants_.edram_rt_flags[i] = rt_flags;
      if (system_constants_color_formats_[i] != color_format) {
        dirty = true;
        DxbcShaderTranslator::SetColorFormatSystemConstants(system_constants_,
                                                            i, color_format);
        system_constants_color_formats_[i] = color_format;
      }
    } else {
      dirty |= system_constants_.color_output_map[i] !=
               render_targets[i].guest_render_target;
      system_constants_.color_output_map[i] =
          render_targets[i].guest_render_target;
    }
  }

  // Resolution scale, depth/stencil testing and blend constant for ROV.
  if (IsROVUsedForEDRAM()) {
    uint32_t resolution_scale_log2 =
        texture_cache_->IsResolutionScale2X() ? 1 : 0;
    dirty |=
        system_constants_.edram_resolution_scale_log2 != resolution_scale_log2;
    system_constants_.edram_resolution_scale_log2 = resolution_scale_log2;

    uint32_t depth_base_dwords = (rb_depth_info & 0xFFF) * 1280;
    dirty |= system_constants_.edram_depth_base_dwords != depth_base_dwords;
    system_constants_.edram_depth_base_dwords = depth_base_dwords;

    // The Z range is reversed in the vertex shader if it's reverse - use the
    // absolute value of the scale.
    float depth_range_scale = std::abs(viewport_scale_z);
    dirty |= system_constants_.edram_depth_range_scale != depth_range_scale;
    system_constants_.edram_depth_range_scale = depth_range_scale;
    float depth_range_offset = (pa_cl_vte_cntl & (1 << 5))
                                   ? regs[XE_GPU_REG_PA_CL_VPORT_ZOFFSET].f32
                                   : 0.0f;
    if (viewport_scale_z < 0.0f) {
      // Similar to MinDepth in fixed-function viewport calculation.
      depth_range_offset += viewport_scale_z;
    }
    dirty |= system_constants_.edram_depth_range_offset != depth_range_offset;
    system_constants_.edram_depth_range_offset = depth_range_offset;

    // For points and lines, front polygon offset is used, and it's enabled if
    // POLY_OFFSET_PARA_ENABLED is set, for polygons, separate front and back
    // are used.
    float poly_offset_front_scale = 0.0f, poly_offset_front_offset = 0.0f;
    float poly_offset_back_scale = 0.0f, poly_offset_back_offset = 0.0f;
    if (primitive_type == PrimitiveType::kPointList ||
        primitive_type == PrimitiveType::kLineList ||
        primitive_type == PrimitiveType::kLineStrip ||
        primitive_type == PrimitiveType::kLineLoop ||
        primitive_type == PrimitiveType::k2DLineStrip) {
      if (pa_su_sc_mode_cntl & (1 << 13)) {
        poly_offset_front_scale =
            regs[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE].f32;
        poly_offset_front_offset =
            regs[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET].f32;
        poly_offset_back_scale = poly_offset_front_scale;
        poly_offset_back_offset = poly_offset_front_offset;
      }
    } else {
      if (pa_su_sc_mode_cntl & (1 << 11)) {
        poly_offset_front_scale =
            regs[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE].f32;
        poly_offset_front_offset =
            regs[XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET].f32;
      }
      if (pa_su_sc_mode_cntl & (1 << 12)) {
        poly_offset_back_scale =
            regs[XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE].f32;
        poly_offset_back_offset =
            regs[XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET].f32;
      }
    }
    if (viewport_scale_z < 0.0f) {
      // Clip space flipped in the vertex shader, so flip polygon offset too.
      poly_offset_front_scale = -poly_offset_front_scale;
      poly_offset_front_offset = -poly_offset_front_offset;
      poly_offset_back_scale = -poly_offset_back_scale;
      poly_offset_back_offset = -poly_offset_back_offset;
    }
    // See PipelineCache for explanation.
    poly_offset_front_scale *= 1.0f / 16.0f;
    poly_offset_back_scale *= 1.0f / 16.0f;
    dirty |= system_constants_.edram_poly_offset_front_scale !=
             poly_offset_front_scale;
    system_constants_.edram_poly_offset_front_scale = poly_offset_front_scale;
    dirty |= system_constants_.edram_poly_offset_front_offset !=
             poly_offset_front_offset;
    system_constants_.edram_poly_offset_front_offset = poly_offset_front_offset;
    dirty |= system_constants_.edram_poly_offset_back_scale !=
             poly_offset_back_scale;
    system_constants_.edram_poly_offset_back_scale = poly_offset_back_scale;
    dirty |= system_constants_.edram_poly_offset_back_offset !=
             poly_offset_back_offset;
    system_constants_.edram_poly_offset_back_offset = poly_offset_back_offset;

    if (rb_depthcontrol & 0x1) {
      uint32_t stencil_value;

      stencil_value = rb_stencilrefmask & 0xFF;
      dirty |= system_constants_.edram_stencil_reference != stencil_value;
      system_constants_.edram_stencil_reference = stencil_value;
      stencil_value = (rb_stencilrefmask >> 8) & 0xFF;
      dirty |= system_constants_.edram_stencil_read_mask != stencil_value;
      system_constants_.edram_stencil_read_mask = stencil_value;
      stencil_value = (rb_stencilrefmask >> 16) & 0xFF;
      dirty |= system_constants_.edram_stencil_write_mask != stencil_value;
      system_constants_.edram_stencil_write_mask = stencil_value;

      static const uint32_t kStencilOpMap[] = {
          DxbcShaderTranslator::kStencilOp_Keep,
          DxbcShaderTranslator::kStencilOp_Zero,
          DxbcShaderTranslator::kStencilOp_Replace,
          DxbcShaderTranslator::kStencilOp_IncrementSaturate,
          DxbcShaderTranslator::kStencilOp_DecrementSaturate,
          DxbcShaderTranslator::kStencilOp_Invert,
          DxbcShaderTranslator::kStencilOp_Increment,
          DxbcShaderTranslator::kStencilOp_Decrement,
      };

      stencil_value = kStencilOpMap[(rb_depthcontrol >> 11) & 0x7];
      dirty |= system_constants_.edram_stencil_front_fail != stencil_value;
      system_constants_.edram_stencil_front_fail = stencil_value;
      stencil_value = kStencilOpMap[(rb_depthcontrol >> 17) & 0x7];
      dirty |=
          system_constants_.edram_stencil_front_depth_fail != stencil_value;
      system_constants_.edram_stencil_front_depth_fail = stencil_value;
      stencil_value = kStencilOpMap[(rb_depthcontrol >> 14) & 0x7];
      dirty |= system_constants_.edram_stencil_front_pass != stencil_value;
      system_constants_.edram_stencil_front_pass = stencil_value;
      stencil_value = (rb_depthcontrol >> 8) & 0x7;
      dirty |=
          system_constants_.edram_stencil_front_comparison != stencil_value;
      system_constants_.edram_stencil_front_comparison = stencil_value;

      if (rb_depthcontrol & 0x80) {
        stencil_value = kStencilOpMap[(rb_depthcontrol >> 23) & 0x7];
        dirty |= system_constants_.edram_stencil_back_fail != stencil_value;
        system_constants_.edram_stencil_back_fail = stencil_value;
        stencil_value = kStencilOpMap[(rb_depthcontrol >> 29) & 0x7];
        dirty |=
            system_constants_.edram_stencil_back_depth_fail != stencil_value;
        system_constants_.edram_stencil_back_depth_fail = stencil_value;
        stencil_value = kStencilOpMap[(rb_depthcontrol >> 26) & 0x7];
        dirty |= system_constants_.edram_stencil_back_pass != stencil_value;
        system_constants_.edram_stencil_back_pass = stencil_value;
        stencil_value = (rb_depthcontrol >> 20) & 0x7;
        dirty |=
            system_constants_.edram_stencil_back_comparison != stencil_value;
        system_constants_.edram_stencil_back_comparison = stencil_value;
      } else {
        dirty |= std::memcmp(system_constants_.edram_stencil_back,
                             system_constants_.edram_stencil_front,
                             4 * sizeof(uint32_t)) != 0;
        std::memcpy(system_constants_.edram_stencil_back,
                    system_constants_.edram_stencil_front,
                    4 * sizeof(uint32_t));
      }
    }

    dirty |= system_constants_.edram_blend_constant[0] !=
             regs[XE_GPU_REG_RB_BLEND_RED].f32;
    system_constants_.edram_blend_constant[0] =
        regs[XE_GPU_REG_RB_BLEND_RED].f32;
    dirty |= system_constants_.edram_blend_constant[1] !=
             regs[XE_GPU_REG_RB_BLEND_GREEN].f32;
    system_constants_.edram_blend_constant[1] =
        regs[XE_GPU_REG_RB_BLEND_GREEN].f32;
    dirty |= system_constants_.edram_blend_constant[2] !=
             regs[XE_GPU_REG_RB_BLEND_BLUE].f32;
    system_constants_.edram_blend_constant[2] =
        regs[XE_GPU_REG_RB_BLEND_BLUE].f32;
    dirty |= system_constants_.edram_blend_constant[3] !=
             regs[XE_GPU_REG_RB_BLEND_ALPHA].f32;
    system_constants_.edram_blend_constant[3] =
        regs[XE_GPU_REG_RB_BLEND_ALPHA].f32;
  }

  cbuffer_bindings_system_.up_to_date &= !dirty;
}

bool D3D12CommandProcessor::UpdateBindings(
    const D3D12Shader* vertex_shader, const D3D12Shader* pixel_shader,
    ID3D12RootSignature* root_signature) {
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
    deferred_command_list_->D3DSetGraphicsRootSignature(root_signature);
  }

  XXH64_state_t hash_state;

  // Get textures and samplers used by the vertex shader.
  uint32_t texture_count_vertex, sampler_count_vertex;
  const D3D12Shader::TextureSRV* textures_vertex =
      vertex_shader->GetTextureSRVs(texture_count_vertex);
  uint64_t texture_bindings_hash_vertex =
      texture_count_vertex != 0
          ? texture_cache_->GetDescriptorHashForActiveTextures(
                textures_vertex, texture_count_vertex)
          : 0;
  const D3D12Shader::SamplerBinding* samplers_vertex =
      vertex_shader->GetSamplerBindings(sampler_count_vertex);
  XXH64_reset(&hash_state, 0);
  for (uint32_t i = 0; i < sampler_count_vertex; ++i) {
    TextureCache::SamplerParameters sampler_parameters =
        texture_cache_->GetSamplerParameters(samplers_vertex[i]);
    XXH64_update(&hash_state, &sampler_parameters, sizeof(sampler_parameters));
  }
  uint64_t samplers_hash_vertex = XXH64_digest(&hash_state);

  // Get textures and samplers used by the pixel shader.
  uint32_t texture_count_pixel, sampler_count_pixel;
  const D3D12Shader::TextureSRV* textures_pixel;
  const D3D12Shader::SamplerBinding* samplers_pixel;
  if (pixel_shader != nullptr) {
    textures_pixel = pixel_shader->GetTextureSRVs(texture_count_pixel);
    samplers_pixel = pixel_shader->GetSamplerBindings(sampler_count_pixel);
  } else {
    textures_pixel = nullptr;
    texture_count_pixel = 0;
    samplers_pixel = nullptr;
    sampler_count_pixel = 0;
  }
  uint64_t texture_bindings_hash_pixel =
      texture_count_pixel != 0
          ? texture_cache_->GetDescriptorHashForActiveTextures(
                textures_pixel, texture_count_pixel)
          : 0;
  XXH64_reset(&hash_state, 0);
  for (uint32_t i = 0; i < sampler_count_pixel; ++i) {
    TextureCache::SamplerParameters sampler_parameters =
        texture_cache_->GetSamplerParameters(samplers_pixel[i]);
    XXH64_update(&hash_state, &sampler_parameters, sizeof(sampler_parameters));
  }
  uint64_t samplers_hash_pixel = XXH64_digest(&hash_state);

  // Begin updating descriptors.
  bool write_system_constant_view = false;
  bool write_float_constant_view_vertex = false;
  bool write_float_constant_view_pixel = false;
  bool write_bool_loop_constant_view = false;
  bool write_fetch_constant_view = false;
  bool write_textures_vertex =
      texture_count_vertex != 0 &&
      (!texture_bindings_written_vertex_ ||
       current_texture_bindings_hash_vertex_ != texture_bindings_hash_vertex);
  bool write_textures_pixel =
      texture_count_pixel != 0 &&
      (!texture_bindings_written_pixel_ ||
       current_texture_bindings_hash_pixel_ != texture_bindings_hash_pixel);
  bool write_samplers_vertex =
      sampler_count_vertex != 0 &&
      (!samplers_written_vertex_ ||
       current_samplers_hash_vertex_ != samplers_hash_vertex);
  bool write_samplers_pixel =
      sampler_count_pixel != 0 &&
      (!samplers_written_pixel_ ||
       current_samplers_hash_pixel_ != samplers_hash_pixel);

  // Check if the float constant layout is still the same and get the counts.
  const Shader::ConstantRegisterMap& float_constant_map_vertex =
      vertex_shader->constant_register_map();
  uint32_t float_constant_count_vertex = float_constant_map_vertex.float_count;
  // Even if the shader doesn't need any float constants, a valid binding must
  // still be provided, so if the first draw in the frame with the current root
  // signature doesn't have float constants at all, still allocate an empty
  // buffer.
  uint32_t float_constant_size_vertex = xe::align(
      uint32_t(std::max(float_constant_count_vertex, 1u) * 4 * sizeof(float)),
      256u);
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_float_constant_map_vertex_[i] !=
        float_constant_map_vertex.float_bitmap[i]) {
      current_float_constant_map_vertex_[i] =
          float_constant_map_vertex.float_bitmap[i];
      // If no float constants at all, we can reuse any buffer for them, so not
      // invalidating.
      if (float_constant_map_vertex.float_count != 0) {
        cbuffer_bindings_float_vertex_.up_to_date = false;
      }
    }
  }
  uint32_t float_constant_count_pixel = 0;
  if (pixel_shader != nullptr) {
    const Shader::ConstantRegisterMap& float_constant_map_pixel =
        pixel_shader->constant_register_map();
    float_constant_count_pixel = float_constant_map_pixel.float_count;
    for (uint32_t i = 0; i < 4; ++i) {
      if (current_float_constant_map_pixel_[i] !=
          float_constant_map_pixel.float_bitmap[i]) {
        current_float_constant_map_pixel_[i] =
            float_constant_map_pixel.float_bitmap[i];
        if (float_constant_map_pixel.float_count != 0) {
          cbuffer_bindings_float_pixel_.up_to_date = false;
        }
      }
    }
  } else {
    std::memset(current_float_constant_map_pixel_, 0,
                sizeof(current_float_constant_map_pixel_));
  }
  uint32_t float_constant_size_pixel = xe::align(
      uint32_t(std::max(float_constant_count_pixel, 1u) * 4 * sizeof(float)),
      256u);

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
    write_system_constant_view = true;
  }
  if (!cbuffer_bindings_float_vertex_.up_to_date) {
    uint8_t* float_constants = constant_buffer_pool_->RequestFull(
        float_constant_size_vertex, nullptr, nullptr,
        &cbuffer_bindings_float_vertex_.buffer_address);
    if (float_constants == nullptr) {
      return false;
    }
    for (uint32_t i = 0; i < 4; ++i) {
      uint64_t float_constant_map_entry =
          float_constant_map_vertex.float_bitmap[i];
      uint32_t float_constant_index;
      while (xe::bit_scan_forward(float_constant_map_entry,
                                  &float_constant_index)) {
        float_constant_map_entry &= ~(1ull << float_constant_index);
        std::memcpy(float_constants,
                    &regs[XE_GPU_REG_SHADER_CONSTANT_000_X + (i << 8) +
                          (float_constant_index << 2)]
                         .f32,
                    4 * sizeof(float));
        float_constants += 4 * sizeof(float);
      }
    }
    cbuffer_bindings_float_vertex_.up_to_date = true;
    write_float_constant_view_vertex = true;
  }
  if (!cbuffer_bindings_float_pixel_.up_to_date) {
    uint8_t* float_constants = constant_buffer_pool_->RequestFull(
        float_constant_size_pixel, nullptr, nullptr,
        &cbuffer_bindings_float_pixel_.buffer_address);
    if (float_constants == nullptr) {
      return false;
    }
    if (pixel_shader != nullptr) {
      const Shader::ConstantRegisterMap& float_constant_map_pixel =
          pixel_shader->constant_register_map();
      for (uint32_t i = 0; i < 4; ++i) {
        uint64_t float_constant_map_entry =
            float_constant_map_pixel.float_bitmap[i];
        uint32_t float_constant_index;
        while (xe::bit_scan_forward(float_constant_map_entry,
                                    &float_constant_index)) {
          float_constant_map_entry &= ~(1ull << float_constant_index);
          std::memcpy(float_constants,
                      &regs[XE_GPU_REG_SHADER_CONSTANT_256_X + (i << 8) +
                            (float_constant_index << 2)]
                           .f32,
                      4 * sizeof(float));
          float_constants += 4 * sizeof(float);
        }
      }
    }
    cbuffer_bindings_float_pixel_.up_to_date = true;
    write_float_constant_view_pixel = true;
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
    write_bool_loop_constant_view = true;
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

  // Allocate the descriptors.
  uint32_t view_count_partial_update = 0;
  if (write_system_constant_view) {
    ++view_count_partial_update;
  }
  if (write_float_constant_view_vertex) {
    ++view_count_partial_update;
  }
  if (write_float_constant_view_pixel) {
    ++view_count_partial_update;
  }
  if (write_bool_loop_constant_view) {
    ++view_count_partial_update;
  }
  if (write_fetch_constant_view) {
    ++view_count_partial_update;
  }
  if (write_textures_vertex) {
    view_count_partial_update += texture_count_vertex;
  }
  if (write_textures_pixel) {
    view_count_partial_update += texture_count_pixel;
  }
  // All the constants + shared memory SRV and UAV + textures.
  uint32_t view_count_full_update =
      7 + texture_count_vertex + texture_count_pixel;
  if (IsROVUsedForEDRAM()) {
    // + EDRAM UAV.
    ++view_count_full_update;
  }
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
  uint32_t sampler_count_partial_update = 0;
  if (write_samplers_vertex) {
    sampler_count_partial_update += sampler_count_vertex;
  }
  if (write_samplers_pixel) {
    sampler_count_partial_update += sampler_count_pixel;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_handle = {};
  D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_handle = {};
  uint32_t descriptor_size_sampler = provider->GetSamplerDescriptorSize();
  uint64_t sampler_full_update_index = 0;
  if (sampler_count_vertex != 0 || sampler_count_pixel != 0) {
    sampler_full_update_index = RequestSamplerDescriptors(
        draw_sampler_full_update_, sampler_count_partial_update,
        sampler_count_vertex + sampler_count_pixel, sampler_cpu_handle,
        sampler_gpu_handle);
    if (sampler_full_update_index == 0) {
      XELOGE("Failed to allocate sampler descriptors!");
      return false;
    }
  }
  if (draw_view_full_update_ != view_full_update_index) {
    // Need to update all view descriptors.
    write_system_constant_view = true;
    write_fetch_constant_view = true;
    write_float_constant_view_vertex = true;
    write_float_constant_view_pixel = true;
    write_bool_loop_constant_view = true;
    write_textures_vertex = texture_count_vertex != 0;
    write_textures_pixel = texture_count_pixel != 0;
    texture_bindings_written_vertex_ = false;
    texture_bindings_written_pixel_ = false;
    // If updating fully, write the shared memory SRV and UAV descriptors and,
    // if needed, the EDRAM descriptor.
    gpu_handle_shared_memory_and_edram_ = view_gpu_handle;
    shared_memory_->WriteRawSRVDescriptor(view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    shared_memory_->WriteRawUAVDescriptor(view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    if (IsROVUsedForEDRAM()) {
      render_target_cache_->WriteEDRAMUint32UAVDescriptor(view_cpu_handle);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
    }
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_SharedMemoryAndEDRAM);
  }
  if (draw_sampler_full_update_ != sampler_full_update_index) {
    write_samplers_vertex = sampler_count_vertex != 0;
    write_samplers_pixel = sampler_count_pixel != 0;
    samplers_written_vertex_ = false;
    samplers_written_pixel_ = false;
  }

  // Write the descriptors.
  D3D12_CONSTANT_BUFFER_VIEW_DESC constant_buffer_desc;
  if (write_system_constant_view) {
    gpu_handle_system_constants_ = view_gpu_handle;
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_system_.buffer_address;
    constant_buffer_desc.SizeInBytes =
        xe::align(uint32_t(sizeof(system_constants_)), 256u);
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_SystemConstants);
  }
  if (write_float_constant_view_vertex) {
    gpu_handle_float_constants_vertex_ = view_gpu_handle;
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_float_vertex_.buffer_address;
    constant_buffer_desc.SizeInBytes = float_constant_size_vertex;
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_FloatConstantsVertex);
  }
  if (write_float_constant_view_pixel) {
    gpu_handle_float_constants_pixel_ = view_gpu_handle;
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_float_pixel_.buffer_address;
    constant_buffer_desc.SizeInBytes = float_constant_size_pixel;
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_FloatConstantsPixel);
  }
  if (write_bool_loop_constant_view) {
    gpu_handle_bool_loop_constants_ = view_gpu_handle;
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_bool_loop_.buffer_address;
    constant_buffer_desc.SizeInBytes = 768;
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_BoolLoopConstants);
  }
  if (write_fetch_constant_view) {
    gpu_handle_fetch_constants_ = view_gpu_handle;
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_fetch_.buffer_address;
    constant_buffer_desc.SizeInBytes = 768;
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    current_graphics_root_up_to_date_ &= ~(1u << kRootParameter_FetchConstants);
  }
  if (write_textures_vertex) {
    assert_true(current_graphics_root_extras_.textures_vertex !=
                RootExtraParameterIndices::kUnavailable);
    gpu_handle_textures_vertex_ = view_gpu_handle;
    for (uint32_t i = 0; i < texture_count_vertex; ++i) {
      const D3D12Shader::TextureSRV& srv = textures_vertex[i];
      texture_cache_->WriteTextureSRV(srv, view_cpu_handle);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
    }
    texture_bindings_written_vertex_ = true;
    current_texture_bindings_hash_vertex_ = texture_bindings_hash_vertex;
    current_graphics_root_up_to_date_ &=
        ~(1u << current_graphics_root_extras_.textures_vertex);
  }
  if (write_textures_pixel) {
    assert_true(current_graphics_root_extras_.textures_pixel !=
                RootExtraParameterIndices::kUnavailable);
    gpu_handle_textures_pixel_ = view_gpu_handle;
    for (uint32_t i = 0; i < texture_count_pixel; ++i) {
      const D3D12Shader::TextureSRV& srv = textures_pixel[i];
      texture_cache_->WriteTextureSRV(srv, view_cpu_handle);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
    }
    texture_bindings_written_pixel_ = true;
    current_texture_bindings_hash_pixel_ = texture_bindings_hash_pixel;
    current_graphics_root_up_to_date_ &=
        ~(1u << current_graphics_root_extras_.textures_pixel);
  }
  if (write_samplers_vertex) {
    assert_true(current_graphics_root_extras_.samplers_vertex !=
                RootExtraParameterIndices::kUnavailable);
    gpu_handle_samplers_vertex_ = sampler_gpu_handle;
    for (uint32_t i = 0; i < sampler_count_vertex; ++i) {
      texture_cache_->WriteSampler(
          texture_cache_->GetSamplerParameters(samplers_vertex[i]),
          sampler_cpu_handle);
      sampler_cpu_handle.ptr += descriptor_size_sampler;
      sampler_gpu_handle.ptr += descriptor_size_sampler;
    }
    samplers_written_vertex_ = true;
    current_samplers_hash_vertex_ = samplers_hash_vertex;
    current_graphics_root_up_to_date_ &=
        ~(1u << current_graphics_root_extras_.samplers_vertex);
  }
  if (write_samplers_pixel) {
    assert_true(current_graphics_root_extras_.samplers_pixel !=
                RootExtraParameterIndices::kUnavailable);
    gpu_handle_samplers_pixel_ = sampler_gpu_handle;
    for (uint32_t i = 0; i < sampler_count_pixel; ++i) {
      texture_cache_->WriteSampler(
          texture_cache_->GetSamplerParameters(samplers_pixel[i]),
          sampler_cpu_handle);
      sampler_cpu_handle.ptr += descriptor_size_sampler;
      sampler_gpu_handle.ptr += descriptor_size_sampler;
    }
    samplers_written_pixel_ = true;
    current_samplers_hash_pixel_ = samplers_hash_pixel;
    current_graphics_root_up_to_date_ &=
        ~(1u << current_graphics_root_extras_.samplers_pixel);
  }

  // Wrote new descriptors on the current page.
  draw_view_full_update_ = view_full_update_index;
  draw_sampler_full_update_ = sampler_full_update_index;

  // Update the root parameters.
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_FetchConstants))) {
    deferred_command_list_->D3DSetGraphicsRootDescriptorTable(
        kRootParameter_FetchConstants, gpu_handle_fetch_constants_);
    current_graphics_root_up_to_date_ |= 1u << kRootParameter_FetchConstants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_FloatConstantsVertex))) {
    deferred_command_list_->D3DSetGraphicsRootDescriptorTable(
        kRootParameter_FloatConstantsVertex,
        gpu_handle_float_constants_vertex_);
    current_graphics_root_up_to_date_ |= 1u
                                         << kRootParameter_FloatConstantsVertex;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_FloatConstantsPixel))) {
    deferred_command_list_->D3DSetGraphicsRootDescriptorTable(
        kRootParameter_FloatConstantsPixel, gpu_handle_float_constants_pixel_);
    current_graphics_root_up_to_date_ |= 1u
                                         << kRootParameter_FloatConstantsPixel;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_SystemConstants))) {
    deferred_command_list_->D3DSetGraphicsRootDescriptorTable(
        kRootParameter_SystemConstants, gpu_handle_system_constants_);
    current_graphics_root_up_to_date_ |= 1u << kRootParameter_SystemConstants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_BoolLoopConstants))) {
    deferred_command_list_->D3DSetGraphicsRootDescriptorTable(
        kRootParameter_BoolLoopConstants, gpu_handle_bool_loop_constants_);
    current_graphics_root_up_to_date_ |= 1u << kRootParameter_BoolLoopConstants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << kRootParameter_SharedMemoryAndEDRAM))) {
    deferred_command_list_->D3DSetGraphicsRootDescriptorTable(
        kRootParameter_SharedMemoryAndEDRAM,
        gpu_handle_shared_memory_and_edram_);
    current_graphics_root_up_to_date_ |= 1u
                                         << kRootParameter_SharedMemoryAndEDRAM;
  }
  uint32_t extra_index;
  extra_index = current_graphics_root_extras_.textures_pixel;
  if (extra_index != RootExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    deferred_command_list_->D3DSetGraphicsRootDescriptorTable(
        extra_index, gpu_handle_textures_pixel_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }
  extra_index = current_graphics_root_extras_.samplers_pixel;
  if (extra_index != RootExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    deferred_command_list_->D3DSetGraphicsRootDescriptorTable(
        extra_index, gpu_handle_samplers_pixel_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }
  extra_index = current_graphics_root_extras_.textures_vertex;
  if (extra_index != RootExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    deferred_command_list_->D3DSetGraphicsRootDescriptorTable(
        extra_index, gpu_handle_textures_vertex_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }
  extra_index = current_graphics_root_extras_.samplers_vertex;
  if (extra_index != RootExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    deferred_command_list_->D3DSetGraphicsRootDescriptorTable(
        extra_index, gpu_handle_samplers_vertex_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }

  return true;
}

uint32_t D3D12CommandProcessor::GetSupportedMemExportFormatSize(
    ColorFormat format) {
  switch (format) {
    case ColorFormat::k_8_8_8_8:
    case ColorFormat::k_2_10_10_10:
    // TODO(Triang3l): Investigate how k_8_8_8_8_A works - not supported in the
    // texture cache currently.
    // case ColorFormat::k_8_8_8_8_A:
    case ColorFormat::k_10_11_11:
    case ColorFormat::k_11_11_10:
    case ColorFormat::k_16_16:
    case ColorFormat::k_16_16_FLOAT:
    case ColorFormat::k_32_FLOAT:
    case ColorFormat::k_8_8_8_8_AS_16_16_16_16:
    case ColorFormat::k_2_10_10_10_AS_16_16_16_16:
    case ColorFormat::k_10_11_11_AS_16_16_16_16:
    case ColorFormat::k_11_11_10_AS_16_16_16_16:
      return 1;
    case ColorFormat::k_16_16_16_16:
    case ColorFormat::k_16_16_16_16_FLOAT:
    case ColorFormat::k_32_32_FLOAT:
      return 2;
    case ColorFormat::k_32_32_32_32_FLOAT:
      return 4;
    default:
      break;
  }
  return 0;
}

ID3D12Resource* D3D12CommandProcessor::RequestReadbackBuffer(uint32_t size) {
  if (size == 0) {
    return nullptr;
  }
  size = xe::align(size, kReadbackBufferSizeIncrement);
  if (size > readback_buffer_size_) {
    auto context = GetD3D12Context();
    auto device = context->GetD3D12Provider()->GetDevice();
    D3D12_RESOURCE_DESC buffer_desc;
    ui::d3d12::util::FillBufferResourceDesc(buffer_desc, size,
                                            D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* buffer;
    if (FAILED(device->CreateCommittedResource(
            &ui::d3d12::util::kHeapPropertiesReadback, D3D12_HEAP_FLAG_NONE,
            &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&buffer)))) {
      XELOGE("Failed to create a %u MB readback buffer", size >> 20);
      return nullptr;
    }
    if (readback_buffer_ != nullptr) {
      readback_buffer_->Release();
    }
    readback_buffer_ = buffer;
  }
  return readback_buffer_;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

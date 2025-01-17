/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>
#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#include "xenia/gpu/d3d12/d3d12_shader.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/packet_disassembler.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/d3d12/d3d12_presenter.h"
#include "xenia/ui/d3d12/d3d12_util.h"

DEFINE_bool(d3d12_bindless, true,
            "Use bindless resources where available - may improve performance, "
            "but may make debugging more complicated.",
            "D3D12");

DEFINE_bool(d3d12_submit_on_primary_buffer_end, true,
            "Submit the command list when a PM4 primary buffer ends if it's "
            "possible to submit immediately to try to reduce frame latency.",
            "D3D12");

DECLARE_bool(clear_memory_page_state);

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/apply_gamma_pwl_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/apply_gamma_pwl_fxaa_luma_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/apply_gamma_table_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/apply_gamma_table_fxaa_luma_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/fxaa_cs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/fxaa_extreme_cs.h"
}  // namespace shaders

D3D12CommandProcessor::D3D12CommandProcessor(
    D3D12GraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state),
      deferred_command_list_(*this) {}
D3D12CommandProcessor::~D3D12CommandProcessor() = default;

void D3D12CommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();
  cache_clear_requested_ = true;
}

void D3D12CommandProcessor::InitializeShaderStorage(
    const std::filesystem::path& cache_root, uint32_t title_id, bool blocking) {
  CommandProcessor::InitializeShaderStorage(cache_root, title_id, blocking);
  pipeline_cache_->InitializeShaderStorage(cache_root, title_id, blocking);
}

void D3D12CommandProcessor::RequestFrameTrace(
    const std::filesystem::path& root_path) {
  // Capture with PIX if attached.
  if (GetD3D12Provider().GetGraphicsAnalysis() != nullptr) {
    pix_capture_requested_.store(true, std::memory_order_relaxed);
    return;
  }
  CommandProcessor::RequestFrameTrace(root_path);
}

void D3D12CommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                     uint32_t length) {
  shared_memory_->MemoryInvalidationCallback(base_ptr, length, true);
  primitive_processor_->MemoryInvalidationCallback(base_ptr, length, true);
}

void D3D12CommandProcessor::RestoreEdramSnapshot(const void* snapshot) {
  // Starting a new frame because descriptors may be needed.
  if (!BeginSubmission(true)) {
    return;
  }
  render_target_cache_->RestoreEdramSnapshot(snapshot);
}

bool D3D12CommandProcessor::PushTransitionBarrier(
    ID3D12Resource* resource, D3D12_RESOURCE_STATES old_state,
    D3D12_RESOURCE_STATES new_state, UINT subresource) {
  if (old_state == new_state) {
    return false;
  }
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = subresource;
  barrier.Transition.StateBefore = old_state;
  barrier.Transition.StateAfter = new_state;
  barriers_.push_back(barrier);
  return true;
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
    deferred_command_list_.D3DResourceBarrier(barrier_count, barriers_.data());
    barriers_.clear();
  }
}

ID3D12RootSignature* D3D12CommandProcessor::GetRootSignature(
    const DxbcShader* vertex_shader, const DxbcShader* pixel_shader,
    bool tessellated) {
  if (bindless_resources_used_) {
    return tessellated ? root_signature_bindless_ds_
                       : root_signature_bindless_vs_;
  }

  D3D12_SHADER_VISIBILITY vertex_visibility =
      tessellated ? D3D12_SHADER_VISIBILITY_DOMAIN
                  : D3D12_SHADER_VISIBILITY_VERTEX;

  uint32_t texture_count_vertex =
      uint32_t(vertex_shader->GetTextureBindingsAfterTranslation().size());
  uint32_t sampler_count_vertex =
      uint32_t(vertex_shader->GetSamplerBindingsAfterTranslation().size());
  uint32_t texture_count_pixel =
      pixel_shader
          ? uint32_t(pixel_shader->GetTextureBindingsAfterTranslation().size())
          : 0;
  uint32_t sampler_count_pixel =
      pixel_shader
          ? uint32_t(pixel_shader->GetSamplerBindingsAfterTranslation().size())
          : 0;

  // Better put the pixel texture/sampler in the lower bits probably because it
  // changes often.
  uint32_t index = 0;
  uint32_t index_offset = 0;
  index |= texture_count_pixel << index_offset;
  index_offset += D3D12Shader::kMaxTextureBindingIndexBits;
  index |= sampler_count_pixel << index_offset;
  index_offset += D3D12Shader::kMaxSamplerBindingIndexBits;
  index |= texture_count_vertex << index_offset;
  index_offset += D3D12Shader::kMaxTextureBindingIndexBits;
  index |= sampler_count_vertex << index_offset;
  index_offset += D3D12Shader::kMaxSamplerBindingIndexBits;
  index |= uint32_t(vertex_visibility == D3D12_SHADER_VISIBILITY_DOMAIN)
           << index_offset;
  ++index_offset;
  assert_true(index_offset <= 32);

  // Try an existing root signature.
  auto it = root_signatures_bindful_.find(index);
  if (it != root_signatures_bindful_.end()) {
    return it->second;
  }

  // Create a new one.
  D3D12_ROOT_SIGNATURE_DESC desc;
  D3D12_ROOT_PARAMETER parameters[kRootParameter_Bindful_Count_Max];
  desc.NumParameters = kRootParameter_Bindful_Count_Base;
  desc.pParameters = parameters;
  desc.NumStaticSamplers = 0;
  desc.pStaticSamplers = nullptr;
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  // Base parameters.

  // Fetch constants.
  {
    auto& parameter = parameters[kRootParameter_Bindful_FetchConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kFetchConstants);
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  // Vertex float constants.
  {
    auto& parameter = parameters[kRootParameter_Bindful_FloatConstantsVertex];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kFloatConstants);
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = vertex_visibility;
  }

  // Pixel float constants.
  {
    auto& parameter = parameters[kRootParameter_Bindful_FloatConstantsPixel];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kFloatConstants);
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }

  // System constants.
  {
    auto& parameter = parameters[kRootParameter_Bindful_SystemConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kSystemConstants);
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  // Bool and loop constants.
  {
    auto& parameter = parameters[kRootParameter_Bindful_BoolLoopConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kBoolLoopConstants);
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  // Shared memory and, if ROVs are used, EDRAM.
  D3D12_DESCRIPTOR_RANGE shared_memory_and_edram_ranges[3];
  {
    auto& parameter = parameters[kRootParameter_Bindful_SharedMemoryAndEdram];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges =
        shared_memory_and_edram_ranges;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    shared_memory_and_edram_ranges[0].RangeType =
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shared_memory_and_edram_ranges[0].NumDescriptors = 1;
    shared_memory_and_edram_ranges[0].BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::SRVMainRegister::kSharedMemory);
    shared_memory_and_edram_ranges[0].RegisterSpace =
        uint32_t(DxbcShaderTranslator::SRVSpace::kMain);
    shared_memory_and_edram_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    shared_memory_and_edram_ranges[1].RangeType =
        D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    shared_memory_and_edram_ranges[1].NumDescriptors = 1;
    shared_memory_and_edram_ranges[1].BaseShaderRegister =
        UINT(DxbcShaderTranslator::UAVRegister::kSharedMemory);
    shared_memory_and_edram_ranges[1].RegisterSpace = 0;
    shared_memory_and_edram_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    if (render_target_cache_->GetPath() ==
        RenderTargetCache::Path::kPixelShaderInterlock) {
      ++parameter.DescriptorTable.NumDescriptorRanges;
      shared_memory_and_edram_ranges[2].RangeType =
          D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
      shared_memory_and_edram_ranges[2].NumDescriptors = 1;
      shared_memory_and_edram_ranges[2].BaseShaderRegister =
          UINT(DxbcShaderTranslator::UAVRegister::kEdram);
      shared_memory_and_edram_ranges[2].RegisterSpace = 0;
      shared_memory_and_edram_ranges[2].OffsetInDescriptorsFromTableStart = 2;
    }
  }

  // Extra parameters.

  // Pixel textures.
  D3D12_DESCRIPTOR_RANGE range_textures_pixel;
  if (texture_count_pixel > 0) {
    auto& parameter = parameters[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range_textures_pixel;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range_textures_pixel.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range_textures_pixel.NumDescriptors = texture_count_pixel;
    range_textures_pixel.BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::SRVMainRegister::kBindfulTexturesStart);
    range_textures_pixel.RegisterSpace =
        uint32_t(DxbcShaderTranslator::SRVSpace::kMain);
    range_textures_pixel.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Pixel samplers.
  D3D12_DESCRIPTOR_RANGE range_samplers_pixel;
  if (sampler_count_pixel > 0) {
    auto& parameter = parameters[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range_samplers_pixel;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range_samplers_pixel.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    range_samplers_pixel.NumDescriptors = sampler_count_pixel;
    range_samplers_pixel.BaseShaderRegister = 0;
    range_samplers_pixel.RegisterSpace = 0;
    range_samplers_pixel.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Vertex textures.
  D3D12_DESCRIPTOR_RANGE range_textures_vertex;
  if (texture_count_vertex > 0) {
    auto& parameter = parameters[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range_textures_vertex;
    parameter.ShaderVisibility = vertex_visibility;
    range_textures_vertex.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range_textures_vertex.NumDescriptors = texture_count_vertex;
    range_textures_vertex.BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::SRVMainRegister::kBindfulTexturesStart);
    range_textures_vertex.RegisterSpace =
        uint32_t(DxbcShaderTranslator::SRVSpace::kMain);
    range_textures_vertex.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Vertex samplers.
  D3D12_DESCRIPTOR_RANGE range_samplers_vertex;
  if (sampler_count_vertex > 0) {
    auto& parameter = parameters[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range_samplers_vertex;
    parameter.ShaderVisibility = vertex_visibility;
    range_samplers_vertex.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    range_samplers_vertex.NumDescriptors = sampler_count_vertex;
    range_samplers_vertex.BaseShaderRegister = 0;
    range_samplers_vertex.RegisterSpace = 0;
    range_samplers_vertex.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  ID3D12RootSignature* root_signature =
      ui::d3d12::util::CreateRootSignature(GetD3D12Provider(), desc);
  if (root_signature == nullptr) {
    XELOGE(
        "Failed to create a root signature with {} pixel textures, {} pixel "
        "samplers, {} vertex textures and {} vertex samplers",
        texture_count_pixel, sampler_count_pixel, texture_count_vertex,
        sampler_count_vertex);
    return nullptr;
  }
  root_signatures_bindful_.emplace(index, root_signature);
  return root_signature;
}
XE_NOINLINE
XE_COLD
uint32_t D3D12CommandProcessor::GetRootBindfulExtraParameterIndices(
    const DxbcShader* vertex_shader, const DxbcShader* pixel_shader,
    RootBindfulExtraParameterIndices& indices_out) {
  uint32_t index = kRootParameter_Bindful_Count_Base;
  if (pixel_shader &&
      !pixel_shader->GetTextureBindingsAfterTranslation().empty()) {
    indices_out.textures_pixel = index++;
  } else {
    indices_out.textures_pixel = RootBindfulExtraParameterIndices::kUnavailable;
  }
  if (pixel_shader &&
      !pixel_shader->GetSamplerBindingsAfterTranslation().empty()) {
    indices_out.samplers_pixel = index++;
  } else {
    indices_out.samplers_pixel = RootBindfulExtraParameterIndices::kUnavailable;
  }
  if (!vertex_shader->GetTextureBindingsAfterTranslation().empty()) {
    indices_out.textures_vertex = index++;
  } else {
    indices_out.textures_vertex =
        RootBindfulExtraParameterIndices::kUnavailable;
  }
  if (!vertex_shader->GetSamplerBindingsAfterTranslation().empty()) {
    indices_out.samplers_vertex = index++;
  } else {
    indices_out.samplers_vertex =
        RootBindfulExtraParameterIndices::kUnavailable;
  }
  return index;
}

uint64_t D3D12CommandProcessor::RequestViewBindfulDescriptors(
    uint64_t previous_heap_index, uint32_t count_for_partial_update,
    uint32_t count_for_full_update, D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle_out,
    D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle_out) {
  assert_false(bindless_resources_used_);
  assert_true(submission_open_);
  uint32_t descriptor_index;
  uint64_t current_heap_index = view_bindful_heap_pool_->Request(
      frame_current_, previous_heap_index, count_for_partial_update,
      count_for_full_update, descriptor_index);
  if (current_heap_index ==
      ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
    // There was an error.
    return ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid;
  }
  ID3D12DescriptorHeap* heap = view_bindful_heap_pool_->GetLastRequestHeap();
  if (view_bindful_heap_current_ != heap) {
    view_bindful_heap_current_ = heap;
    deferred_command_list_.SetDescriptorHeaps(view_bindful_heap_current_,
                                              sampler_bindful_heap_current_);
  }
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  cpu_handle_out = provider.OffsetViewDescriptor(
      view_bindful_heap_pool_->GetLastRequestHeapCPUStart(), descriptor_index);
  gpu_handle_out = provider.OffsetViewDescriptor(
      view_bindful_heap_pool_->GetLastRequestHeapGPUStart(), descriptor_index);
  return current_heap_index;
}

uint32_t D3D12CommandProcessor::RequestPersistentViewBindlessDescriptor() {
  assert_true(bindless_resources_used_);
  if (!view_bindless_heap_free_.empty()) {
    uint32_t descriptor_index = view_bindless_heap_free_.back();
    view_bindless_heap_free_.pop_back();
    return descriptor_index;
  }
  if (view_bindless_heap_allocated_ >= kViewBindlessHeapSize) {
    return UINT32_MAX;
  }
  return view_bindless_heap_allocated_++;
}

void D3D12CommandProcessor::ReleaseViewBindlessDescriptorImmediately(
    uint32_t descriptor_index) {
  assert_true(bindless_resources_used_);
  view_bindless_heap_free_.push_back(descriptor_index);
}

bool D3D12CommandProcessor::RequestOneUseSingleViewDescriptors(
    uint32_t count, ui::d3d12::util::DescriptorCpuGpuHandlePair* handles_out) {
  assert_true(submission_open_);
  if (!count) {
    return true;
  }
  assert_not_null(handles_out);
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  if (bindless_resources_used_) {
    // Request separate bindless descriptors that will be freed when this
    // submission is completed by the GPU.
    if (count > kViewBindlessHeapSize - view_bindless_heap_allocated_ +
                    view_bindless_heap_free_.size()) {
      return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t descriptor_index;
      if (!view_bindless_heap_free_.empty()) {
        descriptor_index = view_bindless_heap_free_.back();
        view_bindless_heap_free_.pop_back();
      } else {
        descriptor_index = view_bindless_heap_allocated_++;
      }
      view_bindless_one_use_descriptors_.push_back(
          std::make_pair(descriptor_index, submission_current_));
      handles_out[i] =
          std::make_pair(provider.OffsetViewDescriptor(
                             view_bindless_heap_cpu_start_, descriptor_index),
                         provider.OffsetViewDescriptor(
                             view_bindless_heap_gpu_start_, descriptor_index));
    }
  } else {
    // Request a range within the current heap for bindful resources path.
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle_start;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_start;
    if (RequestViewBindfulDescriptors(
            ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid, count, count,
            cpu_handle_start, gpu_handle_start) ==
        ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
      return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
      handles_out[i] =
          std::make_pair(provider.OffsetViewDescriptor(cpu_handle_start, i),
                         provider.OffsetViewDescriptor(gpu_handle_start, i));
    }
  }
  return true;
}

ui::d3d12::util::DescriptorCpuGpuHandlePair
D3D12CommandProcessor::GetSystemBindlessViewHandlePair(
    SystemBindlessView view) const {
  assert_true(bindless_resources_used_);
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  return std::make_pair(provider.OffsetViewDescriptor(
                            view_bindless_heap_cpu_start_, uint32_t(view)),
                        provider.OffsetViewDescriptor(
                            view_bindless_heap_gpu_start_, uint32_t(view)));
}

ui::d3d12::util::DescriptorCpuGpuHandlePair
D3D12CommandProcessor::GetSharedMemoryUintPow2BindlessSRVHandlePair(
    uint32_t element_size_bytes_pow2) const {
  SystemBindlessView view;
  switch (element_size_bytes_pow2) {
    case 2:
      view = SystemBindlessView::kSharedMemoryR32UintSRV;
      break;
    case 3:
      view = SystemBindlessView::kSharedMemoryR32G32UintSRV;
      break;
    case 4:
      view = SystemBindlessView::kSharedMemoryR32G32B32A32UintSRV;
      break;
    default:
      assert_unhandled_case(element_size_bytes_pow2);
      view = SystemBindlessView::kSharedMemoryR32UintSRV;
  }
  return GetSystemBindlessViewHandlePair(view);
}

ui::d3d12::util::DescriptorCpuGpuHandlePair
D3D12CommandProcessor::GetSharedMemoryUintPow2BindlessUAVHandlePair(
    uint32_t element_size_bytes_pow2) const {
  SystemBindlessView view;
  switch (element_size_bytes_pow2) {
    case 2:
      view = SystemBindlessView::kSharedMemoryR32UintUAV;
      break;
    case 3:
      view = SystemBindlessView::kSharedMemoryR32G32UintUAV;
      break;
    case 4:
      view = SystemBindlessView::kSharedMemoryR32G32B32A32UintUAV;
      break;
    default:
      assert_unhandled_case(element_size_bytes_pow2);
      view = SystemBindlessView::kSharedMemoryR32UintUAV;
  }
  return GetSystemBindlessViewHandlePair(view);
}

ui::d3d12::util::DescriptorCpuGpuHandlePair
D3D12CommandProcessor::GetEdramUintPow2BindlessSRVHandlePair(
    uint32_t element_size_bytes_pow2) const {
  SystemBindlessView view;
  switch (element_size_bytes_pow2) {
    case 2:
      view = SystemBindlessView::kEdramR32UintSRV;
      break;
    case 3:
      view = SystemBindlessView::kEdramR32G32UintSRV;
      break;
    case 4:
      view = SystemBindlessView::kEdramR32G32B32A32UintSRV;
      break;
    default:
      assert_unhandled_case(element_size_bytes_pow2);
      view = SystemBindlessView::kEdramR32UintSRV;
  }
  return GetSystemBindlessViewHandlePair(view);
}

ui::d3d12::util::DescriptorCpuGpuHandlePair
D3D12CommandProcessor::GetEdramUintPow2BindlessUAVHandlePair(
    uint32_t element_size_bytes_pow2) const {
  SystemBindlessView view;
  switch (element_size_bytes_pow2) {
    case 2:
      view = SystemBindlessView::kEdramR32UintUAV;
      break;
    case 3:
      view = SystemBindlessView::kEdramR32G32UintUAV;
      break;
    case 4:
      view = SystemBindlessView::kEdramR32G32B32A32UintUAV;
      break;
    default:
      assert_unhandled_case(element_size_bytes_pow2);
      view = SystemBindlessView::kEdramR32UintUAV;
  }
  return GetSystemBindlessViewHandlePair(view);
}

uint64_t D3D12CommandProcessor::RequestSamplerBindfulDescriptors(
    uint64_t previous_heap_index, uint32_t count_for_partial_update,
    uint32_t count_for_full_update, D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle_out,
    D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle_out) {
  assert_false(bindless_resources_used_);
  assert_true(submission_open_);
  uint32_t descriptor_index;
  uint64_t current_heap_index = sampler_bindful_heap_pool_->Request(
      frame_current_, previous_heap_index, count_for_partial_update,
      count_for_full_update, descriptor_index);
  if (current_heap_index ==
      ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
    // There was an error.
    return ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid;
  }
  ID3D12DescriptorHeap* heap = sampler_bindful_heap_pool_->GetLastRequestHeap();
  if (sampler_bindful_heap_current_ != heap) {
    sampler_bindful_heap_current_ = heap;
    deferred_command_list_.SetDescriptorHeaps(view_bindful_heap_current_,
                                              sampler_bindful_heap_current_);
  }
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  cpu_handle_out = provider.OffsetSamplerDescriptor(
      sampler_bindful_heap_pool_->GetLastRequestHeapCPUStart(),
      descriptor_index);
  gpu_handle_out = provider.OffsetSamplerDescriptor(
      sampler_bindful_heap_pool_->GetLastRequestHeapGPUStart(),
      descriptor_index);
  return current_heap_index;
}

ID3D12Resource* D3D12CommandProcessor::RequestScratchGPUBuffer(
    uint32_t size, D3D12_RESOURCE_STATES state) {
  assert_true(submission_open_);
  assert_false(scratch_buffer_used_);
  if (!submission_open_ || scratch_buffer_used_ || size == 0) {
    return nullptr;
  }

  if (size <= scratch_buffer_size_) {
    PushTransitionBarrier(scratch_buffer_, scratch_buffer_state_, state);
    scratch_buffer_state_ = state;
    scratch_buffer_used_ = true;
    return scratch_buffer_;
  }

  size = xe::align(size, kScratchBufferSizeIncrement);

  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  D3D12_RESOURCE_DESC buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(
      buffer_desc, size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ID3D12Resource* buffer;
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault,
          provider.GetHeapFlagCreateNotZeroed(), &buffer_desc, state, nullptr,
          IID_PPV_ARGS(&buffer)))) {
    XELOGE("Failed to create a {} MB scratch GPU buffer", size >> 20);
    return nullptr;
  }
  if (scratch_buffer_ != nullptr) {
    resources_for_deletion_.emplace_back(submission_current_, scratch_buffer_);
  }
  scratch_buffer_ = buffer;
  scratch_buffer_size_ = size;
  scratch_buffer_state_ = state;
  scratch_buffer_used_ = true;
  return scratch_buffer_;
}

void D3D12CommandProcessor::ReleaseScratchGPUBuffer(
    ID3D12Resource* buffer, D3D12_RESOURCE_STATES new_state) {
  assert_true(submission_open_);
  assert_true(scratch_buffer_used_);
  scratch_buffer_used_ = false;
  if (buffer == scratch_buffer_) {
    scratch_buffer_state_ = new_state;
  }
}

void D3D12CommandProcessor::SetExternalPipeline(ID3D12PipelineState* pipeline) {
  if (current_external_pipeline_ != pipeline) {
    current_external_pipeline_ = pipeline;
    current_guest_pipeline_ = nullptr;
    deferred_command_list_.D3DSetPipelineState(pipeline);
  }
}

void D3D12CommandProcessor::SetExternalGraphicsRootSignature(
    ID3D12RootSignature* root_signature) {
  if (current_graphics_root_signature_ != root_signature) {
    current_graphics_root_signature_ = root_signature;
    deferred_command_list_.D3DSetGraphicsRootSignature(root_signature);
  }
  // Force-invalidate because setting a non-guest root signature.
  current_graphics_root_up_to_date_ = 0;
}

void D3D12CommandProcessor::SetViewport(const D3D12_VIEWPORT& viewport) {
#if XE_ARCH_AMD64 == 1
  __m128 zero_register = _mm_setzero_ps();
  __m128 ff_viewport_low4 = _mm_loadu_ps(&ff_viewport_.TopLeftX);
  __m128 ff_viewport_high2 =
      _mm_loadl_pi(zero_register, (const __m64*)&ff_viewport_.MinDepth);

  __m128 viewport_low4 = _mm_loadu_ps(&viewport.TopLeftX);
  __m128 viewport_high2 =
      _mm_loadl_pi(zero_register, (const __m64*)&viewport.MinDepth);

  __m128 first_four_cmp = _mm_cmpeq_ps(ff_viewport_low4, viewport_low4);
  __m128 last_two_cmp = _mm_cmpeq_ps(ff_viewport_high2, viewport_high2);

  __m128 combined_condition = _mm_and_ps(first_four_cmp, last_two_cmp);

  int movmask = _mm_movemask_ps(combined_condition);

  XE_UNLIKELY_IF(ff_viewport_update_needed_ || movmask != 0b1111)
#else
  ff_viewport_update_needed_ |= ff_viewport_.TopLeftX != viewport.TopLeftX;
  ff_viewport_update_needed_ |= ff_viewport_.TopLeftY != viewport.TopLeftY;
  ff_viewport_update_needed_ |= ff_viewport_.Width != viewport.Width;
  ff_viewport_update_needed_ |= ff_viewport_.Height != viewport.Height;
  ff_viewport_update_needed_ |= ff_viewport_.MinDepth != viewport.MinDepth;
  ff_viewport_update_needed_ |= ff_viewport_.MaxDepth != viewport.MaxDepth;
  if (XE_UNLIKELY(ff_viewport_update_needed_))
#endif
  {
    ff_viewport_ = viewport;
    deferred_command_list_.RSSetViewport(ff_viewport_);
    ff_viewport_update_needed_ = false;
  }
}

void D3D12CommandProcessor::SetScissorRect(const D3D12_RECT& scissor_rect) {
#if XE_ARCH_AMD64 == 1
  // vtune suggested that this and SetViewport be vectorized, high retiring
  // figure
  __m128i scissor_m128 = _mm_loadu_si128((const __m128i*)&scissor_rect);
  __m128i ff_scissor_m128 = _mm_loadu_si128((const __m128i*)&ff_scissor_);
  __m128i comparison_result = _mm_cmpeq_epi32(scissor_m128, ff_scissor_m128);
  if (ff_scissor_update_needed_ ||
      _mm_movemask_epi8(comparison_result) != 0xFFFF)
#else
  ff_scissor_update_needed_ |= ff_scissor_.left != scissor_rect.left;
  ff_scissor_update_needed_ |= ff_scissor_.top != scissor_rect.top;
  ff_scissor_update_needed_ |= ff_scissor_.right != scissor_rect.right;
  ff_scissor_update_needed_ |= ff_scissor_.bottom != scissor_rect.bottom;

  if (ff_scissor_update_needed_)
#endif
  {
    ff_scissor_ = scissor_rect;
    deferred_command_list_.RSSetScissorRect(ff_scissor_);
    ff_scissor_update_needed_ = false;
  }
}

void D3D12CommandProcessor::SetStencilReference(uint32_t stencil_ref) {
  ff_stencil_ref_update_needed_ |= ff_stencil_ref_ != stencil_ref;
  if (ff_stencil_ref_update_needed_) {
    ff_stencil_ref_ = stencil_ref;
    deferred_command_list_.D3DOMSetStencilRef(stencil_ref);
    ff_stencil_ref_update_needed_ = false;
  }
}

void D3D12CommandProcessor::SetPrimitiveTopology(
    D3D12_PRIMITIVE_TOPOLOGY primitive_topology) {
  if (primitive_topology_ != primitive_topology) {
    primitive_topology_ = primitive_topology;
    deferred_command_list_.D3DIASetPrimitiveTopology(primitive_topology);
  }
}

std::string D3D12CommandProcessor::GetWindowTitleText() const {
  std::ostringstream title;
  title << "Direct3D 12";
  if (render_target_cache_) {
    // Rasterizer-ordered views are a feature very rarely used as of 2020 and
    // that faces adoption complications (outside of Direct3D - on Vulkan - at
    // least), but crucial to Xenia - raise awareness of its usage.
    // https://github.com/KhronosGroup/Vulkan-Ecosystem/issues/27#issuecomment-455712319
    // "In Xenia's title bar "D3D12 ROV" can be seen, which was a surprise, as I
    //  wasn't aware that Xenia D3D12 backend was using Raster Order Views
    //  feature" - oscarbg in that issue.
    switch (render_target_cache_->GetPath()) {
      case RenderTargetCache::Path::kHostRenderTargets:
        title << " - RTV/DSV";
        break;
      case RenderTargetCache::Path::kPixelShaderInterlock:
        title << " - ROV";
        break;
      default:
        break;
    }
    uint32_t draw_resolution_scale_x =
        texture_cache_ ? texture_cache_->draw_resolution_scale_x() : 1;
    uint32_t draw_resolution_scale_y =
        texture_cache_ ? texture_cache_->draw_resolution_scale_y() : 1;
    if (draw_resolution_scale_x > 1 || draw_resolution_scale_y > 1) {
      title << ' ' << draw_resolution_scale_x << 'x' << draw_resolution_scale_y;
    }
  }
  return title.str();
}

bool D3D12CommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Failed to initialize base command processor context");
    return false;
  }

  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  ID3D12CommandQueue* direct_queue = provider.GetDirectQueue();

  fence_completion_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (fence_completion_event_ == nullptr) {
    XELOGE("Failed to create the fence completion event");
    return false;
  }
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                 IID_PPV_ARGS(&submission_fence_)))) {
    XELOGE("Failed to create the submission fence");
    return false;
  }
  if (FAILED(device->CreateFence(
          0, D3D12_FENCE_FLAG_NONE,
          IID_PPV_ARGS(&queue_operations_since_submission_fence_)))) {
    XELOGE(
        "Failed to create the fence for awaiting queue operations done since "
        "the latest submission");
    return false;
  }

  // Create the command list and one allocator because it's needed for a command
  // list.
  ID3D12CommandAllocator* command_allocator;
  if (FAILED(device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)))) {
    XELOGE("Failed to create a command allocator");
    return false;
  }
  command_allocator_writable_first_ = new CommandAllocator;
  command_allocator_writable_first_->command_allocator = command_allocator;
  command_allocator_writable_first_->last_usage_submission = 0;
  command_allocator_writable_first_->next = nullptr;
  command_allocator_writable_last_ = command_allocator_writable_first_;
  if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       command_allocator, nullptr,
                                       IID_PPV_ARGS(&command_list_)))) {
    XELOGE("Failed to create the graphics command list");
    return false;
  }
  // Initially in open state, wait until a deferred command list submission.
  command_list_->Close();
  // Optional - added in Creators Update (SDK 10.0.15063.0).
  command_list_->QueryInterface(IID_PPV_ARGS(&command_list_1_));

  bindless_resources_used_ =
      cvars::d3d12_bindless &&
      provider.GetResourceBindingTier() >= D3D12_RESOURCE_BINDING_TIER_2;

  // Get the draw resolution scale for the render target cache and the texture
  // cache.
  uint32_t draw_resolution_scale_x, draw_resolution_scale_y;
  bool draw_resolution_scale_not_clamped =
      TextureCache::GetConfigDrawResolutionScale(draw_resolution_scale_x,
                                                 draw_resolution_scale_y);

  if (!D3D12TextureCache::ClampDrawResolutionScaleToMaxSupported(
          draw_resolution_scale_x, draw_resolution_scale_y, provider)) {
    draw_resolution_scale_not_clamped = false;
  }
  if (!draw_resolution_scale_not_clamped) {
    XELOGW(
        "The requested draw resolution scale is not supported by the device or "
        "the emulator, reducing to {}x{}",
        draw_resolution_scale_x, draw_resolution_scale_y);
  }

  shared_memory_ =
      std::make_unique<D3D12SharedMemory>(*this, *memory_, trace_writer_);
  if (!shared_memory_->Initialize()) {
    XELOGE("Failed to initialize shared memory");
    return false;
  }

  // Initialize the render target cache before configuring binding - need to
  // know if using rasterizer-ordered views for the bindless root signature.
  render_target_cache_ = std::make_unique<D3D12RenderTargetCache>(
      *register_file_, *memory_, trace_writer_, draw_resolution_scale_x,
      draw_resolution_scale_y, *this, bindless_resources_used_);
  if (!render_target_cache_->Initialize()) {
    XELOGE("Failed to initialize the render target cache");
    return false;
  }

  // Initialize resource binding.
  constant_buffer_pool_ = std::make_unique<ui::d3d12::D3D12UploadBufferPool>(
      provider,
      std::max(ui::d3d12::D3D12UploadBufferPool::kDefaultPageSize,
               sizeof(float) * 4 * D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT));
  if (bindless_resources_used_) {
    D3D12_DESCRIPTOR_HEAP_DESC view_bindless_heap_desc;
    view_bindless_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    view_bindless_heap_desc.NumDescriptors = kViewBindlessHeapSize;
    view_bindless_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    view_bindless_heap_desc.NodeMask = 0;
    if (FAILED(device->CreateDescriptorHeap(
            &view_bindless_heap_desc, IID_PPV_ARGS(&view_bindless_heap_)))) {
      XELOGE("Failed to create the bindless CBV/SRV/UAV descriptor heap");
      return false;
    }
    view_bindless_heap_cpu_start_ =
        view_bindless_heap_->GetCPUDescriptorHandleForHeapStart();
    view_bindless_heap_gpu_start_ =
        view_bindless_heap_->GetGPUDescriptorHandleForHeapStart();
    view_bindless_heap_allocated_ = uint32_t(SystemBindlessView::kCount);

    D3D12_DESCRIPTOR_HEAP_DESC sampler_bindless_heap_desc;
    sampler_bindless_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_bindless_heap_desc.NumDescriptors = kSamplerHeapSize;
    sampler_bindless_heap_desc.Flags =
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    sampler_bindless_heap_desc.NodeMask = 0;
    if (FAILED(device->CreateDescriptorHeap(
            &sampler_bindless_heap_desc,
            IID_PPV_ARGS(&sampler_bindless_heap_current_)))) {
      XELOGE("Failed to create the bindless sampler descriptor heap");
      return false;
    }
    sampler_bindless_heap_cpu_start_ =
        sampler_bindless_heap_current_->GetCPUDescriptorHandleForHeapStart();
    sampler_bindless_heap_gpu_start_ =
        sampler_bindless_heap_current_->GetGPUDescriptorHandleForHeapStart();
    sampler_bindless_heap_allocated_ = 0;
  } else {
    view_bindful_heap_pool_ =
        std::make_unique<ui::d3d12::D3D12DescriptorHeapPool>(
            device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            kViewBindfulHeapSize);
    sampler_bindful_heap_pool_ =
        std::make_unique<ui::d3d12::D3D12DescriptorHeapPool>(
            device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerHeapSize);
  }

  if (bindless_resources_used_) {
    // Global bindless resource root signatures.
    // No CBV or UAV descriptor ranges with any descriptors to be allocated
    // dynamically (via RequestPersistentViewBindlessDescriptor or
    // RequestOneUseSingleViewDescriptors) should be here, because they would
    // overlap the unbounded SRV range, which is not allowed on Nvidia Fermi!
    D3D12_ROOT_SIGNATURE_DESC root_signature_bindless_desc;
    D3D12_ROOT_PARAMETER
    root_parameters_bindless[kRootParameter_Bindless_Count];
    root_signature_bindless_desc.NumParameters = kRootParameter_Bindless_Count;
    root_signature_bindless_desc.pParameters = root_parameters_bindless;
    root_signature_bindless_desc.NumStaticSamplers = 0;
    root_signature_bindless_desc.pStaticSamplers = nullptr;
    root_signature_bindless_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    // Fetch constants.
    {
      auto& parameter =
          root_parameters_bindless[kRootParameter_Bindless_FetchConstants];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kFetchConstants);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    // Vertex float constants.
    {
      auto& parameter = root_parameters_bindless
          [kRootParameter_Bindless_FloatConstantsVertex];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kFloatConstants);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    }
    // Pixel float constants.
    {
      auto& parameter =
          root_parameters_bindless[kRootParameter_Bindless_FloatConstantsPixel];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kFloatConstants);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    // Pixel shader descriptor indices.
    {
      auto& parameter = root_parameters_bindless
          [kRootParameter_Bindless_DescriptorIndicesPixel];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kDescriptorIndices);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    // Vertex shader descriptor indices.
    {
      auto& parameter = root_parameters_bindless
          [kRootParameter_Bindless_DescriptorIndicesVertex];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kDescriptorIndices);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    }
    // System constants.
    {
      auto& parameter =
          root_parameters_bindless[kRootParameter_Bindless_SystemConstants];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kSystemConstants);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    // Bool and loop constants.
    {
      auto& parameter =
          root_parameters_bindless[kRootParameter_Bindless_BoolLoopConstants];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kBoolLoopConstants);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    // Shared memory SRV and UAV.
    D3D12_DESCRIPTOR_RANGE root_shared_memory_view_ranges[2];
    {
      auto& parameter =
          root_parameters_bindless[kRootParameter_Bindless_SharedMemory];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameter.DescriptorTable.NumDescriptorRanges =
          uint32_t(xe::countof(root_shared_memory_view_ranges));
      parameter.DescriptorTable.pDescriptorRanges =
          root_shared_memory_view_ranges;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      {
        auto& range = root_shared_memory_view_ranges[0];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister =
            UINT(DxbcShaderTranslator::SRVMainRegister::kSharedMemory);
        range.RegisterSpace = UINT(DxbcShaderTranslator::SRVSpace::kMain);
        range.OffsetInDescriptorsFromTableStart = 0;
      }
      {
        auto& range = root_shared_memory_view_ranges[1];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister =
            UINT(DxbcShaderTranslator::UAVRegister::kSharedMemory);
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = 1;
      }
    }
    // Sampler heap.
    D3D12_DESCRIPTOR_RANGE root_bindless_sampler_range;
    {
      auto& parameter =
          root_parameters_bindless[kRootParameter_Bindless_SamplerHeap];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      // Will be appending.
      parameter.DescriptorTable.NumDescriptorRanges = 1;
      parameter.DescriptorTable.pDescriptorRanges =
          &root_bindless_sampler_range;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      root_bindless_sampler_range.RangeType =
          D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
      root_bindless_sampler_range.NumDescriptors = UINT_MAX;
      root_bindless_sampler_range.BaseShaderRegister = 0;
      root_bindless_sampler_range.RegisterSpace = 0;
      root_bindless_sampler_range.OffsetInDescriptorsFromTableStart = 0;
    }
    // View heap.
    D3D12_DESCRIPTOR_RANGE root_bindless_view_ranges[4];
    {
      auto& parameter =
          root_parameters_bindless[kRootParameter_Bindless_ViewHeap];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      // Will be appending.
      parameter.DescriptorTable.NumDescriptorRanges = 0;
      parameter.DescriptorTable.pDescriptorRanges = root_bindless_view_ranges;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      // EDRAM.
      if (render_target_cache_->GetPath() ==
          RenderTargetCache::Path::kPixelShaderInterlock) {
        assert_true(parameter.DescriptorTable.NumDescriptorRanges <
                    xe::countof(root_bindless_view_ranges));
        auto& range = root_bindless_view_ranges[parameter.DescriptorTable
                                                    .NumDescriptorRanges++];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister =
            UINT(DxbcShaderTranslator::UAVRegister::kEdram);
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart =
            UINT(SystemBindlessView::kEdramR32UintUAV);
      }
      // Used UAV and SRV ranges must not overlap on Nvidia Fermi, so textures
      // have OffsetInDescriptorsFromTableStart after all static descriptors of
      // other types.
      // 2D array textures.
      {
        assert_true(parameter.DescriptorTable.NumDescriptorRanges <
                    xe::countof(root_bindless_view_ranges));
        auto& range = root_bindless_view_ranges[parameter.DescriptorTable
                                                    .NumDescriptorRanges++];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = UINT_MAX;
        range.BaseShaderRegister = 0;
        range.RegisterSpace =
            UINT(DxbcShaderTranslator::SRVSpace::kBindlessTextures2DArray);
        range.OffsetInDescriptorsFromTableStart =
            UINT(SystemBindlessView::kUnboundedSRVsStart);
      }
      // 3D textures.
      {
        assert_true(parameter.DescriptorTable.NumDescriptorRanges <
                    xe::countof(root_bindless_view_ranges));
        auto& range = root_bindless_view_ranges[parameter.DescriptorTable
                                                    .NumDescriptorRanges++];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = UINT_MAX;
        range.BaseShaderRegister = 0;
        range.RegisterSpace =
            UINT(DxbcShaderTranslator::SRVSpace::kBindlessTextures3D);
        range.OffsetInDescriptorsFromTableStart =
            UINT(SystemBindlessView::kUnboundedSRVsStart);
      }
      // Cube textures.
      {
        assert_true(parameter.DescriptorTable.NumDescriptorRanges <
                    xe::countof(root_bindless_view_ranges));
        auto& range = root_bindless_view_ranges[parameter.DescriptorTable
                                                    .NumDescriptorRanges++];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = UINT_MAX;
        range.BaseShaderRegister = 0;
        range.RegisterSpace =
            UINT(DxbcShaderTranslator::SRVSpace::kBindlessTexturesCube);
        range.OffsetInDescriptorsFromTableStart =
            UINT(SystemBindlessView::kUnboundedSRVsStart);
      }
    }
    root_signature_bindless_vs_ = ui::d3d12::util::CreateRootSignature(
        provider, root_signature_bindless_desc);
    if (!root_signature_bindless_vs_) {
      XELOGE(
          "Failed to create the global root signature for bindless resources, "
          "the version for use without tessellation");
      return false;
    }
    root_parameters_bindless[kRootParameter_Bindless_FloatConstantsVertex]
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_DOMAIN;
    root_parameters_bindless[kRootParameter_Bindless_DescriptorIndicesVertex]
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_DOMAIN;
    root_signature_bindless_ds_ = ui::d3d12::util::CreateRootSignature(
        provider, root_signature_bindless_desc);
    if (!root_signature_bindless_ds_) {
      XELOGE(
          "Failed to create the global root signature for bindless resources, "
          "the version for use with tessellation");
      return false;
    }
  }

  primitive_processor_ = std::make_unique<D3D12PrimitiveProcessor>(
      *register_file_, *memory_, trace_writer_, *shared_memory_, *this);
  if (!primitive_processor_->Initialize()) {
    XELOGE("Failed to initialize the geometric primitive processor");
    return false;
  }

  texture_cache_ = D3D12TextureCache::Create(
      *register_file_, *shared_memory_, draw_resolution_scale_x,
      draw_resolution_scale_y, *this, bindless_resources_used_);
  if (!texture_cache_) {
    XELOGE("Failed to initialize the texture cache");
    return false;
  }

  pipeline_cache_ = std::make_unique<PipelineCache>(*this, *register_file_,
                                                    *render_target_cache_.get(),
                                                    bindless_resources_used_);
  if (!pipeline_cache_->Initialize()) {
    XELOGE("Failed to initialize the graphics pipeline cache");
    return false;
  }

  D3D12_HEAP_FLAGS heap_flag_create_not_zeroed =
      provider.GetHeapFlagCreateNotZeroed();

  // Create gamma ramp resources.
  gamma_ramp_256_entry_table_up_to_date_ = false;
  gamma_ramp_pwl_up_to_date_ = false;
  D3D12_RESOURCE_DESC gamma_ramp_buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(
      gamma_ramp_buffer_desc, (256 + 128 * 3) * 4, D3D12_RESOURCE_FLAG_NONE);
  // The first action will be uploading.
  gamma_ramp_buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault, heap_flag_create_not_zeroed,
          &gamma_ramp_buffer_desc, gamma_ramp_buffer_state_, nullptr,
          IID_PPV_ARGS(&gamma_ramp_buffer_)))) {
    XELOGE("Failed to create the gamma ramp buffer");
    return false;
  }
  // The upload buffer is frame-buffered.
  gamma_ramp_buffer_desc.Width *= kQueueFrames;

  if (!GetD3D12Provider().CreateUploadResource(
          heap_flag_create_not_zeroed, &gamma_ramp_buffer_desc,
          D3D12_RESOURCE_STATE_GENERIC_READ,
          IID_PPV_ARGS(&gamma_ramp_upload_buffer_))) {
    XELOGE("Failed to create the gamma ramp upload buffer");
    return false;
  }

  if (FAILED(gamma_ramp_upload_buffer_->Map(
          0, nullptr,
          reinterpret_cast<void**>(&gamma_ramp_upload_buffer_mapping_)))) {
    XELOGE("Failed to map the gamma ramp upload buffer");
    gamma_ramp_upload_buffer_mapping_ = nullptr;
    return false;
  }

  // Initialize compute pipelines for output with gamma ramp.
  D3D12_ROOT_PARAMETER
  apply_gamma_root_parameters[UINT(ApplyGammaRootParameter::kCount)];
  {
    D3D12_ROOT_PARAMETER& apply_gamma_root_parameter_constants =
        apply_gamma_root_parameters[UINT(ApplyGammaRootParameter::kConstants)];
    apply_gamma_root_parameter_constants.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    apply_gamma_root_parameter_constants.Constants.ShaderRegister = 0;
    apply_gamma_root_parameter_constants.Constants.RegisterSpace = 0;
    apply_gamma_root_parameter_constants.Constants.Num32BitValues =
        sizeof(ApplyGammaConstants) / sizeof(uint32_t);
    apply_gamma_root_parameter_constants.ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE apply_gamma_root_descriptor_range_dest;
  apply_gamma_root_descriptor_range_dest.RangeType =
      D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  apply_gamma_root_descriptor_range_dest.NumDescriptors = 1;
  apply_gamma_root_descriptor_range_dest.BaseShaderRegister = 0;
  apply_gamma_root_descriptor_range_dest.RegisterSpace = 0;
  apply_gamma_root_descriptor_range_dest.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& apply_gamma_root_parameter_dest =
        apply_gamma_root_parameters[UINT(
            ApplyGammaRootParameter::kDestination)];
    apply_gamma_root_parameter_dest.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    apply_gamma_root_parameter_dest.DescriptorTable.NumDescriptorRanges = 1;
    apply_gamma_root_parameter_dest.DescriptorTable.pDescriptorRanges =
        &apply_gamma_root_descriptor_range_dest;
    apply_gamma_root_parameter_dest.ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE apply_gamma_root_descriptor_range_source;
  apply_gamma_root_descriptor_range_source.RangeType =
      D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  apply_gamma_root_descriptor_range_source.NumDescriptors = 1;
  apply_gamma_root_descriptor_range_source.BaseShaderRegister = 1;
  apply_gamma_root_descriptor_range_source.RegisterSpace = 0;
  apply_gamma_root_descriptor_range_source.OffsetInDescriptorsFromTableStart =
      0;
  {
    D3D12_ROOT_PARAMETER& apply_gamma_root_parameter_source =
        apply_gamma_root_parameters[UINT(ApplyGammaRootParameter::kSource)];
    apply_gamma_root_parameter_source.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    apply_gamma_root_parameter_source.DescriptorTable.NumDescriptorRanges = 1;
    apply_gamma_root_parameter_source.DescriptorTable.pDescriptorRanges =
        &apply_gamma_root_descriptor_range_source;
    apply_gamma_root_parameter_source.ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE apply_gamma_root_descriptor_range_ramp;
  apply_gamma_root_descriptor_range_ramp.RangeType =
      D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  apply_gamma_root_descriptor_range_ramp.NumDescriptors = 1;
  apply_gamma_root_descriptor_range_ramp.BaseShaderRegister = 0;
  apply_gamma_root_descriptor_range_ramp.RegisterSpace = 0;
  apply_gamma_root_descriptor_range_ramp.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& apply_gamma_root_parameter_gamma_ramp =
        apply_gamma_root_parameters[UINT(ApplyGammaRootParameter::kRamp)];
    apply_gamma_root_parameter_gamma_ramp.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    apply_gamma_root_parameter_gamma_ramp.DescriptorTable.NumDescriptorRanges =
        1;
    apply_gamma_root_parameter_gamma_ramp.DescriptorTable.pDescriptorRanges =
        &apply_gamma_root_descriptor_range_ramp;
    apply_gamma_root_parameter_gamma_ramp.ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_ROOT_SIGNATURE_DESC apply_gamma_root_signature_desc;
  apply_gamma_root_signature_desc.NumParameters =
      UINT(ApplyGammaRootParameter::kCount);
  apply_gamma_root_signature_desc.pParameters = apply_gamma_root_parameters;
  apply_gamma_root_signature_desc.NumStaticSamplers = 0;
  apply_gamma_root_signature_desc.pStaticSamplers = nullptr;
  apply_gamma_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  *(apply_gamma_root_signature_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateRootSignature(provider,
                                           apply_gamma_root_signature_desc);
  if (!apply_gamma_root_signature_) {
    XELOGE("Failed to create the gamma ramp application root signature");
    return false;
  }
  *(apply_gamma_table_pipeline_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateComputePipeline(
          device, shaders::apply_gamma_table_cs,
          sizeof(shaders::apply_gamma_table_cs),
          apply_gamma_root_signature_.Get());
  if (!apply_gamma_table_pipeline_) {
    XELOGE(
        "Failed to create the 256-entry table gamma ramp application compute "
        "pipeline");
    return false;
  }
  *(apply_gamma_table_fxaa_luma_pipeline_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateComputePipeline(
          device, shaders::apply_gamma_table_fxaa_luma_cs,
          sizeof(shaders::apply_gamma_table_fxaa_luma_cs),
          apply_gamma_root_signature_.Get());
  if (!apply_gamma_table_fxaa_luma_pipeline_) {
    XELOGE(
        "Failed to create the 256-entry table gamma ramp application compute "
        "pipeline with perceptual luma output");
    return false;
  }
  *(apply_gamma_pwl_pipeline_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateComputePipeline(
          device, shaders::apply_gamma_pwl_cs,
          sizeof(shaders::apply_gamma_pwl_cs),
          apply_gamma_root_signature_.Get());
  if (!apply_gamma_pwl_pipeline_) {
    XELOGE("Failed to create the PWL gamma ramp application compute pipeline");
    return false;
  }
  *(apply_gamma_pwl_fxaa_luma_pipeline_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateComputePipeline(
          device, shaders::apply_gamma_pwl_fxaa_luma_cs,
          sizeof(shaders::apply_gamma_pwl_fxaa_luma_cs),
          apply_gamma_root_signature_.Get());
  if (!apply_gamma_pwl_fxaa_luma_pipeline_) {
    XELOGE(
        "Failed to create the PWL gamma ramp application compute pipeline with "
        "perceptual luma output");
    return false;
  }

  // Initialize compute pipelines for post-processing anti-aliasing.
  D3D12_ROOT_PARAMETER fxaa_root_parameters[UINT(FxaaRootParameter::kCount)];
  {
    D3D12_ROOT_PARAMETER& fxaa_root_parameter_constants =
        fxaa_root_parameters[UINT(ApplyGammaRootParameter::kConstants)];
    fxaa_root_parameter_constants.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    fxaa_root_parameter_constants.Constants.ShaderRegister = 0;
    fxaa_root_parameter_constants.Constants.RegisterSpace = 0;
    fxaa_root_parameter_constants.Constants.Num32BitValues =
        sizeof(FxaaConstants) / sizeof(uint32_t);
    fxaa_root_parameter_constants.ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE fxaa_root_descriptor_range_dest;
  fxaa_root_descriptor_range_dest.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  fxaa_root_descriptor_range_dest.NumDescriptors = 1;
  fxaa_root_descriptor_range_dest.BaseShaderRegister = 0;
  fxaa_root_descriptor_range_dest.RegisterSpace = 0;
  fxaa_root_descriptor_range_dest.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& fxaa_root_parameter_dest =
        fxaa_root_parameters[UINT(FxaaRootParameter::kDestination)];
    fxaa_root_parameter_dest.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    fxaa_root_parameter_dest.DescriptorTable.NumDescriptorRanges = 1;
    fxaa_root_parameter_dest.DescriptorTable.pDescriptorRanges =
        &fxaa_root_descriptor_range_dest;
    fxaa_root_parameter_dest.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE fxaa_root_descriptor_range_source;
  fxaa_root_descriptor_range_source.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  fxaa_root_descriptor_range_source.NumDescriptors = 1;
  fxaa_root_descriptor_range_source.BaseShaderRegister = 0;
  fxaa_root_descriptor_range_source.RegisterSpace = 0;
  fxaa_root_descriptor_range_source.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& fxaa_root_parameter_source =
        fxaa_root_parameters[UINT(FxaaRootParameter::kSource)];
    fxaa_root_parameter_source.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    fxaa_root_parameter_source.DescriptorTable.NumDescriptorRanges = 1;
    fxaa_root_parameter_source.DescriptorTable.pDescriptorRanges =
        &fxaa_root_descriptor_range_source;
    fxaa_root_parameter_source.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_STATIC_SAMPLER_DESC fxaa_root_sampler;
  fxaa_root_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  fxaa_root_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  fxaa_root_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  fxaa_root_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  fxaa_root_sampler.MipLODBias = 0.0f;
  fxaa_root_sampler.MaxAnisotropy = 1;
  fxaa_root_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  fxaa_root_sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
  fxaa_root_sampler.MinLOD = 0.0f;
  fxaa_root_sampler.MaxLOD = 0.0f;
  fxaa_root_sampler.ShaderRegister = 0;
  fxaa_root_sampler.RegisterSpace = 0;
  fxaa_root_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC fxaa_root_signature_desc;
  fxaa_root_signature_desc.NumParameters = UINT(FxaaRootParameter::kCount);
  fxaa_root_signature_desc.pParameters = fxaa_root_parameters;
  fxaa_root_signature_desc.NumStaticSamplers = 1;
  fxaa_root_signature_desc.pStaticSamplers = &fxaa_root_sampler;
  fxaa_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  *(fxaa_root_signature_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateRootSignature(provider, fxaa_root_signature_desc);
  if (!fxaa_root_signature_) {
    XELOGE("Failed to create the FXAA root signature");
    return false;
  }
  *(fxaa_pipeline_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateComputePipeline(device, shaders::fxaa_cs,
                                             sizeof(shaders::fxaa_cs),
                                             fxaa_root_signature_.Get());
  if (!fxaa_pipeline_) {
    XELOGE("Failed to create the FXAA compute pipeline");
    return false;
  }
  *(fxaa_extreme_pipeline_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateComputePipeline(device, shaders::fxaa_extreme_cs,
                                             sizeof(shaders::fxaa_extreme_cs),
                                             fxaa_root_signature_.Get());
  if (!fxaa_pipeline_) {
    XELOGE("Failed to create the extreme-quality FXAA compute pipeline");
    return false;
  }

  if (bindless_resources_used_) {
    // Create the system bindless descriptors once all resources are
    // initialized.
    // kNullRawSRV.
    ui::d3d12::util::CreateBufferRawSRV(
        device,
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kNullRawSRV)),
        nullptr, 0);
    // kNullRawUAV.
    ui::d3d12::util::CreateBufferRawUAV(
        device,
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kNullRawUAV)),
        nullptr, 0);
    // kNullTexture2DArray.
    D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc;
    null_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_srv_desc.Shader4ComponentMapping =
        D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
            D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
            D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
            D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
            D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0);
    null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    null_srv_desc.Texture2DArray.MostDetailedMip = 0;
    null_srv_desc.Texture2DArray.MipLevels = 1;
    null_srv_desc.Texture2DArray.FirstArraySlice = 0;
    null_srv_desc.Texture2DArray.ArraySize = 1;
    null_srv_desc.Texture2DArray.PlaneSlice = 0;
    null_srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(
        nullptr, &null_srv_desc,
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kNullTexture2DArray)));
    // kNullTexture3D.
    null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    null_srv_desc.Texture3D.MostDetailedMip = 0;
    null_srv_desc.Texture3D.MipLevels = 1;
    null_srv_desc.Texture3D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(
        nullptr, &null_srv_desc,
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kNullTexture3D)));
    // kNullTextureCube.
    null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    null_srv_desc.TextureCube.MostDetailedMip = 0;
    null_srv_desc.TextureCube.MipLevels = 1;
    null_srv_desc.TextureCube.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(
        nullptr, &null_srv_desc,
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kNullTextureCube)));
    // kSharedMemoryRawSRV.
    shared_memory_->WriteRawSRVDescriptor(provider.OffsetViewDescriptor(
        view_bindless_heap_cpu_start_,
        uint32_t(SystemBindlessView::kSharedMemoryRawSRV)));
    // kSharedMemoryR32UintSRV.
    shared_memory_->WriteUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kSharedMemoryR32UintSRV)),
        2);
    // kSharedMemoryR32G32UintSRV.
    shared_memory_->WriteUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kSharedMemoryR32G32UintSRV)),
        3);
    // kSharedMemoryR32G32B32A32UintSRV.
    shared_memory_->WriteUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kSharedMemoryR32G32B32A32UintSRV)),
        4);
    // kSharedMemoryRawUAV.
    shared_memory_->WriteRawUAVDescriptor(provider.OffsetViewDescriptor(
        view_bindless_heap_cpu_start_,
        uint32_t(SystemBindlessView::kSharedMemoryRawUAV)));
    // kSharedMemoryR32UintUAV.
    shared_memory_->WriteUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kSharedMemoryR32UintUAV)),
        2);
    // kSharedMemoryR32G32UintUAV.
    shared_memory_->WriteUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kSharedMemoryR32G32UintUAV)),
        3);
    // kSharedMemoryR32G32B32A32UintUAV.
    shared_memory_->WriteUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kSharedMemoryR32G32B32A32UintUAV)),
        4);
    // kEdramRawSRV.
    render_target_cache_->WriteEdramRawSRVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kEdramRawSRV)));
    // kEdramR32UintSRV.
    render_target_cache_->WriteEdramUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kEdramR32UintSRV)),
        2);
    // kEdramR32G32UintSRV.
    render_target_cache_->WriteEdramUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kEdramR32G32UintSRV)),
        3);
    // kEdramR32G32B32A32UintSRV.
    render_target_cache_->WriteEdramUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kEdramR32G32B32A32UintSRV)),
        4);
    // kEdramRawUAV.
    render_target_cache_->WriteEdramRawUAVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kEdramRawUAV)));
    // kEdramR32UintUAV.
    render_target_cache_->WriteEdramUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kEdramR32UintUAV)),
        2);
    // kEdramR32G32UintUAV.
    render_target_cache_->WriteEdramUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kEdramR32G32UintUAV)),
        3);
    // kEdramR32G32B32A32UintUAV.
    render_target_cache_->WriteEdramUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kEdramR32G32B32A32UintUAV)),
        4);
    // kGammaRampTableSRV.
    WriteGammaRampSRV(false,
                      provider.OffsetViewDescriptor(
                          view_bindless_heap_cpu_start_,
                          uint32_t(SystemBindlessView::kGammaRampTableSRV)));
    // kGammaRampPWLSRV.
    WriteGammaRampSRV(true,
                      provider.OffsetViewDescriptor(
                          view_bindless_heap_cpu_start_,
                          uint32_t(SystemBindlessView::kGammaRampPWLSRV)));
  }

  pix_capture_requested_.store(false, std::memory_order_relaxed);
  pix_capturing_ = false;

  // Just not to expose uninitialized memory.
  std::memset(&system_constants_, 0, sizeof(system_constants_));

  return true;
}

void D3D12CommandProcessor::ShutdownContext() {
  AwaitAllQueueOperationsCompletion();

  ui::d3d12::util::ReleaseAndNull(readback_buffer_);
  readback_buffer_size_ = 0;

  ui::d3d12::util::ReleaseAndNull(scratch_buffer_);
  scratch_buffer_size_ = 0;

  for (const std::pair<uint64_t, ID3D12Resource*>& resource_for_deletion :
       resources_for_deletion_) {
    resource_for_deletion.second->Release();
  }
  resources_for_deletion_.clear();

  fxaa_source_texture_submission_ = 0;
  fxaa_source_texture_.Reset();

  fxaa_extreme_pipeline_.Reset();
  fxaa_pipeline_.Reset();
  fxaa_root_signature_.Reset();

  apply_gamma_pwl_fxaa_luma_pipeline_.Reset();
  apply_gamma_pwl_pipeline_.Reset();
  apply_gamma_table_fxaa_luma_pipeline_.Reset();
  apply_gamma_table_pipeline_.Reset();
  apply_gamma_root_signature_.Reset();

  // Unmapping will be done implicitly by the destruction.
  gamma_ramp_upload_buffer_mapping_ = nullptr;
  gamma_ramp_upload_buffer_.Reset();
  gamma_ramp_buffer_.Reset();

  texture_cache_.reset();

  pipeline_cache_.reset();

  primitive_processor_.reset();

  // Shut down binding - bindless descriptors may be owned by subsystems like
  // the texture cache.

  // Root signatures are used by pipelines, thus freed after the pipelines.
  ui::d3d12::util::ReleaseAndNull(root_signature_bindless_ds_);
  ui::d3d12::util::ReleaseAndNull(root_signature_bindless_vs_);
  for (auto it : root_signatures_bindful_) {
    it.second->Release();
  }
  root_signatures_bindful_.clear();

  if (bindless_resources_used_) {
    texture_cache_bindless_sampler_map_.clear();
    for (const auto& sampler_bindless_heap_overflowed :
         sampler_bindless_heaps_overflowed_) {
      sampler_bindless_heap_overflowed.first->Release();
    }
    sampler_bindless_heaps_overflowed_.clear();
    sampler_bindless_heap_allocated_ = 0;
    ui::d3d12::util::ReleaseAndNull(sampler_bindless_heap_current_);
    view_bindless_one_use_descriptors_.clear();
    view_bindless_heap_free_.clear();
    ui::d3d12::util::ReleaseAndNull(view_bindless_heap_);
  } else {
    sampler_bindful_heap_pool_.reset();
    view_bindful_heap_pool_.reset();
  }
  constant_buffer_pool_.reset();

  render_target_cache_.reset();

  shared_memory_.reset();

  deferred_command_list_.Reset();
  ui::d3d12::util::ReleaseAndNull(command_list_1_);
  ui::d3d12::util::ReleaseAndNull(command_list_);
  ClearCommandAllocatorCache();

  frame_open_ = false;
  frame_current_ = 1;
  frame_completed_ = 0;
  std::memset(closed_frame_submissions_, 0, sizeof(closed_frame_submissions_));

  // First release the fences since they may reference fence_completion_event_.

  queue_operations_done_since_submission_signal_ = false;
  queue_operations_since_submission_fence_last_ = 0;
  ui::d3d12::util::ReleaseAndNull(queue_operations_since_submission_fence_);

  ui::d3d12::util::ReleaseAndNull(submission_fence_);
  submission_open_ = false;
  submission_current_ = 1;
  submission_completed_ = 0;

  if (fence_completion_event_) {
    CloseHandle(fence_completion_event_);
    fence_completion_event_ = nullptr;
  }

  device_removed_ = false;

  CommandProcessor::ShutdownContext();
}

XE_FORCEINLINE
void D3D12CommandProcessor::WriteRegisterForceinline(uint32_t index,
                                                     uint32_t value) {
  __m128i to_rangecheck = _mm_set1_epi16(static_cast<short>(index));

  __m128i lower_bounds = _mm_setr_epi16(
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 - 1,
      XE_GPU_REG_SHADER_CONSTANT_000_X - 1,
      XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 - 1, XE_GPU_REG_SCRATCH_REG0 - 1,
      XE_GPU_REG_COHER_STATUS_HOST - 1, XE_GPU_REG_DC_LUT_RW_INDEX - 1, 0, 0);
  __m128i upper_bounds = _mm_setr_epi16(
      XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5 + 1,
      XE_GPU_REG_SHADER_CONSTANT_511_W + 1,
      XE_GPU_REG_SHADER_CONSTANT_LOOP_31 + 1, XE_GPU_REG_SCRATCH_REG7 + 1,
      XE_GPU_REG_COHER_STATUS_HOST + 1, XE_GPU_REG_DC_LUT_30_COLOR + 1, 0, 0);

  // quick pre-test
  // todo: figure out just how unlikely this is. if very (it ought to be,
  // theres a ton of registers other than these) make this predicate
  // branchless and mark with unlikely, then make HandleSpecialRegisterWrite
  // noinline yep, its very unlikely. these ORS here are meant to be bitwise
  // ors, so that we do not do branching evaluation of the conditions (we will
  // almost always take all of the branches)
  /* unsigned expr =
      (index - XE_GPU_REG_SCRATCH_REG0 < 8) |
                  (index == XE_GPU_REG_COHER_STATUS_HOST) |
                  ((index - XE_GPU_REG_DC_LUT_RW_INDEX) <=
                   (XE_GPU_REG_DC_LUT_30_COLOR - XE_GPU_REG_DC_LUT_RW_INDEX));*/
  __m128i is_above_lower = _mm_cmpgt_epi16(to_rangecheck, lower_bounds);
  __m128i is_below_upper = _mm_cmplt_epi16(to_rangecheck, upper_bounds);
  __m128i is_within_range = _mm_and_si128(is_above_lower, is_below_upper);
  register_file_->values[index] = value;

  uint32_t movmask = static_cast<uint32_t>(_mm_movemask_epi8(is_within_range));

  if (movmask) {
    if (movmask & (1 << 3)) {
      if (frame_open_) {
        uint32_t float_constant_index =
            (index - XE_GPU_REG_SHADER_CONSTANT_000_X) >> 2;
        uint64_t float_constant_mask = 1ULL << float_constant_index;

        if (float_constant_index >= 256) {
          float_constant_index =
              static_cast<unsigned char>(float_constant_index);
          if (current_float_constant_map_pixel_[float_constant_index >> 6] &
              float_constant_mask) {  // take advantage of x86
                                      // modulus shift
            cbuffer_binding_float_pixel_.up_to_date = false;
          }
        } else {
          if (current_float_constant_map_vertex_[float_constant_index >> 6] &
              float_constant_mask) {
            cbuffer_binding_float_vertex_.up_to_date = false;
          }
        }
      }
    } else if (movmask & (1 << 5)) {
      cbuffer_binding_bool_loop_.up_to_date = false;
    } else if (movmask & (1 << 1)) {
      cbuffer_binding_fetch_.up_to_date = false;

      texture_cache_->TextureFetchConstantWritten(
          (index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) / 6);
    } else {
      HandleSpecialRegisterWrite(index, value);
    }
  } else {
    _ReadWriteBarrier();
    return;
  }
}
// todo: bit-pack the bools and use bitarith to reduce branches
void D3D12CommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  LogRegisterSet(index, value);
  WriteRegisterForceinline(index, value);
}

void D3D12CommandProcessor::WriteRegistersFromMem(uint32_t start_index,
                                                  uint32_t* base,
                                                  uint32_t num_registers) {
  WriteRegisterRangeFromMem_WithKnownBound<0, 0xFFFF>(start_index, base,
                                                      num_registers);
}

void D3D12CommandProcessor::WriteALURangeFromRing(xe::RingBuffer* ring,
                                                  uint32_t base,
                                                  uint32_t num_times) {
  WriteRegisterRangeFromRing_WithKnownBound<
      XE_GPU_REG_SHADER_CONSTANT_000_X, XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0>(
      ring, base + XE_GPU_REG_SHADER_CONSTANT_000_X, num_times);
}

void D3D12CommandProcessor::WriteFetchRangeFromRing(xe::RingBuffer* ring,
                                                    uint32_t base,
                                                    uint32_t num_times) {
  WriteRegisterRangeFromRing_WithKnownBound<0x4800, 0x5002>(ring, base + 0x4800,
                                                            num_times);
}

XE_FORCEINLINE
void D3D12CommandProcessor::WriteBoolRangeFromRing(xe::RingBuffer* ring,
                                                   uint32_t base,
                                                   uint32_t num_times) {
  // D3D12CommandProcessor::WriteRegisterRangeFromRing(ring, base + 0x4900,
  //                                                   num_times);

  WriteRegisterRangeFromRing_WithKnownBound<0x4900, 0x5002>(ring, base + 0x4900,
                                                            num_times);
}

XE_FORCEINLINE
void D3D12CommandProcessor::WriteLoopRangeFromRing(xe::RingBuffer* ring,
                                                   uint32_t base,
                                                   uint32_t num_times) {
  // D3D12CommandProcessor::WriteRegisterRangeFromRing(ring, base + 0x4908,
  //                                                   num_times);

  WriteRegisterRangeFromRing_WithKnownBound<0x4908, 0x5002>(ring, base + 0x4908,
                                                            num_times);
}

XE_FORCEINLINE
void D3D12CommandProcessor::WriteREGISTERSRangeFromRing(xe::RingBuffer* ring,
                                                        uint32_t base,
                                                        uint32_t num_times) {
  // D3D12CommandProcessor::WriteRegisterRangeFromRing(ring, base + 0x2000,
  //                                                  num_times);

  WriteRegisterRangeFromRing_WithKnownBound<0x2000, 0x2000 + 0x800>(
      ring, base + 0x2000, num_times);
}

XE_FORCEINLINE
void D3D12CommandProcessor::WriteALURangeFromMem(uint32_t start_index,
                                                 uint32_t* base,
                                                 uint32_t num_registers) {
  WriteRegisterRangeFromMem_WithKnownBound<
      XE_GPU_REG_SHADER_CONSTANT_000_X, XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0>(
      start_index + 0x4000, base, num_registers);
}

XE_FORCEINLINE
void D3D12CommandProcessor::WriteFetchRangeFromMem(uint32_t start_index,
                                                   uint32_t* base,
                                                   uint32_t num_registers) {
  WriteRegisterRangeFromMem_WithKnownBound<0x4800, 0x5002>(start_index + 0x4800,
                                                           base, num_registers);
}

XE_FORCEINLINE
void D3D12CommandProcessor::WriteBoolRangeFromMem(uint32_t start_index,
                                                  uint32_t* base,
                                                  uint32_t num_registers) {
  WriteRegisterRangeFromMem_WithKnownBound<0x4900, 0x5002>(start_index + 0x4900,
                                                           base, num_registers);
}

XE_FORCEINLINE
void D3D12CommandProcessor::WriteLoopRangeFromMem(uint32_t start_index,
                                                  uint32_t* base,
                                                  uint32_t num_registers) {
  WriteRegisterRangeFromMem_WithKnownBound<0x4908, 0x5002>(start_index + 0x4908,
                                                           base, num_registers);
}

XE_FORCEINLINE
void D3D12CommandProcessor::WriteREGISTERSRangeFromMem(uint32_t start_index,
                                                       uint32_t* base,
                                                       uint32_t num_registers) {
  WriteRegisterRangeFromMem_WithKnownBound<0x2000, 0x2000 + 0x800>(
      start_index + 0x2000, base, num_registers);
}
/*
wraparound rarely happens, so its best to hoist this out of
writeregisterrangefromring, and structure the two functions so that this can be
tail called
*/
XE_NOINLINE
void D3D12CommandProcessor::WriteRegisterRangeFromRing_WraparoundCase(
    xe::RingBuffer* ring, uint32_t base, uint32_t num_registers) {
  // we already brought it into L2 earlier
  RingBuffer::ReadRange range =
      ring->BeginPrefetchedRead<swcache::PrefetchTag::Level1>(num_registers *
                                                              sizeof(uint32_t));

  uint32_t num_regs_firstrange =
      static_cast<uint32_t>(range.first_length / sizeof(uint32_t));

  D3D12CommandProcessor::WriteRegistersFromMem(
      base, reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(range.first)),
      num_regs_firstrange);

  D3D12CommandProcessor::WriteRegistersFromMem(
      base + num_regs_firstrange,
      reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(range.second)),
      num_registers - num_regs_firstrange);

  ring->EndRead(range);
}
void D3D12CommandProcessor::WriteRegisterRangeFromRing(xe::RingBuffer* ring,
                                                       uint32_t base,
                                                       uint32_t num_registers) {
  RingBuffer::ReadRange range =
      ring->BeginRead(num_registers * sizeof(uint32_t));

  XE_LIKELY_IF(!range.second) {
    WriteRegistersFromMem(
        base, reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(range.first)),
        num_registers);

    ring->EndRead(range);
  }
  else {
    return WriteRegisterRangeFromRing_WraparoundCase(ring, base, num_registers);
  }
}

template <uint32_t register_lower_bound, uint32_t register_upper_bound>
constexpr bool bounds_may_have_reg(uint32_t reg) {
  return reg >= register_lower_bound && reg < register_upper_bound;
}

template <uint32_t register_lower_bound, uint32_t register_upper_bound>
constexpr bool bounds_may_have_bounds(uint32_t reg, uint32_t last_reg) {
  return bounds_may_have_reg<register_lower_bound, register_upper_bound>(reg) ||
         bounds_may_have_reg<register_lower_bound, register_upper_bound>(
             last_reg);
}
XE_FORCEINLINE
void D3D12CommandProcessor::WriteShaderConstantsFromMem(
    uint32_t start_index, uint32_t* base, uint32_t num_registers) {
  if (frame_open_) {
    bool cbuffer_pixel_uptodate = cbuffer_binding_float_pixel_.up_to_date;
    bool cbuffer_vertex_uptodate = cbuffer_binding_float_vertex_.up_to_date;
    if (cbuffer_pixel_uptodate || cbuffer_vertex_uptodate) {
      // super naive, could just do some bit magic and interval checking,
      // but we just need this hoisted out of the copy so we do a bulk copy
      // because its the actual load/swap/store we're getting murdered by
      // this precheck followed by copy_and_swap_32_unaligned reduced the cpu
      // usage from packettype0/writeregistersfrommem from 10-11% of cpu time
      // spent on xenia to like 1%
      // chrispy: todo, this can be reduced even further, should be split into
      // two loops and should skip whole words, this could net us even bigger
      // gains
      uint32_t map_index = (start_index - XE_GPU_REG_SHADER_CONSTANT_000_X) / 4;
      uint32_t end_map_index =
          (start_index + num_registers - XE_GPU_REG_SHADER_CONSTANT_000_X) / 4;

      if (map_index < 256 && cbuffer_vertex_uptodate) {
        for (; map_index < end_map_index; ++map_index) {
          if (current_float_constant_map_vertex_[map_index >> 6] &
              (1ull << map_index)) {
            cbuffer_vertex_uptodate = false;
            break;
          }
        }
      }
      if (end_map_index > 256 && cbuffer_pixel_uptodate) {
        for (; map_index < end_map_index; ++map_index) {
          uint32_t float_constant_index = map_index;
          float_constant_index -= 256;
          if (current_float_constant_map_pixel_[float_constant_index >> 6] &
              (1ull << float_constant_index)) {
            cbuffer_pixel_uptodate = false;
            break;
          }
        }
      }
    }
    cbuffer_binding_float_pixel_.up_to_date = cbuffer_pixel_uptodate;
    cbuffer_binding_float_vertex_.up_to_date = cbuffer_vertex_uptodate;
  }

  // maybe use non-temporal copy if possible...
  copy_and_swap_32_unaligned(&register_file_->values[start_index], base,
                             num_registers);
}
XE_FORCEINLINE
void D3D12CommandProcessor::WriteBoolLoopFromMem(uint32_t start_index,
                                                 uint32_t* base,
                                                 uint32_t num_registers) {
  cbuffer_binding_bool_loop_.up_to_date = false;
  copy_and_swap_32_unaligned(&register_file_->values[start_index], base,
                             num_registers);
}
XE_FORCEINLINE
void D3D12CommandProcessor::WriteFetchFromMem(uint32_t start_index,
                                              uint32_t* base,
                                              uint32_t num_registers) {
  cbuffer_binding_fetch_.up_to_date = false;

  uint32_t first_fetch =
      ((start_index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) / 6);
  uint32_t last_fetch =  // i think last_fetch should be inclusive if its modulo
                         // is nz...
      (((start_index + num_registers) - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) /
       6);
  texture_cache_->TextureFetchConstantsWritten(first_fetch, last_fetch);

  copy_and_swap_32_unaligned(&register_file_->values[start_index], base,
                             num_registers);
}

void D3D12CommandProcessor::WritePossiblySpecialRegistersFromMem(
    uint32_t start_index, uint32_t* base, uint32_t numregs) {
  uint32_t end = numregs + start_index;
  for (uint32_t index = start_index; index < end; ++index, ++base) {
    uint32_t value = xe::load_and_swap<uint32_t>(base);

    register_file_->values[index] = value;

    unsigned expr = 0;

    expr |= (index - XE_GPU_REG_SCRATCH_REG0 < 8);

    expr |= (index == XE_GPU_REG_COHER_STATUS_HOST);

    expr |= ((index - XE_GPU_REG_DC_LUT_RW_INDEX) <=
             (XE_GPU_REG_DC_LUT_30_COLOR - XE_GPU_REG_DC_LUT_RW_INDEX));

    if (expr == 0) {
    } else {
      HandleSpecialRegisterWrite(index, value);
    }
  }
}
template <uint32_t register_lower_bound, uint32_t register_upper_bound>
XE_FORCEINLINE void
D3D12CommandProcessor::WriteRegisterRangeFromMem_WithKnownBound(
    uint32_t start_index, uint32_t* base, uint32_t num_registers) {
  uint32_t end = start_index + num_registers;
  LogRegisterSets(start_index, base, num_registers);
  uint32_t current_index = start_index;

  auto get_end_before_qty = [&end, current_index](uint32_t regnum) {
    return std::min<uint32_t>(regnum, end) - current_index;
  };
#define REGULAR_WRITE_CALLBACK(s, e, i, b, n) \
  copy_and_swap_32_unaligned(&register_file_->values[i], b, n)
#define WRITE_FETCH_CONSTANTS_CALLBACK(str, er, ind, b, n) \
  WriteFetchFromMem(ind, b, n)
#define SPECIAL_REG_RANGE_CALLBACK(str, edr, ind, bs, n) \
  WritePossiblySpecialRegistersFromMem(ind, bs, n)
#define WRITE_SHADER_CONSTANTS_CALLBACK(start_range, end_range, index, base, \
                                        n)                                   \
  WriteShaderConstantsFromMem(index, base, n)
#define WRITE_BOOL_LOOP_CALLBACK(s, e, i, b, n) WriteBoolLoopFromMem(i, b, n)

#define DO_A_RANGE(start_range, end_range, cb)                       \
  if constexpr (start_range >= register_lower_bound ||               \
                end_range > register_lower_bound) {                  \
    if (current_index < (end_range)) {                               \
      uint32_t ntowrite = get_end_before_qty(end_range);             \
      cb((start_range), (end_range), current_index, base, ntowrite); \
      current_index += ntowrite;                                     \
      base += ntowrite;                                              \
    }                                                                \
    if (current_index >= end) {                                      \
      return;                                                        \
    }                                                                \
  }

#define REFRESH_MSVC_RANGE()                              \
  XE_MSVC_ASSUME(current_index >= register_lower_bound && \
                 current_index < register_upper_bound)

  REFRESH_MSVC_RANGE();

  DO_A_RANGE(0, XE_GPU_REG_SCRATCH_REG0, REGULAR_WRITE_CALLBACK);
  REFRESH_MSVC_RANGE();
  DO_A_RANGE(XE_GPU_REG_SCRATCH_REG0, XE_GPU_REG_DC_LUT_30_COLOR + 1,
             SPECIAL_REG_RANGE_CALLBACK);
  REFRESH_MSVC_RANGE();
  DO_A_RANGE(XE_GPU_REG_DC_LUT_30_COLOR + 1, XE_GPU_REG_SHADER_CONSTANT_000_X,
             REGULAR_WRITE_CALLBACK);
  REFRESH_MSVC_RANGE();
  DO_A_RANGE(XE_GPU_REG_SHADER_CONSTANT_000_X,
             XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0,
             WRITE_SHADER_CONSTANTS_CALLBACK);
  REFRESH_MSVC_RANGE();
  DO_A_RANGE(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0,
             XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5 + 1,
             WRITE_FETCH_CONSTANTS_CALLBACK);
  REFRESH_MSVC_RANGE();
  DO_A_RANGE(XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031,
             XE_GPU_REG_SHADER_CONSTANT_LOOP_31 + 1, WRITE_BOOL_LOOP_CALLBACK);
  REFRESH_MSVC_RANGE();
  DO_A_RANGE(XE_GPU_REG_SHADER_CONSTANT_LOOP_31 + 1, 65536,
             REGULAR_WRITE_CALLBACK);
}
template <uint32_t register_lower_bound, uint32_t register_upper_bound>
XE_FORCEINLINE void
D3D12CommandProcessor::WriteRegisterRangeFromRing_WithKnownBound(
    xe::RingBuffer* ring, uint32_t base, uint32_t num_registers) {
  RingBuffer::ReadRange range =
      ring->BeginRead(num_registers * sizeof(uint32_t));

  XE_LIKELY_IF(!range.second) {
    WriteRegisterRangeFromMem_WithKnownBound<register_lower_bound,
                                             register_upper_bound>(
        base, reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(range.first)),
        num_registers);

    ring->EndRead(range);
  }
  else {
    return WriteRegisterRangeFromRing_WraparoundCase(ring, base, num_registers);
  }
}
XE_NOINLINE
void D3D12CommandProcessor::WriteOneRegisterFromRing(uint32_t base,
                                                     uint32_t num_times) {
  auto read = reader_.BeginPrefetchedRead<swcache::PrefetchTag::Level1>(
      num_times * sizeof(uint32_t));

  uint32_t first_length = read.first_length / sizeof(uint32_t);

  for (uint32_t i = 0; i < first_length; ++i) {
    D3D12CommandProcessor::WriteRegister(
        base, xe::load_and_swap<uint32_t>(read.first + (sizeof(uint32_t) * i)));
  }

  XE_UNLIKELY_IF(read.second) {
    uint32_t second_length = read.second_length / sizeof(uint32_t);

    for (uint32_t i = 0; i < second_length; ++i) {
      D3D12CommandProcessor::WriteRegister(
          base,
          xe::load_and_swap<uint32_t>(read.second + (sizeof(uint32_t) * i)));
    }
  }
  reader_.EndRead(read);
}
void D3D12CommandProcessor::OnGammaRamp256EntryTableValueWritten() {
  gamma_ramp_256_entry_table_up_to_date_ = false;
}

void D3D12CommandProcessor::OnGammaRampPWLValueWritten() {
  gamma_ramp_pwl_up_to_date_ = false;
}

void D3D12CommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                      uint32_t frontbuffer_width,
                                      uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");

  ui::Presenter* presenter = graphics_system_->presenter();
  if (!presenter) {
    return;
  }

  // In case the swap command is the only one in the frame.
  if (!BeginSubmission(true)) {
    return;
  }

  // Obtain the actual front buffer size to pass to RefreshGuestOutput,
  // resolution-scaled if it's a resolve destination, or not otherwise.
  D3D12_SHADER_RESOURCE_VIEW_DESC swap_texture_srv_desc;
  xenos::TextureFormat frontbuffer_format;
  ID3D12Resource* swap_texture_resource = texture_cache_->RequestSwapTexture(
      swap_texture_srv_desc, frontbuffer_format);
  if (!swap_texture_resource) {
    return;
  }
  D3D12_RESOURCE_DESC swap_texture_desc = swap_texture_resource->GetDesc();

  auto aspect = graphics_system_->GetScaledAspectRatio();

  presenter->RefreshGuestOutput(
      uint32_t(swap_texture_desc.Width), uint32_t(swap_texture_desc.Height),
      aspect.first, aspect.second,
      [this, &swap_texture_srv_desc, frontbuffer_format, swap_texture_resource,
       &swap_texture_desc](
          ui::Presenter::GuestOutputRefreshContext& context) -> bool {
        const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
        ID3D12Device* device = provider.GetDevice();

        SwapPostEffect swap_post_effect = GetActualSwapPostEffect();
        bool use_fxaa = swap_post_effect == SwapPostEffect::kFxaa ||
                        swap_post_effect == SwapPostEffect::kFxaaExtreme;
        if (use_fxaa) {
          // Make sure the texture of the correct size is available for FXAA.
          if (fxaa_source_texture_) {
            D3D12_RESOURCE_DESC fxaa_source_texture_desc =
                fxaa_source_texture_->GetDesc();
            if (fxaa_source_texture_desc.Width != swap_texture_desc.Width ||
                fxaa_source_texture_desc.Height != swap_texture_desc.Height) {
              if (submission_completed_ < fxaa_source_texture_submission_) {
                fxaa_source_texture_->AddRef();
                resources_for_deletion_.emplace_back(
                    fxaa_source_texture_submission_,
                    fxaa_source_texture_.Get());
              }
              fxaa_source_texture_.Reset();
              fxaa_source_texture_submission_ = 0;
            }
          }
          if (!fxaa_source_texture_) {
            D3D12_RESOURCE_DESC fxaa_source_texture_desc;
            fxaa_source_texture_desc.Dimension =
                D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            fxaa_source_texture_desc.Alignment = 0;
            fxaa_source_texture_desc.Width = swap_texture_desc.Width;
            fxaa_source_texture_desc.Height = swap_texture_desc.Height;
            fxaa_source_texture_desc.DepthOrArraySize = 1;
            fxaa_source_texture_desc.MipLevels = 1;
            fxaa_source_texture_desc.Format = kFxaaSourceTextureFormat;
            fxaa_source_texture_desc.SampleDesc.Count = 1;
            fxaa_source_texture_desc.SampleDesc.Quality = 0;
            fxaa_source_texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            fxaa_source_texture_desc.Flags =
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (FAILED(device->CreateCommittedResource(
                    &ui::d3d12::util::kHeapPropertiesDefault,
                    provider.GetHeapFlagCreateNotZeroed(),
                    &fxaa_source_texture_desc,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                    IID_PPV_ARGS(&fxaa_source_texture_)))) {
              XELOGE("Failed to create the FXAA input texture");
              swap_post_effect = SwapPostEffect::kNone;
              use_fxaa = false;
            }
          }
        }

        // This is according to D3D::InitializePresentationParameters from a
        // game executable, which initializes the 256-entry table gamma ramp for
        // 8_8_8_8 output and the PWL gamma ramp for 2_10_10_10.
        // TODO(Triang3l): Choose between the table and PWL based on
        // DC_LUTA_CONTROL, support both for all formats (and also different
        // increments for PWL).
        bool use_pwl_gamma_ramp =
            frontbuffer_format == xenos::TextureFormat::k_2_10_10_10 ||
            frontbuffer_format ==
                xenos::TextureFormat::k_2_10_10_10_AS_16_16_16_16;

        context.SetIs8bpc(!use_pwl_gamma_ramp && !use_fxaa);

        // Upload the new gamma ramp, using the upload buffer for the current
        // frame (will close the frame after this anyway, so can't write
        // multiple times per frame).
        if (!(use_pwl_gamma_ramp ? gamma_ramp_pwl_up_to_date_
                                 : gamma_ramp_256_entry_table_up_to_date_)) {
          uint32_t gamma_ramp_offset_bytes = use_pwl_gamma_ramp ? 256 * 4 : 0;
          uint32_t gamma_ramp_upload_offset_bytes =
              uint32_t(frame_current_ % kQueueFrames) * ((256 + 128 * 3) * 4) +
              gamma_ramp_offset_bytes;
          uint32_t gamma_ramp_size_bytes =
              (use_pwl_gamma_ramp ? 128 * 3 : 256) * 4;
          if (std::endian::native != std::endian::little &&
              use_pwl_gamma_ramp) {
            // R16G16 is first R16, where the shader expects the base, and
            // second G16, where the delta should be, but gamma_ramp_pwl_rgb()
            // is an array of 32-bit DC_LUT_PWL_DATA registers - swap 16 bits in
            // each 32.
            auto gamma_ramp_pwl_upload_buffer =
                reinterpret_cast<reg::DC_LUT_PWL_DATA*>(
                    gamma_ramp_upload_buffer_mapping_ +
                    gamma_ramp_upload_offset_bytes);
            const reg::DC_LUT_PWL_DATA* gamma_ramp_pwl = gamma_ramp_pwl_rgb();
            for (size_t i = 0; i < 128 * 3; ++i) {
              reg::DC_LUT_PWL_DATA& gamma_ramp_pwl_upload_buffer_entry =
                  gamma_ramp_pwl_upload_buffer[i];
              reg::DC_LUT_PWL_DATA gamma_ramp_pwl_entry = gamma_ramp_pwl[i];
              gamma_ramp_pwl_upload_buffer_entry.base =
                  gamma_ramp_pwl_entry.delta;
              gamma_ramp_pwl_upload_buffer_entry.delta =
                  gamma_ramp_pwl_entry.base;
            }
          } else {
            std::memcpy(
                gamma_ramp_upload_buffer_mapping_ +
                    gamma_ramp_upload_offset_bytes,
                use_pwl_gamma_ramp
                    ? static_cast<const void*>(gamma_ramp_pwl_rgb())
                    : static_cast<const void*>(gamma_ramp_256_entry_table()),
                gamma_ramp_size_bytes);
          }
          PushTransitionBarrier(gamma_ramp_buffer_.Get(),
                                gamma_ramp_buffer_state_,
                                D3D12_RESOURCE_STATE_COPY_DEST);
          gamma_ramp_buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
          SubmitBarriers();
          deferred_command_list_.D3DCopyBufferRegion(
              gamma_ramp_buffer_.Get(), gamma_ramp_offset_bytes,
              gamma_ramp_upload_buffer_.Get(), gamma_ramp_upload_offset_bytes,
              gamma_ramp_size_bytes);
          (use_pwl_gamma_ramp ? gamma_ramp_pwl_up_to_date_
                              : gamma_ramp_256_entry_table_up_to_date_) = true;
        }

        // Destination, source, and if bindful, gamma ramp.
        ui::d3d12::util::DescriptorCpuGpuHandlePair apply_gamma_descriptors[3];
        ui::d3d12::util::DescriptorCpuGpuHandlePair
            apply_gamma_descriptor_gamma_ramp;
        if (!RequestOneUseSingleViewDescriptors(
                bindless_resources_used_ ? 2 : 3, apply_gamma_descriptors)) {
          return false;
        }
        // Must not call anything that can change the descriptor heap from now
        // on!
        if (bindless_resources_used_) {
          apply_gamma_descriptor_gamma_ramp = GetSystemBindlessViewHandlePair(
              use_pwl_gamma_ramp ? SystemBindlessView::kGammaRampPWLSRV
                                 : SystemBindlessView::kGammaRampTableSRV);
        } else {
          apply_gamma_descriptor_gamma_ramp = apply_gamma_descriptors[2];
          WriteGammaRampSRV(use_pwl_gamma_ramp,
                            apply_gamma_descriptor_gamma_ramp.first);
        }

        ID3D12Resource* guest_output_resource =
            static_cast<
                ui::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(
                context)
                .resource_uav_capable();

        if (use_fxaa) {
          fxaa_source_texture_submission_ = submission_current_;
        }

        ID3D12Resource* apply_gamma_dest =
            use_fxaa ? fxaa_source_texture_.Get() : guest_output_resource;
        D3D12_RESOURCE_STATES apply_gamma_dest_initial_state =
            use_fxaa ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                     : ui::d3d12::D3D12Presenter::kGuestOutputInternalState;
        static_cast<ui::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(
            context)
            .resource_uav_capable();
        PushTransitionBarrier(apply_gamma_dest, apply_gamma_dest_initial_state,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // From now on, even in case of failure, apply_gamma_dest must be
        // transitioned back to apply_gamma_dest_initial_state!
        D3D12_UNORDERED_ACCESS_VIEW_DESC apply_gamma_dest_uav_desc;
        apply_gamma_dest_uav_desc.Format =
            use_fxaa ? kFxaaSourceTextureFormat
                     : ui::d3d12::D3D12Presenter::kGuestOutputFormat;
        apply_gamma_dest_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        apply_gamma_dest_uav_desc.Texture2D.MipSlice = 0;
        apply_gamma_dest_uav_desc.Texture2D.PlaneSlice = 0;
        device->CreateUnorderedAccessView(apply_gamma_dest, nullptr,
                                          &apply_gamma_dest_uav_desc,
                                          apply_gamma_descriptors[0].first);

        device->CreateShaderResourceView(swap_texture_resource,
                                         &swap_texture_srv_desc,
                                         apply_gamma_descriptors[1].first);

        PushTransitionBarrier(gamma_ramp_buffer_.Get(),
                              gamma_ramp_buffer_state_,
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gamma_ramp_buffer_state_ =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        deferred_command_list_.D3DSetComputeRootSignature(
            apply_gamma_root_signature_.Get());
        ApplyGammaConstants apply_gamma_constants;
        apply_gamma_constants.size[0] = uint32_t(swap_texture_desc.Width);
        apply_gamma_constants.size[1] = uint32_t(swap_texture_desc.Height);
        deferred_command_list_.D3DSetComputeRoot32BitConstants(
            UINT(ApplyGammaRootParameter::kConstants),
            sizeof(apply_gamma_constants) / sizeof(uint32_t),
            &apply_gamma_constants, 0);
        deferred_command_list_.D3DSetComputeRootDescriptorTable(
            UINT(ApplyGammaRootParameter::kDestination),
            apply_gamma_descriptors[0].second);
        deferred_command_list_.D3DSetComputeRootDescriptorTable(
            UINT(ApplyGammaRootParameter::kSource),
            apply_gamma_descriptors[1].second);
        deferred_command_list_.D3DSetComputeRootDescriptorTable(
            UINT(ApplyGammaRootParameter::kRamp),
            apply_gamma_descriptor_gamma_ramp.second);
        ID3D12PipelineState* apply_gamma_pipeline;
        if (use_pwl_gamma_ramp) {
          apply_gamma_pipeline = use_fxaa
                                     ? apply_gamma_pwl_fxaa_luma_pipeline_.Get()
                                     : apply_gamma_pwl_pipeline_.Get();
        } else {
          apply_gamma_pipeline =
              use_fxaa ? apply_gamma_table_fxaa_luma_pipeline_.Get()
                       : apply_gamma_table_pipeline_.Get();
        }
        SetExternalPipeline(apply_gamma_pipeline);
        SubmitBarriers();
        uint32_t group_count_x = (uint32_t(swap_texture_desc.Width) + 15) / 16;
        uint32_t group_count_y = (uint32_t(swap_texture_desc.Height) + 7) / 8;
        deferred_command_list_.D3DDispatch(group_count_x, group_count_y, 1);

        // Apply FXAA.
        if (use_fxaa) {
          // Destination and source.
          ui::d3d12::util::DescriptorCpuGpuHandlePair fxaa_descriptors[2];
          if (!RequestOneUseSingleViewDescriptors(
                  uint32_t(xe::countof(fxaa_descriptors)), fxaa_descriptors)) {
            // Failed to obtain descriptors for FXAA - just copy after gamma
            // ramp application without applying FXAA.
            PushTransitionBarrier(apply_gamma_dest,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_STATE_COPY_SOURCE);
            PushTransitionBarrier(
                guest_output_resource,
                ui::d3d12::D3D12Presenter::kGuestOutputInternalState,
                D3D12_RESOURCE_STATE_COPY_DEST);
            SubmitBarriers();
            deferred_command_list_.D3DCopyResource(guest_output_resource,
                                                   apply_gamma_dest);
            PushTransitionBarrier(apply_gamma_dest,
                                  D3D12_RESOURCE_STATE_COPY_SOURCE,
                                  apply_gamma_dest_initial_state);
            PushTransitionBarrier(
                guest_output_resource, D3D12_RESOURCE_STATE_COPY_DEST,
                ui::d3d12::D3D12Presenter::kGuestOutputInternalState);
            return false;
          } else {
            assert_true(apply_gamma_dest_initial_state ==
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            PushTransitionBarrier(apply_gamma_dest,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                  apply_gamma_dest_initial_state);
            PushTransitionBarrier(
                guest_output_resource,
                ui::d3d12::D3D12Presenter::kGuestOutputInternalState,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            // From now on, even in case of failure, guest_output_resource must
            // be transitioned back to kGuestOutputInternalState!
            deferred_command_list_.D3DSetComputeRootSignature(
                fxaa_root_signature_.Get());
            FxaaConstants fxaa_constants;
            fxaa_constants.size[0] = uint32_t(swap_texture_desc.Width);
            fxaa_constants.size[1] = uint32_t(swap_texture_desc.Height);
            fxaa_constants.size_inv[0] = 1.0f / float(fxaa_constants.size[0]);
            fxaa_constants.size_inv[1] = 1.0f / float(fxaa_constants.size[1]);
            deferred_command_list_.D3DSetComputeRoot32BitConstants(
                UINT(FxaaRootParameter::kConstants),
                sizeof(fxaa_constants) / sizeof(uint32_t), &fxaa_constants, 0);
            D3D12_UNORDERED_ACCESS_VIEW_DESC fxaa_dest_uav_desc;
            fxaa_dest_uav_desc.Format =
                ui::d3d12::D3D12Presenter::kGuestOutputFormat;
            fxaa_dest_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            fxaa_dest_uav_desc.Texture2D.MipSlice = 0;
            fxaa_dest_uav_desc.Texture2D.PlaneSlice = 0;
            device->CreateUnorderedAccessView(guest_output_resource, nullptr,
                                              &fxaa_dest_uav_desc,
                                              fxaa_descriptors[0].first);
            deferred_command_list_.D3DSetComputeRootDescriptorTable(
                UINT(FxaaRootParameter::kDestination),
                fxaa_descriptors[0].second);
            D3D12_SHADER_RESOURCE_VIEW_DESC fxaa_source_srv_desc;
            fxaa_source_srv_desc.Format = kFxaaSourceTextureFormat;
            fxaa_source_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            fxaa_source_srv_desc.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            fxaa_source_srv_desc.Texture2D.MostDetailedMip = 0;
            fxaa_source_srv_desc.Texture2D.MipLevels = 1;
            fxaa_source_srv_desc.Texture2D.PlaneSlice = 0;
            fxaa_source_srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
            device->CreateShaderResourceView(fxaa_source_texture_.Get(),
                                             &fxaa_source_srv_desc,
                                             fxaa_descriptors[1].first);
            deferred_command_list_.D3DSetComputeRootDescriptorTable(
                UINT(FxaaRootParameter::kSource), fxaa_descriptors[1].second);
            SetExternalPipeline(swap_post_effect == SwapPostEffect::kFxaaExtreme
                                    ? fxaa_extreme_pipeline_.Get()
                                    : fxaa_pipeline_.Get());
            SubmitBarriers();
            deferred_command_list_.D3DDispatch(group_count_x, group_count_y, 1);
            PushTransitionBarrier(
                guest_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                ui::d3d12::D3D12Presenter::kGuestOutputInternalState);
          }
        } else {
          assert_true(apply_gamma_dest_initial_state ==
                      ui::d3d12::D3D12Presenter::kGuestOutputInternalState);
          PushTransitionBarrier(apply_gamma_dest,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                apply_gamma_dest_initial_state);
        }

        // Need to submit all the commands before giving the image back to the
        // presenter so it can submit its own commands for displaying it to the
        // queue.
        SubmitBarriers();
        EndSubmission(true);
        return true;
      });

  // End the frame even if did not present for any reason (the image refresher
  // was not called), to prevent leaking per-frame resources.
  EndSubmission(true);
}

void D3D12CommandProcessor::OnPrimaryBufferEnd() {
  if (cvars::d3d12_submit_on_primary_buffer_end && submission_open_ &&
      CanEndSubmissionImmediately()) {
    EndSubmission(false);
  }
}

Shader* D3D12CommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                          uint32_t guest_address,
                                          const uint32_t* host_address,
                                          uint32_t dword_count) {
  return pipeline_cache_->LoadShader(shader_type, host_address, dword_count);
}

bool D3D12CommandProcessor::IssueDraw(xenos::PrimitiveType primitive_type,
                                      uint32_t index_count,
                                      IndexBufferInfo* index_buffer_info,
                                      bool major_mode_explicit) {
#if XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES

  ID3D12Device* device = GetD3D12Provider().GetDevice();
  const RegisterFile& regs = *register_file_;

  xenos::ModeControl edram_mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
  if (edram_mode == xenos::ModeControl::kCopy) {
    // Special copy handling.
    return IssueCopy();
  }

  if (regs.Get<reg::RB_SURFACE_INFO>().surface_pitch == 0) {
    // Doesn't actually draw.
    // TODO(Triang3l): Do something so memexport still works in this case maybe?
    // Unlikely that zero would even really be legal though.
    return true;
  }

  // Vertex shader analysis.
  auto vertex_shader = static_cast<D3D12Shader*>(active_vertex_shader());
  if (!vertex_shader) {
    // Always need a vertex shader.
    return false;
  }
  pipeline_cache_->AnalyzeShaderUcode(*vertex_shader);

  const bool memexport_used_vertex = vertex_shader->memexport_eM_written() != 0;

  // Pixel shader analysis.
  bool primitive_polygonal = draw_util::IsPrimitivePolygonal(regs);
  bool is_rasterization_done =
      draw_util::IsRasterizationPotentiallyDone(regs, primitive_polygonal);
  D3D12Shader* pixel_shader = nullptr;
  if (is_rasterization_done) {
    // See xenos::ModeControl for explanation why the pixel shader is only used
    // when it's kColorDepth here.
    if (edram_mode == xenos::ModeControl::kColorDepth) {
      pixel_shader = static_cast<D3D12Shader*>(active_pixel_shader());
      if (pixel_shader) {
        pipeline_cache_->AnalyzeShaderUcode(*pixel_shader);
        if (!draw_util::IsPixelShaderNeededWithRasterization(*pixel_shader,
                                                             regs)) {
          pixel_shader = nullptr;
        }
      }
    }
  } else {
    // Disabling pixel shader for this case is also required by the pipeline
    // cache.
    if (!memexport_used_vertex) {
      // This draw has no effect.
      return true;
    }
  }

  const bool memexport_used_pixel =
      pixel_shader && (pixel_shader->memexport_eM_written() != 0);
  const bool memexport_used = memexport_used_vertex || memexport_used_pixel;

  if (!BeginSubmission(true)) {
    return false;
  }

  // Process primitives.
  PrimitiveProcessor::ProcessingResult primitive_processing_result;
  if (!primitive_processor_->Process(primitive_processing_result)) {
    return false;
  }
  if (!primitive_processing_result.host_draw_vertex_count) {
    // Nothing to draw.
    return true;
  }

  reg::RB_DEPTHCONTROL normalized_depth_control =
      draw_util::GetNormalizedDepthControl(regs);

  // Shader modifications.
  uint32_t ps_param_gen_pos = UINT32_MAX;
  uint32_t interpolator_mask =
      pixel_shader ? (vertex_shader->writes_interpolators() &
                      pixel_shader->GetInterpolatorInputMask(
                          regs.Get<reg::SQ_PROGRAM_CNTL>(),
                          regs.Get<reg::SQ_CONTEXT_MISC>(), ps_param_gen_pos))
                   : 0;
  DxbcShaderTranslator::Modification vertex_shader_modification =
      pipeline_cache_->GetCurrentVertexShaderModification(
          *vertex_shader, primitive_processing_result.host_vertex_shader_type,
          interpolator_mask);
  DxbcShaderTranslator::Modification pixel_shader_modification =
      pixel_shader ? pipeline_cache_->GetCurrentPixelShaderModification(
                         *pixel_shader, interpolator_mask, ps_param_gen_pos,
                         normalized_depth_control)
                   : DxbcShaderTranslator::Modification(0);

  // Set up the render targets - this may perform dispatches and draws.
  uint32_t normalized_color_mask =
      pixel_shader ? draw_util::GetNormalizedColorMask(
                         regs, pixel_shader->writes_color_targets())
                   : 0;
  if (!render_target_cache_->Update(is_rasterization_done,
                                    normalized_depth_control,
                                    normalized_color_mask, *vertex_shader)) {
    return false;
  }

  // Create the pipeline (for this, need the actually used render target formats
  // from the render target cache), translating the shaders - doing this now to
  // obtain the used textures.
  D3D12Shader::D3D12Translation* vertex_shader_translation =
      static_cast<D3D12Shader::D3D12Translation*>(
          vertex_shader->GetOrCreateTranslation(
              vertex_shader_modification.value));
  D3D12Shader::D3D12Translation* pixel_shader_translation =
      pixel_shader ? static_cast<D3D12Shader::D3D12Translation*>(
                         pixel_shader->GetOrCreateTranslation(
                             pixel_shader_modification.value))
                   : nullptr;
  uint32_t bound_depth_and_color_render_target_bits;
  uint32_t bound_depth_and_color_render_target_formats
      [1 + xenos::kMaxColorRenderTargets];
  bool host_render_targets_used = render_target_cache_->GetPath() ==
                                  RenderTargetCache::Path::kHostRenderTargets;
  if (host_render_targets_used) {
    bound_depth_and_color_render_target_bits =
        render_target_cache_->GetLastUpdateBoundRenderTargets(
            render_target_cache_->gamma_render_target_as_srgb(),
            bound_depth_and_color_render_target_formats);
  } else {
    bound_depth_and_color_render_target_bits = 0;
  }
  void* pipeline_handle;
  ID3D12RootSignature* root_signature;
  if (!pipeline_cache_->ConfigurePipeline(
          vertex_shader_translation, pixel_shader_translation,
          primitive_processing_result, normalized_depth_control,
          normalized_color_mask, bound_depth_and_color_render_target_bits,
          bound_depth_and_color_render_target_formats, &pipeline_handle,
          &root_signature)) {
    return false;
  }

  // Update the textures - this may bind pipelines.
  uint32_t used_texture_mask =
      vertex_shader->GetUsedTextureMaskAfterTranslation() |
      (pixel_shader != nullptr
           ? pixel_shader->GetUsedTextureMaskAfterTranslation()
           : 0);
  texture_cache_->RequestTextures(used_texture_mask);

  // Bind the pipeline after configuring it and doing everything that may bind
  // other pipelines.
  if (current_guest_pipeline_ != pipeline_handle) {
    deferred_command_list_.SetPipelineStateHandle(
        reinterpret_cast<void*>(pipeline_handle));
    current_guest_pipeline_ = pipeline_handle;
    current_external_pipeline_ = nullptr;
  }

  // Get dynamic rasterizer state.
  uint32_t draw_resolution_scale_x = texture_cache_->draw_resolution_scale_x();
  uint32_t draw_resolution_scale_y = texture_cache_->draw_resolution_scale_y();
  draw_util::ViewportInfo viewport_info;
  draw_util::GetViewportInfoArgs gviargs{};

  gviargs.Setup(
      draw_resolution_scale_x, draw_resolution_scale_y,
      texture_cache_->draw_resolution_scale_x_divisor(),
      texture_cache_->draw_resolution_scale_y_divisor(), true,
      D3D12_VIEWPORT_BOUNDS_MAX, D3D12_VIEWPORT_BOUNDS_MAX, false,
      normalized_depth_control,
      host_render_targets_used &&
          render_target_cache_->depth_float24_convert_in_pixel_shader(),
      host_render_targets_used, pixel_shader && pixel_shader->writes_depth());
  gviargs.SetupRegisterValues(regs);

  if (gviargs == previous_viewport_info_args_) {
    viewport_info = previous_viewport_info_;
  } else {
    draw_util::GetHostViewportInfo(&gviargs, viewport_info);
    previous_viewport_info_args_ = gviargs;
    previous_viewport_info_ = viewport_info;
  }
  // todo: use SIMD for getscissor + scaling here, should reduce code size more
  draw_util::Scissor scissor;
  draw_util::GetScissor(regs, scissor);
#if XE_ARCH_AMD64 == 1
  __m128i* scisp = (__m128i*)&scissor;
  *scisp = _mm_mullo_epi32(
      *scisp, _mm_setr_epi32(draw_resolution_scale_x, draw_resolution_scale_y,
                             draw_resolution_scale_x, draw_resolution_scale_y));
#else
  scissor.offset[0] *= draw_resolution_scale_x;
  scissor.offset[1] *= draw_resolution_scale_y;
  scissor.extent[0] *= draw_resolution_scale_x;
  scissor.extent[1] *= draw_resolution_scale_y;
#endif
  // Update viewport, scissor, blend factor and stencil reference.
  UpdateFixedFunctionState(viewport_info, scissor, primitive_polygonal,
                           normalized_depth_control);

  // Update system constants before uploading them.
  // TODO(Triang3l): With ROV, pass the disabled render target mask for safety.
  UpdateSystemConstantValues(
      memexport_used, primitive_polygonal,
      primitive_processing_result.line_loop_closing_index,
      primitive_processing_result.host_shader_index_endian, viewport_info,
      used_texture_mask, normalized_depth_control, normalized_color_mask);

  // Update constant buffers, descriptors and root parameters.
  if (!UpdateBindings(vertex_shader, pixel_shader, root_signature,
                      memexport_used)) {
    return false;
  }
  // Must not call anything that can change the descriptor heap from now on!

  // Ensure vertex buffers are resident.
  // TODO(Triang3l): Cache residency for ranges in a way similar to how texture
  // validity is tracked.
  const Shader::ConstantRegisterMap& constant_map_vertex =
      vertex_shader->constant_register_map();
  {
    uint32_t vfetch_addresses[96];
    uint32_t vfetch_sizes[96];
    uint32_t vfetch_current_queued = 0;
    for (uint32_t i = 0;
         i < xe::countof(constant_map_vertex.vertex_fetch_bitmap); ++i) {
      uint32_t vfetch_bits_remaining =
          constant_map_vertex.vertex_fetch_bitmap[i];
      uint32_t j;
      while (xe::bit_scan_forward(vfetch_bits_remaining, &j)) {
        vfetch_bits_remaining = xe::clear_lowest_bit(vfetch_bits_remaining);
        uint32_t vfetch_index = i * 32 + j;
        xenos::xe_gpu_vertex_fetch_t vfetch_constant =
            regs.GetVertexFetch(vfetch_index);
        switch (vfetch_constant.type) {
          case xenos::FetchConstantType::kVertex:
            break;
          case xenos::FetchConstantType::kInvalidVertex:
            if (cvars::gpu_allow_invalid_fetch_constants) {
              break;
            }
            XELOGW(
                "Vertex fetch constant {} ({:08X} {:08X}) has \"invalid\" "
                "type! "
                "This is incorrect behavior, but you can try bypassing this by "
                "launching Xenia with "
                "--gpu_allow_invalid_fetch_constants=true.",
                vfetch_index, vfetch_constant.dword_0, vfetch_constant.dword_1);
            return false;
          default:
            XELOGW(
                "Vertex fetch constant {} ({:08X} {:08X}) is completely "
                "invalid!",
                vfetch_index, vfetch_constant.dword_0, vfetch_constant.dword_1);
            return false;
        }
        vfetch_addresses[vfetch_current_queued] = vfetch_constant.address;
        vfetch_sizes[vfetch_current_queued++] = vfetch_constant.size;
      }
    }

    if (vfetch_current_queued) {
      // so far, i have never seen vfetch_current_queued > 4. 1 is most common,
      // 2 happens occasionally. did not test many games though pre-acquire the
      // critical region so we're not repeatedly re-acquiring it in requestrange
      auto shared_memory_request_range_hoisted =
          global_critical_region::Acquire();

      for (uint32_t i = 0; i < vfetch_current_queued; ++i) {
        if (!shared_memory_->RequestRange(vfetch_addresses[i] << 2,
                                          vfetch_sizes[i] << 2)) {
          XELOGE(
              "Failed to request vertex buffer at 0x{:08X} (size {}) in the "
              "shared memory",
              vfetch_addresses[i] << 2, vfetch_sizes[i] << 2);
          return false;
        }
      }
    }
  }
  // Gather memexport ranges and ensure the heaps for them are resident, and
  // also load the data surrounding the export and to fill the regions that
  // won't be modified by the shaders.
  memexport_ranges_.clear();
  if (memexport_used_vertex) {
    draw_util::AddMemExportRanges(regs, *vertex_shader, memexport_ranges_);
  }
  if (memexport_used_pixel) {
    draw_util::AddMemExportRanges(regs, *pixel_shader, memexport_ranges_);
  }
  for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
    if (!shared_memory_->RequestRange(memexport_range.base_address_dwords << 2,
                                      memexport_range.size_bytes)) {
      XELOGE(
          "Failed to request memexport stream at 0x{:08X} (size {}) in the "
          "shared memory",
          memexport_range.base_address_dwords << 2, memexport_range.size_bytes);
      return false;
    }
  }
  // Primitive topology.
  D3D_PRIMITIVE_TOPOLOGY primitive_topology;
  if (primitive_processing_result.IsTessellated()) {
    switch (primitive_processing_result.host_primitive_type) {
      // TODO(Triang3l): Support all primitive types.
      case xenos::PrimitiveType::kTriangleList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
        break;
      case xenos::PrimitiveType::kQuadList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
        break;
      case xenos::PrimitiveType::kTrianglePatch:
        primitive_topology =
            (regs.Get<reg::VGT_HOS_CNTL>().tess_mode ==
             xenos::TessellationMode::kAdaptive)
                ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST
                : D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        break;
      case xenos::PrimitiveType::kQuadPatch:
        primitive_topology =
            (regs.Get<reg::VGT_HOS_CNTL>().tess_mode ==
             xenos::TessellationMode::kAdaptive)
                ? D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST
                : D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        break;
      default:
        XELOGE(
            "Host tessellated primitive type {} returned by the primitive "
            "processor is not supported by the Direct3D 12 command processor",
            uint32_t(primitive_processing_result.host_primitive_type));
        assert_unhandled_case(primitive_processing_result.host_primitive_type);
        return false;
    }
  } else {
    switch (primitive_processing_result.host_primitive_type) {
      case xenos::PrimitiveType::kPointList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        break;
      case xenos::PrimitiveType::kLineList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        break;
      case xenos::PrimitiveType::kLineStrip:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        break;
      case xenos::PrimitiveType::kTriangleList:
      case xenos::PrimitiveType::kRectangleList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        break;
      case xenos::PrimitiveType::kTriangleStrip:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        break;
      case xenos::PrimitiveType::kQuadList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
        break;
      default:
        XELOGE(
            "Host primitive type {} returned by the primitive processor is not "
            "supported by the Direct3D 12 command processor",
            uint32_t(primitive_processing_result.host_primitive_type));
        assert_unhandled_case(primitive_processing_result.host_primitive_type);
        return false;
    }
  }
  SetPrimitiveTopology(primitive_topology);
  // Must not call anything that may change the primitive topology from now on!

  // Draw.
  if (primitive_processing_result.index_buffer_type ==
      PrimitiveProcessor::ProcessedIndexBufferType::kNone) {
    if (!memexport_used) {
      shared_memory_->UseForReading();

    } else {
      shared_memory_->UseForWriting();
    }
    SubmitBarriers();
    deferred_command_list_.D3DDrawInstanced(
        primitive_processing_result.host_draw_vertex_count, 1, 0, 0);
  } else {
    D3D12_INDEX_BUFFER_VIEW index_buffer_view;
    index_buffer_view.SizeInBytes =
        primitive_processing_result.host_draw_vertex_count;
    if (primitive_processing_result.host_index_format ==
        xenos::IndexFormat::kInt16) {
      index_buffer_view.SizeInBytes *= sizeof(uint16_t);
      index_buffer_view.Format = DXGI_FORMAT_R16_UINT;
    } else {
      index_buffer_view.SizeInBytes *= sizeof(uint32_t);
      index_buffer_view.Format = DXGI_FORMAT_R32_UINT;
    }
    ID3D12Resource* scratch_index_buffer = nullptr;
    switch (primitive_processing_result.index_buffer_type) {
      case PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA: {
        if (memexport_used) {
          // If the shared memory is a UAV, it can't be used as an index buffer
          // (UAV is a read/write state, index buffer is a read-only state).
          // Need to copy the indices to a buffer in the index buffer state.
          scratch_index_buffer = RequestScratchGPUBuffer(
              index_buffer_view.SizeInBytes, D3D12_RESOURCE_STATE_COPY_DEST);
          if (scratch_index_buffer == nullptr) {
            return false;
          }
          shared_memory_->UseAsCopySource();
          SubmitBarriers();
          deferred_command_list_.D3DCopyBufferRegion(
              scratch_index_buffer, 0, shared_memory_->GetBuffer(),
              primitive_processing_result.guest_index_base,
              index_buffer_view.SizeInBytes);
          PushTransitionBarrier(scratch_index_buffer,
                                D3D12_RESOURCE_STATE_COPY_DEST,
                                D3D12_RESOURCE_STATE_INDEX_BUFFER);
          index_buffer_view.BufferLocation =
              scratch_index_buffer->GetGPUVirtualAddress();
        } else {
          index_buffer_view.BufferLocation =
              shared_memory_->GetGPUAddress() +
              primitive_processing_result.guest_index_base;
        }
      } break;
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostConverted:
        index_buffer_view.BufferLocation =
            primitive_processor_->GetConvertedIndexBufferGpuAddress(
                primitive_processing_result.host_index_buffer_handle);
        break;
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostBuiltinForAuto:
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostBuiltinForDMA:
        index_buffer_view.BufferLocation =
            primitive_processor_->GetBuiltinIndexBufferGpuAddress(
                primitive_processing_result.host_index_buffer_handle);
        break;
      default:
        assert_unhandled_case(primitive_processing_result.index_buffer_type);
        return false;
    }
    deferred_command_list_.D3DIASetIndexBuffer(&index_buffer_view);
    if (memexport_used) {
      shared_memory_->UseForWriting();
    } else {
      shared_memory_->UseForReading();
    }
    SubmitBarriers();
    deferred_command_list_.D3DDrawIndexedInstanced(
        primitive_processing_result.host_draw_vertex_count, 1, 0, 0, 0);
    if (scratch_index_buffer != nullptr) {
      ReleaseScratchGPUBuffer(scratch_index_buffer,
                              D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
  }

  if (memexport_used) {
    // Make sure this memexporting draw is ordered with other work using shared
    // memory as a UAV.
    // TODO(Triang3l): Find some PM4 command that can be used for indication of
    // when memexports should be awaited?
    shared_memory_->MarkUAVWritesCommitNeeded();
    // Invalidate textures in memexported memory and watch for changes.
    for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
      shared_memory_->RangeWrittenByGpu(
          memexport_range.base_address_dwords << 2, memexport_range.size_bytes,
          false);
    }
    if (GetGPUSetting(GPUSetting::ReadbackResolve)) {
      // Read the exported data on the CPU.
      uint32_t memexport_total_size = 0;
      for (const draw_util::MemExportRange& memexport_range :
           memexport_ranges_) {
        memexport_total_size += memexport_range.size_bytes;
      }
      if (memexport_total_size != 0) {
        ID3D12Resource* readback_buffer =
            RequestReadbackBuffer(memexport_total_size);
        if (readback_buffer != nullptr) {
          shared_memory_->UseAsCopySource();
          SubmitBarriers();
          ID3D12Resource* shared_memory_buffer = shared_memory_->GetBuffer();
          uint32_t readback_buffer_offset = 0;
          for (const draw_util::MemExportRange& memexport_range :
               memexport_ranges_) {
            uint32_t memexport_range_size = memexport_range.size_bytes;
            deferred_command_list_.D3DCopyBufferRegion(
                readback_buffer, readback_buffer_offset, shared_memory_buffer,
                memexport_range.base_address_dwords << 2, memexport_range_size);
            readback_buffer_offset += memexport_range_size;
          }
          if (AwaitAllQueueOperationsCompletion()) {
            D3D12_RANGE readback_range;
            readback_range.Begin = 0;
            readback_range.End = memexport_total_size;
            void* readback_mapping;
            if (SUCCEEDED(readback_buffer->Map(0, &readback_range,
                                               &readback_mapping))) {
              const uint8_t* readback_bytes =
                  reinterpret_cast<const uint8_t*>(readback_mapping);
              for (const draw_util::MemExportRange& memexport_range :
                   memexport_ranges_) {
                std::memcpy(memory_->TranslatePhysical(
                                memexport_range.base_address_dwords << 2),
                            readback_bytes, memexport_range.size_bytes);
                readback_bytes += memexport_range.size_bytes;
              }
              D3D12_RANGE readback_write_range = {};
              readback_buffer->Unmap(0, &readback_write_range);
            }
          }
        }
      }
    }
  }

  return true;
}

void D3D12CommandProcessor::InitializeTrace() {
  CommandProcessor::InitializeTrace();

  if (!BeginSubmission(false)) {
    return;
  }
  bool render_target_cache_submitted =
      render_target_cache_->InitializeTraceSubmitDownloads();
  bool shared_memory_submitted =
      shared_memory_->InitializeTraceSubmitDownloads();
  if (!render_target_cache_submitted && !shared_memory_submitted) {
    return;
  }
  AwaitAllQueueOperationsCompletion();
  if (render_target_cache_submitted) {
    render_target_cache_->InitializeTraceCompleteDownloads();
  }
  if (shared_memory_submitted) {
    shared_memory_->InitializeTraceCompleteDownloads();
  }
}

bool D3D12CommandProcessor::IssueCopy() {
#if XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
  if (!BeginSubmission(true)) {
    return false;
  }
  if (!GetGPUSetting(GPUSetting::ReadbackResolve)) {
    uint32_t written_address, written_length;
    return render_target_cache_->Resolve(*memory_, *shared_memory_,
                                         *texture_cache_, written_address,
                                         written_length);
  } else {
    return IssueCopy_ReadbackResolvePath();
  }
  return true;
}
XE_NOINLINE
bool D3D12CommandProcessor::IssueCopy_ReadbackResolvePath() {
  uint32_t written_address, written_length;
  if (render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_,
                                    written_address, written_length)) {
    if (!texture_cache_->IsDrawResolutionScaled() && written_length) {
      // Read the resolved data on the CPU.
      ID3D12Resource* readback_buffer = RequestReadbackBuffer(written_length);
      if (readback_buffer != nullptr) {
        shared_memory_->UseAsCopySource();
        SubmitBarriers();
        ID3D12Resource* shared_memory_buffer = shared_memory_->GetBuffer();
        deferred_command_list_.D3DCopyBufferRegion(
            readback_buffer, 0, shared_memory_buffer, written_address,
            written_length);
        if (AwaitAllQueueOperationsCompletion()) {
          D3D12_RANGE readback_range;
          readback_range.Begin = 0;
          readback_range.End = written_length;
          void* readback_mapping;
          if (SUCCEEDED(readback_buffer->Map(0, &readback_range,
                                             &readback_mapping))) {
            // chrispy: this memcpy needs to be optimized as much as possible

            auto physaddr = memory_->TranslatePhysical(written_address);
            memory::vastcpy(physaddr, (uint8_t*)readback_mapping,
                            written_length);
            D3D12_RANGE readback_write_range = {};
            readback_buffer->Unmap(0, &readback_write_range);
          }
        }
      }
    }
  } else {
    return false;
  }
  return true;
}
void D3D12CommandProcessor::CheckSubmissionFence(uint64_t await_submission) {
  if (await_submission >= submission_current_) {
    if (submission_open_) {
      EndSubmission(false);
    }
    // Ending an open submission should result in queue operations done directly
    // (like UpdateTileMappings) to be tracked within the scope of that
    // submission, but just in case of a failure, or queue operations being done
    // outside of a submission, await explicitly.
    if (queue_operations_done_since_submission_signal_) {
      UINT64 fence_value = ++queue_operations_since_submission_fence_last_;
      ID3D12CommandQueue* direct_queue = GetD3D12Provider().GetDirectQueue();
      if (SUCCEEDED(direct_queue->Signal(
              queue_operations_since_submission_fence_, fence_value)) &&
          SUCCEEDED(
              queue_operations_since_submission_fence_->SetEventOnCompletion(
                  fence_value, nullptr))) {
        queue_operations_done_since_submission_signal_ = false;
      } else {
        XELOGE(
            "Failed to await an out-of-submission queue operation completion "
            "Direct3D 12 fence");
      }
    }
    // A submission won't be ended if it hasn't been started, or if ending
    // has failed - clamp the index.
    await_submission = submission_current_ - 1;
  }

  uint64_t submission_completed_before = submission_completed_;
  submission_completed_ = submission_fence_->GetCompletedValue();
  if (submission_completed_ < await_submission) {
    if (SUCCEEDED(submission_fence_->SetEventOnCompletion(await_submission,
                                                          nullptr))) {
      submission_completed_ = submission_fence_->GetCompletedValue();
    }
  }
  if (submission_completed_ < await_submission) {
    XELOGE("Failed to await a submission completion Direct3D 12 fence");
  }
  if (submission_completed_ <= submission_completed_before) {
    // Not updated - no need to reclaim or download things.
    return;
  }

  // Reclaim command allocators.
  while (command_allocator_submitted_first_) {
    if (command_allocator_submitted_first_->last_usage_submission >
        submission_completed_) {
      break;
    }
    if (command_allocator_writable_last_) {
      command_allocator_writable_last_->next =
          command_allocator_submitted_first_;
    } else {
      command_allocator_writable_first_ = command_allocator_submitted_first_;
    }
    command_allocator_writable_last_ = command_allocator_submitted_first_;
    command_allocator_submitted_first_ =
        command_allocator_submitted_first_->next;
    command_allocator_writable_last_->next = nullptr;
  }
  if (!command_allocator_submitted_first_) {
    command_allocator_submitted_last_ = nullptr;
  }

  // Release single-use bindless descriptors.
  while (!view_bindless_one_use_descriptors_.empty()) {
    if (view_bindless_one_use_descriptors_.front().second >
        submission_completed_) {
      break;
    }
    ReleaseViewBindlessDescriptorImmediately(
        view_bindless_one_use_descriptors_.front().first);
    view_bindless_one_use_descriptors_.pop_front();
  }

  // Delete transient resources marked for deletion.
  while (!resources_for_deletion_.empty()) {
    if (resources_for_deletion_.front().first > submission_completed_) {
      break;
    }
    resources_for_deletion_.front().second->Release();
    resources_for_deletion_.pop_front();
  }

  shared_memory_->CompletedSubmissionUpdated();

  render_target_cache_->CompletedSubmissionUpdated();

  primitive_processor_->CompletedSubmissionUpdated();

  texture_cache_->CompletedSubmissionUpdated(submission_completed_);
}

bool D3D12CommandProcessor::BeginSubmission(bool is_guest_command) {
#if XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES

  if (device_removed_) {
    return false;
  }

  bool is_opening_frame = is_guest_command && !frame_open_;
  if (submission_open_ && !is_opening_frame) {
    return true;
  }

  // Check if the device is still available.
  ID3D12Device* device = GetD3D12Provider().GetDevice();
  HRESULT device_removed_reason = device->GetDeviceRemovedReason();
  if (FAILED(device_removed_reason)) {
    device_removed_ = true;
    graphics_system_->OnHostGpuLossFromAnyThread(device_removed_reason !=
                                                 DXGI_ERROR_DEVICE_REMOVED);
    return false;
  }

  // Check the fence - needed for all kinds of submissions (to reclaim transient
  // resources early) and specifically for frames (not to queue too many), and
  // await the availability of the current frame.
  CheckSubmissionFence(
      is_opening_frame
          ? closed_frame_submissions_[frame_current_ % kQueueFrames]
          : 0);
  // TODO(Triang3l): If failed to await (completed submission < awaited frame
  // submission), do something like dropping the draw command that wanted to
  // open the frame.
  if (is_opening_frame) {
    // Update the completed frame index, also obtaining the actual completed
    // frame number (since the CPU may be actually less than 3 frames behind)
    // before reclaiming resources tracked with the frame number.
    frame_completed_ =
        std::max(frame_current_, uint64_t(kQueueFrames)) - kQueueFrames;
    for (uint64_t frame = frame_completed_ + 1; frame < frame_current_;
         ++frame) {
      if (closed_frame_submissions_[frame % kQueueFrames] >
          submission_completed_) {
        break;
      }
      frame_completed_ = frame;
    }
  }

  if (!submission_open_) {
    submission_open_ = true;

    // Start a new deferred command list - will submit it to the real one in the
    // end of the submission (when async pipeline creation requests are
    // fulfilled).
    deferred_command_list_.Reset();

    // Reset cached state of the command list.
    ff_viewport_update_needed_ = true;
    ff_scissor_update_needed_ = true;
    ff_blend_factor_update_needed_ = true;
    ff_stencil_ref_update_needed_ = true;
    current_guest_pipeline_ = nullptr;
    current_external_pipeline_ = nullptr;
    current_graphics_root_signature_ = nullptr;
    current_graphics_root_up_to_date_ = 0;
    if (bindless_resources_used_) {
      deferred_command_list_.SetDescriptorHeaps(view_bindless_heap_,
                                                sampler_bindless_heap_current_);
    } else {
      view_bindful_heap_current_ = nullptr;
      sampler_bindful_heap_current_ = nullptr;
    }
    primitive_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    render_target_cache_->BeginSubmission();

    primitive_processor_->BeginSubmission();

    texture_cache_->BeginSubmission(submission_current_);
  }

  if (is_opening_frame) {
    frame_open_ = true;

    // Reset bindings that depend on the data stored in the pools.
    std::memset(current_float_constant_map_vertex_, 0,
                sizeof(current_float_constant_map_vertex_));
    std::memset(current_float_constant_map_pixel_, 0,
                sizeof(current_float_constant_map_pixel_));
    cbuffer_binding_system_.up_to_date = false;
    cbuffer_binding_float_vertex_.up_to_date = false;
    cbuffer_binding_float_pixel_.up_to_date = false;
    cbuffer_binding_bool_loop_.up_to_date = false;
    cbuffer_binding_fetch_.up_to_date = false;
    current_shared_memory_binding_is_uav_.reset();
    if (bindless_resources_used_) {
      cbuffer_binding_descriptor_indices_vertex_.up_to_date = false;
      cbuffer_binding_descriptor_indices_pixel_.up_to_date = false;
    } else {
      draw_view_bindful_heap_index_ =
          ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid;
      draw_sampler_bindful_heap_index_ =
          ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid;
      bindful_textures_written_vertex_ = false;
      bindful_textures_written_pixel_ = false;
      bindful_samplers_written_vertex_ = false;
      bindful_samplers_written_pixel_ = false;
    }

    // Reclaim pool pages - no need to do this every small submission since some
    // may be reused.
    constant_buffer_pool_->Reclaim(frame_completed_);
    if (!bindless_resources_used_) {
      view_bindful_heap_pool_->Reclaim(frame_completed_);
      sampler_bindful_heap_pool_->Reclaim(frame_completed_);
    }

    pix_capturing_ =
        pix_capture_requested_.exchange(false, std::memory_order_relaxed);
    if (pix_capturing_) {
      IDXGraphicsAnalysis* graphics_analysis =
          GetD3D12Provider().GetGraphicsAnalysis();
      if (graphics_analysis != nullptr) {
        graphics_analysis->BeginCapture();
      }
    }

    primitive_processor_->BeginFrame();

    texture_cache_->BeginFrame();
  }

  return true;
}

bool D3D12CommandProcessor::EndSubmission(bool is_swap) {
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();

  // Make sure there is a command allocator to write commands to.
  if (submission_open_ && !command_allocator_writable_first_) {
    ID3D12CommandAllocator* command_allocator;
    if (FAILED(provider.GetDevice()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&command_allocator)))) {
      XELOGE("Failed to create a command allocator");
      // Try to submit later. Completely dropping the submission is not
      // permitted because resources would be left in an undefined state.
      return false;
    }
    command_allocator_writable_first_ = new CommandAllocator;
    command_allocator_writable_first_->command_allocator = command_allocator;
    command_allocator_writable_first_->last_usage_submission = 0;
    command_allocator_writable_first_->next = nullptr;
    command_allocator_writable_last_ = command_allocator_writable_first_;
  }

  bool is_closing_frame = is_swap && frame_open_;

  if (is_closing_frame) {
    texture_cache_->EndFrame();

    primitive_processor_->EndFrame();
  }

  if (submission_open_) {
    assert_false(scratch_buffer_used_);

    pipeline_cache_->EndSubmission();

    // Submit barriers now because resources with the queued barriers may be
    // destroyed between frames.
    SubmitBarriers();

    ID3D12CommandQueue* direct_queue = provider.GetDirectQueue();

    // Submit the deferred command list.
    // Only one deferred command list must be executed in the same
    // ExecuteCommandLists - the boundaries of ExecuteCommandLists are a full
    // UAV and aliasing barrier, and subsystems of the emulator assume it
    // happens between Xenia submissions.
    ID3D12CommandAllocator* command_allocator =
        command_allocator_writable_first_->command_allocator;
    command_allocator->Reset();
    command_list_->Reset(command_allocator, nullptr);
    deferred_command_list_.Execute(command_list_, command_list_1_);
    command_list_->Close();
    ID3D12CommandList* execute_command_lists[] = {command_list_};
    direct_queue->ExecuteCommandLists(1, execute_command_lists);
    command_allocator_writable_first_->last_usage_submission =
        submission_current_;
    if (command_allocator_submitted_last_) {
      command_allocator_submitted_last_->next =
          command_allocator_writable_first_;
    } else {
      command_allocator_submitted_first_ = command_allocator_writable_first_;
    }
    command_allocator_submitted_last_ = command_allocator_writable_first_;
    command_allocator_writable_first_ = command_allocator_writable_first_->next;
    command_allocator_submitted_last_->next = nullptr;
    if (!command_allocator_writable_first_) {
      command_allocator_writable_last_ = nullptr;
    }

    direct_queue->Signal(submission_fence_, submission_current_++);

    submission_open_ = false;

    // Queue operations done directly (like UpdateTileMappings) will be awaited
    // alongside the last submission if needed.
    queue_operations_done_since_submission_signal_ = false;
  }

  if (is_closing_frame) {
    if (cvars::clear_memory_page_state) {
      shared_memory_->SetSystemPageBlocksValidWithGpuDataWritten();
    }
    // Close the capture after submitting.
    if (pix_capturing_) {
      IDXGraphicsAnalysis* graphics_analysis = provider.GetGraphicsAnalysis();
      if (graphics_analysis != nullptr) {
        graphics_analysis->EndCapture();
      }
      pix_capturing_ = false;
    }
    frame_open_ = false;
    // Submission already closed now, so minus 1.
    closed_frame_submissions_[(frame_current_++) % kQueueFrames] =
        submission_current_ - 1;

    if (cache_clear_requested_ && AwaitAllQueueOperationsCompletion()) {
      cache_clear_requested_ = false;

      ClearCommandAllocatorCache();

      ui::d3d12::util::ReleaseAndNull(scratch_buffer_);
      scratch_buffer_size_ = 0;

      if (bindless_resources_used_) {
        texture_cache_bindless_sampler_map_.clear();
        for (const auto& sampler_bindless_heap_overflowed :
             sampler_bindless_heaps_overflowed_) {
          sampler_bindless_heap_overflowed.first->Release();
        }
        sampler_bindless_heaps_overflowed_.clear();
        sampler_bindless_heap_allocated_ = 0;
      } else {
        sampler_bindful_heap_pool_->ClearCache();
        view_bindful_heap_pool_->ClearCache();
      }
      constant_buffer_pool_->ClearCache();

      texture_cache_->ClearCache();

      // Not clearing the root signatures as they're referenced by pipelines,
      // which are not destroyed.

      primitive_processor_->ClearCache();

      render_target_cache_->ClearCache();

      shared_memory_->ClearCache();
    }
  }

  return true;
}

bool D3D12CommandProcessor::CanEndSubmissionImmediately() const {
  return !submission_open_ || !pipeline_cache_->IsCreatingPipelines();
}

void D3D12CommandProcessor::ClearCommandAllocatorCache() {
  while (command_allocator_submitted_first_) {
    auto next = command_allocator_submitted_first_->next;
    command_allocator_submitted_first_->command_allocator->Release();
    delete command_allocator_submitted_first_;
    command_allocator_submitted_first_ = next;
  }
  command_allocator_submitted_last_ = nullptr;
  while (command_allocator_writable_first_) {
    auto next = command_allocator_writable_first_->next;
    command_allocator_writable_first_->command_allocator->Release();
    delete command_allocator_writable_first_;
    command_allocator_writable_first_ = next;
  }
  command_allocator_writable_last_ = nullptr;
}

void D3D12CommandProcessor::UpdateFixedFunctionState(
    const draw_util::ViewportInfo& viewport_info,
    const draw_util::Scissor& scissor, bool primitive_polygonal,
    reg::RB_DEPTHCONTROL normalized_depth_control) {
#if XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES

  // Viewport.
  D3D12_VIEWPORT viewport;
  viewport.TopLeftX = float(viewport_info.xy_offset[0]);
  viewport.TopLeftY = float(viewport_info.xy_offset[1]);
  viewport.Width = float(viewport_info.xy_extent[0]);
  viewport.Height = float(viewport_info.xy_extent[1]);
  viewport.MinDepth = viewport_info.z_min;
  viewport.MaxDepth = viewport_info.z_max;
  SetViewport(viewport);

  // Scissor.
  D3D12_RECT scissor_rect;
  scissor_rect.left = LONG(scissor.offset[0]);
  scissor_rect.top = LONG(scissor.offset[1]);
  scissor_rect.right = LONG(scissor.offset[0] + scissor.extent[0]);
  scissor_rect.bottom = LONG(scissor.offset[1] + scissor.extent[1]);
  SetScissorRect(scissor_rect);

  if (render_target_cache_->GetPath() ==
      RenderTargetCache::Path::kHostRenderTargets) {
    const RegisterFile& regs = *register_file_;

    // Blend factor.
    float blend_factor[] = {
        regs.Get<float>(XE_GPU_REG_RB_BLEND_RED),
        regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN),
        regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE),
        regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA),
    };
    // std::memcmp instead of != so in case of NaN, every draw won't be
    // invalidating it.
    ff_blend_factor_update_needed_ |=
        std::memcmp(ff_blend_factor_, blend_factor, sizeof(float) * 4) != 0;
    if (ff_blend_factor_update_needed_) {
      std::memcpy(ff_blend_factor_, blend_factor, sizeof(float) * 4);
      deferred_command_list_.D3DOMSetBlendFactor(ff_blend_factor_);
      ff_blend_factor_update_needed_ = false;
    }

    // Stencil reference value. Per-face reference not supported by Direct3D 12,
    // choose the back face one only if drawing only back faces.
    Register stencil_ref_mask_reg;
    auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
    if (primitive_polygonal && normalized_depth_control.backface_enable &&
        pa_su_sc_mode_cntl.cull_front && !pa_su_sc_mode_cntl.cull_back) {
      stencil_ref_mask_reg = XE_GPU_REG_RB_STENCILREFMASK_BF;
    } else {
      stencil_ref_mask_reg = XE_GPU_REG_RB_STENCILREFMASK;
    }
    uint32_t stencil_ref =
        regs.Get<reg::RB_STENCILREFMASK>(stencil_ref_mask_reg).stencilref;
    ff_stencil_ref_update_needed_ |= ff_stencil_ref_ != stencil_ref;
    if (ff_stencil_ref_update_needed_) {
      ff_stencil_ref_ = stencil_ref;
      deferred_command_list_.D3DOMSetStencilRef(ff_stencil_ref_);
      ff_stencil_ref_update_needed_ = false;
    }
  }
}
template <bool primitive_polygonal, bool edram_rov_used>
XE_NOINLINE void D3D12CommandProcessor::UpdateSystemConstantValues_Impl(
    bool shared_memory_is_uav, uint32_t line_loop_closing_index,
    xenos::Endian index_endian, const draw_util::ViewportInfo& viewport_info,
    uint32_t used_texture_mask, reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask) {
  const RegisterFile& regs = *register_file_;
  auto pa_cl_clip_cntl = regs.Get<reg::PA_CL_CLIP_CNTL>();
  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  auto rb_alpha_ref = regs.Get<float>(XE_GPU_REG_RB_ALPHA_REF);
  auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
  auto rb_stencilrefmask = regs.Get<reg::RB_STENCILREFMASK>();
  auto rb_stencilrefmask_bf =
      regs.Get<reg::RB_STENCILREFMASK>(XE_GPU_REG_RB_STENCILREFMASK_BF);
  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  auto sq_context_misc = regs.Get<reg::SQ_CONTEXT_MISC>();
  auto sq_program_cntl = regs.Get<reg::SQ_PROGRAM_CNTL>();
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  uint32_t vgt_indx_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t vgt_max_vtx_indx = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  uint32_t vgt_min_vtx_indx = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;

  uint32_t draw_resolution_scale_x = texture_cache_->draw_resolution_scale_x();
  uint32_t draw_resolution_scale_y = texture_cache_->draw_resolution_scale_y();

  // Get the color info register values for each render target. Also, for ROV,
  // exclude components that don't exist in the format from the write mask.
  // Don't exclude fully overlapping render targets, however - two render
  // targets with the same base address are used in the lighting pass of
  // 4D5307E6, for example, with the needed one picked with dynamic control
  // flow.
  reg::RB_COLOR_INFO color_infos[4];
  float rt_clamp[4][4];
  // Two UINT32_MAX if no components actually existing in the RT are written.
  uint32_t rt_keep_masks[4][2];
  for (uint32_t i = 0; i < 4; ++i) {
    auto color_info = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[i]);
    color_infos[i] = color_info;
    if (edram_rov_used) {
      RenderTargetCache::GetPSIColorFormatInfo(
          color_info.color_format, (normalized_color_mask >> (i * 4)) & 0b1111,
          rt_clamp[i][0], rt_clamp[i][1], rt_clamp[i][2], rt_clamp[i][3],
          rt_keep_masks[i][0], rt_keep_masks[i][1]);
    }
  }

  // Disable depth and stencil if it aliases a color render target (for
  // instance, during the XBLA logo in 58410954, though depth writing is already
  // disabled there).
  bool depth_stencil_enabled = normalized_depth_control.stencil_enable ||
                               normalized_depth_control.z_enable;
  if (edram_rov_used && depth_stencil_enabled) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (rb_depth_info.depth_base == color_infos[i].color_base &&
          (rt_keep_masks[i][0] != UINT32_MAX ||
           rt_keep_masks[i][1] != UINT32_MAX)) {
        depth_stencil_enabled = false;
        break;
      }
    }
  }

  uint32_t dirty = 0u;
  ArchFloatMask dirty_float_mask = floatmask_zero;

  auto update_dirty_floatmask = [&dirty_float_mask](float x, float y) {
    dirty_float_mask =
        ArchORFloatMask(dirty_float_mask, ArchCmpneqFloatMask(x, y));
  };
  /*
        chrispy: instead of (cmp x, y; setnz lobyte; or mask, lobyte;
        we can do (xor z, x, y; or mask, z)
        this ought to have much better throughput on all processors
  */
  auto update_dirty_uint32_cmp = [&dirty](uint32_t x, uint32_t y) {
    dirty |= (x ^ y);
  };

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
  if (pa_cl_vte_cntl.vtx_xy_fmt) {
    flags |= DxbcShaderTranslator::kSysFlag_XYDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_z_fmt) {
    flags |= DxbcShaderTranslator::kSysFlag_ZDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_w0_fmt) {
    flags |= DxbcShaderTranslator::kSysFlag_WNotReciprocal;
  }
  // Whether the primitive is polygonal and SV_IsFrontFace matters.
  if constexpr (primitive_polygonal) {
    flags |= DxbcShaderTranslator::kSysFlag_PrimitivePolygonal;
  }
  // Primitive type.
  if (draw_util::IsPrimitiveLine(regs)) {
    flags |= DxbcShaderTranslator::kSysFlag_PrimitiveLine;
  }
  // Depth format.
  if (rb_depth_info.depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
    flags |= DxbcShaderTranslator::kSysFlag_DepthFloat24;
  }
  // Alpha test.
  xenos::CompareFunction alpha_test_function =
      rb_colorcontrol.alpha_test_enable ? rb_colorcontrol.alpha_func
                                        : xenos::CompareFunction::kAlways;
  flags |= uint32_t(alpha_test_function)
           << DxbcShaderTranslator::kSysFlag_AlphaPassIfLess_Shift;
  // Gamma writing.
  if (!render_target_cache_->gamma_render_target_as_srgb()) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (color_infos[i].color_format ==
          xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {
        flags |= DxbcShaderTranslator::kSysFlag_ConvertColor0ToGamma << i;
      }
    }
  }
  if constexpr (edram_rov_used) {
    if (depth_stencil_enabled) {
      flags |= DxbcShaderTranslator::kSysFlag_ROVDepthStencil;
      if (normalized_depth_control.z_enable) {
        flags |= uint32_t(normalized_depth_control.zfunc)
                 << DxbcShaderTranslator::kSysFlag_ROVDepthPassIfLess_Shift;
        if (normalized_depth_control.z_write_enable) {
          flags |= DxbcShaderTranslator::kSysFlag_ROVDepthWrite;
        }
      } else {
        // In case stencil is used without depth testing - always pass, and
        // don't modify the stored depth.
        flags |= DxbcShaderTranslator::kSysFlag_ROVDepthPassIfLess |
                 DxbcShaderTranslator::kSysFlag_ROVDepthPassIfEqual |
                 DxbcShaderTranslator::kSysFlag_ROVDepthPassIfGreater;
      }
      if (normalized_depth_control.stencil_enable) {
        flags |= DxbcShaderTranslator::kSysFlag_ROVStencilTest;
      }
      // Hint - if not applicable to the shader, will not have effect.
      if (alpha_test_function == xenos::CompareFunction::kAlways &&
          !rb_colorcontrol.alpha_to_mask_enable) {
        flags |= DxbcShaderTranslator::kSysFlag_ROVDepthStencilEarlyWrite;
      }
    }
  }
  update_dirty_uint32_cmp(system_constants_.flags, flags);
  system_constants_.flags = flags;

  // Tessellation factor range, plus 1.0 according to the images in
  // https://www.slideshare.net/blackdevilvikas/next-generation-graphics-programming-on-xbox-360
  auto tessellation_factor_min =
      regs.Get<float>(XE_GPU_REG_VGT_HOS_MIN_TESS_LEVEL) + 1.0f;
  auto tessellation_factor_max =
      regs.Get<float>(XE_GPU_REG_VGT_HOS_MAX_TESS_LEVEL) + 1.0f;

  update_dirty_floatmask(system_constants_.tessellation_factor_range_min,
                         tessellation_factor_min);

  system_constants_.tessellation_factor_range_min = tessellation_factor_min;
  update_dirty_floatmask(system_constants_.tessellation_factor_range_max,
                         tessellation_factor_max);
  system_constants_.tessellation_factor_range_max = tessellation_factor_max;

  // Line loop closing index (or 0 when drawing other primitives or using an
  // index buffer).

  update_dirty_uint32_cmp(system_constants_.line_loop_closing_index,
                          line_loop_closing_index);
  system_constants_.line_loop_closing_index = line_loop_closing_index;

  // Index or tessellation edge factor buffer endianness.
  update_dirty_uint32_cmp(
      static_cast<uint32_t>(system_constants_.vertex_index_endian),
      static_cast<uint32_t>(index_endian));
  system_constants_.vertex_index_endian = index_endian;

  // Vertex index offset.

  update_dirty_uint32_cmp(system_constants_.vertex_index_offset,
                          vgt_indx_offset);
  system_constants_.vertex_index_offset = vgt_indx_offset;

  // Vertex index range.

  update_dirty_uint32_cmp(system_constants_.vertex_index_min, vgt_min_vtx_indx);
  update_dirty_uint32_cmp(system_constants_.vertex_index_max, vgt_max_vtx_indx);

  system_constants_.vertex_index_min = vgt_min_vtx_indx;
  system_constants_.vertex_index_max = vgt_max_vtx_indx;

  // User clip planes (UCP_ENA_#), when not CLIP_DISABLE.
  // The shader knows only the total count - tightly packing the user clip
  // planes that are actually used.
  if (!pa_cl_clip_cntl.clip_disable) {
    float* user_clip_plane_write_ptr = system_constants_.user_clip_planes[0];
    uint32_t user_clip_planes_remaining = pa_cl_clip_cntl.ucp_ena;
    uint32_t user_clip_plane_index;
    while (xe::bit_scan_forward(user_clip_planes_remaining,
                                &user_clip_plane_index)) {
      user_clip_planes_remaining =
          xe::clear_lowest_bit(user_clip_planes_remaining);
      const void* user_clip_plane_regs =
          &regs[XE_GPU_REG_PA_CL_UCP_0_X + user_clip_plane_index * 4];
      if (std::memcmp(user_clip_plane_write_ptr, user_clip_plane_regs,
                      4 * sizeof(float))) {
        dirty = true;
        std::memcpy(user_clip_plane_write_ptr, user_clip_plane_regs,
                    4 * sizeof(float));
      }
      user_clip_plane_write_ptr += 4;
    }
  }

  // Conversion to Direct3D 12 normalized device coordinates.
  for (uint32_t i = 0; i < 3; ++i) {
    update_dirty_floatmask(system_constants_.ndc_scale[i],
                           viewport_info.ndc_scale[i]);

    update_dirty_floatmask(system_constants_.ndc_offset[i],
                           viewport_info.ndc_offset[i]);

    system_constants_.ndc_scale[i] = viewport_info.ndc_scale[i];
    system_constants_.ndc_offset[i] = viewport_info.ndc_offset[i];
  }

  // Point size.
  if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
    auto pa_su_point_minmax = regs.Get<reg::PA_SU_POINT_MINMAX>();
    auto pa_su_point_size = regs.Get<reg::PA_SU_POINT_SIZE>();
    float point_vertex_diameter_min =
        float(pa_su_point_minmax.min_size) * (2.0f / 16.0f);
    float point_vertex_diameter_max =
        float(pa_su_point_minmax.max_size) * (2.0f / 16.0f);
    float point_constant_diameter_x =
        float(pa_su_point_size.width) * (2.0f / 16.0f);
    float point_constant_diameter_y =
        float(pa_su_point_size.height) * (2.0f / 16.0f);

    update_dirty_floatmask(system_constants_.point_vertex_diameter_min,
                           point_vertex_diameter_min);

    update_dirty_floatmask(system_constants_.point_vertex_diameter_max,
                           point_vertex_diameter_max);

    update_dirty_floatmask(system_constants_.point_constant_diameter[0],
                           point_constant_diameter_x);
    update_dirty_floatmask(system_constants_.point_constant_diameter[1],
                           point_constant_diameter_y);

    system_constants_.point_vertex_diameter_min = point_vertex_diameter_min;
    system_constants_.point_vertex_diameter_max = point_vertex_diameter_max;
    system_constants_.point_constant_diameter[0] = point_constant_diameter_x;
    system_constants_.point_constant_diameter[1] = point_constant_diameter_y;
    // 2 because 1 in the NDC is half of the viewport's axis, 0.5 for diameter
    // to radius conversion to avoid multiplying the per-vertex diameter by an
    // additional constant in the shader.
    float point_screen_diameter_to_ndc_radius_x =
        (/* 0.5f * 2.0f * */ float(draw_resolution_scale_x)) /
        std::max(viewport_info.xy_extent[0], uint32_t(1));
    float point_screen_diameter_to_ndc_radius_y =
        (/* 0.5f * 2.0f * */ float(draw_resolution_scale_y)) /
        std::max(viewport_info.xy_extent[1], uint32_t(1));

    update_dirty_floatmask(
        system_constants_.point_screen_diameter_to_ndc_radius[0],
        point_screen_diameter_to_ndc_radius_x);

    update_dirty_floatmask(
        system_constants_.point_screen_diameter_to_ndc_radius[1],
        point_screen_diameter_to_ndc_radius_y);

    system_constants_.point_screen_diameter_to_ndc_radius[0] =
        point_screen_diameter_to_ndc_radius_x;
    system_constants_.point_screen_diameter_to_ndc_radius[1] =
        point_screen_diameter_to_ndc_radius_y;
  }

  // Texture signedness / gamma.
  bool gamma_render_target_as_srgb =
      render_target_cache_->gamma_render_target_as_srgb();
  uint32_t textures_resolved = 0;
  uint32_t textures_remaining = used_texture_mask;
  uint32_t texture_index;
  while (xe::bit_scan_forward(textures_remaining, &texture_index)) {
    textures_remaining = xe::clear_lowest_bit(textures_remaining);
    uint32_t& texture_signs_uint =
        system_constants_.texture_swizzled_signs[texture_index >> 2];
    uint32_t texture_signs_shift = (texture_index & 3) * 8;
    uint8_t texture_signs =
        texture_cache_->GetActiveTextureSwizzledSigns(texture_index);
    uint32_t texture_signs_shifted = uint32_t(texture_signs)
                                     << texture_signs_shift;
    uint32_t texture_signs_mask = uint32_t(0b11111111) << texture_signs_shift;

    update_dirty_uint32_cmp((texture_signs_uint & texture_signs_mask),
                            texture_signs_shifted);

    texture_signs_uint =
        (texture_signs_uint & ~texture_signs_mask) | texture_signs_shifted;
    // cache misses here, we're accessing the texture bindings out of order
    textures_resolved |=
        uint32_t(texture_cache_->IsActiveTextureResolved(texture_index))
        << texture_index;
  }

  update_dirty_uint32_cmp(system_constants_.textures_resolved,
                          textures_resolved);
  system_constants_.textures_resolved = textures_resolved;

  // Log2 of sample count, for alpha to mask and with ROV, for EDRAM address
  // calculation with MSAA.
  uint32_t sample_count_log2_x =
      rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X ? 1 : 0;
  uint32_t sample_count_log2_y =
      rb_surface_info.msaa_samples >= xenos::MsaaSamples::k2X ? 1 : 0;

  update_dirty_uint32_cmp(system_constants_.sample_count_log2[0],
                          sample_count_log2_x);
  update_dirty_uint32_cmp(system_constants_.sample_count_log2[1],
                          sample_count_log2_y);
  system_constants_.sample_count_log2[0] = sample_count_log2_x;
  system_constants_.sample_count_log2[1] = sample_count_log2_y;

  // Alpha test and alpha to coverage.
  update_dirty_floatmask(system_constants_.alpha_test_reference, rb_alpha_ref);
  system_constants_.alpha_test_reference = rb_alpha_ref;
  uint32_t alpha_to_mask = rb_colorcontrol.alpha_to_mask_enable
                               ? (rb_colorcontrol.value >> 24) | (1 << 8)
                               : 0;

  update_dirty_uint32_cmp(system_constants_.alpha_to_mask, alpha_to_mask);
  system_constants_.alpha_to_mask = alpha_to_mask;

  uint32_t edram_tile_dwords_scaled =
      xenos::kEdramTileWidthSamples * xenos::kEdramTileHeightSamples *
      (draw_resolution_scale_x * draw_resolution_scale_y);

  // EDRAM pitch for ROV writing.
  if constexpr (edram_rov_used) {
    // Align, then multiply by 32bpp tile size in dwords.
    uint32_t edram_32bpp_tile_pitch_dwords_scaled =
        ((rb_surface_info.surface_pitch *
          (rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X ? 2 : 1)) +
         (xenos::kEdramTileWidthSamples - 1)) /
        xenos::kEdramTileWidthSamples * edram_tile_dwords_scaled;
    update_dirty_uint32_cmp(
        system_constants_.edram_32bpp_tile_pitch_dwords_scaled,
        edram_32bpp_tile_pitch_dwords_scaled);
    system_constants_.edram_32bpp_tile_pitch_dwords_scaled =
        edram_32bpp_tile_pitch_dwords_scaled;
  }

#if XE_ARCH_AMD64 == 1
  __m128i rt_clamp_dirty = _mm_set1_epi8((char)0xff);
#endif
  // Color exponent bias and ROV render target writing.
  for (uint32_t i = 0; i < 4; ++i) {
    reg::RB_COLOR_INFO color_info = color_infos[i];
    // Exponent bias is in bits 20:25 of RB_COLOR_INFO.
    int32_t color_exp_bias = color_info.color_exp_bias;
    if (color_info.color_format == xenos::ColorRenderTargetFormat::k_16_16 ||
        color_info.color_format ==
            xenos::ColorRenderTargetFormat::k_16_16_16_16) {
      if (render_target_cache_->GetPath() ==
              RenderTargetCache::Path::kHostRenderTargets &&
          !render_target_cache_->IsFixed16TruncatedToMinus1To1()) {
        // Remap from -32...32 to -1...1 by dividing the output values by 32,
        // losing blending correctness, but getting the full range.
        color_exp_bias -= 5;
      }
    }
    auto color_exp_bias_scale = xe::memory::Reinterpret<float>(
        int32_t(0x3F800000 + (color_exp_bias << 23)));

    update_dirty_floatmask(system_constants_.color_exp_bias[i],
                           color_exp_bias_scale);

    system_constants_.color_exp_bias[i] = color_exp_bias_scale;
    if constexpr (edram_rov_used) {
      update_dirty_uint32_cmp(system_constants_.edram_rt_keep_mask[i][0],
                              rt_keep_masks[i][0]);

      system_constants_.edram_rt_keep_mask[i][0] = rt_keep_masks[i][0];

      update_dirty_uint32_cmp(system_constants_.edram_rt_keep_mask[i][1],
                              rt_keep_masks[i][1]);

      system_constants_.edram_rt_keep_mask[i][1] = rt_keep_masks[i][1];
      if (rt_keep_masks[i][0] != UINT32_MAX ||
          rt_keep_masks[i][1] != UINT32_MAX) {
        uint32_t rt_base_dwords_scaled =
            color_info.color_base * edram_tile_dwords_scaled;
        update_dirty_uint32_cmp(
            system_constants_.edram_rt_base_dwords_scaled[i],
            rt_base_dwords_scaled);
        system_constants_.edram_rt_base_dwords_scaled[i] =
            rt_base_dwords_scaled;
        uint32_t format_flags =
            RenderTargetCache::AddPSIColorFormatFlags(color_info.color_format);
        update_dirty_uint32_cmp(system_constants_.edram_rt_format_flags[i],
                                format_flags);
        system_constants_.edram_rt_format_flags[i] = format_flags;
        // Can't do float comparisons here because NaNs would result in always
        // setting the dirty flag.

#if XE_ARCH_AMD64 == 1

        __m128i edram_rt_clamp_loaded = _mm_loadu_si128(
            (const __m128i*)&system_constants_.edram_rt_clamp[i]);
        __m128i rt_clamp_loaded = _mm_loadu_si128((const __m128i*)&rt_clamp[i]);

        rt_clamp_dirty = _mm_and_si128(
            rt_clamp_dirty,
            _mm_cmpeq_epi8(edram_rt_clamp_loaded, rt_clamp_loaded));
        _mm_storeu_si128((__m128i*)&system_constants_.edram_rt_clamp[i],
                         rt_clamp_loaded);
#else
        dirty |= std::memcmp(system_constants_.edram_rt_clamp[i], rt_clamp[i],
                             4 * sizeof(float)) != 0;
        std::memcpy(system_constants_.edram_rt_clamp[i], rt_clamp[i],
                    4 * sizeof(float));

#endif
        uint32_t blend_factors_ops =
            regs[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1FFF1FFF;

        update_dirty_uint32_cmp(system_constants_.edram_rt_blend_factors_ops[i],
                                blend_factors_ops);

        system_constants_.edram_rt_blend_factors_ops[i] = blend_factors_ops;
      }
    }
  }
#if XE_ARCH_AMD64 == 1
  if constexpr (edram_rov_used) {
    update_dirty_uint32_cmp(
        static_cast<uint32_t>(_mm_movemask_epi8(rt_clamp_dirty)), 0xFFFFU);
  }

#endif
  if constexpr (edram_rov_used) {
    uint32_t depth_base_dwords_scaled =
        rb_depth_info.depth_base * edram_tile_dwords_scaled;
    update_dirty_uint32_cmp(system_constants_.edram_depth_base_dwords_scaled,
                            depth_base_dwords_scaled);

    system_constants_.edram_depth_base_dwords_scaled = depth_base_dwords_scaled;

    // For non-polygons, front polygon offset is used, and it's enabled if
    // POLY_OFFSET_PARA_ENABLED is set, for polygons, separate front and back
    // are used.
    float poly_offset_front_scale = 0.0f, poly_offset_front_offset = 0.0f;
    float poly_offset_back_scale = 0.0f, poly_offset_back_offset = 0.0f;
    if (primitive_polygonal) {
      if (pa_su_sc_mode_cntl.poly_offset_front_enable) {
        poly_offset_front_scale =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
        poly_offset_front_offset =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
      }
      if (pa_su_sc_mode_cntl.poly_offset_back_enable) {
        poly_offset_back_scale =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE);
        poly_offset_back_offset =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET);
      }
    } else {
      if (pa_su_sc_mode_cntl.poly_offset_para_enable) {
        poly_offset_front_scale =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
        poly_offset_front_offset =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
        poly_offset_back_scale = poly_offset_front_scale;
        poly_offset_back_offset = poly_offset_front_offset;
      }
    }
    // With non-square resolution scaling, make sure the worst-case impact is
    // reverted (slope only along the scaled axis), thus max. More bias is
    // better than less bias, because less bias means Z fighting with the
    // background is more likely.
    float poly_offset_scale_factor =
        xenos::kPolygonOffsetScaleSubpixelUnit *
        std::max(draw_resolution_scale_x, draw_resolution_scale_y);
    poly_offset_front_scale *= poly_offset_scale_factor;
    poly_offset_back_scale *= poly_offset_scale_factor;
    update_dirty_floatmask(system_constants_.edram_poly_offset_front_scale,
                           poly_offset_front_scale);

    system_constants_.edram_poly_offset_front_scale = poly_offset_front_scale;

    update_dirty_floatmask(system_constants_.edram_poly_offset_front_offset,
                           poly_offset_front_offset);

    system_constants_.edram_poly_offset_front_offset = poly_offset_front_offset;
    update_dirty_floatmask(system_constants_.edram_poly_offset_back_scale,
                           poly_offset_back_scale);
    system_constants_.edram_poly_offset_back_scale = poly_offset_back_scale;
    update_dirty_floatmask(system_constants_.edram_poly_offset_back_offset,
                           poly_offset_back_offset);
    system_constants_.edram_poly_offset_back_offset = poly_offset_back_offset;

    if (depth_stencil_enabled && normalized_depth_control.stencil_enable) {
      update_dirty_uint32_cmp(system_constants_.edram_stencil_front_reference,
                              rb_stencilrefmask.stencilref);

      system_constants_.edram_stencil_front_reference =
          rb_stencilrefmask.stencilref;
      update_dirty_uint32_cmp(system_constants_.edram_stencil_front_read_mask,
                              rb_stencilrefmask.stencilmask);
      system_constants_.edram_stencil_front_read_mask =
          rb_stencilrefmask.stencilmask;
      update_dirty_uint32_cmp(system_constants_.edram_stencil_front_write_mask,
                              rb_stencilrefmask.stencilwritemask);
      system_constants_.edram_stencil_front_write_mask =
          rb_stencilrefmask.stencilwritemask;
      uint32_t stencil_func_ops =
          (normalized_depth_control.value >> 8) & ((1 << 12) - 1);
      update_dirty_uint32_cmp(system_constants_.edram_stencil_front_func_ops,
                              stencil_func_ops);
      system_constants_.edram_stencil_front_func_ops = stencil_func_ops;

      if (primitive_polygonal && normalized_depth_control.backface_enable) {
        update_dirty_uint32_cmp(system_constants_.edram_stencil_back_reference,
                                rb_stencilrefmask_bf.stencilref);
        system_constants_.edram_stencil_back_reference =
            rb_stencilrefmask_bf.stencilref;
        update_dirty_uint32_cmp(system_constants_.edram_stencil_back_read_mask,
                                rb_stencilrefmask_bf.stencilmask);
        system_constants_.edram_stencil_back_read_mask =
            rb_stencilrefmask_bf.stencilmask;
        update_dirty_uint32_cmp(system_constants_.edram_stencil_back_write_mask,
                                rb_stencilrefmask_bf.stencilwritemask);
        system_constants_.edram_stencil_back_write_mask =
            rb_stencilrefmask_bf.stencilwritemask;
        uint32_t stencil_func_ops_bf =
            (normalized_depth_control.value >> 20) & ((1 << 12) - 1);
        update_dirty_uint32_cmp(system_constants_.edram_stencil_back_func_ops,
                                stencil_func_ops_bf);
        system_constants_.edram_stencil_back_func_ops = stencil_func_ops_bf;
      } else {
        dirty |= std::memcmp(system_constants_.edram_stencil_back,
                             system_constants_.edram_stencil_front,
                             4 * sizeof(uint32_t)) != 0;
        std::memcpy(system_constants_.edram_stencil_back,
                    system_constants_.edram_stencil_front,
                    4 * sizeof(uint32_t));
      }
    }
    update_dirty_floatmask(system_constants_.edram_blend_constant[0],
                           regs.Get<float>(XE_GPU_REG_RB_BLEND_RED));

    system_constants_.edram_blend_constant[0] =
        regs.Get<float>(XE_GPU_REG_RB_BLEND_RED);

    update_dirty_floatmask(system_constants_.edram_blend_constant[1],
                           regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN));

    system_constants_.edram_blend_constant[1] =
        regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN);
    update_dirty_floatmask(system_constants_.edram_blend_constant[2],
                           regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE));

    system_constants_.edram_blend_constant[2] =
        regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE);
    update_dirty_floatmask(system_constants_.edram_blend_constant[3],
                           regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA));

    system_constants_.edram_blend_constant[3] =
        regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA);
  }
  dirty |= ArchFloatMaskSignbit(dirty_float_mask);

  cbuffer_binding_system_.up_to_date &= !dirty;
}

void D3D12CommandProcessor::UpdateSystemConstantValues(
    bool shared_memory_is_uav, bool primitive_polygonal,
    uint32_t line_loop_closing_index, xenos::Endian index_endian,
    const draw_util::ViewportInfo& viewport_info, uint32_t used_texture_mask,
    reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask) {
  bool edram_rov_used = render_target_cache_->GetPath() ==
                        RenderTargetCache::Path::kPixelShaderInterlock;

  if (!edram_rov_used) {
    if (primitive_polygonal) {
      UpdateSystemConstantValues_Impl<true, false>(
          shared_memory_is_uav, line_loop_closing_index, index_endian,
          viewport_info, used_texture_mask, normalized_depth_control,
          normalized_color_mask);
    } else {
      UpdateSystemConstantValues_Impl<false, false>(
          shared_memory_is_uav, line_loop_closing_index, index_endian,
          viewport_info, used_texture_mask, normalized_depth_control,
          normalized_color_mask);
    }
  } else {
    if (primitive_polygonal) {
      UpdateSystemConstantValues_Impl<true, true>(
          shared_memory_is_uav, line_loop_closing_index, index_endian,
          viewport_info, used_texture_mask, normalized_depth_control,
          normalized_color_mask);
    } else {
      UpdateSystemConstantValues_Impl<false, true>(
          shared_memory_is_uav, line_loop_closing_index, index_endian,
          viewport_info, used_texture_mask, normalized_depth_control,
          normalized_color_mask);
    }
  }
}

bool D3D12CommandProcessor::UpdateBindings(const D3D12Shader* vertex_shader,
                                           const D3D12Shader* pixel_shader,
                                           ID3D12RootSignature* root_signature,
                                           bool shared_memory_is_uav) {
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  const RegisterFile& regs = *register_file_;

#if XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_UI_D3D12_FINE_GRAINED_DRAW_SCOPES

  // Set the new root signature.
  if (current_graphics_root_signature_ != root_signature) {
    current_graphics_root_signature_ = root_signature;
    if (!bindless_resources_used_) {
      GetRootBindfulExtraParameterIndices(
          vertex_shader, pixel_shader, current_graphics_root_bindful_extras_);
    }
    // Changing the root signature invalidates all bindings.
    current_graphics_root_up_to_date_ = 0;
    deferred_command_list_.D3DSetGraphicsRootSignature(root_signature);
  }

  // Select the root parameter indices depending on the used binding model.
  uint32_t root_parameter_fetch_constants =
      bindless_resources_used_ ? kRootParameter_Bindless_FetchConstants
                               : kRootParameter_Bindful_FetchConstants;
  uint32_t root_parameter_float_constants_vertex =
      bindless_resources_used_ ? kRootParameter_Bindless_FloatConstantsVertex
                               : kRootParameter_Bindful_FloatConstantsVertex;
  uint32_t root_parameter_float_constants_pixel =
      bindless_resources_used_ ? kRootParameter_Bindless_FloatConstantsPixel
                               : kRootParameter_Bindful_FloatConstantsPixel;
  uint32_t root_parameter_system_constants =
      bindless_resources_used_ ? kRootParameter_Bindless_SystemConstants
                               : kRootParameter_Bindful_SystemConstants;
  uint32_t root_parameter_bool_loop_constants =
      bindless_resources_used_ ? kRootParameter_Bindless_BoolLoopConstants
                               : kRootParameter_Bindful_BoolLoopConstants;
  uint32_t root_parameter_shared_memory_and_bindful_edram =
      bindless_resources_used_ ? kRootParameter_Bindless_SharedMemory
                               : kRootParameter_Bindful_SharedMemoryAndEdram;

  //
  // Update root constant buffers that are common for bindful and bindless.
  //

  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ on the Xbox
  // 360 (however, OpenGL ES on Adreno 200 on Android has different ranges).
  assert_true(regs[XE_GPU_REG_SQ_VS_CONST] == 0x000FF000 ||
              regs[XE_GPU_REG_SQ_VS_CONST] == 0x00000000);
  assert_true(regs[XE_GPU_REG_SQ_PS_CONST] == 0x000FF100 ||
              regs[XE_GPU_REG_SQ_PS_CONST] == 0x00000000);
  // Check if the float constant layout is still the same and get the counts.
  const Shader::ConstantRegisterMap& float_constant_map_vertex =
      vertex_shader->constant_register_map();
  uint32_t float_constant_count_vertex = float_constant_map_vertex.float_count;
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_float_constant_map_vertex_[i] !=
        float_constant_map_vertex.float_bitmap[i]) {
      current_float_constant_map_vertex_[i] =
          float_constant_map_vertex.float_bitmap[i];
      // If no float constants at all, we can reuse any buffer for them, so not
      // invalidating.
      if (float_constant_count_vertex) {
        cbuffer_binding_float_vertex_.up_to_date = false;
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
        if (float_constant_count_pixel) {
          cbuffer_binding_float_pixel_.up_to_date = false;
        }
      }
    }
  } else {
    std::memset(current_float_constant_map_pixel_, 0,
                sizeof(current_float_constant_map_pixel_));
  }

  // Write the constant buffer data.
  if (!cbuffer_binding_system_.up_to_date) {
    uint8_t* system_constants = constant_buffer_pool_->Request(
        frame_current_, sizeof(system_constants_),
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
        &cbuffer_binding_system_.address);
    if (system_constants == nullptr) {
      return false;
    }
    std::memcpy(system_constants, &system_constants_,
                sizeof(system_constants_));
    cbuffer_binding_system_.up_to_date = true;
    current_graphics_root_up_to_date_ &=
        ~(1u << root_parameter_system_constants);
  }
  if (!cbuffer_binding_float_vertex_.up_to_date) {
    // Even if the shader doesn't need any float constants, a valid binding must
    // still be provided, so if the first draw in the frame with the current
    // root signature doesn't have float constants at all, still allocate an
    // empty buffer.
    uint8_t* float_constants = constant_buffer_pool_->Request(
        frame_current_,
        sizeof(float) * 4 * std::max(float_constant_count_vertex, uint32_t(1)),
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
        &cbuffer_binding_float_vertex_.address);
    if (float_constants == nullptr) {
      return false;
    }
    for (uint32_t i = 0; i < 4; ++i) {
      uint64_t float_constant_map_entry =
          float_constant_map_vertex.float_bitmap[i];
      uint32_t float_constant_index;
      while (xe::bit_scan_forward(float_constant_map_entry,
                                  &float_constant_index)) {
        float_constant_map_entry =
            xe::clear_lowest_bit(float_constant_map_entry);
        std::memcpy(float_constants,
                    &regs[XE_GPU_REG_SHADER_CONSTANT_000_X + (i << 8) +
                          (float_constant_index << 2)],
                    4 * sizeof(float));
        float_constants += 4 * sizeof(float);
      }
    }
    cbuffer_binding_float_vertex_.up_to_date = true;
    current_graphics_root_up_to_date_ &=
        ~(1u << root_parameter_float_constants_vertex);
  }
  if (!cbuffer_binding_float_pixel_.up_to_date) {
    uint8_t* float_constants = constant_buffer_pool_->Request(
        frame_current_,
        sizeof(float) * 4 * std::max(float_constant_count_pixel, uint32_t(1)),
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
        &cbuffer_binding_float_pixel_.address);
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
          float_constant_map_entry =
              xe::clear_lowest_bit(float_constant_map_entry);
          std::memcpy(float_constants,
                      &regs[XE_GPU_REG_SHADER_CONSTANT_256_X + (i << 8) +
                            (float_constant_index << 2)],
                      4 * sizeof(float));
          float_constants += 4 * sizeof(float);
        }
      }
    }
    cbuffer_binding_float_pixel_.up_to_date = true;
    current_graphics_root_up_to_date_ &=
        ~(1u << root_parameter_float_constants_pixel);
  }
  if (!cbuffer_binding_bool_loop_.up_to_date) {
    constexpr uint32_t kBoolLoopConstantsSize = (8 + 32) * sizeof(uint32_t);
    uint8_t* bool_loop_constants = constant_buffer_pool_->Request(
        frame_current_, kBoolLoopConstantsSize,
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
        &cbuffer_binding_bool_loop_.address);
    if (bool_loop_constants == nullptr) {
      return false;
    }
    xe::smallcpy_const<kBoolLoopConstantsSize>(
        bool_loop_constants, &regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031]);

    cbuffer_binding_bool_loop_.up_to_date = true;
    current_graphics_root_up_to_date_ &=
        ~(1u << root_parameter_bool_loop_constants);
  }
  if (!cbuffer_binding_fetch_.up_to_date) {
    constexpr uint32_t kFetchConstantsSize = 32 * 6 * sizeof(uint32_t);
    uint8_t* fetch_constants = constant_buffer_pool_->Request(
        frame_current_, kFetchConstantsSize,
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
        &cbuffer_binding_fetch_.address);
    if (fetch_constants == nullptr) {
      return false;
    }
    xe::smallcpy_const<kFetchConstantsSize>(
        fetch_constants, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0]);

    cbuffer_binding_fetch_.up_to_date = true;
    current_graphics_root_up_to_date_ &=
        ~(1u << root_parameter_fetch_constants);
  }

  //
  // Update descriptors.
  //

  if (!current_shared_memory_binding_is_uav_.has_value() ||
      current_shared_memory_binding_is_uav_.value() != shared_memory_is_uav) {
    current_shared_memory_binding_is_uav_ = shared_memory_is_uav;
    current_graphics_root_up_to_date_ &=
        ~(1u << root_parameter_shared_memory_and_bindful_edram);
  }

  // Get textures and samplers used by the vertex shader, check if the last used
  // samplers are compatible and update them.
  size_t texture_layout_uid_vertex =
      vertex_shader->GetTextureBindingLayoutUserUID();
  size_t sampler_layout_uid_vertex =
      vertex_shader->GetSamplerBindingLayoutUserUID();
  const std::vector<D3D12Shader::TextureBinding>& textures_vertex =
      vertex_shader->GetTextureBindingsAfterTranslation();
  const std::vector<D3D12Shader::SamplerBinding>& samplers_vertex =
      vertex_shader->GetSamplerBindingsAfterTranslation();
  size_t texture_count_vertex = textures_vertex.size();
  size_t sampler_count_vertex = samplers_vertex.size();
  if (sampler_count_vertex) {
    if (current_sampler_layout_uid_vertex_ != sampler_layout_uid_vertex) {
      current_sampler_layout_uid_vertex_ = sampler_layout_uid_vertex;
      cbuffer_binding_descriptor_indices_vertex_.up_to_date = false;
      bindful_samplers_written_vertex_ = false;
    }
    current_samplers_vertex_.resize(
        std::max(current_samplers_vertex_.size(), sampler_count_vertex));
    for (size_t i = 0; i < sampler_count_vertex; ++i) {
      if (i + 2 < sampler_count_vertex) {
        texture_cache_->PrefetchSamplerParameters(samplers_vertex[i + 2]);
      }
      D3D12TextureCache::SamplerParameters parameters =
          texture_cache_->GetSamplerParameters(samplers_vertex[i]);
      if (current_samplers_vertex_[i] != parameters) {
        cbuffer_binding_descriptor_indices_vertex_.up_to_date = false;
        bindful_samplers_written_vertex_ = false;
        current_samplers_vertex_[i] = parameters;
      }
    }
  }

  // Get textures and samplers used by the pixel shader, check if the last used
  // samplers are compatible and update them.
  size_t texture_layout_uid_pixel, sampler_layout_uid_pixel;
  const std::vector<D3D12Shader::TextureBinding>* textures_pixel;
  const std::vector<D3D12Shader::SamplerBinding>* samplers_pixel;
  size_t texture_count_pixel, sampler_count_pixel;
  if (pixel_shader != nullptr) {
    texture_layout_uid_pixel = pixel_shader->GetTextureBindingLayoutUserUID();
    sampler_layout_uid_pixel = pixel_shader->GetSamplerBindingLayoutUserUID();
    textures_pixel = &pixel_shader->GetTextureBindingsAfterTranslation();
    texture_count_pixel = textures_pixel->size();
    samplers_pixel = &pixel_shader->GetSamplerBindingsAfterTranslation();
    sampler_count_pixel = samplers_pixel->size();
    if (sampler_count_pixel) {
      if (current_sampler_layout_uid_pixel_ != sampler_layout_uid_pixel) {
        current_sampler_layout_uid_pixel_ = sampler_layout_uid_pixel;
        cbuffer_binding_descriptor_indices_pixel_.up_to_date = false;
        bindful_samplers_written_pixel_ = false;
      }
      current_samplers_pixel_.resize(std::max(current_samplers_pixel_.size(),
                                              size_t(sampler_count_pixel)));
      const auto samplers_pixel_derefed = samplers_pixel->data();

      for (uint32_t i = 0; i < sampler_count_pixel; ++i) {
        if (i + 2 < sampler_count_pixel) {
          texture_cache_->PrefetchSamplerParameters(
              samplers_pixel_derefed[i + 2]);
        }
        D3D12TextureCache::SamplerParameters parameters =
            texture_cache_->GetSamplerParameters(samplers_pixel_derefed[i]);
        if (current_samplers_pixel_[i] != parameters) {
          current_samplers_pixel_[i] = parameters;
          cbuffer_binding_descriptor_indices_pixel_.up_to_date = false;
          bindful_samplers_written_pixel_ = false;
        }
      }
    }
  } else {
    texture_layout_uid_pixel = PipelineCache::kLayoutUIDEmpty;
    sampler_layout_uid_pixel = PipelineCache::kLayoutUIDEmpty;
    textures_pixel = nullptr;
    texture_count_pixel = 0;
    samplers_pixel = nullptr;
    sampler_count_pixel = 0;
  }

  assert_true(sampler_count_vertex + sampler_count_pixel <= kSamplerHeapSize);

  if (bindless_resources_used_) {
    //
    // Bindless descriptors path.
    //

    // Check if need to write new descriptor indices.
    // Samplers have already been checked.
    if (texture_count_vertex &&
        cbuffer_binding_descriptor_indices_vertex_.up_to_date &&
        (current_texture_layout_uid_vertex_ != texture_layout_uid_vertex ||
         !texture_cache_->AreActiveTextureSRVKeysUpToDate(
             current_texture_srv_keys_vertex_.data(), textures_vertex.data(),
             texture_count_vertex))) {
      cbuffer_binding_descriptor_indices_vertex_.up_to_date = false;
    }
    if (texture_count_pixel &&
        cbuffer_binding_descriptor_indices_pixel_.up_to_date &&
        (current_texture_layout_uid_pixel_ != texture_layout_uid_pixel ||
         !texture_cache_->AreActiveTextureSRVKeysUpToDate(
             current_texture_srv_keys_pixel_.data(), textures_pixel->data(),
             texture_count_pixel))) {
      cbuffer_binding_descriptor_indices_pixel_.up_to_date = false;
    }

    // Get sampler descriptor indices, write new samplers, and handle sampler
    // heap overflow if it happens.
    if ((sampler_count_vertex &&
         !cbuffer_binding_descriptor_indices_vertex_.up_to_date) ||
        (sampler_count_pixel &&
         !cbuffer_binding_descriptor_indices_pixel_.up_to_date)) {
      for (uint32_t i = 0; i < 2; ++i) {
        if (i) {
          // Overflow happened - invalidate sampler bindings because their
          // descriptor indices can't be used anymore (and even if heap creation
          // fails, because current_sampler_bindless_indices_#_ are in an
          // undefined state now) and switch to a new sampler heap.
          cbuffer_binding_descriptor_indices_vertex_.up_to_date = false;
          cbuffer_binding_descriptor_indices_pixel_.up_to_date = false;
          ID3D12DescriptorHeap* sampler_heap_new;
          if (!sampler_bindless_heaps_overflowed_.empty() &&
              sampler_bindless_heaps_overflowed_.front().second <=
                  submission_completed_) {
            sampler_heap_new = sampler_bindless_heaps_overflowed_.front().first;
            sampler_bindless_heaps_overflowed_.pop_front();
          } else {
            D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_new_desc;
            sampler_heap_new_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            sampler_heap_new_desc.NumDescriptors = kSamplerHeapSize;
            sampler_heap_new_desc.Flags =
                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            sampler_heap_new_desc.NodeMask = 0;
            if (FAILED(device->CreateDescriptorHeap(
                    &sampler_heap_new_desc, IID_PPV_ARGS(&sampler_heap_new)))) {
              XELOGE(
                  "Failed to create a new bindless sampler descriptor heap "
                  "after an overflow of the previous one");
              return false;
            }
          }
          // Only change the heap if a new heap was created successfully, not to
          // leave the values in an undefined state in case CreateDescriptorHeap
          // has failed.
          sampler_bindless_heaps_overflowed_.push_back(std::make_pair(
              sampler_bindless_heap_current_, submission_current_));
          sampler_bindless_heap_current_ = sampler_heap_new;
          sampler_bindless_heap_cpu_start_ =
              sampler_bindless_heap_current_
                  ->GetCPUDescriptorHandleForHeapStart();
          sampler_bindless_heap_gpu_start_ =
              sampler_bindless_heap_current_
                  ->GetGPUDescriptorHandleForHeapStart();
          sampler_bindless_heap_allocated_ = 0;
          // The only thing the heap is used for now is texture cache samplers -
          // invalidate all of them.
          texture_cache_bindless_sampler_map_.clear();
          deferred_command_list_.SetDescriptorHeaps(
              view_bindless_heap_, sampler_bindless_heap_current_);
          current_graphics_root_up_to_date_ &=
              ~(1u << kRootParameter_Bindless_SamplerHeap);
        }
        bool samplers_overflowed = false;
        if (sampler_count_vertex &&
            !cbuffer_binding_descriptor_indices_vertex_.up_to_date) {
          current_sampler_bindless_indices_vertex_.resize(
              std::max(current_sampler_bindless_indices_vertex_.size(),
                       size_t(sampler_count_vertex)));
          for (uint32_t j = 0; j < sampler_count_vertex; ++j) {
            D3D12TextureCache::SamplerParameters sampler_parameters =
                current_samplers_vertex_[j];
            uint32_t sampler_index;
            auto it = texture_cache_bindless_sampler_map_.find(
                sampler_parameters.value);
            if (it != texture_cache_bindless_sampler_map_.end()) {
              sampler_index = it->second;
            } else {
              if (sampler_bindless_heap_allocated_ >= kSamplerHeapSize) {
                samplers_overflowed = true;
                break;
              }
              sampler_index = sampler_bindless_heap_allocated_++;
              texture_cache_->WriteSampler(
                  sampler_parameters,
                  provider.OffsetSamplerDescriptor(
                      sampler_bindless_heap_cpu_start_, sampler_index));
              texture_cache_bindless_sampler_map_.emplace(
                  sampler_parameters.value, sampler_index);
            }
            current_sampler_bindless_indices_vertex_[j] = sampler_index;
          }
        }
        if (samplers_overflowed) {
          continue;
        }
        if (sampler_count_pixel &&
            !cbuffer_binding_descriptor_indices_pixel_.up_to_date) {
          current_sampler_bindless_indices_pixel_.resize(
              std::max(current_sampler_bindless_indices_pixel_.size(),
                       size_t(sampler_count_pixel)));
          for (uint32_t j = 0; j < sampler_count_pixel; ++j) {
            D3D12TextureCache::SamplerParameters sampler_parameters =
                current_samplers_pixel_[j];
            uint32_t sampler_index;
            auto it = texture_cache_bindless_sampler_map_.find(
                sampler_parameters.value);
            if (it != texture_cache_bindless_sampler_map_.end()) {
              sampler_index = it->second;
            } else {
              if (sampler_bindless_heap_allocated_ >= kSamplerHeapSize) {
                samplers_overflowed = true;
                break;
              }
              sampler_index = sampler_bindless_heap_allocated_++;
              texture_cache_->WriteSampler(
                  sampler_parameters,
                  provider.OffsetSamplerDescriptor(
                      sampler_bindless_heap_cpu_start_, sampler_index));
              texture_cache_bindless_sampler_map_.emplace(
                  sampler_parameters.value, sampler_index);
            }
            current_sampler_bindless_indices_pixel_[j] = sampler_index;
          }
        }
        if (!samplers_overflowed) {
          break;
        }
      }
    }

    if (!cbuffer_binding_descriptor_indices_vertex_.up_to_date) {
      uint32_t* descriptor_indices =
          reinterpret_cast<uint32_t*>(constant_buffer_pool_->Request(
              frame_current_,
              std::max(texture_count_vertex + sampler_count_vertex, size_t(1)) *
                  sizeof(uint32_t),
              D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
              &cbuffer_binding_descriptor_indices_vertex_.address));
      if (!descriptor_indices) {
        return false;
      }
      for (size_t i = 0; i < texture_count_vertex; ++i) {
        if (i + 8 < texture_count_vertex) {
          texture_cache_->PrefetchTextureBinding<swcache::PrefetchTag::Level2>(
              textures_vertex[i + 8].fetch_constant);
        }
        const D3D12Shader::TextureBinding& texture = textures_vertex[i];
        descriptor_indices[texture.bindless_descriptor_index] =
            texture_cache_->GetActiveTextureBindlessSRVIndex(texture) -
            uint32_t(SystemBindlessView::kUnboundedSRVsStart);
      }
      current_texture_layout_uid_vertex_ = texture_layout_uid_vertex;
      if (texture_count_vertex) {
        current_texture_srv_keys_vertex_.resize(
            std::max(current_texture_srv_keys_vertex_.size(),
                     size_t(texture_count_vertex)));
        texture_cache_->WriteActiveTextureSRVKeys(
            current_texture_srv_keys_vertex_.data(), textures_vertex.data(),
            texture_count_vertex);
      }
      // Current samplers have already been updated.
      for (size_t i = 0; i < sampler_count_vertex; ++i) {
        descriptor_indices[samplers_vertex[i].bindless_descriptor_index] =
            current_sampler_bindless_indices_vertex_[i];
      }
      cbuffer_binding_descriptor_indices_vertex_.up_to_date = true;
      current_graphics_root_up_to_date_ &=
          ~(1u << kRootParameter_Bindless_DescriptorIndicesVertex);
    }

    if (!cbuffer_binding_descriptor_indices_pixel_.up_to_date) {
      uint32_t* descriptor_indices =
          reinterpret_cast<uint32_t*>(constant_buffer_pool_->Request(
              frame_current_,
              std::max(texture_count_pixel + sampler_count_pixel, size_t(1)) *
                  sizeof(uint32_t),
              D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
              &cbuffer_binding_descriptor_indices_pixel_.address));
      if (!descriptor_indices) {
        return false;
      }
      for (size_t i = 0; i < texture_count_pixel; ++i) {
        const D3D12Shader::TextureBinding& texture = (*textures_pixel)[i];
        descriptor_indices[texture.bindless_descriptor_index] =
            texture_cache_->GetActiveTextureBindlessSRVIndex(texture) -
            uint32_t(SystemBindlessView::kUnboundedSRVsStart);
      }
      current_texture_layout_uid_pixel_ = texture_layout_uid_pixel;
      if (texture_count_pixel) {
        current_texture_srv_keys_pixel_.resize(
            std::max(current_texture_srv_keys_pixel_.size(),
                     size_t(texture_count_pixel)));
        texture_cache_->WriteActiveTextureSRVKeys(
            current_texture_srv_keys_pixel_.data(), textures_pixel->data(),
            texture_count_pixel);
      }
      // Current samplers have already been updated.
      for (size_t i = 0; i < sampler_count_pixel; ++i) {
        descriptor_indices[(*samplers_pixel)[i].bindless_descriptor_index] =
            current_sampler_bindless_indices_pixel_[i];
      }
      cbuffer_binding_descriptor_indices_pixel_.up_to_date = true;
      current_graphics_root_up_to_date_ &=
          ~(1u << kRootParameter_Bindless_DescriptorIndicesPixel);
    }
  } else {
    bool retflag;
    bool retval = UpdateBindings_BindfulPath(
        texture_layout_uid_vertex, textures_vertex, texture_layout_uid_pixel,
        textures_pixel, sampler_count_vertex, sampler_count_pixel, retflag);
    if (retflag) {
      return retval;
    }
  }

  // Update the root parameters.
  if (!(current_graphics_root_up_to_date_ &
        (1u << root_parameter_fetch_constants))) {
    deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
        root_parameter_fetch_constants, cbuffer_binding_fetch_.address);
    current_graphics_root_up_to_date_ |= 1u << root_parameter_fetch_constants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << root_parameter_float_constants_vertex))) {
    deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
        root_parameter_float_constants_vertex,
        cbuffer_binding_float_vertex_.address);
    current_graphics_root_up_to_date_ |=
        1u << root_parameter_float_constants_vertex;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << root_parameter_float_constants_pixel))) {
    deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
        root_parameter_float_constants_pixel,
        cbuffer_binding_float_pixel_.address);
    current_graphics_root_up_to_date_ |=
        1u << root_parameter_float_constants_pixel;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << root_parameter_system_constants))) {
    deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
        root_parameter_system_constants, cbuffer_binding_system_.address);
    current_graphics_root_up_to_date_ |= 1u << root_parameter_system_constants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << root_parameter_bool_loop_constants))) {
    deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
        root_parameter_bool_loop_constants, cbuffer_binding_bool_loop_.address);
    current_graphics_root_up_to_date_ |= 1u
                                         << root_parameter_bool_loop_constants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << root_parameter_shared_memory_and_bindful_edram))) {
    assert_true(current_shared_memory_binding_is_uav_.has_value());
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_shared_memory_and_bindful_edram;
    if (bindless_resources_used_) {
      gpu_handle_shared_memory_and_bindful_edram =
          provider.OffsetViewDescriptor(
              view_bindless_heap_gpu_start_,
              uint32_t(current_shared_memory_binding_is_uav_.value()
                           ? SystemBindlessView ::
                                 kNullRawSRVAndSharedMemoryRawUAVStart
                           : SystemBindlessView ::
                                 kSharedMemoryRawSRVAndNullRawUAVStart));
    } else {
      gpu_handle_shared_memory_and_bindful_edram =
          current_shared_memory_binding_is_uav_.value()
              ? gpu_handle_shared_memory_uav_and_edram_
              : gpu_handle_shared_memory_srv_and_edram_;
    }
    deferred_command_list_.D3DSetGraphicsRootDescriptorTable(
        root_parameter_shared_memory_and_bindful_edram,
        gpu_handle_shared_memory_and_bindful_edram);
    current_graphics_root_up_to_date_ |=
        1u << root_parameter_shared_memory_and_bindful_edram;
  }
  if (bindless_resources_used_) {
    if (!(current_graphics_root_up_to_date_ &
          (1u << kRootParameter_Bindless_DescriptorIndicesPixel))) {
      deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
          kRootParameter_Bindless_DescriptorIndicesPixel,
          cbuffer_binding_descriptor_indices_pixel_.address);
      current_graphics_root_up_to_date_ |=
          1u << kRootParameter_Bindless_DescriptorIndicesPixel;
    }
    if (!(current_graphics_root_up_to_date_ &
          (1u << kRootParameter_Bindless_DescriptorIndicesVertex))) {
      deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
          kRootParameter_Bindless_DescriptorIndicesVertex,
          cbuffer_binding_descriptor_indices_vertex_.address);
      current_graphics_root_up_to_date_ |=
          1u << kRootParameter_Bindless_DescriptorIndicesVertex;
    }
    if (!(current_graphics_root_up_to_date_ &
          (1u << kRootParameter_Bindless_SamplerHeap))) {
      deferred_command_list_.D3DSetGraphicsRootDescriptorTable(
          kRootParameter_Bindless_SamplerHeap,
          sampler_bindless_heap_gpu_start_);
      current_graphics_root_up_to_date_ |=
          1u << kRootParameter_Bindless_SamplerHeap;
    }
    if (!(current_graphics_root_up_to_date_ &
          (1u << kRootParameter_Bindless_ViewHeap))) {
      deferred_command_list_.D3DSetGraphicsRootDescriptorTable(
          kRootParameter_Bindless_ViewHeap, view_bindless_heap_gpu_start_);
      current_graphics_root_up_to_date_ |= 1u
                                           << kRootParameter_Bindless_ViewHeap;
    }
  } else {
    UpdateBindings_UpdateRootBindful();
  }

  return true;
}
XE_COLD
XE_NOINLINE
void D3D12CommandProcessor::UpdateBindings_UpdateRootBindful() {
  uint32_t extra_index;
  extra_index = current_graphics_root_bindful_extras_.textures_pixel;
  if (extra_index != RootBindfulExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    deferred_command_list_.D3DSetGraphicsRootDescriptorTable(
        extra_index, gpu_handle_textures_pixel_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }
  extra_index = current_graphics_root_bindful_extras_.samplers_pixel;
  if (extra_index != RootBindfulExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    deferred_command_list_.D3DSetGraphicsRootDescriptorTable(
        extra_index, gpu_handle_samplers_pixel_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }
  extra_index = current_graphics_root_bindful_extras_.textures_vertex;
  if (extra_index != RootBindfulExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    deferred_command_list_.D3DSetGraphicsRootDescriptorTable(
        extra_index, gpu_handle_textures_vertex_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }
  extra_index = current_graphics_root_bindful_extras_.samplers_vertex;
  if (extra_index != RootBindfulExtraParameterIndices::kUnavailable &&
      !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
    deferred_command_list_.D3DSetGraphicsRootDescriptorTable(
        extra_index, gpu_handle_samplers_vertex_);
    current_graphics_root_up_to_date_ |= 1u << extra_index;
  }
}
XE_NOINLINE
XE_COLD
bool D3D12CommandProcessor::UpdateBindings_BindfulPath(
    const size_t texture_layout_uid_vertex,
    const std::vector<xe::gpu::DxbcShader::TextureBinding>& textures_vertex,
    const size_t texture_layout_uid_pixel,
    const std::vector<xe::gpu::DxbcShader::TextureBinding>* textures_pixel,
    const size_t sampler_count_vertex, const size_t sampler_count_pixel,
    bool& retflag) {
  retflag = true;
  auto& provider = this->GetD3D12Provider();
  size_t texture_count_pixel = textures_pixel ? textures_pixel->size() : 0;
  size_t texture_count_vertex = textures_vertex.size();
  //
  // Bindful descriptors path.
  //

  // See what descriptors need to be updated.
  // Samplers have already been checked.
  bool write_textures_vertex =
      texture_count_vertex &&
      (!bindful_textures_written_vertex_ ||
       current_texture_layout_uid_vertex_ != texture_layout_uid_vertex ||
       !texture_cache_->AreActiveTextureSRVKeysUpToDate(
           current_texture_srv_keys_vertex_.data(), textures_vertex.data(),
           texture_count_vertex));
  bool write_textures_pixel =
      texture_count_pixel &&
      (!bindful_textures_written_pixel_ ||
       current_texture_layout_uid_pixel_ != texture_layout_uid_pixel ||
       !texture_cache_->AreActiveTextureSRVKeysUpToDate(
           current_texture_srv_keys_pixel_.data(), textures_pixel->data(),
           texture_count_pixel));
  bool write_samplers_vertex =
      sampler_count_vertex && !bindful_samplers_written_vertex_;
  bool write_samplers_pixel =
      sampler_count_pixel && !bindful_samplers_written_pixel_;
  bool edram_rov_used = render_target_cache_->GetPath() ==
                        RenderTargetCache::Path::kPixelShaderInterlock;

  // Allocate the descriptors.
  size_t view_count_partial_update = 0;
  if (write_textures_vertex) {
    view_count_partial_update += texture_count_vertex;
  }
  if (write_textures_pixel) {
    view_count_partial_update += texture_count_pixel;
  }
  // All the constants + shared memory SRV and UAV + textures.
  size_t view_count_full_update =
      2 + texture_count_vertex + texture_count_pixel;
  if (edram_rov_used) {
    // + EDRAM UAV.
    ++view_count_full_update;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE view_cpu_handle;
  D3D12_GPU_DESCRIPTOR_HANDLE view_gpu_handle;
  uint32_t descriptor_size_view = provider.GetViewDescriptorSize();
  uint64_t view_heap_index = RequestViewBindfulDescriptors(
      draw_view_bindful_heap_index_, uint32_t(view_count_partial_update),
      uint32_t(view_count_full_update), view_cpu_handle, view_gpu_handle);
  if (view_heap_index ==
      ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
    XELOGE("Failed to allocate view descriptors");
    return false;
  }
  size_t sampler_count_partial_update = 0;
  if (write_samplers_vertex) {
    sampler_count_partial_update += sampler_count_vertex;
  }
  if (write_samplers_pixel) {
    sampler_count_partial_update += sampler_count_pixel;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_handle = {};
  D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_handle = {};
  uint32_t descriptor_size_sampler = provider.GetSamplerDescriptorSize();
  uint64_t sampler_heap_index =
      ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid;
  if (sampler_count_vertex != 0 || sampler_count_pixel != 0) {
    sampler_heap_index = RequestSamplerBindfulDescriptors(
        draw_sampler_bindful_heap_index_,
        uint32_t(sampler_count_partial_update),
        uint32_t(sampler_count_vertex + sampler_count_pixel),
        sampler_cpu_handle, sampler_gpu_handle);
    if (sampler_heap_index ==
        ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
      XELOGE("Failed to allocate sampler descriptors");
      return false;
    }
  }
  if (draw_view_bindful_heap_index_ != view_heap_index) {
    // Need to update all view descriptors.
    write_textures_vertex = texture_count_vertex != 0;
    write_textures_pixel = texture_count_pixel != 0;
    bindful_textures_written_vertex_ = false;
    bindful_textures_written_pixel_ = false;
    // If updating fully, write the shared memory SRV and UAV descriptors and,
    // if needed, the EDRAM descriptor.
    gpu_handle_shared_memory_srv_and_edram_ = view_gpu_handle;
    shared_memory_->WriteRawSRVDescriptor(view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    shared_memory_->WriteRawUAVDescriptor(view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    if (edram_rov_used) {
      render_target_cache_->WriteEdramUintPow2UAVDescriptor(view_cpu_handle, 2);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
    }
    // Null SRV + UAV + EDRAM.
    gpu_handle_shared_memory_uav_and_edram_ = view_gpu_handle;
    ui::d3d12::util::CreateBufferRawSRV(provider.GetDevice(), view_cpu_handle,
                                        nullptr, 0);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    shared_memory_->WriteRawUAVDescriptor(view_cpu_handle);
    view_cpu_handle.ptr += descriptor_size_view;
    view_gpu_handle.ptr += descriptor_size_view;
    if (edram_rov_used) {
      render_target_cache_->WriteEdramUintPow2UAVDescriptor(view_cpu_handle, 2);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
    }
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_Bindful_SharedMemoryAndEdram);
  }
  if (sampler_heap_index !=
          ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid &&
      draw_sampler_bindful_heap_index_ != sampler_heap_index) {
    write_samplers_vertex = sampler_count_vertex != 0;
    write_samplers_pixel = sampler_count_pixel != 0;
    bindful_samplers_written_vertex_ = false;
    bindful_samplers_written_pixel_ = false;
  }

  // Write the descriptors.
  if (write_textures_vertex) {
    assert_true(current_graphics_root_bindful_extras_.textures_vertex !=
                RootBindfulExtraParameterIndices::kUnavailable);
    gpu_handle_textures_vertex_ = view_gpu_handle;
    for (size_t i = 0; i < texture_count_vertex; ++i) {
      texture_cache_->WriteActiveTextureBindfulSRV(textures_vertex[i],
                                                   view_cpu_handle);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
    }
    current_texture_layout_uid_vertex_ = texture_layout_uid_vertex;
    current_texture_srv_keys_vertex_.resize(std::max(
        current_texture_srv_keys_vertex_.size(), size_t(texture_count_vertex)));
    texture_cache_->WriteActiveTextureSRVKeys(
        current_texture_srv_keys_vertex_.data(), textures_vertex.data(),
        texture_count_vertex);
    bindful_textures_written_vertex_ = true;
    current_graphics_root_up_to_date_ &=
        ~(1u << current_graphics_root_bindful_extras_.textures_vertex);
  }
  if (write_textures_pixel) {
    assert_true(current_graphics_root_bindful_extras_.textures_pixel !=
                RootBindfulExtraParameterIndices::kUnavailable);
    gpu_handle_textures_pixel_ = view_gpu_handle;
    for (size_t i = 0; i < texture_count_pixel; ++i) {
      texture_cache_->WriteActiveTextureBindfulSRV((*textures_pixel)[i],
                                                   view_cpu_handle);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
    }
    current_texture_layout_uid_pixel_ = texture_layout_uid_pixel;
    current_texture_srv_keys_pixel_.resize(std::max(
        current_texture_srv_keys_pixel_.size(), size_t(texture_count_pixel)));
    texture_cache_->WriteActiveTextureSRVKeys(
        current_texture_srv_keys_pixel_.data(), textures_pixel->data(),
        texture_count_pixel);
    bindful_textures_written_pixel_ = true;
    current_graphics_root_up_to_date_ &=
        ~(1u << current_graphics_root_bindful_extras_.textures_pixel);
  }
  if (write_samplers_vertex) {
    assert_true(current_graphics_root_bindful_extras_.samplers_vertex !=
                RootBindfulExtraParameterIndices::kUnavailable);
    gpu_handle_samplers_vertex_ = sampler_gpu_handle;
    for (size_t i = 0; i < sampler_count_vertex; ++i) {
      texture_cache_->WriteSampler(current_samplers_vertex_[i],
                                   sampler_cpu_handle);
      sampler_cpu_handle.ptr += descriptor_size_sampler;
      sampler_gpu_handle.ptr += descriptor_size_sampler;
    }
    // Current samplers have already been updated.
    bindful_samplers_written_vertex_ = true;
    current_graphics_root_up_to_date_ &=
        ~(1u << current_graphics_root_bindful_extras_.samplers_vertex);
  }
  if (write_samplers_pixel) {
    assert_true(current_graphics_root_bindful_extras_.samplers_pixel !=
                RootBindfulExtraParameterIndices::kUnavailable);
    gpu_handle_samplers_pixel_ = sampler_gpu_handle;
    for (size_t i = 0; i < sampler_count_pixel; ++i) {
      texture_cache_->WriteSampler(current_samplers_pixel_[i],
                                   sampler_cpu_handle);
      sampler_cpu_handle.ptr += descriptor_size_sampler;
      sampler_gpu_handle.ptr += descriptor_size_sampler;
    }
    // Current samplers have already been updated.
    bindful_samplers_written_pixel_ = true;
    current_graphics_root_up_to_date_ &=
        ~(1u << current_graphics_root_bindful_extras_.samplers_pixel);
  }

  // Wrote new descriptors on the current page.
  draw_view_bindful_heap_index_ = view_heap_index;
  if (sampler_heap_index !=
      ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
    draw_sampler_bindful_heap_index_ = sampler_heap_index;
  }
  retflag = false;
  return {};
}

ID3D12Resource* D3D12CommandProcessor::RequestReadbackBuffer(uint32_t size) {
  if (size == 0) {
    return nullptr;
  }
  size = xe::align(size, kReadbackBufferSizeIncrement);
  if (size > readback_buffer_size_) {
    const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
    ID3D12Device* device = provider.GetDevice();
    D3D12_RESOURCE_DESC buffer_desc;
    ui::d3d12::util::FillBufferResourceDesc(buffer_desc, size,
                                            D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* buffer;
    if (FAILED(device->CreateCommittedResource(
            &ui::d3d12::util::kHeapPropertiesReadback,
            provider.GetHeapFlagCreateNotZeroed(), &buffer_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)))) {
      XELOGE("Failed to create a {} MB readback buffer", size >> 20);
      return nullptr;
    }
    if (readback_buffer_ != nullptr) {
      readback_buffer_->Release();
    }
    readback_buffer_ = buffer;
    readback_buffer_size_ = size;
  }
  return readback_buffer_;
}

void D3D12CommandProcessor::WriteGammaRampSRV(
    bool is_pwl, D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
  ID3D12Device* device = GetD3D12Provider().GetDevice();
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  if (is_pwl) {
    desc.Format = DXGI_FORMAT_R16G16_UINT;
    desc.Buffer.FirstElement = 256 * 4 / 4;
    desc.Buffer.NumElements = 128 * 3;
  } else {
    desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = 256;
  }
  device->CreateShaderResourceView(gamma_ramp_buffer_.Get(), &desc, handle);
}

#define COMMAND_PROCESSOR D3D12CommandProcessor

#include "../pm4_command_processor_implement.h"
#undef COMMAND_PROCESSOR
}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
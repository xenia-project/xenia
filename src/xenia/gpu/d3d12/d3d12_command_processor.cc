/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_command_processor.h"

#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
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

ID3D12RootSignature* D3D12CommandProcessor::GetRootSignature(
    const D3D12Shader* vertex_shader, const D3D12Shader* pixel_shader) {
  assert_true(vertex_shader->is_translated());
  assert_true(pixel_shader == nullptr || pixel_shader->is_translated());

  uint32_t pixel_textures =
      pixel_shader != nullptr ? pixel_shader->GetTextureSRVCount() : 0;
  uint32_t pixel_samplers =
      pixel_shader != nullptr ? pixel_shader->GetSamplerCount() : 0;
  uint32_t vertex_textures = vertex_shader->GetTextureSRVCount();
  uint32_t vertex_samplers = vertex_shader->GetSamplerCount();
  // Max 96 textures (if all kinds of tfetch instructions are used for all fetch
  // registers) and 32 samplers (one sampler per used fetch), but different
  // shader stages have different texture sets.
  uint32_t index = pixel_textures | (pixel_samplers << 7) |
                   (vertex_textures << 12) | (vertex_samplers << 19);

  // Try an existing root signature.
  auto it = root_signatures_.find(index);
  if (it != root_signatures_.end()) {
    return it->second;
  }

  // Create a new one.
  D3D12_ROOT_SIGNATURE_DESC desc;
  D3D12_ROOT_PARAMETER parameters[kRootParameter_Count_TwoStageTextures];
  D3D12_DESCRIPTOR_RANGE ranges[kRootParameter_Count_TwoStageTextures];
  desc.NumParameters = kRootParameter_Count_NoTextures;
  desc.pParameters = parameters;
  desc.NumStaticSamplers = 0;
  desc.pStaticSamplers = nullptr;
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  // Fetch constants.
  {
    auto& parameter = parameters[kRootParameter_FetchConstants];
    auto& range = ranges[kRootParameter_FetchConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 10;
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
    range.BaseShaderRegister = 2;
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
    range.BaseShaderRegister = 2;
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
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 1;
    range.OffsetInDescriptorsFromTableStart = 0;
  }

  if (pixel_textures > 0 || vertex_textures > 0) {
    desc.NumParameters = kRootParameter_Count_OneStageTextures;

    // Pixel or vertex textures.
    {
      auto& parameter = parameters[kRootParameter_PixelOrVertexTextures];
      auto& range = ranges[kRootParameter_PixelOrVertexTextures];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameter.DescriptorTable.NumDescriptorRanges = 1;
      parameter.DescriptorTable.pDescriptorRanges = &range;
      range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      range.BaseShaderRegister = 0;
      range.RegisterSpace = 0;
      range.OffsetInDescriptorsFromTableStart = 0;
      if (pixel_textures > 0) {
        assert_true(pixel_samplers > 0);
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        range.NumDescriptors = pixel_textures;
      } else {
        assert_true(vertex_samplers > 0);
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        range.NumDescriptors = vertex_textures;
      }
    }

    // Pixel or vertex samplers.
    {
      auto& parameter = parameters[kRootParameter_PixelOrVertexSamplers];
      auto& range = ranges[kRootParameter_PixelOrVertexSamplers];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameter.DescriptorTable.NumDescriptorRanges = 1;
      parameter.DescriptorTable.pDescriptorRanges = &range;
      range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
      range.BaseShaderRegister = 0;
      range.RegisterSpace = 0;
      range.OffsetInDescriptorsFromTableStart = 0;
      if (pixel_samplers > 0) {
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        range.NumDescriptors = pixel_samplers;
      } else {
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        range.NumDescriptors = vertex_samplers;
      }
    }

    if (pixel_textures > 0 && vertex_textures > 0) {
      assert_true(vertex_samplers > 0);

      desc.NumParameters = UINT(kRootParameter_Count_TwoStageTextures);

      // Vertex textures.
      {
        auto& parameter = parameters[kRootParameter_VertexTextures];
        auto& range = ranges[kRootParameter_VertexTextures];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges = 1;
        parameter.DescriptorTable.pDescriptorRanges = &range;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = vertex_textures;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = 0;
      }

      // Vertex samplers.
      {
        auto& parameter = parameters[kRootParameter_VertexSamplers];
        auto& range = ranges[kRootParameter_VertexSamplers];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges = 1;
        parameter.DescriptorTable.pDescriptorRanges = &range;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        range.NumDescriptors = vertex_samplers;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = 0;
      }
    }
  }

  ID3DBlob* blob;
  ID3DBlob* error_blob = nullptr;
  if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &blob, &error_blob))) {
    XELOGE(
        "Failed to serialize a root signature with %u pixel textures, %u "
        "pixel samplers, %u vertex textures and %u vertex samplers",
        pixel_textures, pixel_samplers, vertex_textures, vertex_samplers);
    if (error_blob != nullptr) {
      XELOGE("%s",
             reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
      error_blob->Release();
    }
    return nullptr;
  }
  if (error_blob != nullptr) {
    error_blob->Release();
  }

  auto device = GetD3D12Context()->GetD3D12Provider()->GetDevice();
  ID3D12RootSignature* root_signature;
  if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(),
                                         blob->GetBufferSize(),
                                         IID_PPV_ARGS(&root_signature)))) {
    XELOGE(
        "Failed to create a root signature with %u pixel textures, %u pixel "
        "samplers, %u vertex textures and %u vertex samplers",
        pixel_textures, pixel_samplers, vertex_textures, vertex_samplers);
    blob->Release();
    return nullptr;
  }
  blob->Release();

  root_signatures_.insert({index, root_signature});
  return root_signature;
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
    command_lists_[current_queue_frame_]->GetCommandList()->SetDescriptorHeaps(
        heap_count, heaps);
  }
  uint32_t descriptor_offset =
      descriptor_index *
      GetD3D12Context()->GetD3D12Provider()->GetDescriptorSizeView();
  cpu_handle_out.ptr =
      view_heap_pool_->GetLastRequestHeapCPUStart().ptr + descriptor_offset;
  gpu_handle_out.ptr =
      view_heap_pool_->GetLastRequestHeapGPUStart().ptr + descriptor_offset;
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
    command_lists_[current_queue_frame_]->GetCommandList()->SetDescriptorHeaps(
        heap_count, heaps);
  }
  uint32_t descriptor_offset =
      descriptor_index *
      GetD3D12Context()->GetD3D12Provider()->GetDescriptorSizeSampler();
  cpu_handle_out.ptr =
      view_heap_pool_->GetLastRequestHeapCPUStart().ptr + descriptor_offset;
  gpu_handle_out.ptr =
      view_heap_pool_->GetLastRequestHeapGPUStart().ptr + descriptor_offset;
  return current_full_update;
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
    command_lists_setup_[i] = ui::d3d12::CommandList::Create(
        device, direct_queue, D3D12_COMMAND_LIST_TYPE_DIRECT);
    command_lists_[i] = ui::d3d12::CommandList::Create(
        device, direct_queue, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (command_lists_setup_[i] == nullptr || command_lists_[i] == nullptr) {
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

  shared_memory_ = std::make_unique<SharedMemory>(memory_, context);
  if (!shared_memory_->Initialize()) {
    XELOGE("Failed to initialize shared memory");
    return false;
  }

  pipeline_cache_ =
      std::make_unique<PipelineCache>(this, register_file_, context);

  return true;
}

void D3D12CommandProcessor::ShutdownContext() {
  auto context = GetD3D12Context();
  context->AwaitAllFramesCompletion();

  sampler_heap_pool_.reset();
  view_heap_pool_.reset();
  constant_buffer_pool_.reset();

  pipeline_cache_.reset();

  // Root signatured are used by pipelines, thus freed after the pipelines.
  for (auto it : root_signatures_) {
    it.second->Release();
  }
  root_signatures_.clear();

  shared_memory_.reset();

  for (uint32_t i = 0; i < ui::d3d12::D3D12Context::kQueuedFrames; ++i) {
    command_lists_[i].reset();
    command_lists_setup_[i].reset();
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
  }
}

void D3D12CommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                        uint32_t frontbuffer_width,
                                        uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");

  EndFrame();

  if (cache_clear_requested_) {
    cache_clear_requested_ = false;
    GetD3D12Context()->AwaitAllFramesCompletion();

    sampler_heap_pool_->ClearCache();
    view_heap_pool_->ClearCache();
    constant_buffer_pool_->ClearCache();

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
  } else if (enable_mode == xenos::ModeControl::kCopy) {
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
    return true;
  }

  bool new_frame = BeginFrame();
  ID3D12GraphicsCommandList* command_list =
      command_lists_[current_queue_frame_]->GetCommandList();

  // Get the pipeline and translate the shaders so used textures are known.
  ID3D12PipelineState* pipeline;
  ID3D12RootSignature* root_signature;
  auto pipeline_status = pipeline_cache_->ConfigurePipeline(
      vertex_shader, pixel_shader, primitive_type,
      index_buffer_info != nullptr ? index_buffer_info->format
                                   : IndexFormat::kInt16,
      &pipeline, &root_signature);
  if (pipeline_status == PipelineCache::UpdateStatus::kError) {
    return false;
  }

  // Bind the pipeline.
  if (current_pipeline_ != pipeline) {
    current_pipeline_ = pipeline;
    command_list->SetPipelineState(pipeline);
  }

  // Update constant buffers, descriptors and root parameters.
  if (!UpdateBindings(command_list, vertex_shader, pixel_shader,
                      root_signature)) {
    return false;
  }

  // Shared memory test.
  if (index_buffer_info != nullptr && index_buffer_info->guest_base != 0) {
    uint32_t index_size = index_buffer_info->format == IndexFormat::kInt32
                              ? sizeof(uint32_t)
                              : sizeof(uint16_t);
    shared_memory_->UseRange(index_buffer_info->guest_base,
                             index_buffer_info->count * index_size);
  }

  return true;
}

bool D3D12CommandProcessor::IssueCopy() { return true; }

bool D3D12CommandProcessor::BeginFrame() {
  if (current_queue_frame_ != UINT32_MAX) {
    return false;
  }

  auto context = GetD3D12Context();
  context->BeginSwap();
  current_queue_frame_ = context->GetCurrentQueueFrame();

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

  command_lists_setup_[current_queue_frame_]->BeginRecording();
  command_lists_[current_queue_frame_]->BeginRecording();

  shared_memory_->BeginFrame();

  return true;
}

bool D3D12CommandProcessor::EndFrame() {
  if (current_queue_frame_ == UINT32_MAX) {
    return false;
  }

  auto command_list_setup = command_lists_setup_[current_queue_frame_].get();
  auto command_list = command_lists_[current_queue_frame_].get();

  bool setup_written = shared_memory_->EndFrame(
      command_list_setup->GetCommandList(), command_list->GetCommandList());

  if (setup_written) {
    command_list_setup->Execute();
  } else {
    command_list_setup->AbortRecording();
  }
  command_list->Execute();

  auto context = GetD3D12Context();
  context->EndSwap();
  current_queue_frame_ = UINT32_MAX;

  return true;
}

bool D3D12CommandProcessor::UpdateBindings(
    ID3D12GraphicsCommandList* command_list, const D3D12Shader* vertex_shader,
    const D3D12Shader* pixel_shader, ID3D12RootSignature* root_signature) {
  auto provider = GetD3D12Context()->GetD3D12Provider();
  auto device = provider->GetDevice();
  auto& regs = *register_file_;

  // Bind the new root signature.
  if (current_graphics_root_signature_ != root_signature) {
    current_graphics_root_signature_ = root_signature;
    // We don't know which root parameters are up to date anymore.
    current_graphics_root_up_to_date_ = 0;
    command_list->SetGraphicsRootSignature(root_signature);
  }

  // Begin updating descriptors.
  bool write_common_constant_views = false;
  bool write_vertex_float_constant_views = false;
  bool write_pixel_float_constant_views = false;
  bool write_fetch_constant_view = false;

  // Update constant buffers.
  // TODO(Triang3l): Update the system constant buffer - will crash without it.
  ID3D12Resource* constant_buffer;
  uint32_t constant_buffer_offset;
  if (!cbuffer_bindings_system_.up_to_date) {
    uint8_t* system_constants = constant_buffer_pool_->RequestFull(
        xe::align(uint32_t(sizeof(cbuffer_system_)), 256u), constant_buffer,
        constant_buffer_offset);
    if (system_constants == nullptr) {
      return false;
    }
    std::memcpy(system_constants, &cbuffer_system_, sizeof(cbuffer_system_));
    cbuffer_bindings_system_.buffer_address =
        constant_buffer->GetGPUVirtualAddress() + constant_buffer_offset;
    cbuffer_bindings_system_.up_to_date = true;
    write_common_constant_views = true;
  }
  if (!cbuffer_bindings_bool_loop_.up_to_date) {
    uint8_t* bool_loop_constants = constant_buffer_pool_->RequestFull(
        256, constant_buffer, constant_buffer_offset);
    if (bool_loop_constants == nullptr) {
      return false;
    }
    std::memcpy(bool_loop_constants,
                &regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031].u32,
                40 * sizeof(uint32_t));
    cbuffer_bindings_bool_loop_.buffer_address =
        constant_buffer->GetGPUVirtualAddress() + constant_buffer_offset;
    cbuffer_bindings_bool_loop_.up_to_date = true;
    write_common_constant_views = true;
  }
  for (uint32_t i = 0; i < 16; ++i) {
    ConstantBufferBinding& float_binding = cbuffer_bindings_float_[i];
    if (float_binding.up_to_date) {
      continue;
    }
    uint8_t* float_constants = constant_buffer_pool_->RequestFull(
        512, constant_buffer, constant_buffer_offset);
    if (float_constants == nullptr) {
      return false;
    }
    std::memcpy(float_constants,
                &regs[XE_GPU_REG_SHADER_CONSTANT_000_X + (i << 7)].f32,
                32 * 4 * sizeof(uint32_t));
    float_binding.buffer_address =
        constant_buffer->GetGPUVirtualAddress() + constant_buffer_offset;
    float_binding.up_to_date = true;
    if (i < 8) {
      write_vertex_float_constant_views = true;
    } else {
      write_pixel_float_constant_views = true;
    }
  }
  if (!cbuffer_bindings_fetch_.up_to_date) {
    uint8_t* fetch_constants = constant_buffer_pool_->RequestFull(
        768, constant_buffer, constant_buffer_offset);
    if (fetch_constants == nullptr) {
      return false;
    }
    std::memcpy(fetch_constants,
                &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0].u32,
                32 * 6 * sizeof(uint32_t));
    cbuffer_bindings_fetch_.buffer_address =
        constant_buffer->GetGPUVirtualAddress() + constant_buffer_offset;
    cbuffer_bindings_fetch_.up_to_date = true;
    write_fetch_constant_view = true;
  }

  // Update the descriptors.
  uint32_t view_count_partial_update = 0;
  if (write_common_constant_views) {
    // System and bool/loop constants.
    view_count_partial_update += 2;
  }
  if (write_vertex_float_constant_views) {
    // Vertex float constants.
    view_count_partial_update += 8;
  }
  if (write_pixel_float_constant_views) {
    // Pixel float constants.
    view_count_partial_update += 8;
  }
  if (write_fetch_constant_view) {
    // Fetch constants.
    ++view_count_partial_update;
  }
  // All the constants + shared memory.
  uint32_t view_count_full_update = 20;
  D3D12_CPU_DESCRIPTOR_HANDLE view_cpu_handle;
  D3D12_GPU_DESCRIPTOR_HANDLE view_gpu_handle;
  uint32_t view_handle_size = provider->GetDescriptorSizeView();
  uint64_t view_full_update_index = RequestViewDescriptors(
      draw_view_full_update_, view_count_partial_update, view_count_full_update,
      view_cpu_handle, view_gpu_handle);
  if (view_full_update_index == 0) {
    return false;
  }
  if (draw_view_full_update_ != view_full_update_index) {
    // Need to update all descriptors.
    draw_view_full_update_ = view_full_update_index;
    write_common_constant_views = true;
    write_vertex_float_constant_views = true;
    write_pixel_float_constant_views = true;
    write_fetch_constant_view = true;
    // If updating fully, write the shared memory descriptor (t0, space1).
    shared_memory_->CreateSRV(view_cpu_handle);
    gpu_handle_shared_memory_ = view_gpu_handle;
    view_cpu_handle.ptr += view_handle_size;
    view_gpu_handle.ptr += view_handle_size;
    current_graphics_root_up_to_date_ &= ~(1u << kRootParameter_SharedMemory);
  }
  D3D12_CONSTANT_BUFFER_VIEW_DESC constant_buffer_desc;
  if (write_common_constant_views) {
    gpu_handle_common_constants_ = view_gpu_handle;
    // System constants (b0).
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_system_.buffer_address;
    constant_buffer_desc.SizeInBytes =
        xe::align(uint32_t(sizeof(cbuffer_system_)), 256u);
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += view_handle_size;
    view_gpu_handle.ptr += view_handle_size;
    // Bool/loop constants (b1).
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_bool_loop_.buffer_address;
    constant_buffer_desc.SizeInBytes = 256;
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += view_handle_size;
    view_gpu_handle.ptr += view_handle_size;
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_CommonConstants);
  }
  if (write_vertex_float_constant_views) {
    gpu_handle_vertex_float_constants_ = view_gpu_handle;
    // Vertex float constants (b2-b9).
    for (uint32_t i = 0; i < 8; ++i) {
      constant_buffer_desc.BufferLocation =
          cbuffer_bindings_float_[i].buffer_address;
      constant_buffer_desc.SizeInBytes = 512;
      device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
      view_cpu_handle.ptr += view_handle_size;
      view_gpu_handle.ptr += view_handle_size;
    }
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_VertexFloatConstants);
  }
  if (write_pixel_float_constant_views) {
    gpu_handle_pixel_float_constants_ = view_gpu_handle;
    // Pixel float constants (b2-b9).
    for (uint32_t i = 0; i < 8; ++i) {
      constant_buffer_desc.BufferLocation =
          cbuffer_bindings_float_[8 + i].buffer_address;
      constant_buffer_desc.SizeInBytes = 512;
      device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
      view_cpu_handle.ptr += view_handle_size;
      view_gpu_handle.ptr += view_handle_size;
    }
    current_graphics_root_up_to_date_ &=
        ~(1u << kRootParameter_PixelFloatConstants);
  }
  if (write_fetch_constant_view) {
    gpu_handle_fetch_constants_ = view_gpu_handle;
    // Fetch constants (b10).
    constant_buffer_desc.BufferLocation =
        cbuffer_bindings_fetch_.buffer_address;
    constant_buffer_desc.SizeInBytes = 768;
    device->CreateConstantBufferView(&constant_buffer_desc, view_cpu_handle);
    view_cpu_handle.ptr += view_handle_size;
    view_gpu_handle.ptr += view_handle_size;
    current_graphics_root_up_to_date_ &= ~(1u << kRootParameter_FetchConstants);
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

  return true;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

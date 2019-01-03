/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_DEFERRED_COMMAND_LIST_H_
#define XENIA_GPU_D3D12_DEFERRED_COMMAND_LIST_H_

#include <algorithm>
#include <cstring>
#include <vector>

#include "xenia/base/math.h"
#include "xenia/ui/d3d12/command_list.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor;

class DeferredCommandList {
 public:
  DeferredCommandList(D3D12CommandProcessor* command_processor,
                      size_t initial_size = 256 * 1024);

  void Reset();
  void Execute(ID3D12GraphicsCommandList* command_list,
               ID3D12GraphicsCommandList1* command_list_1);

  inline void D3DCopyBufferRegion(ID3D12Resource* dst_buffer, UINT64 dst_offset,
                                  ID3D12Resource* src_buffer, UINT64 src_offset,
                                  UINT64 num_bytes) {
    auto& args = *reinterpret_cast<D3DCopyBufferRegionArguments*>(WriteCommand(
        Command::kD3DCopyBufferRegion, sizeof(D3DCopyBufferRegionArguments)));
    args.dst_buffer = dst_buffer;
    args.dst_offset = dst_offset;
    args.src_buffer = src_buffer;
    args.src_offset = src_offset;
    args.num_bytes = num_bytes;
  }

  inline void D3DCopyResource(ID3D12Resource* dst_resource,
                              ID3D12Resource* src_resource) {
    auto& args = *reinterpret_cast<D3DCopyResourceArguments*>(WriteCommand(
        Command::kD3DCopyResource, sizeof(D3DCopyResourceArguments)));
    args.dst_resource = dst_resource;
    args.src_resource = src_resource;
  }

  inline void CopyTexture(const D3D12_TEXTURE_COPY_LOCATION& dst,
                          const D3D12_TEXTURE_COPY_LOCATION& src) {
    auto& args = *reinterpret_cast<CopyTextureArguments*>(
        WriteCommand(Command::kCopyTexture, sizeof(CopyTextureArguments)));
    std::memcpy(&args.dst, &dst, sizeof(D3D12_TEXTURE_COPY_LOCATION));
    std::memcpy(&args.src, &src, sizeof(D3D12_TEXTURE_COPY_LOCATION));
  }

  inline void D3DDispatch(UINT thread_group_count_x, UINT thread_group_count_y,
                          UINT thread_group_count_z) {
    auto& args = *reinterpret_cast<D3DDispatchArguments*>(
        WriteCommand(Command::kD3DDispatch, sizeof(D3DDispatchArguments)));
    args.thread_group_count_x = thread_group_count_x;
    args.thread_group_count_y = thread_group_count_y;
    args.thread_group_count_z = thread_group_count_z;
  }

  inline void D3DDrawIndexedInstanced(UINT index_count_per_instance,
                                      UINT instance_count,
                                      UINT start_index_location,
                                      INT base_vertex_location,
                                      UINT start_instance_location) {
    auto& args = *reinterpret_cast<D3DDrawIndexedInstancedArguments*>(
        WriteCommand(Command::kD3DDrawIndexedInstanced,
                     sizeof(D3DDrawIndexedInstancedArguments)));
    args.index_count_per_instance = index_count_per_instance;
    args.instance_count = instance_count;
    args.start_index_location = start_index_location;
    args.base_vertex_location = base_vertex_location;
    args.start_instance_location = start_instance_location;
  }

  inline void D3DDrawInstanced(UINT vertex_count_per_instance,
                               UINT instance_count, UINT start_vertex_location,
                               UINT start_instance_location) {
    auto& args = *reinterpret_cast<D3DDrawInstancedArguments*>(WriteCommand(
        Command::kD3DDrawInstanced, sizeof(D3DDrawInstancedArguments)));
    args.vertex_count_per_instance = vertex_count_per_instance;
    args.instance_count = instance_count;
    args.start_vertex_location = start_vertex_location;
    args.start_instance_location = start_instance_location;
  }

  inline void D3DIASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* view) {
    auto& args = *reinterpret_cast<D3D12_INDEX_BUFFER_VIEW*>(WriteCommand(
        Command::kD3DIASetIndexBuffer, sizeof(D3D12_INDEX_BUFFER_VIEW)));
    if (view != nullptr) {
      args.BufferLocation = view->BufferLocation;
      args.SizeInBytes = view->SizeInBytes;
      args.Format = view->Format;
    } else {
      args.BufferLocation = D3D12_GPU_VIRTUAL_ADDRESS(0);
      args.SizeInBytes = 0;
      args.Format = DXGI_FORMAT_UNKNOWN;
    }
  }

  inline void D3DIASetPrimitiveTopology(
      D3D12_PRIMITIVE_TOPOLOGY primitive_topology) {
    auto& arg = *reinterpret_cast<D3D12_PRIMITIVE_TOPOLOGY*>(WriteCommand(
        Command::kD3DIASetPrimitiveTopology, sizeof(D3D12_PRIMITIVE_TOPOLOGY)));
    arg = primitive_topology;
  }

  inline void D3DOMSetBlendFactor(const FLOAT blend_factor[4]) {
    auto args = reinterpret_cast<FLOAT*>(
        WriteCommand(Command::kD3DOMSetBlendFactor, 4 * sizeof(FLOAT)));
    args[0] = blend_factor[0];
    args[1] = blend_factor[1];
    args[2] = blend_factor[2];
    args[3] = blend_factor[3];
  }

  inline void D3DOMSetRenderTargets(
      UINT num_render_target_descriptors,
      const D3D12_CPU_DESCRIPTOR_HANDLE* render_target_descriptors,
      BOOL rts_single_handle_to_descriptor_range,
      const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil_descriptor) {
    auto& args = *reinterpret_cast<D3DOMSetRenderTargetsArguments*>(
        WriteCommand(Command::kD3DOMSetRenderTargets,
                     sizeof(D3DOMSetRenderTargetsArguments)));
    num_render_target_descriptors =
        std::min(num_render_target_descriptors,
                 UINT(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT));
    args.num_render_target_descriptors = num_render_target_descriptors;
    args.rts_single_handle_to_descriptor_range =
        rts_single_handle_to_descriptor_range ? 1 : 0;
    if (num_render_target_descriptors != 0) {
      std::memcpy(args.render_target_descriptors, render_target_descriptors,
                  (rts_single_handle_to_descriptor_range
                       ? 1
                       : num_render_target_descriptors) *
                      sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));
    }
    args.depth_stencil = (depth_stencil_descriptor != nullptr) ? 1 : 0;
    if (depth_stencil_descriptor != nullptr) {
      args.depth_stencil_descriptor.ptr = depth_stencil_descriptor->ptr;
    }
  }

  inline void D3DOMSetStencilRef(UINT stencil_ref) {
    auto& arg = *reinterpret_cast<UINT*>(
        WriteCommand(Command::kD3DOMSetStencilRef, sizeof(UINT)));
    arg = stencil_ref;
  }

  inline void D3DResourceBarrier(UINT num_barriers,
                                 const D3D12_RESOURCE_BARRIER* barriers) {
    if (num_barriers == 0) {
      return;
    }
    const size_t header_size =
        xe::align(sizeof(UINT), alignof(D3D12_RESOURCE_BARRIER));
    uint8_t* args = reinterpret_cast<uint8_t*>(WriteCommand(
        Command::kD3DResourceBarrier,
        header_size + num_barriers * sizeof(D3D12_RESOURCE_BARRIER)));
    *reinterpret_cast<UINT*>(args) = num_barriers;
    std::memcpy(args + header_size, barriers,
                num_barriers * sizeof(D3D12_RESOURCE_BARRIER));
  }

  inline void RSSetScissorRect(const D3D12_RECT& rect) {
    auto& arg = *reinterpret_cast<D3D12_RECT*>(
        WriteCommand(Command::kRSSetScissorRect, sizeof(D3D12_RECT)));
    arg = rect;
  }

  inline void RSSetViewport(const D3D12_VIEWPORT& viewport) {
    auto& arg = *reinterpret_cast<D3D12_VIEWPORT*>(
        WriteCommand(Command::kRSSetViewport, sizeof(D3D12_VIEWPORT)));
    arg = viewport;
  }

  inline void D3DSetComputeRoot32BitConstants(
      UINT root_parameter_index, UINT num_32bit_values_to_set,
      const void* src_data, UINT dest_offset_in_32bit_values) {
    if (num_32bit_values_to_set == 0) {
      return;
    }
    auto args = reinterpret_cast<SetRoot32BitConstantsHeader*>(
        WriteCommand(Command::kD3DSetComputeRoot32BitConstants,
                     sizeof(SetRoot32BitConstantsHeader) +
                         num_32bit_values_to_set * sizeof(uint32_t)));
    args->root_parameter_index = root_parameter_index;
    args->num_32bit_values_to_set = num_32bit_values_to_set;
    args->dest_offset_in_32bit_values = dest_offset_in_32bit_values;
    std::memcpy(args + 1, src_data, num_32bit_values_to_set * sizeof(uint32_t));
  }

  inline void D3DSetGraphicsRoot32BitConstants(
      UINT root_parameter_index, UINT num_32bit_values_to_set,
      const void* src_data, UINT dest_offset_in_32bit_values) {
    if (num_32bit_values_to_set == 0) {
      return;
    }
    auto args = reinterpret_cast<SetRoot32BitConstantsHeader*>(
        WriteCommand(Command::kD3DSetGraphicsRoot32BitConstants,
                     sizeof(SetRoot32BitConstantsHeader) +
                         num_32bit_values_to_set * sizeof(uint32_t)));
    args->root_parameter_index = root_parameter_index;
    args->num_32bit_values_to_set = num_32bit_values_to_set;
    args->dest_offset_in_32bit_values = dest_offset_in_32bit_values;
    std::memcpy(args + 1, src_data, num_32bit_values_to_set * sizeof(uint32_t));
  }

  inline void D3DSetComputeRootConstantBufferView(
      UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS buffer_location) {
    auto& args = *reinterpret_cast<SetRootConstantBufferViewArguments*>(
        WriteCommand(Command::kD3DSetComputeRootConstantBufferView,
                     sizeof(SetRootConstantBufferViewArguments)));
    args.root_parameter_index = root_parameter_index;
    args.buffer_location = buffer_location;
  }

  inline void D3DSetGraphicsRootConstantBufferView(
      UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS buffer_location) {
    auto& args = *reinterpret_cast<SetRootConstantBufferViewArguments*>(
        WriteCommand(Command::kD3DSetGraphicsRootConstantBufferView,
                     sizeof(SetRootConstantBufferViewArguments)));
    args.root_parameter_index = root_parameter_index;
    args.buffer_location = buffer_location;
  }

  inline void D3DSetComputeRootDescriptorTable(
      UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor) {
    auto& args = *reinterpret_cast<SetRootDescriptorTableArguments*>(
        WriteCommand(Command::kD3DSetComputeRootDescriptorTable,
                     sizeof(SetRootDescriptorTableArguments)));
    args.root_parameter_index = root_parameter_index;
    args.base_descriptor.ptr = base_descriptor.ptr;
  }

  inline void D3DSetGraphicsRootDescriptorTable(
      UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor) {
    auto& args = *reinterpret_cast<SetRootDescriptorTableArguments*>(
        WriteCommand(Command::kD3DSetGraphicsRootDescriptorTable,
                     sizeof(SetRootDescriptorTableArguments)));
    args.root_parameter_index = root_parameter_index;
    args.base_descriptor.ptr = base_descriptor.ptr;
  }

  inline void D3DSetComputeRootSignature(ID3D12RootSignature* root_signature) {
    auto& arg = *reinterpret_cast<ID3D12RootSignature**>(WriteCommand(
        Command::kD3DSetComputeRootSignature, sizeof(ID3D12RootSignature*)));
    arg = root_signature;
  }

  inline void D3DSetGraphicsRootSignature(ID3D12RootSignature* root_signature) {
    auto& arg = *reinterpret_cast<ID3D12RootSignature**>(WriteCommand(
        Command::kD3DSetGraphicsRootSignature, sizeof(ID3D12RootSignature*)));
    arg = root_signature;
  }

  inline void SetDescriptorHeaps(
      ID3D12DescriptorHeap* cbv_srv_uav_descriptor_heap,
      ID3D12DescriptorHeap* sampler_descriptor_heap) {
    auto& args = *reinterpret_cast<SetDescriptorHeapsArguments*>(WriteCommand(
        Command::kSetDescriptorHeaps, sizeof(SetDescriptorHeapsArguments)));
    args.cbv_srv_uav_descriptor_heap = cbv_srv_uav_descriptor_heap;
    args.sampler_descriptor_heap = sampler_descriptor_heap;
  }

  inline void D3DSetPipelineState(ID3D12PipelineState* pipeline_state) {
    auto& arg = *reinterpret_cast<ID3D12PipelineState**>(WriteCommand(
        Command::kD3DSetPipelineState, sizeof(ID3D12PipelineState*)));
    arg = pipeline_state;
  }

  inline void SetPipelineStateHandle(void* pipeline_state_handle) {
    auto& arg = *reinterpret_cast<void**>(
        WriteCommand(Command::kSetPipelineStateHandle, sizeof(void*)));
    arg = pipeline_state_handle;
  }

  inline void D3DSetSamplePositions(
      UINT num_samples_per_pixel, UINT num_pixels,
      const D3D12_SAMPLE_POSITION* sample_positions) {
    auto& args = *reinterpret_cast<D3DSetSamplePositionsArguments*>(
        WriteCommand(Command::kD3DSetSamplePositions,
                     sizeof(D3DSetSamplePositionsArguments)));
    args.num_samples_per_pixel = num_samples_per_pixel;
    args.num_pixels = num_pixels;
    std::memcpy(args.sample_positions, sample_positions,
                std::min(num_samples_per_pixel * num_pixels, UINT(16)) *
                    sizeof(D3D12_SAMPLE_POSITION));
  }

 private:
  static constexpr size_t kAlignment = std::max(sizeof(void*), sizeof(UINT64));

  enum class Command : uint32_t {
    kD3DCopyBufferRegion,
    kD3DCopyResource,
    kCopyTexture,
    kD3DDispatch,
    kD3DDrawIndexedInstanced,
    kD3DDrawInstanced,
    kD3DIASetIndexBuffer,
    kD3DIASetPrimitiveTopology,
    kD3DOMSetBlendFactor,
    kD3DOMSetRenderTargets,
    kD3DOMSetStencilRef,
    kD3DResourceBarrier,
    kRSSetScissorRect,
    kRSSetViewport,
    kD3DSetComputeRoot32BitConstants,
    kD3DSetGraphicsRoot32BitConstants,
    kD3DSetComputeRootConstantBufferView,
    kD3DSetGraphicsRootConstantBufferView,
    kD3DSetComputeRootDescriptorTable,
    kD3DSetGraphicsRootDescriptorTable,
    kD3DSetComputeRootSignature,
    kD3DSetGraphicsRootSignature,
    kSetDescriptorHeaps,
    kD3DSetPipelineState,
    kSetPipelineStateHandle,
    kD3DSetSamplePositions,
  };

  struct D3DCopyBufferRegionArguments {
    ID3D12Resource* dst_buffer;
    UINT64 dst_offset;
    ID3D12Resource* src_buffer;
    UINT64 src_offset;
    UINT64 num_bytes;
  };

  struct D3DCopyResourceArguments {
    ID3D12Resource* dst_resource;
    ID3D12Resource* src_resource;
  };

  struct CopyTextureArguments {
    D3D12_TEXTURE_COPY_LOCATION dst;
    D3D12_TEXTURE_COPY_LOCATION src;
  };

  struct D3DDispatchArguments {
    UINT thread_group_count_x;
    UINT thread_group_count_y;
    UINT thread_group_count_z;
  };

  struct D3DDrawIndexedInstancedArguments {
    UINT index_count_per_instance;
    UINT instance_count;
    UINT start_index_location;
    INT base_vertex_location;
    UINT start_instance_location;
  };

  struct D3DDrawInstancedArguments {
    UINT vertex_count_per_instance;
    UINT instance_count;
    UINT start_vertex_location;
    UINT start_instance_location;
  };

  struct D3DOMSetRenderTargetsArguments {
    uint8_t num_render_target_descriptors;
    bool rts_single_handle_to_descriptor_range;
    bool depth_stencil;
    D3D12_CPU_DESCRIPTOR_HANDLE
    render_target_descriptors[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    D3D12_CPU_DESCRIPTOR_HANDLE depth_stencil_descriptor;
  };

  struct SetRoot32BitConstantsHeader {
    UINT root_parameter_index;
    UINT num_32bit_values_to_set;
    UINT dest_offset_in_32bit_values;
  };

  struct SetRootConstantBufferViewArguments {
    UINT root_parameter_index;
    D3D12_GPU_VIRTUAL_ADDRESS buffer_location;
  };

  struct SetRootDescriptorTableArguments {
    UINT root_parameter_index;
    D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor;
  };

  struct SetDescriptorHeapsArguments {
    ID3D12DescriptorHeap* cbv_srv_uav_descriptor_heap;
    ID3D12DescriptorHeap* sampler_descriptor_heap;
  };

  struct D3DSetSamplePositionsArguments {
    UINT num_samples_per_pixel;
    UINT num_pixels;
    D3D12_SAMPLE_POSITION sample_positions[16];
  };

  void* WriteCommand(Command command, size_t arguments_size);

  D3D12CommandProcessor* command_processor_;

  std::vector<uint8_t> command_stream_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_DEFERRED_COMMAND_LIST_H_

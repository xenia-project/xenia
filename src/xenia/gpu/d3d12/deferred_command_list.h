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
#include <cstdint>
#include <cstring>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/literals.h"
#include "xenia/base/math.h"
#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/base/memory.h"
namespace xe {
namespace gpu {
namespace d3d12 {

using namespace xe::literals;

class D3D12CommandProcessor;

class DeferredCommandList {
 public:
  static constexpr size_t MAX_SIZEOF_COMMANDLIST = 65536 * 128; //around 8 mb
  /*
	chrispy: upped from 1_MiB to 4_MiB, m:durandal hits frequent resizes in large open maps
  */
  DeferredCommandList(const D3D12CommandProcessor& command_processor,
                      size_t initial_size_bytes = MAX_SIZEOF_COMMANDLIST);

  void Reset();
  void Execute(ID3D12GraphicsCommandList* command_list,
               ID3D12GraphicsCommandList1* command_list_1);

  D3D12_RECT* ClearDepthStencilViewAllocatedRects(
      D3D12_CPU_DESCRIPTOR_HANDLE depth_stencil_view,
      D3D12_CLEAR_FLAGS clear_flags, FLOAT depth, UINT8 stencil,
      UINT num_rects) {
    auto args = reinterpret_cast<ClearDepthStencilViewHeader*>(WriteCommand(
        Command::kD3DClearDepthStencilView,
        sizeof(ClearDepthStencilViewHeader) + num_rects * sizeof(D3D12_RECT)));
    args->depth_stencil_view = depth_stencil_view;
    args->clear_flags = clear_flags;
    args->depth = depth;
    args->stencil = stencil;
    args->num_rects = num_rects;
    return num_rects ? reinterpret_cast<D3D12_RECT*>(args + 1) : nullptr;
  }

  void D3DClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE depth_stencil_view,
                                D3D12_CLEAR_FLAGS clear_flags, FLOAT depth,
                                UINT8 stencil, UINT num_rects,
                                const D3D12_RECT* rects) {
    D3D12_RECT* allocated_rects = ClearDepthStencilViewAllocatedRects(
        depth_stencil_view, clear_flags, depth, stencil, num_rects);
    if (num_rects) {
      assert_not_null(allocated_rects);
      std::memcpy(allocated_rects, rects, num_rects * sizeof(D3D12_RECT));
    }
  }

  void D3DClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE render_target_view,
                                const FLOAT color_rgba[4], UINT num_rects,
                                const D3D12_RECT* rects) {
    auto args = reinterpret_cast<ClearRenderTargetViewHeader*>(WriteCommand(
        Command::kD3DClearRenderTargetView,
        sizeof(ClearRenderTargetViewHeader) + num_rects * sizeof(D3D12_RECT)));
    args->render_target_view = render_target_view;
    std::memcpy(args->color_rgba, color_rgba, 4 * sizeof(FLOAT));
    args->num_rects = num_rects;
    if (num_rects != 0) {
      std::memcpy(args + 1, rects, num_rects * sizeof(D3D12_RECT));
    }
  }

  void D3DClearUnorderedAccessViewUint(
      D3D12_GPU_DESCRIPTOR_HANDLE view_gpu_handle_in_current_heap,
      D3D12_CPU_DESCRIPTOR_HANDLE view_cpu_handle, ID3D12Resource* resource,
      const UINT values[4], UINT num_rects, const D3D12_RECT* rects) {
    auto args = reinterpret_cast<ClearUnorderedAccessViewHeader*>(
        WriteCommand(Command::kD3DClearUnorderedAccessViewUint,
                     sizeof(ClearUnorderedAccessViewHeader) +
                         num_rects * sizeof(D3D12_RECT)));
    args->view_gpu_handle_in_current_heap = view_gpu_handle_in_current_heap;
    args->view_cpu_handle = view_cpu_handle;
    args->resource = resource;
    std::memcpy(args->values_uint, values, 4 * sizeof(UINT));
    args->num_rects = num_rects;
    if (num_rects != 0) {
      std::memcpy(args + 1, rects, num_rects * sizeof(D3D12_RECT));
    }
  }

  void D3DCopyBufferRegion(ID3D12Resource* dst_buffer, UINT64 dst_offset,
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

  void D3DCopyResource(ID3D12Resource* dst_resource,
                       ID3D12Resource* src_resource) {
    auto& args = *reinterpret_cast<D3DCopyResourceArguments*>(WriteCommand(
        Command::kD3DCopyResource, sizeof(D3DCopyResourceArguments)));
    args.dst_resource = dst_resource;
    args.src_resource = src_resource;
  }

  void CopyTexture(const D3D12_TEXTURE_COPY_LOCATION& dst,
                   const D3D12_TEXTURE_COPY_LOCATION& src) {
    auto& args = *reinterpret_cast<CopyTextureArguments*>(
        WriteCommand(Command::kCopyTexture, sizeof(CopyTextureArguments)));
    std::memcpy(&args.dst, &dst, sizeof(D3D12_TEXTURE_COPY_LOCATION));
    std::memcpy(&args.src, &src, sizeof(D3D12_TEXTURE_COPY_LOCATION));
  }

  void D3DCopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* dst, UINT dst_x,
                            UINT dst_y, UINT dst_z,
                            const D3D12_TEXTURE_COPY_LOCATION* src,
                            const D3D12_BOX* src_box) {
    assert_not_null(dst);
    assert_not_null(src);
    auto& args = *reinterpret_cast<D3DCopyTextureRegionArguments*>(WriteCommand(
        Command::kD3DCopyTextureRegion, sizeof(D3DCopyTextureRegionArguments)));
    std::memcpy(&args.dst, dst, sizeof(D3D12_TEXTURE_COPY_LOCATION));
    args.dst_x = dst_x;
    args.dst_y = dst_y;
    args.dst_z = dst_z;
    std::memcpy(&args.src, src, sizeof(D3D12_TEXTURE_COPY_LOCATION));
    if (src_box) {
      args.has_src_box = true;
      args.src_box = *src_box;
    } else {
      args.has_src_box = false;
    }
  }

  void D3DDispatch(UINT thread_group_count_x, UINT thread_group_count_y,
                   UINT thread_group_count_z) {
    auto& args = *reinterpret_cast<D3DDispatchArguments*>(
        WriteCommand(Command::kD3DDispatch, sizeof(D3DDispatchArguments)));
    args.thread_group_count_x = thread_group_count_x;
    args.thread_group_count_y = thread_group_count_y;
    args.thread_group_count_z = thread_group_count_z;
  }

  void D3DDrawIndexedInstanced(UINT index_count_per_instance,
                               UINT instance_count, UINT start_index_location,
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

  void D3DDrawInstanced(UINT vertex_count_per_instance, UINT instance_count,
                        UINT start_vertex_location,
                        UINT start_instance_location) {
    auto& args = *reinterpret_cast<D3DDrawInstancedArguments*>(WriteCommand(
        Command::kD3DDrawInstanced, sizeof(D3DDrawInstancedArguments)));
    args.vertex_count_per_instance = vertex_count_per_instance;
    args.instance_count = instance_count;
    args.start_vertex_location = start_vertex_location;
    args.start_instance_location = start_instance_location;
  }

  void D3DIASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* view) {
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

  void D3DIASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY primitive_topology) {
    auto& arg = *reinterpret_cast<D3D12_PRIMITIVE_TOPOLOGY*>(WriteCommand(
        Command::kD3DIASetPrimitiveTopology, sizeof(D3D12_PRIMITIVE_TOPOLOGY)));
    arg = primitive_topology;
  }

  void D3DIASetVertexBuffers(UINT start_slot, UINT num_views,
                             const D3D12_VERTEX_BUFFER_VIEW* views) {
    if (num_views == 0) {
      return;
    }
    static_assert(alignof(D3D12_VERTEX_BUFFER_VIEW) <= alignof(uintmax_t));
    const size_t header_size = xe::align(sizeof(D3DIASetVertexBuffersHeader),
                                         alignof(D3D12_VERTEX_BUFFER_VIEW));
    auto args = reinterpret_cast<D3DIASetVertexBuffersHeader*>(WriteCommand(
        Command::kD3DIASetVertexBuffers,
        header_size + num_views * sizeof(D3D12_VERTEX_BUFFER_VIEW)));
    args->start_slot = start_slot;
    args->num_views = num_views;
    std::memcpy(reinterpret_cast<uint8_t*>(args) + header_size, views,
                sizeof(D3D12_VERTEX_BUFFER_VIEW) * num_views);
  }

  void D3DOMSetBlendFactor(const FLOAT blend_factor[4]) {
    auto args = reinterpret_cast<FLOAT*>(
        WriteCommand(Command::kD3DOMSetBlendFactor, 4 * sizeof(FLOAT)));
    args[0] = blend_factor[0];
    args[1] = blend_factor[1];
    args[2] = blend_factor[2];
    args[3] = blend_factor[3];
  }

  void D3DOMSetRenderTargets(
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

  void D3DOMSetStencilRef(UINT stencil_ref) {
    auto& arg = *reinterpret_cast<UINT*>(
        WriteCommand(Command::kD3DOMSetStencilRef, sizeof(UINT)));
    arg = stencil_ref;
  }

  void D3DResourceBarrier(UINT num_barriers,
                          const D3D12_RESOURCE_BARRIER* barriers) {
    if (num_barriers == 0) {
      return;
    }
    static_assert(alignof(D3D12_RESOURCE_BARRIER) <= alignof(uintmax_t));
    const size_t header_size =
        xe::align(sizeof(UINT), alignof(D3D12_RESOURCE_BARRIER));
    uint8_t* args = reinterpret_cast<uint8_t*>(WriteCommand(
        Command::kD3DResourceBarrier,
        header_size + num_barriers * sizeof(D3D12_RESOURCE_BARRIER)));
    *reinterpret_cast<UINT*>(args) = num_barriers;
    std::memcpy(args + header_size, barriers,
                num_barriers * sizeof(D3D12_RESOURCE_BARRIER));
  }

  void RSSetScissorRect(const D3D12_RECT& rect) {
    auto& arg = *reinterpret_cast<D3D12_RECT*>(
        WriteCommand(Command::kRSSetScissorRect, sizeof(D3D12_RECT)));
    arg = rect;
  }

  void RSSetViewport(const D3D12_VIEWPORT& viewport) {
    auto& arg = *reinterpret_cast<D3D12_VIEWPORT*>(
        WriteCommand(Command::kRSSetViewport, sizeof(D3D12_VIEWPORT)));
    arg = viewport;
  }

  void D3DSetComputeRoot32BitConstants(UINT root_parameter_index,
                                       UINT num_32bit_values_to_set,
                                       const void* src_data,
                                       UINT dest_offset_in_32bit_values) {
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

  void D3DSetGraphicsRoot32BitConstants(UINT root_parameter_index,
                                        UINT num_32bit_values_to_set,
                                        const void* src_data,
                                        UINT dest_offset_in_32bit_values) {
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

  void D3DSetComputeRootConstantBufferView(
      UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS buffer_location) {
    auto& args = *reinterpret_cast<SetRootConstantBufferViewArguments*>(
        WriteCommand(Command::kD3DSetComputeRootConstantBufferView,
                     sizeof(SetRootConstantBufferViewArguments)));
    args.root_parameter_index = root_parameter_index;
    args.buffer_location = buffer_location;
  }

  void D3DSetGraphicsRootConstantBufferView(
      UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS buffer_location) {
    auto& args = *reinterpret_cast<SetRootConstantBufferViewArguments*>(
        WriteCommand(Command::kD3DSetGraphicsRootConstantBufferView,
                     sizeof(SetRootConstantBufferViewArguments)));
    args.root_parameter_index = root_parameter_index;
    args.buffer_location = buffer_location;
  }

  void D3DSetComputeRootDescriptorTable(
      UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor) {
    auto& args = *reinterpret_cast<SetRootDescriptorTableArguments*>(
        WriteCommand(Command::kD3DSetComputeRootDescriptorTable,
                     sizeof(SetRootDescriptorTableArguments)));
    args.root_parameter_index = root_parameter_index;
    args.base_descriptor.ptr = base_descriptor.ptr;
  }

  void D3DSetGraphicsRootDescriptorTable(
      UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor) {
    auto& args = *reinterpret_cast<SetRootDescriptorTableArguments*>(
        WriteCommand(Command::kD3DSetGraphicsRootDescriptorTable,
                     sizeof(SetRootDescriptorTableArguments)));
    args.root_parameter_index = root_parameter_index;
    args.base_descriptor.ptr = base_descriptor.ptr;
  }

  void D3DSetComputeRootSignature(ID3D12RootSignature* root_signature) {
    auto& arg = *reinterpret_cast<ID3D12RootSignature**>(WriteCommand(
        Command::kD3DSetComputeRootSignature, sizeof(ID3D12RootSignature*)));
    arg = root_signature;
  }

  void D3DSetGraphicsRootSignature(ID3D12RootSignature* root_signature) {
    auto& arg = *reinterpret_cast<ID3D12RootSignature**>(WriteCommand(
        Command::kD3DSetGraphicsRootSignature, sizeof(ID3D12RootSignature*)));
    arg = root_signature;
  }

  void SetDescriptorHeaps(ID3D12DescriptorHeap* cbv_srv_uav_descriptor_heap,
                          ID3D12DescriptorHeap* sampler_descriptor_heap) {
    auto& args = *reinterpret_cast<SetDescriptorHeapsArguments*>(WriteCommand(
        Command::kSetDescriptorHeaps, sizeof(SetDescriptorHeapsArguments)));
    args.cbv_srv_uav_descriptor_heap = cbv_srv_uav_descriptor_heap;
    args.sampler_descriptor_heap = sampler_descriptor_heap;
  }

  void D3DSetPipelineState(ID3D12PipelineState* pipeline_state) {
    auto& arg = *reinterpret_cast<ID3D12PipelineState**>(WriteCommand(
        Command::kD3DSetPipelineState, sizeof(ID3D12PipelineState*)));
    arg = pipeline_state;
  }

  void SetPipelineStateHandle(void* pipeline_state_handle) {
    auto& arg = *reinterpret_cast<void**>(
        WriteCommand(Command::kSetPipelineStateHandle, sizeof(void*)));
    arg = pipeline_state_handle;
  }

  void D3DSetSamplePositions(UINT num_samples_per_pixel, UINT num_pixels,
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
  enum class Command {
    kD3DClearDepthStencilView,
    kD3DClearRenderTargetView,
    kD3DClearUnorderedAccessViewUint,
    kD3DCopyBufferRegion,
    kD3DCopyResource,
    kCopyTexture,
    kD3DCopyTextureRegion,
    kD3DDispatch,
    kD3DDrawIndexedInstanced,
    kD3DDrawInstanced,
    kD3DIASetIndexBuffer,
    kD3DIASetPrimitiveTopology,
    kD3DIASetVertexBuffers,
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

  struct CommandHeader {
    Command command;
    uint32_t arguments_size_elements;
  };
  static constexpr size_t kCommandHeaderSizeElements =
      (sizeof(CommandHeader) + sizeof(uintmax_t) - 1) / sizeof(uintmax_t);

  struct ClearDepthStencilViewHeader {
    D3D12_CPU_DESCRIPTOR_HANDLE depth_stencil_view;
    D3D12_CLEAR_FLAGS clear_flags;
    FLOAT depth;
    UINT8 stencil;
    UINT num_rects;
  };

  struct ClearRenderTargetViewHeader {
    D3D12_CPU_DESCRIPTOR_HANDLE render_target_view;
    FLOAT color_rgba[4];
    UINT num_rects;
  };

  struct ClearUnorderedAccessViewHeader {
    D3D12_GPU_DESCRIPTOR_HANDLE view_gpu_handle_in_current_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE view_cpu_handle;
    ID3D12Resource* resource;
    union {
      FLOAT values_float[4];
      UINT values_uint[4];
    };
    UINT num_rects;
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

  struct D3DCopyTextureRegionArguments {
    D3D12_TEXTURE_COPY_LOCATION dst;
    UINT dst_x;
    UINT dst_y;
    UINT dst_z;
    D3D12_TEXTURE_COPY_LOCATION src;
    D3D12_BOX src_box;
    bool has_src_box;
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

  struct D3DIASetVertexBuffersHeader {
    UINT start_slot;
    UINT num_views;
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

  void* WriteCommand(Command command, size_t arguments_size_bytes);

  const D3D12CommandProcessor& command_processor_;

  // uintmax_t to ensure uint64_t and pointer alignment of all structures.
  //std::vector<uintmax_t> command_stream_;
  FixedVMemVector<MAX_SIZEOF_COMMANDLIST> command_stream_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_DEFERRED_COMMAND_LIST_H_

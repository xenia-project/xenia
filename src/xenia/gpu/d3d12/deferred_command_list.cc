/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/deferred_command_list.h"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"

namespace xe {
namespace gpu {
namespace d3d12 {

constexpr size_t DeferredCommandList::kAlignment;

DeferredCommandList::DeferredCommandList(
    D3D12CommandProcessor* command_processor, size_t initial_size)
    : command_processor_(command_processor) {
  command_stream_.reserve(initial_size);
}

void DeferredCommandList::Reset() { command_stream_.clear(); }

void DeferredCommandList::Execute(ID3D12GraphicsCommandList* command_list,
                                  ID3D12GraphicsCommandList1* command_list_1) {
  const uint8_t* stream = command_stream_.data();
  size_t stream_remaining = command_stream_.size();
  ID3D12PipelineState* current_pipeline_state = nullptr;
  while (stream_remaining != 0) {
    const uint32_t* header = reinterpret_cast<const uint32_t*>(stream);
    const size_t header_size = xe::align(2 * sizeof(uint32_t), kAlignment);
    stream += header_size;
    stream_remaining -= header_size;
    switch (Command(header[0])) {
      case Command::kD3DCopyBufferRegion: {
        auto& args =
            *reinterpret_cast<const D3DCopyBufferRegionArguments*>(stream);
        command_list->CopyBufferRegion(args.dst_buffer, args.dst_offset,
                                       args.src_buffer, args.src_offset,
                                       args.num_bytes);
      } break;
      case Command::kD3DCopyResource: {
        auto& args = *reinterpret_cast<const D3DCopyResourceArguments*>(stream);
        command_list->CopyResource(args.dst_resource, args.src_resource);
      } break;
      case Command::kCopyTexture: {
        auto& args = *reinterpret_cast<const CopyTextureArguments*>(stream);
        command_list->CopyTextureRegion(&args.dst, 0, 0, 0, &args.src, nullptr);
      } break;
      case Command::kD3DDispatch: {
        if (current_pipeline_state != nullptr) {
          auto& args = *reinterpret_cast<const D3DDispatchArguments*>(stream);
          command_list->Dispatch(args.thread_group_count_x,
                                 args.thread_group_count_y,
                                 args.thread_group_count_z);
        }
      } break;
      case Command::kD3DDrawIndexedInstanced: {
        if (current_pipeline_state != nullptr) {
          auto& args =
              *reinterpret_cast<const D3DDrawIndexedInstancedArguments*>(
                  stream);
          command_list->DrawIndexedInstanced(
              args.index_count_per_instance, args.instance_count,
              args.start_index_location, args.base_vertex_location,
              args.start_instance_location);
        }
      } break;
      case Command::kD3DDrawInstanced: {
        if (current_pipeline_state != nullptr) {
          auto& args =
              *reinterpret_cast<const D3DDrawInstancedArguments*>(stream);
          command_list->DrawInstanced(
              args.vertex_count_per_instance, args.instance_count,
              args.start_vertex_location, args.start_instance_location);
        }
      } break;
      case Command::kD3DIASetIndexBuffer: {
        auto view = reinterpret_cast<const D3D12_INDEX_BUFFER_VIEW*>(stream);
        command_list->IASetIndexBuffer(
            view->Format != DXGI_FORMAT_UNKNOWN ? view : nullptr);
      } break;
      case Command::kD3DIASetPrimitiveTopology: {
        command_list->IASetPrimitiveTopology(
            *reinterpret_cast<const D3D12_PRIMITIVE_TOPOLOGY*>(stream));
      } break;
      case Command::kD3DOMSetBlendFactor: {
        command_list->OMSetBlendFactor(reinterpret_cast<const FLOAT*>(stream));
      } break;
      case Command::kD3DOMSetRenderTargets: {
        auto& args =
            *reinterpret_cast<const D3DOMSetRenderTargetsArguments*>(stream);
        command_list->OMSetRenderTargets(
            args.num_render_target_descriptors, args.render_target_descriptors,
            args.rts_single_handle_to_descriptor_range ? TRUE : FALSE,
            args.depth_stencil ? &args.depth_stencil_descriptor : nullptr);
      } break;
      case Command::kD3DOMSetStencilRef: {
        command_list->OMSetStencilRef(*reinterpret_cast<const UINT*>(stream));
      } break;
      case Command::kD3DResourceBarrier: {
        command_list->ResourceBarrier(
            *reinterpret_cast<const UINT*>(stream),
            reinterpret_cast<const D3D12_RESOURCE_BARRIER*>(
                stream +
                xe::align(sizeof(UINT), alignof(D3D12_RESOURCE_BARRIER))));
      } break;
      case Command::kRSSetScissorRect: {
        command_list->RSSetScissorRects(
            1, reinterpret_cast<const D3D12_RECT*>(stream));
      } break;
      case Command::kRSSetViewport: {
        command_list->RSSetViewports(
            1, reinterpret_cast<const D3D12_VIEWPORT*>(stream));
      } break;
      case Command::kD3DSetComputeRoot32BitConstants: {
        auto args =
            reinterpret_cast<const SetRoot32BitConstantsHeader*>(stream);
        command_list->SetComputeRoot32BitConstants(
            args->root_parameter_index, args->num_32bit_values_to_set, args + 1,
            args->dest_offset_in_32bit_values);
      } break;
      case Command::kD3DSetGraphicsRoot32BitConstants: {
        auto args =
            reinterpret_cast<const SetRoot32BitConstantsHeader*>(stream);
        command_list->SetGraphicsRoot32BitConstants(
            args->root_parameter_index, args->num_32bit_values_to_set, args + 1,
            args->dest_offset_in_32bit_values);
      } break;
      case Command::kD3DSetComputeRootConstantBufferView: {
        auto& args =
            *reinterpret_cast<const SetRootConstantBufferViewArguments*>(
                stream);
        command_list->SetComputeRootConstantBufferView(
            args.root_parameter_index, args.buffer_location);
      } break;
      case Command::kD3DSetGraphicsRootConstantBufferView: {
        auto& args =
            *reinterpret_cast<const SetRootConstantBufferViewArguments*>(
                stream);
        command_list->SetGraphicsRootConstantBufferView(
            args.root_parameter_index, args.buffer_location);
      } break;
      case Command::kD3DSetComputeRootDescriptorTable: {
        auto& args =
            *reinterpret_cast<const SetRootDescriptorTableArguments*>(stream);
        command_list->SetComputeRootDescriptorTable(args.root_parameter_index,
                                                    args.base_descriptor);
      } break;
      case Command::kD3DSetGraphicsRootDescriptorTable: {
        auto& args =
            *reinterpret_cast<const SetRootDescriptorTableArguments*>(stream);
        command_list->SetGraphicsRootDescriptorTable(args.root_parameter_index,
                                                     args.base_descriptor);
      } break;
      case Command::kD3DSetComputeRootSignature: {
        command_list->SetComputeRootSignature(
            *reinterpret_cast<ID3D12RootSignature* const*>(stream));
      } break;
      case Command::kD3DSetGraphicsRootSignature: {
        command_list->SetGraphicsRootSignature(
            *reinterpret_cast<ID3D12RootSignature* const*>(stream));
      } break;
      case Command::kSetDescriptorHeaps: {
        auto& args =
            *reinterpret_cast<const SetDescriptorHeapsArguments*>(stream);
        UINT num_descriptor_heaps = 0;
        ID3D12DescriptorHeap* descriptor_heaps[2];
        if (args.cbv_srv_uav_descriptor_heap != nullptr) {
          descriptor_heaps[num_descriptor_heaps++] =
              args.cbv_srv_uav_descriptor_heap;
        }
        if (args.sampler_descriptor_heap != nullptr) {
          descriptor_heaps[num_descriptor_heaps++] =
              args.sampler_descriptor_heap;
        }
        command_list->SetDescriptorHeaps(num_descriptor_heaps,
                                         descriptor_heaps);
      } break;
      case Command::kD3DSetPipelineState: {
        current_pipeline_state =
            *reinterpret_cast<ID3D12PipelineState* const*>(stream);
        command_list->SetPipelineState(current_pipeline_state);
      } break;
      case Command::kSetPipelineStateHandle: {
        current_pipeline_state = command_processor_->GetPipelineStateByHandle(
            *reinterpret_cast<void* const*>(stream));
        command_list->SetPipelineState(current_pipeline_state);
      } break;
      case Command::kD3DSetSamplePositions: {
        if (command_list_1 != nullptr) {
          auto& args =
              *reinterpret_cast<const D3DSetSamplePositionsArguments*>(stream);
          command_list_1->SetSamplePositions(
              args.num_samples_per_pixel, args.num_pixels,
              const_cast<D3D12_SAMPLE_POSITION*>(args.sample_positions));
        }
      } break;
      default:
        assert_unhandled_case(Command(header[0]));
        break;
    }
    stream += header[1];
    stream_remaining -= header[1];
  }
}

void* DeferredCommandList::WriteCommand(Command command,
                                        size_t arguments_size) {
  arguments_size = xe::align(arguments_size, kAlignment);
  const size_t header_size = xe::align(2 * sizeof(uint32_t), kAlignment);
  size_t offset = command_stream_.size();
  command_stream_.resize(offset + header_size + arguments_size);
  uint32_t* header =
      reinterpret_cast<uint32_t*>(command_stream_.data() + offset);
  header[0] = uint32_t(command);
  header[1] = uint32_t(arguments_size);
  return command_stream_.data() + (offset + header_size);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

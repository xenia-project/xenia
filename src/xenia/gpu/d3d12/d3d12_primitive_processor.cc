/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/d3d12_primitive_processor.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/d3d12/deferred_command_list.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/d3d12/d3d12_util.h"

namespace xe {
namespace gpu {
namespace d3d12 {

D3D12PrimitiveProcessor::~D3D12PrimitiveProcessor() { Shutdown(true); }

bool D3D12PrimitiveProcessor::Initialize() {
  if (!InitializeCommon(true, false, false, true, true, true)) {
    Shutdown();
    return false;
  }
  frame_index_buffer_pool_ = std::make_unique<ui::d3d12::D3D12UploadBufferPool>(
      command_processor_.GetD3D12Provider(),
      std::max(size_t(kMinRequiredConvertedIndexBufferSize),
               ui::GraphicsUploadBufferPool::kDefaultPageSize));
  return true;
}

void D3D12PrimitiveProcessor::Shutdown(bool from_destructor) {
  frame_index_buffers_.clear();
  frame_index_buffer_pool_.reset();
  builtin_index_buffer_upload_.Reset();
  builtin_index_buffer_gpu_address_ = 0;
  builtin_index_buffer_.Reset();
  if (!from_destructor) {
    ShutdownCommon();
  }
}

void D3D12PrimitiveProcessor::CompletedSubmissionUpdated() {
  if (builtin_index_buffer_upload_ &&
      command_processor_.GetCompletedSubmission() >=
          builtin_index_buffer_upload_submission_) {
    builtin_index_buffer_upload_.Reset();
  }
}

void D3D12PrimitiveProcessor::BeginSubmission() {
  if (builtin_index_buffer_upload_ &&
      builtin_index_buffer_upload_submission_ == UINT64_MAX) {
    // No need to submit deferred barriers - builtin_index_buffer_ has never
    // been used yet, so it's in the initial state, and
    // builtin_index_buffer_upload_ is in an upload heap, so it's GENERIC_READ.
    command_processor_.GetDeferredCommandList().D3DCopyResource(
        builtin_index_buffer_.Get(), builtin_index_buffer_upload_.Get());
    command_processor_.PushTransitionBarrier(builtin_index_buffer_.Get(),
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_INDEX_BUFFER);
    builtin_index_buffer_upload_submission_ =
        command_processor_.GetCurrentSubmission();
  }
}

void D3D12PrimitiveProcessor::BeginFrame() {
  frame_index_buffer_pool_->Reclaim(command_processor_.GetCompletedFrame());
}

void D3D12PrimitiveProcessor::EndFrame() {
  ClearPerFrameCache();
  frame_index_buffers_.clear();
}

bool D3D12PrimitiveProcessor::InitializeBuiltinIndexBuffer(
    size_t size_bytes, std::function<void(void*)> fill_callback) {
  assert_not_zero(size_bytes);
  assert_null(builtin_index_buffer_);
  assert_null(builtin_index_buffer_upload_);

  const ui::d3d12::D3D12Provider& provider =
      command_processor_.GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();

  D3D12_RESOURCE_DESC resource_desc;
  ui::d3d12::util::FillBufferResourceDesc(resource_desc, UINT64(size_bytes),
                                          D3D12_RESOURCE_FLAG_NONE);
  Microsoft::WRL::ComPtr<ID3D12Resource> draw_resource;
  if (FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesDefault,
          provider.GetHeapFlagCreateNotZeroed(), &resource_desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&draw_resource)))) {
    XELOGE(
        "D3D12 primitive processor: Failed to create the built-in index "
        "buffer GPU resource with {} bytes",
        size_bytes);
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D12Resource> upload_resource;
  if (!provider.CreateUploadResource(
          provider.GetHeapFlagCreateNotZeroed(), &resource_desc,
          D3D12_RESOURCE_STATE_GENERIC_READ, IID_PPV_ARGS(&upload_resource))) {
    XELOGE(
        "D3D12 primitive processor: Failed to create the built-in index "
        "buffer upload resource with {} bytes",
        size_bytes);
    return false;
  }

  D3D12_RANGE upload_read_range = {};
  void* mapping;
  if (FAILED(upload_resource->Map(0, &upload_read_range, &mapping))) {
    XELOGE(
        "D3D12 primitive processor: Failed to map the built-in index buffer "
        "upload resource with {} bytes",
        size_bytes);
    return false;
  }
  fill_callback(reinterpret_cast<uint16_t*>(mapping));
  upload_resource->Unmap(0, nullptr);

  // Successfully created the buffer and wrote the data to upload.
  builtin_index_buffer_ = std::move(draw_resource);
  builtin_index_buffer_gpu_address_ =
      builtin_index_buffer_->GetGPUVirtualAddress();
  builtin_index_buffer_upload_ = std::move(upload_resource);
  // Schedule uploading in the first submission.
  builtin_index_buffer_upload_submission_ = UINT64_MAX;
  return true;
}

void* D3D12PrimitiveProcessor::RequestHostConvertedIndexBufferForCurrentFrame(
    xenos::IndexFormat format, uint32_t index_count, bool coalign_for_simd,
    uint32_t coalignment_original_address, size_t& backend_handle_out) {
  size_t index_size = format == xenos::IndexFormat::kInt16 ? sizeof(uint16_t)
                                                           : sizeof(uint32_t);
  D3D12_GPU_VIRTUAL_ADDRESS gpu_address;
  uint8_t* mapping = frame_index_buffer_pool_->Request(
      command_processor_.GetCurrentFrame(),
      index_size * index_count +
          (coalign_for_simd ? XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE : 0),
      index_size, nullptr, nullptr, &gpu_address);
  if (!mapping) {
    return nullptr;
  }
  if (coalign_for_simd) {
    ptrdiff_t coalignment_offset =
        GetSimdCoalignmentOffset(mapping, coalignment_original_address);
    mapping += coalignment_offset;
    gpu_address = D3D12_GPU_VIRTUAL_ADDRESS(gpu_address + coalignment_offset);
  }
  backend_handle_out = frame_index_buffers_.size();
  frame_index_buffers_.push_back(gpu_address);
  return mapping;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

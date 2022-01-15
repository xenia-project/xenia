/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_D3D12_PRIMITIVE_PROCESSOR_H_
#define XENIA_GPU_D3D12_D3D12_PRIMITIVE_PROCESSOR_H_

#include <cstdint>
#include <deque>
#include <memory>

#include "xenia/base/assert.h"
#include "xenia/gpu/primitive_processor.h"
#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/d3d12/d3d12_upload_buffer_pool.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor;

class D3D12PrimitiveProcessor final : public PrimitiveProcessor {
 public:
  D3D12PrimitiveProcessor(const RegisterFile& register_file, Memory& memory,
                          TraceWriter& trace_writer,
                          SharedMemory& shared_memory,
                          D3D12CommandProcessor& command_processor)
      : PrimitiveProcessor(register_file, memory, trace_writer, shared_memory),
        command_processor_(command_processor) {}
  ~D3D12PrimitiveProcessor();

  bool Initialize();
  void Shutdown(bool from_destructor = false);
  void ClearCache() { frame_index_buffer_pool_->ClearCache(); }

  void CompletedSubmissionUpdated();
  void BeginSubmission();
  void BeginFrame();
  void EndFrame();

  D3D12_GPU_VIRTUAL_ADDRESS GetBuiltinIndexBufferGpuAddress(
      size_t handle) const {
    assert_not_null(builtin_index_buffer_);
    return D3D12_GPU_VIRTUAL_ADDRESS(builtin_index_buffer_gpu_address_ +
                                     GetBuiltinIndexBufferOffsetBytes(handle));
  }
  D3D12_GPU_VIRTUAL_ADDRESS GetConvertedIndexBufferGpuAddress(
      size_t handle) const {
    return frame_index_buffers_[handle];
  }

 protected:
  bool InitializeBuiltin16BitIndexBuffer(
      uint32_t index_count,
      std::function<void(uint16_t*)> fill_callback) override;

  void* RequestHostConvertedIndexBufferForCurrentFrame(
      xenos::IndexFormat format, uint32_t index_count, bool coalign_for_simd,
      uint32_t coalignment_original_address,
      size_t& backend_handle_out) override;

 private:
  D3D12CommandProcessor& command_processor_;

  Microsoft::WRL::ComPtr<ID3D12Resource> builtin_index_buffer_;
  D3D12_GPU_VIRTUAL_ADDRESS builtin_index_buffer_gpu_address_ = 0;
  // Temporary buffer copied in the beginning of the first submission for
  // uploading to builtin_index_buffer_, destroyed when the submission when it
  // was uploaded is completed.
  Microsoft::WRL::ComPtr<ID3D12Resource> builtin_index_buffer_upload_;
  // UINT64_MAX means not uploaded yet and needs uploading in the first
  // submission (if the upload buffer exists at all).
  uint64_t builtin_index_buffer_upload_submission_ = UINT64_MAX;

  std::unique_ptr<ui::d3d12::D3D12UploadBufferPool> frame_index_buffer_pool_;
  // Indexed by the backend handles.
  std::deque<D3D12_GPU_VIRTUAL_ADDRESS> frame_index_buffers_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_PRIMITIVE_PROCESSOR_H_

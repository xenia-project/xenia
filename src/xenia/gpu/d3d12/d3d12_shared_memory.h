/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_D3D12_SHARED_MEMORY_H_
#define XENIA_GPU_D3D12_D3D12_SHARED_MEMORY_H_

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "xenia/gpu/shared_memory.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/memory.h"
#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/d3d12/d3d12_upload_buffer_pool.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor;

class D3D12SharedMemory : public SharedMemory {
 public:
  D3D12SharedMemory(D3D12CommandProcessor& command_processor, Memory& memory,
                    TraceWriter& trace_writer);
  ~D3D12SharedMemory() override;

  bool Initialize();
  void Shutdown(bool from_destructor = false);
  void ClearCache() override;

  ID3D12Resource* GetBuffer() const { return buffer_; }
  D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const {
    return buffer_gpu_address_;
  }

  void CompletedSubmissionUpdated();
  void BeginSubmission();

  // RequestRange may transition the buffer to copy destination - call it before
  // UseForReading or UseForWriting.

  // Makes the buffer usable for vertices, indices and texture untiling.
  void UseForReading() {
    // Vertex fetch is also allowed in pixel shaders.
    CommitUAVWritesAndTransitionBuffer(
        D3D12_RESOURCE_STATE_INDEX_BUFFER |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }
  // Makes the buffer usable for texture tiling after a resolve.
  void UseForWriting() {
    CommitUAVWritesAndTransitionBuffer(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  // Makes the buffer usable as a source for copy commands.
  void UseAsCopySource() {
    CommitUAVWritesAndTransitionBuffer(D3D12_RESOURCE_STATE_COPY_SOURCE);
  }
  // Must be called when doing draws/dispatches modifying data within the shared
  // memory buffer as a UAV, to make sure that when UseForWriting is called the
  // next time, a UAV barrier will be done, and subsequent overlapping UAV
  // writes and reads are ordered.
  void MarkUAVWritesCommitNeeded() {
    if (buffer_state_ == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
      buffer_uav_writes_commit_needed_ = true;
    }
  }

  void WriteRawSRVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void WriteRawUAVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  // Due to the D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP limitation, the
  // smallest supported formats are 32-bit.
  void WriteUintPow2SRVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                  uint32_t element_size_bytes_pow2);
  void WriteUintPow2UAVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                  uint32_t element_size_bytes_pow2);

  // Returns true if any downloads were submitted to the command processor.
  bool InitializeTraceSubmitDownloads();
  void InitializeTraceCompleteDownloads();

 protected:
  bool AllocateSparseHostGpuMemoryRange(uint32_t offset_allocations,
                                        uint32_t length_allocations) override;

  bool UploadRanges(const std::pair<uint32_t, uint32_t>* upload_page_ranges,
                    uint32_t num_ranges) override;

 private:
  D3D12CommandProcessor& command_processor_;
  TraceWriter& trace_writer_;

  // The 512 MB tiled buffer.
  ID3D12Resource* buffer_ = nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS buffer_gpu_address_ = 0;
  std::vector<ID3D12Heap*> buffer_tiled_heaps_;
  D3D12_RESOURCE_STATES buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  bool buffer_uav_writes_commit_needed_ = false;
  void CommitUAVWritesAndTransitionBuffer(D3D12_RESOURCE_STATES new_state);

  // Non-shader-visible buffer descriptor heap for faster binding (via copying
  // rather than creation).
  enum class BufferDescriptorIndex : uint32_t {
    kRawSRV,
    kR32UintSRV,
    kR32G32UintSRV,
    kR32G32B32A32UintSRV,
    kRawUAV,
    kR32UintUAV,
    kR32G32UintUAV,
    kR32G32B32A32UintUAV,

    kCount,
  };
  ID3D12DescriptorHeap* buffer_descriptor_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE buffer_descriptor_heap_start_;

  std::unique_ptr<ui::d3d12::D3D12UploadBufferPool> upload_buffer_pool_;

  // Created temporarily, only for downloading.
  ID3D12Resource* trace_download_buffer_ = nullptr;
  void ResetTraceDownload();
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_D3D12_SHARED_MEMORY_H_

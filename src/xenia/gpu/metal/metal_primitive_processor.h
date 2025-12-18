/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_PRIMITIVE_PROCESSOR_H_
#define XENIA_GPU_METAL_METAL_PRIMITIVE_PROCESSOR_H_

#include <memory>
#include <unordered_map>
#include "third_party/metal-cpp/Metal/Metal.hpp"

#include "xenia/gpu/primitive_processor.h"

namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor;

class MetalPrimitiveProcessor : public PrimitiveProcessor {
 public:
  MetalPrimitiveProcessor(MetalCommandProcessor& command_processor,
                          const RegisterFile& register_file, Memory& memory,
                          TraceWriter& trace_writer,
                          SharedMemory& shared_memory);
  ~MetalPrimitiveProcessor();

  bool Initialize();
  void Shutdown(bool from_destructor = false);

  void CompletedSubmissionUpdated();
  void BeginSubmission();
  void BeginFrame();
  void EndFrame();

  MTL::Buffer* GetBuiltinIndexBuffer() const { return builtin_index_buffer_; }
  MTL::Buffer* GetConvertedIndexBuffer(size_t handle,
                                       uint64_t& offset_bytes_out) const;

 protected:
  bool InitializeBuiltinIndexBuffer(
      size_t size_bytes, std::function<void(void*)> fill_callback) override;

  void* RequestHostConvertedIndexBufferForCurrentFrame(
      xenos::IndexFormat format, uint32_t index_count, bool coalign_for_simd,
      uint32_t coalignment_original_address,
      size_t& backend_handle_out) override;

 private:
  MetalCommandProcessor& command_processor_;

  struct ConvertedIndexBufferBinding {
    MTL::Buffer* buffer = nullptr;
    uint64_t offset_bytes = 0;
  };

  std::vector<ConvertedIndexBufferBinding> converted_index_buffers_;
  uint64_t current_frame_ = 0;

  // Built-in index buffer for primitive type conversion
  MTL::Buffer* builtin_index_buffer_ = nullptr;
  uint64_t builtin_index_buffer_gpu_address_ = 0;

  // Per-frame index buffer for primitive conversion
  struct FrameIndexBuffer {
    MTL::Buffer* buffer = nullptr;
    size_t size = 0;
    uint64_t last_frame_used = 0;
  };
  std::vector<FrameIndexBuffer> frame_index_buffers_;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_PRIMITIVE_PROCESSOR_H_

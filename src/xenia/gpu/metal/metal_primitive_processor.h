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

#include "third_party/metal-cpp/Metal/Metal.hpp"
#include <memory>
#include <unordered_map>

#include "xenia/gpu/primitive_processor.h"

namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor;

class MetalPrimitiveProcessor : public PrimitiveProcessor {
 public:
  MetalPrimitiveProcessor(MetalCommandProcessor& command_processor,
                          const RegisterFile& register_file,
                          Memory& memory, TraceWriter& trace_writer,
                          SharedMemory& shared_memory);
  ~MetalPrimitiveProcessor();

  bool Initialize();
  void Shutdown(bool from_destructor = false);

  void CompletedSubmissionUpdated();
  void BeginSubmission();
  void BeginFrame();
  void EndFrame();

 protected:
  bool InitializeBuiltinIndexBuffer(
      size_t size_bytes,
      std::function<void(void*)> fill_callback) override;

  void* RequestHostConvertedIndexBufferForCurrentFrame(
      xenos::IndexFormat format, uint32_t index_count, bool coalign_for_simd,
      uint32_t coalignment_original_address,
      size_t& backend_handle_out) override;

 private:
  MetalCommandProcessor& command_processor_;

  // Frame-lifetime index buffers for primitive processing
  struct FrameIndexBuffer {
    MTL::Buffer* buffer;
    size_t size;
    uint64_t last_frame_used;
  };
  std::vector<FrameIndexBuffer> frame_index_buffers_;
  
  // Built-in index buffer for primitive type conversion
  MTL::Buffer* builtin_index_buffer_ = nullptr;
  uint64_t builtin_index_buffer_gpu_address_ = 0;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_PRIMITIVE_PROCESSOR_H_
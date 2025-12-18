/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_primitive_processor.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/metal/metal_command_processor.h"

namespace xe {
namespace gpu {
namespace metal {

MetalPrimitiveProcessor::MetalPrimitiveProcessor(
    MetalCommandProcessor& command_processor, const RegisterFile& register_file,
    Memory& memory, TraceWriter& trace_writer, SharedMemory& shared_memory)
    : PrimitiveProcessor(register_file, memory, trace_writer, shared_memory),
      command_processor_(command_processor) {}

MetalPrimitiveProcessor::~MetalPrimitiveProcessor() { Shutdown(true); }

bool MetalPrimitiveProcessor::Initialize() {
  // Initialize the base primitive processor
  // Metal supports all primitive types through conversion
  if (!InitializeCommon(
          true,    // full_32bit_vertex_indices_supported
          false,   // triangle_fans_supported (will convert)
          false,   // line_loops_supported (will convert)
          false,   // quad_lists_supported (will convert)
          true,    // point_sprites_supported_without_vs_expansion
          false))  // rectangle_lists_supported_without_vs_expansion
  {
    Shutdown();
    return false;
  }

  XELOGI("MetalPrimitiveProcessor initialized successfully");
  return true;
}

void MetalPrimitiveProcessor::Shutdown(bool from_destructor) {
  // Release all frame index buffers
  for (auto& frame_buffer : frame_index_buffers_) {
    if (frame_buffer.buffer) {
      frame_buffer.buffer->release();
    }
  }
  frame_index_buffers_.clear();

  // Release built-in index buffer
  if (builtin_index_buffer_) {
    builtin_index_buffer_->release();
    builtin_index_buffer_ = nullptr;
    builtin_index_buffer_gpu_address_ = 0;
  }

  if (!from_destructor) {
    ShutdownCommon();
  }
}

void MetalPrimitiveProcessor::CompletedSubmissionUpdated() {
  // Nothing to do for Metal
}

void MetalPrimitiveProcessor::BeginSubmission() {
  // Nothing to do for Metal
}

void MetalPrimitiveProcessor::BeginFrame() {
  // Clean up old frame index buffers
  static uint64_t frame_counter = 0;
  frame_counter++;
  uint64_t current_frame = frame_counter;

  frame_index_buffers_.erase(
      std::remove_if(frame_index_buffers_.begin(), frame_index_buffers_.end(),
                     [current_frame](const FrameIndexBuffer& buffer) {
                       // Keep buffers used in the last 2 frames
                       if (current_frame - buffer.last_frame_used > 2) {
                         if (buffer.buffer) {
                           buffer.buffer->release();
                         }
                         return true;
                       }
                       return false;
                     }),
      frame_index_buffers_.end());
}

void MetalPrimitiveProcessor::EndFrame() { ClearPerFrameCache(); }

MTL::Buffer* MetalPrimitiveProcessor::GetConvertedIndexBuffer(
    size_t handle, uint64_t& offset_bytes_out) const {
  // The handle is actually a pointer to the MTL::Buffer
  MTL::Buffer* buffer = reinterpret_cast<MTL::Buffer*>(handle);
  offset_bytes_out = 0;  // We use the full buffer from the start
  return buffer;
}

bool MetalPrimitiveProcessor::InitializeBuiltinIndexBuffer(
    size_t size_bytes, std::function<void(void*)> fill_callback) {
  assert_not_zero(size_bytes);
  assert_null(builtin_index_buffer_);

  MTL::Device* device = command_processor_.GetMetalDevice();

  // Create buffer with shared storage so we can write to it
  builtin_index_buffer_ =
      device->newBuffer(size_bytes, MTL::ResourceStorageModeShared);
  if (!builtin_index_buffer_) {
    XELOGE("Failed to create Metal built-in index buffer");
    return false;
  }

  builtin_index_buffer_->setLabel(NS::String::string(
      "Xenia Built-in Index Buffer", NS::UTF8StringEncoding));

  // Fill the buffer with built-in indices
  void* buffer_data = builtin_index_buffer_->contents();
  fill_callback(buffer_data);

  // Get GPU address for binding
  builtin_index_buffer_gpu_address_ = builtin_index_buffer_->gpuAddress();

  XELOGI("Created Metal built-in index buffer ({} bytes)", size_bytes);
  return true;
}

void* MetalPrimitiveProcessor::RequestHostConvertedIndexBufferForCurrentFrame(
    xenos::IndexFormat format, uint32_t index_count, bool coalign_for_simd,
    uint32_t coalignment_original_address, size_t& backend_handle_out) {
  // Calculate required size
  size_t element_size = format == xenos::IndexFormat::kInt16 ? sizeof(uint16_t)
                                                             : sizeof(uint32_t);
  size_t required_size = index_count * element_size;

  // Add padding for SIMD alignment if requested
  if (coalign_for_simd) {
    required_size += XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE;
  }

  // Find or create a buffer large enough
  FrameIndexBuffer* chosen_buffer = nullptr;
  static uint64_t frame_counter = 0;
  uint64_t current_frame = frame_counter;

  // First try to find an existing buffer that's large enough
  for (auto& frame_buffer : frame_index_buffers_) {
    if (frame_buffer.size >= required_size &&
        frame_buffer.last_frame_used != current_frame) {
      chosen_buffer = &frame_buffer;
      break;
    }
  }

  // If no suitable buffer found, create a new one
  if (!chosen_buffer) {
    MTL::Device* device = command_processor_.GetMetalDevice();

    // Round up to next power of 2 for better reuse
    size_t allocation_size = required_size;
    allocation_size = std::max(allocation_size, size_t(4096));
    allocation_size = (allocation_size + 4095) & ~4095;  // Round to 4KB

    MTL::Buffer* new_buffer =
        device->newBuffer(allocation_size, MTL::ResourceStorageModeShared);

    if (!new_buffer) {
      XELOGE("Failed to create Metal index buffer for primitive conversion");
      backend_handle_out = 0;
      return nullptr;
    }

    char label[256];
    snprintf(label, sizeof(label), "Xenia Converted Index Buffer (%zu bytes)",
             allocation_size);
    new_buffer->setLabel(NS::String::string(label, NS::UTF8StringEncoding));

    frame_index_buffers_.push_back({new_buffer, allocation_size, 0});
    chosen_buffer = &frame_index_buffers_.back();

    XELOGI("Created new Metal index buffer for primitive conversion ({} bytes)",
           allocation_size);
  }

  // Mark buffer as used this frame
  chosen_buffer->last_frame_used = current_frame;

  // Return the buffer handle and CPU mapping
  backend_handle_out = reinterpret_cast<size_t>(chosen_buffer->buffer);
  void* cpu_buffer = chosen_buffer->buffer->contents();

  // Apply SIMD co-alignment if requested
  if (coalign_for_simd) {
    ptrdiff_t offset =
        GetSimdCoalignmentOffset(cpu_buffer, coalignment_original_address);
    cpu_buffer = static_cast<uint8_t*>(cpu_buffer) + offset;
  }

  return cpu_buffer;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
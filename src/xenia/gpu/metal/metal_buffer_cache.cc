/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_buffer_cache.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/metal/metal_command_processor.h"


namespace xe {
namespace gpu {
namespace metal {

MetalBufferCache::MetalBufferCache(MetalCommandProcessor* command_processor,
                                   const RegisterFile* register_file,
                                   Memory* memory)
    : command_processor_(command_processor),
      register_file_(register_file),
      memory_(memory) {
  dynamic_vertex_buffer_offset_ = 0;
  dynamic_index_buffer_offset_ = 0;
}

MetalBufferCache::~MetalBufferCache() {
  Shutdown();
}

bool MetalBufferCache::Initialize() {
  SCOPE_profile_cpu_f("gpu");

  // Create dynamic vertex buffer
  dynamic_vertex_buffer_ = std::make_unique<MetalBuffer>();
  dynamic_vertex_buffer_->buffer = CreateBuffer(
      kDynamicVertexBufferSize, 
      MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined);
  if (!dynamic_vertex_buffer_->buffer) {
    XELOGE("Metal buffer cache: Failed to create dynamic vertex buffer");
    return false;
  }
  dynamic_vertex_buffer_->capacity = static_cast<uint32_t>(kDynamicVertexBufferSize);
  dynamic_vertex_buffer_->is_dynamic = true;

  // Create dynamic index buffer
  dynamic_index_buffer_ = std::make_unique<MetalBuffer>();
  dynamic_index_buffer_->buffer = CreateBuffer(
      kDynamicIndexBufferSize,
      MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined);
  if (!dynamic_index_buffer_->buffer) {
    XELOGE("Metal buffer cache: Failed to create dynamic index buffer");
    return false;
  }
  dynamic_index_buffer_->capacity = static_cast<uint32_t>(kDynamicIndexBufferSize);
  dynamic_index_buffer_->is_dynamic = true;

  XELOGD("Metal buffer cache: Initialized successfully");
  
  return true;
}

void MetalBufferCache::Shutdown() {
  SCOPE_profile_cpu_f("gpu");

  // Clear cache first to release any cached buffers
  ClearCache();

  // Explicitly reset dynamic buffers
  // These will call the destructor which releases the Metal buffer
  dynamic_vertex_buffer_.reset();
  dynamic_index_buffer_.reset();

  XELOGD("Metal buffer cache: Shutdown complete");
}

void MetalBufferCache::ClearCache() {
  SCOPE_profile_cpu_f("gpu");

  buffer_cache_.clear();
  dynamic_vertex_buffer_offset_ = 0;
  dynamic_index_buffer_offset_ = 0;

  XELOGD("Metal buffer cache: Cache cleared");
}

bool MetalBufferCache::UploadIndexBuffer(uint32_t guest_base, uint32_t guest_length,
                                         xenos::IndexFormat format) {
  SCOPE_profile_cpu_f("gpu");

  if (!guest_length) {
    return false;
  }

  // Calculate buffer requirements
  uint32_t index_size = (format == xenos::IndexFormat::kInt32) ? 4 : 2;
  uint32_t buffer_size = guest_length * index_size;

  // Get guest data
  const void* guest_data = memory_->TranslatePhysical(guest_base);
  if (!guest_data) {
    XELOGE("Metal buffer cache: Invalid guest index buffer address 0x{:08X}", guest_base);
    return false;
  }

  // For now, use the dynamic buffer approach
  // TODO: Implement static buffer caching for frequently used buffers
  
  // Check if buffer fits in dynamic buffer
  if (dynamic_index_buffer_offset_ + buffer_size > dynamic_index_buffer_->capacity) {
    // Reset dynamic buffer if it's full
    dynamic_index_buffer_offset_ = 0;
  }

  if (buffer_size > dynamic_index_buffer_->capacity) {
    XELOGE("Metal buffer cache: Index buffer size {} exceeds dynamic buffer capacity {}",
           buffer_size, dynamic_index_buffer_->capacity);
    return false;
  }

  // Copy data to dynamic buffer
  uint8_t* buffer_data = static_cast<uint8_t*>(dynamic_index_buffer_->buffer->contents());
  std::memcpy(buffer_data + dynamic_index_buffer_offset_, guest_data, buffer_size);

  dynamic_index_buffer_offset_ += xe::align(buffer_size, uint32_t(16));  // 16-byte alignment

  return true;
}

bool MetalBufferCache::UploadVertexBuffer(uint32_t guest_base, uint32_t guest_length) {
  SCOPE_profile_cpu_f("gpu");

  if (!guest_length) {
    return false;
  }

  // Get guest data
  const void* guest_data = memory_->TranslatePhysical(guest_base);
  if (!guest_data) {
    XELOGE("Metal buffer cache: Invalid guest vertex buffer address 0x{:08X}", guest_base);
    return false;
  }

  // For now, use the dynamic buffer approach
  // TODO: Implement static buffer caching for frequently used buffers
  
  // Check if buffer fits in dynamic buffer
  if (dynamic_vertex_buffer_offset_ + guest_length > dynamic_vertex_buffer_->capacity) {
    // Reset dynamic buffer if it's full
    dynamic_vertex_buffer_offset_ = 0;
  }

  if (guest_length > dynamic_vertex_buffer_->capacity) {
    XELOGE("Metal buffer cache: Vertex buffer size {} exceeds dynamic buffer capacity {}",
           guest_length, dynamic_vertex_buffer_->capacity);
    return false;
  }

  // Copy data to dynamic buffer
  uint8_t* buffer_data = static_cast<uint8_t*>(dynamic_vertex_buffer_->buffer->contents());
  std::memcpy(buffer_data + dynamic_vertex_buffer_offset_, guest_data, guest_length);

  dynamic_vertex_buffer_offset_ += xe::align(guest_length, uint32_t(16));  // 16-byte alignment

  return true;
}


MTL::Buffer* MetalBufferCache::GetIndexBuffer(uint32_t guest_base, uint32_t guest_length,
                                              xenos::IndexFormat format, uint32_t* offset_out) {
  // For now, always use dynamic buffer
  // TODO: Implement proper buffer caching with static buffers for reused data
  
  if (offset_out) {
    uint32_t index_size = (format == xenos::IndexFormat::kInt32) ? 4 : 2;
    uint32_t buffer_size = guest_length * index_size;
    *offset_out = dynamic_index_buffer_offset_ - xe::align(buffer_size, uint32_t(16));
  }
  
  return dynamic_index_buffer_->buffer;
}

MTL::Buffer* MetalBufferCache::GetVertexBuffer(uint32_t guest_base, uint32_t guest_length,
                                               uint32_t* offset_out) {
  // For now, always use dynamic buffer
  // TODO: Implement proper buffer caching with static buffers for reused data
  
  if (offset_out) {
    *offset_out = dynamic_vertex_buffer_offset_ - xe::align(guest_length, uint32_t(16));
  }
  
  return dynamic_vertex_buffer_->buffer;
}

MTL::Buffer* MetalBufferCache::CreateBuffer(size_t size, MTL::ResourceOptions options) {
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal buffer cache: Failed to get Metal device from command processor");
    return nullptr;
  }

  MTL::Buffer* buffer = device->newBuffer(size, options);

  if (!buffer) {
    XELOGE("Metal buffer cache: Failed to create buffer of size {}", size);
    return nullptr;
  }

  return buffer;
}

bool MetalBufferCache::UpdateBuffer(MTL::Buffer* buffer, const void* data, size_t size) {
  if (!buffer || !data || !size) {
    return false;
  }

  void* buffer_contents = buffer->contents();
  if (!buffer_contents) {
    XELOGE("Metal buffer cache: Buffer contents not accessible");
    return false;
  }

  std::memcpy(buffer_contents, data, size);
  return true;
}


bool MetalBufferCache::BufferDescriptor::operator==(const BufferDescriptor& other) const {
  return guest_base == other.guest_base &&
         guest_length == other.guest_length &&
         format == other.format;
}

size_t MetalBufferCache::BufferDescriptorHasher::operator()(const BufferDescriptor& desc) const {
  size_t hash = 0;
  hash ^= std::hash<uint32_t>{}(desc.guest_base) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.guest_length) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.format) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  return hash;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe

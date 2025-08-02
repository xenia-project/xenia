/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_BUFFER_CACHE_H_
#define XENIA_GPU_METAL_METAL_BUFFER_CACHE_H_

#include <memory>
#include <unordered_map>

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"

#if XE_PLATFORM_MAC
#ifdef METAL_CPP_AVAILABLE
#include "third_party/metal-cpp/Metal/Metal.hpp"
#endif  // METAL_CPP_AVAILABLE
#endif  // XE_PLATFORM_MAC

namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor;

class MetalBufferCache {
 public:
  MetalBufferCache(MetalCommandProcessor* command_processor,
                   const RegisterFile* register_file, 
                   Memory* memory);
  ~MetalBufferCache();

  bool Initialize();
  void Shutdown();
  void ClearCache();

  // Buffer management
  bool UploadIndexBuffer(uint32_t guest_base, uint32_t guest_length,
                        xenos::IndexFormat format);
  bool UploadVertexBuffer(uint32_t guest_base, uint32_t guest_length);

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Get Metal buffers for rendering
  MTL::Buffer* GetIndexBuffer(uint32_t guest_base, uint32_t guest_length,
                             xenos::IndexFormat format, uint32_t* offset_out);
  MTL::Buffer* GetVertexBuffer(uint32_t guest_base, uint32_t guest_length,
                              uint32_t* offset_out);
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

 private:
  struct BufferDescriptor {
    uint32_t guest_base;
    uint32_t guest_length;
    uint32_t format;  // For index buffers
    
    bool operator==(const BufferDescriptor& other) const;
  };
  
  struct BufferDescriptorHasher {
    size_t operator()(const BufferDescriptor& desc) const;
  };

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  struct MetalBuffer {
    MTL::Buffer* buffer;
    uint32_t capacity;
    uint32_t size;
    bool is_dynamic;
    
    MetalBuffer() : buffer(nullptr), capacity(0), size(0), is_dynamic(false) {}
    ~MetalBuffer() {
      if (buffer) {
        buffer->release();
      }
    }
  };

  // Metal buffer creation helpers
  MTL::Buffer* CreateBuffer(size_t size, MTL::ResourceOptions options);
  bool UpdateBuffer(MTL::Buffer* buffer, const void* data, size_t size);
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  MetalCommandProcessor* command_processor_;
  const RegisterFile* register_file_;
  Memory* memory_;

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Buffer caches
  std::unordered_map<BufferDescriptor, std::unique_ptr<MetalBuffer>,
                     BufferDescriptorHasher> buffer_cache_;
                     
  // Shared vertex buffer for dynamic data
  std::unique_ptr<MetalBuffer> dynamic_vertex_buffer_;
  uint32_t dynamic_vertex_buffer_offset_;
  
  // Shared index buffer for dynamic data  
  std::unique_ptr<MetalBuffer> dynamic_index_buffer_;
  uint32_t dynamic_index_buffer_offset_;
  
  // Buffer size constants
  static const size_t kDynamicVertexBufferSize = 64 * 1024 * 1024;  // 64MB
  static const size_t kDynamicIndexBufferSize = 16 * 1024 * 1024;   // 16MB
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_BUFFER_CACHE_H_

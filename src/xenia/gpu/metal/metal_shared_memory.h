/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_SHARED_MEMORY_H_
#define XENIA_GPU_METAL_METAL_SHARED_MEMORY_H_

// TODO(Metal): Performance Optimization - Zero-Copy Memory
// Currently, Xbox memory is file-backed (mmap with MAP_SHARED), which Metal
// cannot directly wrap with newBufferWithBytesNoCopy (requires MAP_ANON).
// We create a separate Metal buffer and copy data, similar to D3D12.
// 
// Future optimization for macOS ARM64:
// - Allocate Xbox memory with MAP_ANON | MAP_SHARED instead of file-backed
// - Use vm_remap() for multiple memory views
// - Metal can then directly wrap the memory for zero-copy GPU access
// - Expected benefits: ~50% memory reduction, no UploadRanges overhead
// - See memory_mac.cc for implementation when ready

#include "xenia/gpu/shared_memory.h"
#include "xenia/ui/metal/metal_api.h"

namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor;
class MetalSharedMemory : public SharedMemory {
public:
    MetalSharedMemory(MetalCommandProcessor& command_processor,
                                     Memory& memory);
    ~MetalSharedMemory() override;
    bool Initialize();
    void Shutdown();
    void ClearCache() override;

    MTL::Buffer* GetBuffer() const {return buffer_; }

    // For trace dump, simplified - just make buffer available for reading
    void UseForReading() {
        // No state transitions needed in Metal
    }
    // Override pure virtual function from SharedMemory
    bool UploadRanges(
        const std::vector<std::pair<uint32_t, uint32_t>>& upload_page_ranges) override;

private:
    MetalCommandProcessor& command_processor_;
    MTL::Buffer* buffer_ = nullptr;

};

} // namespace metal
} // namespace gpu
} // namespace xe

#endif
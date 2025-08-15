#include "xenia/gpu/metal/metal_shared_memory.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/ui/metal/metal_util.h"

namespace xe {
namespace gpu {
namespace metal {

MetalSharedMemory::MetalSharedMemory(MetalCommandProcessor& command_processor,
                                     Memory& memory)
    : SharedMemory(memory), command_processor_(command_processor) {}

MetalSharedMemory::~MetalSharedMemory() {
  Shutdown();
}

bool MetalSharedMemory::Initialize() {
  // On Apple Silicon, we can directly map the emulator's memory!
  // No copying needed - GPU and CPU share the same RAM.
  // Initialize base class
  InitializeCommon();

  const ui::metal::MetalProvider& provider =
      command_processor_.GetMetalProvider();
  MTL::Device* device = provider.GetDevice();
  
  if (!device) {
    XELOGE("Metal device is null in MetalSharedMemory::Initialize");
    return false;
  }

  // Create Metal buffer - similar to D3D12's approach
  // On Apple Silicon, ResourceStorageModeShared gives CPU/GPU access
  XELOGI("Creating Metal shared memory buffer: size={}MB", kBufferSize >> 20);
  
  buffer_ = device->newBuffer(kBufferSize, MTL::ResourceStorageModeShared);
  if (!buffer_) {
    XELOGE("Failed to create Metal shared memory buffer");
    return false;
  }
  
  // For trace dump, do initial full copy
  // TODO(Metal): In full implementation, use UploadRanges for incremental updates
  void* xbox_ram = memory().TranslatePhysical(0);
  if (xbox_ram) {
    memcpy(buffer_->contents(), xbox_ram, kBufferSize);
    XELOGI("Copied Xbox memory to Metal buffer (initial sync)");
  }

  XELOGI("Metal shared memory initialized successfully");

  return true;
}

void MetalSharedMemory::ClearCache() { SharedMemory::ClearCache(); }

bool MetalSharedMemory::UploadRanges(
    const std::vector<std::pair<uint32_t, uint32_t>>& upload_page_ranges) {
  // On Apple Silicon with unified memory, we don't need to upload
  // The GPU can directly access the CPU memory
  // Just return true to indicate success

  if (!upload_page_ranges.empty()) {
    // Could log the ranges being "uploaded" for debugging
    XELOGD(
        "MetalSharedMemory::UploadRanges called with {} ranges (no-op on "
        "unified memory)",
        upload_page_ranges.size());
  }

  return true;
}

void MetalSharedMemory::Shutdown() {
  if (buffer_) {
    buffer_->release();
    buffer_ = nullptr;
  }

  ShutdownCommon();  // Base class cleanup
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
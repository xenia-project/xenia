#include "xenia/gpu/metal/metal_shared_memory.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/ui/metal/metal_util.h"

namespace xe {
namespace gpu {
namespace metal {

MetalSharedMemory::MetalSharedMemory(MetalCommandProcessor& command_processor,
                                     Memory& memory)
    : SharedMemory(memory), command_processor_(command_processor) {}

MetalSharedMemory::~MetalSharedMemory() { Shutdown(); }

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
  fflush(stdout);
  fflush(stderr);

  buffer_ = device->newBuffer(kBufferSize, MTL::ResourceStorageModeShared);
  XELOGI("Metal buffer allocated: {}", buffer_ ? "success" : "failed");
  fflush(stdout);
  fflush(stderr);
  if (!buffer_) {
    XELOGE("Failed to create Metal shared memory buffer");
    return false;
  }

  // For trace dump, do initial full copy
  // TODO(Metal): In full implementation, use UploadRanges for incremental
  // updates
  void* xbox_ram = memory().TranslatePhysical(0);
  XELOGI("xbox_ram={}, about to copy 512MB", xbox_ram ? "valid" : "null");
  fflush(stdout);
  fflush(stderr);
  if (xbox_ram) {
    memcpy(buffer_->contents(), xbox_ram, kBufferSize);
    XELOGI("Copied Xbox memory to Metal buffer (initial sync)");
    fflush(stdout);
    fflush(stderr);
  }

  XELOGI("Metal shared memory initialized successfully");
  fflush(stdout);
  fflush(stderr);

  return true;
}

void MetalSharedMemory::ClearCache() { SharedMemory::ClearCache(); }

bool MetalSharedMemory::UploadRanges(
    const std::vector<std::pair<uint32_t, uint32_t>>& upload_page_ranges) {
  // Copy modified ranges from Xbox memory to Metal buffer
  // This is needed because we create a separate Metal buffer rather than
  // using zero-copy (the zero-copy approach requires anonymous memory, but
  // Xbox memory is file-backed).

  static bool first_upload = true;
  if (first_upload) {
    first_upload = false;
    const uint32_t page_size = 1u << page_size_log2();
    XELOGI("MetalSharedMemory::UploadRanges: page_size={}, {} ranges to upload",
           page_size, upload_page_ranges.size());
    for (size_t i = 0; i < std::min(size_t(5), upload_page_ranges.size());
         i++) {
      uint32_t start_byte = upload_page_ranges[i].first * page_size;
      uint32_t length_bytes = upload_page_ranges[i].second * page_size;
      XELOGI("  Range[{}]: page={} count={} -> byte offset=0x{:08X} length={}",
             i, upload_page_ranges[i].first, upload_page_ranges[i].second,
             start_byte, length_bytes);
    }
  }

  if (!buffer_ || upload_page_ranges.empty()) {
    return true;
  }

  void* xbox_ram = memory().TranslatePhysical(0);
  if (!xbox_ram) {
    XELOGE("MetalSharedMemory::UploadRanges: Xbox RAM is null");
    return false;
  }

  uint8_t* buffer_data = static_cast<uint8_t*>(buffer_->contents());
  uint8_t* xbox_data = static_cast<uint8_t*>(xbox_ram);

  const uint32_t page_size = 1u << page_size_log2();
  for (const auto& range : upload_page_ranges) {
    uint32_t page_start = range.first;
    uint32_t page_count = range.second;
    uint32_t byte_offset = page_start * page_size;
    uint32_t byte_length = page_count * page_size;

    // Clamp to buffer size
    if (byte_offset >= kBufferSize) {
      continue;
    }
    if (byte_offset + byte_length > kBufferSize) {
      byte_length = kBufferSize - byte_offset;
    }

    // Mark range as valid BEFORE copying (matching D3D12 behavior)
    // This is critical - without this, pages stay invalid forever
    MakeRangeValid(byte_offset, byte_length, false, false);

    memcpy(buffer_data + byte_offset, xbox_data + byte_offset, byte_length);
  }

  XELOGI("MetalSharedMemory::UploadRanges: Copied {} ranges to Metal buffer",
         upload_page_ranges.size());

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
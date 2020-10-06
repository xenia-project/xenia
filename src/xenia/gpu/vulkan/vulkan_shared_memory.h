/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_SHARED_MEMORY_H_
#define XENIA_GPU_VULKAN_VULKAN_SHARED_MEMORY_H_

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "xenia/gpu/shared_memory.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/memory.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_upload_buffer_pool.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;

class VulkanSharedMemory : public SharedMemory {
 public:
  VulkanSharedMemory(VulkanCommandProcessor& command_processor, Memory& memory,
                     TraceWriter& trace_writer);
  ~VulkanSharedMemory() override;

  bool Initialize();
  void Shutdown(bool from_destructor = false);

  void CompletedSubmissionUpdated();
  void EndSubmission();

  enum class Usage {
    // Index buffer, vfetch, compute read, transfer source.
    kRead,
    // Index buffer, vfetch, memexport.
    kGuestDrawReadWrite,
    kComputeWrite,
    kTransferDestination,
  };
  // Places pipeline barrier for the target usage, also ensuring writes of
  // adjacent are ordered with writes of each other and reads.
  void Use(Usage usage, std::pair<uint32_t, uint32_t> written_range = {});

  VkBuffer buffer() const { return buffer_; }

 protected:
  bool UploadRanges(const std::vector<std::pair<uint32_t, uint32_t>>&
                        upload_page_ranges) override;

 private:
  bool IsSparse() const {
    return buffer_allocation_size_log2_ < kBufferSizeLog2;
  }

  void GetBarrier(Usage usage, VkPipelineStageFlags& stage_mask,
                  VkAccessFlags& access_mask) const;

  VulkanCommandProcessor& command_processor_;
  TraceWriter& trace_writer_;

  VkBuffer buffer_ = VK_NULL_HANDLE;
  uint32_t buffer_memory_type_;
  // Maximum of 1024 allocations in the worst case for all of the buffer because
  // of the overall 4096 allocation count limit on Windows drivers.
  static constexpr uint32_t kMinBufferAllocationSizeLog2 =
      std::max(kHostGpuMemoryOptimalSparseAllocationLog2,
               kBufferSizeLog2 - uint32_t(10));
  uint32_t buffer_allocation_size_log2_ = kBufferSizeLog2;
  // Sparse memory allocations, of different sizes.
  std::vector<VkDeviceMemory> buffer_memory_;
  // One bit per every 2^buffer_allocation_size_log2_ of the buffer.
  std::vector<uint64_t> buffer_memory_allocated_;

  // First usage will likely be uploading.
  Usage last_usage_ = Usage::kTransferDestination;
  std::pair<uint32_t, uint32_t> last_written_range_ = {};

  std::unique_ptr<ui::vulkan::VulkanUploadBufferPool> upload_buffer_pool_;
  std::vector<VkBufferCopy> upload_regions_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_SHARED_MEMORY_H_

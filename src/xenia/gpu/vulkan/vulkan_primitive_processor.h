/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_PRIMITIVE_PROCESSOR_H_
#define XENIA_GPU_VULKAN_VULKAN_PRIMITIVE_PROCESSOR_H_

#include <memory>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/gpu/primitive_processor.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_upload_buffer_pool.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;

class VulkanPrimitiveProcessor final : public PrimitiveProcessor {
 public:
  VulkanPrimitiveProcessor(const RegisterFile& register_file, Memory& memory,
                           TraceWriter& trace_writer,
                           SharedMemory& shared_memory,
                           VulkanCommandProcessor& command_processor)
      : PrimitiveProcessor(register_file, memory, trace_writer, shared_memory),
        command_processor_(command_processor) {}
  ~VulkanPrimitiveProcessor();

  bool Initialize();
  void Shutdown(bool from_destructor = false);
  void ClearCache() { frame_index_buffer_pool_->ClearCache(); }

  void CompletedSubmissionUpdated();
  void BeginSubmission();
  void BeginFrame();
  void EndSubmission();
  void EndFrame();

  std::pair<VkBuffer, VkDeviceSize> GetBuiltinIndexBuffer(size_t handle) const {
    assert_not_null(builtin_index_buffer_);
    return std::make_pair(
        builtin_index_buffer_,
        VkDeviceSize(GetBuiltinIndexBufferOffsetBytes(handle)));
  }
  std::pair<VkBuffer, VkDeviceSize> GetConvertedIndexBuffer(
      size_t handle) const {
    return frame_index_buffers_[handle];
  }

 protected:
  bool InitializeBuiltin16BitIndexBuffer(
      uint32_t index_count,
      std::function<void(uint16_t*)> fill_callback) override;

  void* RequestHostConvertedIndexBufferForCurrentFrame(
      xenos::IndexFormat format, uint32_t index_count, bool coalign_for_simd,
      uint32_t coalignment_original_address,
      size_t& backend_handle_out) override;

 private:
  VulkanCommandProcessor& command_processor_;

  VkDeviceSize builtin_index_buffer_size_ = 0;
  VkBuffer builtin_index_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory builtin_index_buffer_memory_ = VK_NULL_HANDLE;
  // Temporary buffer copied in the beginning of the first submission for
  // uploading to builtin_index_buffer_, destroyed when the submission when it
  // was uploaded is completed.
  VkBuffer builtin_index_buffer_upload_ = VK_NULL_HANDLE;
  VkDeviceMemory builtin_index_buffer_upload_memory_ = VK_NULL_HANDLE;
  // UINT64_MAX means not uploaded yet and needs uploading in the first
  // submission (if the upload buffer exists at all).
  uint64_t builtin_index_buffer_upload_submission_ = UINT64_MAX;

  std::unique_ptr<ui::vulkan::VulkanUploadBufferPool> frame_index_buffer_pool_;
  // Indexed by the backend handles.
  std::deque<std::pair<VkBuffer, VkDeviceSize>> frame_index_buffers_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_PRIMITIVE_PROCESSOR_H_

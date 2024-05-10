/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_primitive_processor.h"

#include <algorithm>
#include <cstdint>
#include <memory>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

VulkanPrimitiveProcessor::~VulkanPrimitiveProcessor() { Shutdown(true); }

bool VulkanPrimitiveProcessor::Initialize() {
  const ui::vulkan::VulkanProvider::DeviceInfo& device_info =
      command_processor_.GetVulkanProvider().device_info();
  if (!InitializeCommon(device_info.fullDrawIndexUint32,
                        device_info.triangleFans, false,
                        device_info.geometryShader, device_info.geometryShader,
                        device_info.geometryShader)) {
    Shutdown();
    return false;
  }
  frame_index_buffer_pool_ =
      std::make_unique<ui::vulkan::VulkanUploadBufferPool>(
          command_processor_.GetVulkanProvider(),
          VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
          std::max(size_t(kMinRequiredConvertedIndexBufferSize),
                   ui::GraphicsUploadBufferPool::kDefaultPageSize));
  return true;
}

void VulkanPrimitiveProcessor::Shutdown(bool from_destructor) {
  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  frame_index_buffers_.clear();
  frame_index_buffer_pool_.reset();
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                         builtin_index_buffer_upload_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                         builtin_index_buffer_upload_memory_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                         builtin_index_buffer_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                         builtin_index_buffer_memory_);

  if (!from_destructor) {
    ShutdownCommon();
  }
}

void VulkanPrimitiveProcessor::CompletedSubmissionUpdated() {
  if (builtin_index_buffer_upload_ != VK_NULL_HANDLE &&
      command_processor_.GetCompletedSubmission() >=
          builtin_index_buffer_upload_submission_) {
    const ui::vulkan::VulkanProvider& provider =
        command_processor_.GetVulkanProvider();
    const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
    VkDevice device = provider.device();
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                           builtin_index_buffer_upload_);
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                           builtin_index_buffer_upload_memory_);
  }
}

void VulkanPrimitiveProcessor::BeginSubmission() {
  if (builtin_index_buffer_upload_ != VK_NULL_HANDLE &&
      builtin_index_buffer_upload_submission_ == UINT64_MAX) {
    // No need to submit deferred barriers - builtin_index_buffer_ has never
    // been used yet, and builtin_index_buffer_upload_ is written before
    // submitting commands reading it.

    command_processor_.EndRenderPass();

    DeferredCommandBuffer& command_buffer =
        command_processor_.deferred_command_buffer();

    VkBufferCopy* copy_region = command_buffer.CmdCopyBufferEmplace(
        builtin_index_buffer_upload_, builtin_index_buffer_, 1);
    copy_region->srcOffset = 0;
    copy_region->dstOffset = 0;
    copy_region->size = builtin_index_buffer_size_;

    command_processor_.PushBufferMemoryBarrier(
        builtin_index_buffer_, 0, VK_WHOLE_SIZE, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_INDEX_READ_BIT);

    builtin_index_buffer_upload_submission_ =
        command_processor_.GetCurrentSubmission();
  }
}

void VulkanPrimitiveProcessor::BeginFrame() {
  frame_index_buffer_pool_->Reclaim(command_processor_.GetCompletedFrame());
}

void VulkanPrimitiveProcessor::EndSubmission() {
  frame_index_buffer_pool_->FlushWrites();
}

void VulkanPrimitiveProcessor::EndFrame() {
  ClearPerFrameCache();
  frame_index_buffers_.clear();
}

bool VulkanPrimitiveProcessor::InitializeBuiltinIndexBuffer(
    size_t size_bytes, std::function<void(void*)> fill_callback) {
  assert_not_zero(size_bytes);
  assert_true(builtin_index_buffer_ == VK_NULL_HANDLE);
  assert_true(builtin_index_buffer_memory_ == VK_NULL_HANDLE);
  assert_true(builtin_index_buffer_upload_ == VK_NULL_HANDLE);
  assert_true(builtin_index_buffer_upload_memory_ == VK_NULL_HANDLE);

  const ui::vulkan::VulkanProvider& provider =
      command_processor_.GetVulkanProvider();
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  builtin_index_buffer_size_ = VkDeviceSize(size_bytes);
  if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
          provider, builtin_index_buffer_size_,
          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, builtin_index_buffer_,
          builtin_index_buffer_memory_)) {
    XELOGE(
        "Vulkan primitive processor: Failed to create the built-in index "
        "buffer GPU resource with {} bytes",
        size_bytes);
    return false;
  }
  uint32_t upload_memory_type;
  if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
          provider, builtin_index_buffer_size_,
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          ui::vulkan::util::MemoryPurpose::kUpload,
          builtin_index_buffer_upload_, builtin_index_buffer_upload_memory_,
          &upload_memory_type)) {
    XELOGE(
        "Vulkan primitive processor: Failed to create the built-in index "
        "buffer upload resource with {} bytes",
        size_bytes);
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                           builtin_index_buffer_);
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                           builtin_index_buffer_memory_);
    return false;
  }

  void* mapping;
  if (dfn.vkMapMemory(device, builtin_index_buffer_upload_memory_, 0,
                      VK_WHOLE_SIZE, 0, &mapping) != VK_SUCCESS) {
    XELOGE(
        "Vulkan primitive processor: Failed to map the built-in index buffer "
        "upload resource with {} bytes",
        size_bytes);
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                           builtin_index_buffer_upload_);
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                           builtin_index_buffer_upload_memory_);
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                           builtin_index_buffer_);
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                           builtin_index_buffer_memory_);
    return false;
  }
  fill_callback(mapping);
  ui::vulkan::util::FlushMappedMemoryRange(
      provider, builtin_index_buffer_memory_, upload_memory_type);
  dfn.vkUnmapMemory(device, builtin_index_buffer_upload_memory_);

  // Schedule uploading in the first submission.
  builtin_index_buffer_upload_submission_ = UINT64_MAX;
  return true;
}

void* VulkanPrimitiveProcessor::RequestHostConvertedIndexBufferForCurrentFrame(
    xenos::IndexFormat format, uint32_t index_count, bool coalign_for_simd,
    uint32_t coalignment_original_address, size_t& backend_handle_out) {
  size_t index_size = format == xenos::IndexFormat::kInt16 ? sizeof(uint16_t)
                                                           : sizeof(uint32_t);
  VkBuffer buffer;
  VkDeviceSize offset;
  uint8_t* mapping = frame_index_buffer_pool_->Request(
      command_processor_.GetCurrentFrame(),
      index_size * index_count +
          (coalign_for_simd ? XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE : 0),
      index_size, buffer, offset);
  if (!mapping) {
    return nullptr;
  }
  if (coalign_for_simd) {
    ptrdiff_t coalignment_offset =
        GetSimdCoalignmentOffset(mapping, coalignment_original_address);
    mapping += coalignment_offset;
    offset = VkDeviceSize(offset + coalignment_offset);
  }
  backend_handle_out = frame_index_buffers_.size();
  frame_index_buffers_.emplace_back(buffer, offset);
  return mapping;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

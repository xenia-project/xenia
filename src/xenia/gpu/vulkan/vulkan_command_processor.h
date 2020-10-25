/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_
#define XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <utility>
#include <vector>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/gpu/vulkan/vulkan_shared_memory.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/ui/vulkan/vulkan_context.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor : public CommandProcessor {
 public:
  VulkanCommandProcessor(VulkanGraphicsSystem* graphics_system,
                         kernel::KernelState* kernel_state);
  ~VulkanCommandProcessor();

  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;

  void RestoreEdramSnapshot(const void* snapshot) override;

  ui::vulkan::VulkanContext& GetVulkanContext() const {
    return static_cast<ui::vulkan::VulkanContext&>(*context_);
  }

  // Returns the deferred drawing command list for the currently open
  // submission.
  DeferredCommandBuffer& deferred_command_buffer() {
    assert_true(submission_open_);
    return deferred_command_buffer_;
  }

  uint64_t GetCurrentSubmission() const {
    return submission_completed_ +
           uint64_t(submissions_in_flight_fences_.size()) + 1;
  }
  uint64_t GetCompletedSubmission() const { return submission_completed_; }

  // Sparse binds are:
  // - In a single submission, all submitted in one vkQueueBindSparse.
  // - Sent to the queue without waiting for a semaphore.
  // Thus, multiple sparse binds between the completed and the current
  // submission, and within one submission, must not touch any overlapping
  // memory regions.
  void SparseBindBuffer(VkBuffer buffer, uint32_t bind_count,
                        const VkSparseMemoryBind* binds,
                        VkPipelineStageFlags wait_stage_mask);

  struct PipelineLayout {
    VkPipelineLayout pipeline_layout;
    VkDescriptorSetLayout descriptor_set_layout_textures_pixel_ref;
    VkDescriptorSetLayout descriptor_set_layout_textures_vertex_ref;
  };
  bool GetPipelineLayout(uint32_t texture_count_pixel,
                         uint32_t texture_count_vertex,
                         PipelineLayout& pipeline_layout_out);

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;

  void PerformSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                   uint32_t frontbuffer_height) override;

  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;
  bool IssueCopy() override;

  void InitializeTrace() override;

 private:
  // BeginSubmission and EndSubmission may be called at any time. If there's an
  // open non-frame submission, BeginSubmission(true) will promote it to a
  // frame. EndSubmission(true) will close the frame no matter whether the
  // submission has already been closed.

  // Rechecks submission number and reclaims per-submission resources. Pass 0 as
  // the submission to await to simply check status, or pass
  // GetCurrentSubmission() to wait for all queue operations to be completed.
  void CheckSubmissionFence(uint64_t await_submission);
  // If is_guest_command is true, a new full frame - with full cleanup of
  // resources and, if needed, starting capturing - is opened if pending (as
  // opposed to simply resuming after mid-frame synchronization).
  void BeginSubmission(bool is_guest_command);
  // If is_swap is true, a full frame is closed - with, if needed, cache
  // clearing and stopping capturing. Returns whether the submission was done
  // successfully, if it has failed, leaves it open.
  bool EndSubmission(bool is_swap);
  bool AwaitAllQueueOperationsCompletion() {
    CheckSubmissionFence(GetCurrentSubmission());
    return !submission_open_ && submissions_in_flight_fences_.empty();
  }

  VkShaderStageFlags GetGuestVertexShaderStageFlags() const;

  bool cache_clear_requested_ = false;

  std::vector<VkFence> fences_free_;
  std::vector<VkSemaphore> semaphores_free_;

  bool submission_open_ = false;
  uint64_t submission_completed_ = 0;
  // In case vkQueueSubmit fails after something like a successful
  // vkQueueBindSparse, to wait correctly on the next attempt.
  std::vector<VkSemaphore> current_submission_wait_semaphores_;
  std::vector<VkPipelineStageFlags> current_submission_wait_stage_masks_;
  std::vector<VkFence> submissions_in_flight_fences_;
  std::deque<std::pair<VkSemaphore, uint64_t>>
      submissions_in_flight_semaphores_;

  static constexpr uint32_t kMaxFramesInFlight = 3;
  bool frame_open_ = false;
  // Guest frame index, since some transient resources can be reused across
  // submissions. Values updated in the beginning of a frame.
  uint64_t frame_current_ = 1;
  uint64_t frame_completed_ = 0;
  // Submission indices of frames that have already been submitted.
  uint64_t closed_frame_submissions_[kMaxFramesInFlight] = {};

  struct CommandBuffer {
    VkCommandPool pool;
    VkCommandBuffer buffer;
  };
  std::vector<CommandBuffer> command_buffers_writable_;
  std::deque<std::pair<CommandBuffer, uint64_t>> command_buffers_submitted_;
  DeferredCommandBuffer deferred_command_buffer_;

  std::vector<VkSparseMemoryBind> sparse_memory_binds_;
  struct SparseBufferBind {
    VkBuffer buffer;
    size_t bind_offset;
    uint32_t bind_count;
  };
  std::vector<SparseBufferBind> sparse_buffer_binds_;
  // SparseBufferBind converted to VkSparseBufferMemoryBindInfo to this buffer
  // on submission (because pBinds should point to a place in std::vector, but
  // it may be reallocated).
  std::vector<VkSparseBufferMemoryBindInfo> sparse_buffer_bind_infos_temp_;
  VkPipelineStageFlags sparse_bind_wait_stage_mask_ = 0;

  // Common descriptor set layouts, usable by anything that may need them.
  VkDescriptorSetLayout descriptor_set_layout_empty_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_uniform_buffer_guest_vertex_ =
      VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_uniform_buffer_guest_pixel_ =
      VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_uniform_buffer_guest_ =
      VK_NULL_HANDLE;

  union TextureDescriptorSetLayoutKey {
    struct {
      uint32_t is_vertex : 1;
      // For 0, use descriptor_set_layout_empty_ instead as these are owning
      // references.
      uint32_t texture_count : 31;
    };
    uint32_t key = 0;
  };
  // TextureDescriptorSetLayoutKey::key -> VkDescriptorSetLayout.
  std::unordered_map<uint32_t, VkDescriptorSetLayout>
      descriptor_set_layouts_textures_;
  union PipelineLayoutKey {
    struct {
      // Pixel textures in the low bits since those are varied much more
      // commonly.
      uint32_t texture_count_pixel : 16;
      uint32_t texture_count_vertex : 16;
    };
    uint32_t key = 0;
  };
  // PipelineLayoutKey::key -> PipelineLayout.
  std::unordered_map<uint32_t, PipelineLayout> pipeline_layouts_;

  std::unique_ptr<VulkanSharedMemory> shared_memory_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_

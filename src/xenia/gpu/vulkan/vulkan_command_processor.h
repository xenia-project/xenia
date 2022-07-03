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

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/threading.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/vulkan/buffer_cache.h"
#include "xenia/gpu/vulkan/render_cache.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/gpu/vulkan/vulkan_pipeline_cache.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/vulkan/vulkan_texture_cache.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/xthread.h"
#include "xenia/memory.h"
#include "xenia/ui/vulkan/blitter.h"
#include "xenia/ui/vulkan/fenced_pools.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_submission_tracker.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanTextureCache;

class VulkanCommandProcessor : public CommandProcessor {
 public:
  VulkanCommandProcessor(VulkanGraphicsSystem* graphics_system,
                         kernel::KernelState* kernel_state);
  ~VulkanCommandProcessor() override;

  void RequestFrameTrace(const std::filesystem::path& root_path) override;
  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;
  void RestoreEdramSnapshot(const void* snapshot) override;
  void ClearCaches() override;

  ui::vulkan::VulkanProvider& GetVulkanProvider() const {
    return *static_cast<ui::vulkan::VulkanProvider*>(
        graphics_system_->provider());
  }

  RenderCache* render_cache() { return render_cache_.get(); }

 private:
  bool SetupContext() override;
  void ShutdownContext() override;

  void MakeCoherent() override;

  void WriteRegister(uint32_t index, uint32_t value) override;

  void BeginFrame();
  void EndFrame();

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType primitive_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;
  bool PopulateConstants(VkCommandBuffer command_buffer,
                         VulkanShader* vertex_shader,
                         VulkanShader* pixel_shader);
  bool PopulateIndexBuffer(VkCommandBuffer command_buffer,
                           IndexBufferInfo* index_buffer_info);
  bool PopulateVertexBuffers(VkCommandBuffer command_buffer,
                             VkCommandBuffer setup_buffer,
                             VulkanShader* vertex_shader);
  bool PopulateSamplers(VkCommandBuffer command_buffer,
                        VkCommandBuffer setup_buffer,
                        VulkanShader* vertex_shader,
                        VulkanShader* pixel_shader);
  bool IssueCopy() override;

  uint64_t dirty_float_constants_ = 0;  // Dirty float constants in blocks of 4
  uint8_t dirty_bool_constants_ = 0;
  uint32_t dirty_loop_constants_ = 0;
  uint8_t dirty_gamma_constants_ = 0;

  uint32_t coher_base_vc_ = 0;
  uint32_t coher_size_vc_ = 0;

  bool capturing_ = false;
  bool trace_requested_ = false;
  bool cache_clear_requested_ = false;

  std::unique_ptr<BufferCache> buffer_cache_;
  std::unique_ptr<VulkanPipelineCache> pipeline_cache_;
  std::unique_ptr<RenderCache> render_cache_;
  std::unique_ptr<VulkanTextureCache> texture_cache_;

  std::unique_ptr<ui::vulkan::Blitter> blitter_;
  std::unique_ptr<ui::vulkan::CommandBufferPool> command_buffer_pool_;

  bool frame_open_ = false;
  const RenderState* current_render_state_ = nullptr;
  VkCommandBuffer current_command_buffer_ = nullptr;
  VkCommandBuffer current_setup_buffer_ = nullptr;
  VkFence current_batch_fence_;

  ui::vulkan::VulkanSubmissionTracker swap_submission_tracker_;
  VkFramebuffer swap_framebuffer_ = VK_NULL_HANDLE;
  uint64_t swap_framebuffer_version_ = UINT64_MAX;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_

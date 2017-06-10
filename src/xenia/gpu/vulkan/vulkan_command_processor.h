/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_
#define XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_

#include <atomic>
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
#include "xenia/gpu/vulkan/pipeline_cache.h"
#include "xenia/gpu/vulkan/render_cache.h"
#include "xenia/gpu/vulkan/texture_cache.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/xthread.h"
#include "xenia/memory.h"
#include "xenia/ui/vulkan/blitter.h"
#include "xenia/ui/vulkan/fenced_pools.h"
#include "xenia/ui/vulkan/vulkan_context.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanGraphicsSystem;
class TextureCache;

class VulkanCommandProcessor : public CommandProcessor {
 public:
  VulkanCommandProcessor(VulkanGraphicsSystem* graphics_system,
                         kernel::KernelState* kernel_state);
  ~VulkanCommandProcessor() override;

  virtual void RequestFrameTrace(const std::wstring& root_path) override;
  void ClearCaches() override;

  RenderCache* render_cache() { return render_cache_.get(); }

 private:
  bool SetupContext() override;
  void ShutdownContext() override;

  void MakeCoherent() override;
  void PrepareForWait() override;
  void ReturnFromWait() override;

  void WriteRegister(uint32_t index, uint32_t value) override;

  void BeginFrame();
  void EndFrame();

  void CreateSwapImage(VkCommandBuffer setup_buffer, VkExtent2D extents);
  void DestroySwapImage();

  void PerformSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                   uint32_t frontbuffer_height) override;

  Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(PrimitiveType primitive_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info) override;
  bool PopulateConstants(VkCommandBuffer command_buffer,
                         VulkanShader* vertex_shader,
                         VulkanShader* pixel_shader);
  bool PopulateIndexBuffer(VkCommandBuffer command_buffer,
                           IndexBufferInfo* index_buffer_info);
  bool PopulateVertexBuffers(VkCommandBuffer command_buffer,
                             VulkanShader* vertex_shader);
  bool PopulateSamplers(VkCommandBuffer command_buffer,
                        VkCommandBuffer setup_buffer,
                        VulkanShader* vertex_shader,
                        VulkanShader* pixel_shader);
  bool IssueCopy() override;

  xe::ui::vulkan::VulkanDevice* device_ = nullptr;

  // front buffer / back buffer memory
  VkDeviceMemory fb_memory_ = nullptr;
  VkImageView fb_image_view_ = nullptr;
  VkFramebuffer fb_framebuffer_ = nullptr;

  uint64_t dirty_float_constants_ = 0;  // Dirty float constants in blocks of 4
  uint8_t dirty_bool_constants_ = 0;
  uint32_t dirty_loop_constants_ = 0;

  uint32_t coher_base_vc_ = 0;
  uint32_t coher_size_vc_ = 0;

  // TODO(benvanik): abstract behind context?
  // Queue used to submit work. This may be a dedicated queue for the command
  // processor and no locking will be required for use. If a dedicated queue
  // was not available this will be the device primary_queue and the
  // queue_mutex must be used to synchronize access to it.
  VkQueue queue_ = nullptr;
  std::mutex* queue_mutex_ = nullptr;

  // Last copy base address, for debugging only.
  uint32_t last_copy_base_ = 0;

  bool capturing_ = false;
  bool trace_requested_ = false;
  bool cache_clear_requested_ = false;

  std::unique_ptr<BufferCache> buffer_cache_;
  std::unique_ptr<PipelineCache> pipeline_cache_;
  std::unique_ptr<RenderCache> render_cache_;
  std::unique_ptr<TextureCache> texture_cache_;

  std::unique_ptr<ui::vulkan::Blitter> blitter_;
  std::unique_ptr<ui::vulkan::CommandBufferPool> command_buffer_pool_;

  bool frame_open_ = false;
  const RenderState* current_render_state_ = nullptr;
  VkCommandBuffer current_command_buffer_ = nullptr;
  VkCommandBuffer current_setup_buffer_ = nullptr;
  VkFence current_batch_fence_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/render_cache.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

RenderCache::RenderCache(RegisterFile* register_file,
                         ui::vulkan::VulkanDevice* device)
    : register_file_(register_file), device_(*device) {}

RenderCache::~RenderCache() = default;

VkRenderPass RenderCache::BeginRenderPass(VkCommandBuffer command_buffer,
                                          VulkanShader* vertex_shader,
                                          VulkanShader* pixel_shader) {
  assert_null(current_command_buffer_);
  current_command_buffer_ = command_buffer;

  // Lookup or construct a render pass compatible with our current state.
  VkRenderPass render_pass = nullptr;

  // Begin render pass.
  VkRenderPassBeginInfo render_pass_begin_info;
  render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin_info.pNext = nullptr;
  render_pass_begin_info.renderPass = render_pass;

  // Target framebuffer.
  // render_pass_begin_info.framebuffer = current_buffer.framebuffer;

  // Render into the entire buffer (or at least tell the API we are doing
  // this). In theory it'd be better to clip this to the scissor region, but
  // the docs warn anything but the full framebuffer may be slow.
  render_pass_begin_info.renderArea.offset.x = 0;
  render_pass_begin_info.renderArea.offset.y = 0;
  // render_pass_begin_info.renderArea.extent.width = surface_width_;
  // render_pass_begin_info.renderArea.extent.height = surface_height_;

  // Configure clear color, if clearing.
  VkClearValue color_clear_value;
  color_clear_value.color.float32[0] = 238 / 255.0f;
  color_clear_value.color.float32[1] = 238 / 255.0f;
  color_clear_value.color.float32[2] = 238 / 255.0f;
  color_clear_value.color.float32[3] = 1.0f;
  VkClearValue clear_values[] = {color_clear_value};
  render_pass_begin_info.clearValueCount =
      static_cast<uint32_t>(xe::countof(clear_values));
  render_pass_begin_info.pClearValues = clear_values;

  vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info,
                       VK_SUBPASS_CONTENTS_INLINE);

  return render_pass;
}

void RenderCache::EndRenderPass() {
  assert_not_null(current_command_buffer_);
  auto command_buffer = current_command_buffer_;
  current_command_buffer_ = nullptr;

  // End the render pass.
  vkCmdEndRenderPass(command_buffer);
}

void RenderCache::ClearCache() {
  // TODO(benvanik): caching.
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

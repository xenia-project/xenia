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

  return render_pass;
}

void RenderCache::EndRenderPass() {
  assert_not_null(current_command_buffer_);
  auto command_buffer = current_command_buffer_;
  current_command_buffer_ = nullptr;
}

void RenderCache::ClearCache() {
  // TODO(benvanik): caching.
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

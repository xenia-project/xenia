/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"

#include "xenia/ui/vulkan/vulkan_context.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanImmediateTexture : public ImmediateTexture {
 public:
  VulkanImmediateTexture(uint32_t width, uint32_t height)
      : ImmediateTexture(width, height) {}
};

VulkanImmediateDrawer::VulkanImmediateDrawer(VulkanContext& graphics_context)
    : ImmediateDrawer(&graphics_context), context_(graphics_context) {}

std::unique_ptr<ImmediateTexture> VulkanImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter, bool repeat,
    const uint8_t* data) {
  auto texture = std::make_unique<VulkanImmediateTexture>(width, height);
  return std::unique_ptr<ImmediateTexture>(texture.release());
}

void VulkanImmediateDrawer::UpdateTexture(ImmediateTexture* texture,
                                          const uint8_t* data) {}

void VulkanImmediateDrawer::Begin(int render_target_width,
                                  int render_target_height) {}

void VulkanImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {}

void VulkanImmediateDrawer::Draw(const ImmediateDraw& draw) {}

void VulkanImmediateDrawer::EndDrawBatch() {}

void VulkanImmediateDrawer::End() {}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

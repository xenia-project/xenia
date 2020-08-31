/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_context.h"

#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vulkan {

VulkanContext::VulkanContext(VulkanProvider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

bool VulkanContext::Initialize() { return false; }

ImmediateDrawer* VulkanContext::immediate_drawer() {
  return immediate_drawer_.get();
}

void VulkanContext::BeginSwap() {}

void VulkanContext::EndSwap() {}

std::unique_ptr<RawImage> VulkanContext::Capture() {
  // TODO(Triang3l): Read back swap chain front buffer.
  return nullptr;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

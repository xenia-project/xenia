/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vk/vulkan_context.h"

#include "xenia/ui/vk/vulkan_immediate_drawer.h"

namespace xe {
namespace ui {
namespace vk {

VulkanContext::VulkanContext(VulkanProvider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

VulkanContext::~VulkanContext() { Shutdown(); }

bool VulkanContext::Initialize() {
  context_lost_ = false;

  if (target_window_) {
    // Initialize the immediate mode drawer if not offscreen.
    immediate_drawer_ = std::make_unique<VulkanImmediateDrawer>(this);
    if (!immediate_drawer_->Initialize()) {
      Shutdown();
      return false;
    }
  }

  return true;
}

void VulkanContext::Shutdown() {
  initialized_fully_ = false;

  immediate_drawer_.reset();
}

ImmediateDrawer* VulkanContext::immediate_drawer() {
  return immediate_drawer_.get();
}

bool VulkanContext::is_current() { return false; }

bool VulkanContext::MakeCurrent() { return true; }

void VulkanContext::ClearCurrent() {}

void VulkanContext::BeginSwap() {}

void VulkanContext::EndSwap() {}

std::unique_ptr<RawImage> VulkanContext::Capture() {
  // TODO(Triang3l): Read back swap chain front buffer.
  return nullptr;
}

}  // namespace vk
}  // namespace ui
}  // namespace xe

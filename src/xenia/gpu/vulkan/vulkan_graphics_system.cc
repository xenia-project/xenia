/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_graphics_system.h"

#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/xbox.h"

namespace xe {
namespace gpu {
namespace vulkan {

VulkanGraphicsSystem::VulkanGraphicsSystem() {}

VulkanGraphicsSystem::~VulkanGraphicsSystem() {}

X_STATUS VulkanGraphicsSystem::Setup(cpu::Processor* processor,
                                     kernel::KernelState* kernel_state,
                                     ui::Window* target_window) {
  provider_ = xe::ui::vulkan::VulkanProvider::Create();

  return GraphicsSystem::Setup(processor, kernel_state, target_window);
}

void VulkanGraphicsSystem::Shutdown() { GraphicsSystem::Shutdown(); }

std::unique_ptr<CommandProcessor>
VulkanGraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new VulkanCommandProcessor(this, kernel_state_));
}

void VulkanGraphicsSystem::Swap(xe::ui::UIEvent* e) {
  if (!command_processor_) {
    return;
  }

  auto& swap_state = command_processor_->swap_state();
  std::lock_guard<std::mutex> lock(swap_state.mutex);
  swap_state.pending = false;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

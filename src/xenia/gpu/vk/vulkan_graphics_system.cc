/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vk/vulkan_graphics_system.h"

#include "xenia/gpu/vk/vulkan_command_processor.h"

namespace xe {
namespace gpu {
namespace vk {

VulkanGraphicsSystem::VulkanGraphicsSystem() {}

VulkanGraphicsSystem::~VulkanGraphicsSystem() {}

std::wstring VulkanGraphicsSystem::name() const { return L"Vulkan Prototype"; }

X_STATUS VulkanGraphicsSystem::Setup(cpu::Processor* processor,
                                     kernel::KernelState* kernel_state,
                                     ui::Window* target_window) {
  provider_ = xe::ui::vk::VulkanProvider::Create(target_window);

  auto result = GraphicsSystem::Setup(processor, kernel_state, target_window);
  if (result != X_STATUS_SUCCESS) {
    return result;
  }

  if (target_window) {
    display_context_ =
        reinterpret_cast<xe::ui::vk::VulkanContext*>(target_window->context());
  }

  return X_STATUS_SUCCESS;
}

void VulkanGraphicsSystem::Shutdown() { GraphicsSystem::Shutdown(); }

std::unique_ptr<CommandProcessor>
VulkanGraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new VulkanCommandProcessor(this, kernel_state_));
}

void VulkanGraphicsSystem::Swap(xe::ui::UIEvent* e) {}

}  // namespace vk
}  // namespace gpu
}  // namespace xe

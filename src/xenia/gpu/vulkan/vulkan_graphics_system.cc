/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
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

std::string VulkanGraphicsSystem::name() const {
  auto vulkan_command_processor =
      static_cast<VulkanCommandProcessor*>(command_processor());
  if (vulkan_command_processor != nullptr) {
    return vulkan_command_processor->GetWindowTitleText();
  }
  return "Vulkan - HEAVILY INCOMPLETE, early development";
}

X_STATUS VulkanGraphicsSystem::Setup(cpu::Processor* processor,
                                     kernel::KernelState* kernel_state,
                                     ui::WindowedAppContext* app_context,
                                     bool with_presentation) {
  provider_ = xe::ui::vulkan::VulkanProvider::Create(true, with_presentation);
  return GraphicsSystem::Setup(processor, kernel_state, app_context,
                               with_presentation);
}

std::unique_ptr<CommandProcessor>
VulkanGraphicsSystem::CreateCommandProcessor() {
  return std::make_unique<VulkanCommandProcessor>(this, kernel_state_);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

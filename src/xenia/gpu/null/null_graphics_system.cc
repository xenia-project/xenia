/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/null/null_graphics_system.h"

#include "xenia/gpu/null//null_command_processor.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/xbox.h"

namespace xe {
namespace gpu {
namespace null {

NullGraphicsSystem::NullGraphicsSystem() {}

NullGraphicsSystem::~NullGraphicsSystem() {}

X_STATUS NullGraphicsSystem::Setup(Memory* memory, cpu::Processor* processor,
                                   kernel::KernelState* kernel_state,
                                   ui::WindowedAppContext* app_context,
                                   bool is_surface_required) {
  // This is a null graphics system, but we still setup vulkan because UI needs
  // it through us :|
  provider_ = xe::ui::vulkan::VulkanProvider::Create(is_surface_required);
  return GraphicsSystem::Setup(memory, processor, kernel_state, app_context,
                               is_surface_required);
}

std::unique_ptr<CommandProcessor> NullGraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new NullCommandProcessor(this, kernel_state_));
}

}  // namespace null
}  // namespace gpu
}  // namespace xe
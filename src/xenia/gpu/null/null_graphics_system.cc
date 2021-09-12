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

X_STATUS NullGraphicsSystem::Setup(cpu::Processor* processor,
                                   kernel::KernelState* kernel_state,
                                   ui::Window* target_window) {
  // This is a null graphics system, but we still setup vulkan because UI needs
  // it through us :|
  provider_ = xe::ui::vulkan::VulkanProvider::Create();

  return GraphicsSystem::Setup(processor, kernel_state, target_window);
}

void NullGraphicsSystem::Shutdown() { GraphicsSystem::Shutdown(); }

std::unique_ptr<CommandProcessor> NullGraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new NullCommandProcessor(this, kernel_state_));
}

void NullGraphicsSystem::Swap(xe::ui::UIEvent* e) {
  if (!command_processor_) {
    return;
  }

  auto& swap_state = command_processor_->swap_state();
  std::lock_guard<std::mutex> lock(swap_state.mutex);
  swap_state.pending = false;
}

}  // namespace null
}  // namespace gpu
}  // namespace xe
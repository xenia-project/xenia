/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/gl4_graphics_system.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/processor.h"
#include "xenia/gpu/gl4/gl4_command_processor.h"
#include "xenia/gpu/gl4/gl4_gpu_flags.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/ui/gl/gl_provider.h"
#include "xenia/ui/window.h"

namespace xe {
namespace gpu {
namespace gl4 {

GL4GraphicsSystem::GL4GraphicsSystem() = default;

GL4GraphicsSystem::~GL4GraphicsSystem() = default;

X_STATUS GL4GraphicsSystem::Setup(cpu::Processor* processor,
                                  kernel::KernelState* kernel_state,
                                  ui::Window* target_window) {
  // Must create the provider so we can create contexts.
  provider_ = xe::ui::gl::GLProvider::Create(target_window);

  auto result = GraphicsSystem::Setup(processor, kernel_state, target_window);
  if (result) {
    return result;
  }

  display_context_ =
      reinterpret_cast<xe::ui::gl::GLContext*>(target_window->context());

  return X_STATUS_SUCCESS;
}

void GL4GraphicsSystem::Shutdown() { GraphicsSystem::Shutdown(); }

std::unique_ptr<CommandProcessor> GL4GraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new GL4CommandProcessor(this, kernel_state_));
}

void GL4GraphicsSystem::Swap(xe::ui::UIEvent* e) {
  if (!command_processor_) {
    return;
  }
  // Check for pending swap.
  auto& swap_state = command_processor_->swap_state();
  {
    std::lock_guard<std::mutex> lock(swap_state.mutex);
    if (swap_state.pending) {
      swap_state.pending = false;
      std::swap(swap_state.front_buffer_texture,
                swap_state.back_buffer_texture);
    }
  }

  if (!swap_state.front_buffer_texture) {
    // Not yet ready.
    return;
  }

  // Blit the frontbuffer.
  display_context_->blitter()->BlitTexture2D(
      static_cast<GLuint>(swap_state.front_buffer_texture),
      Rect2D(0, 0, swap_state.width, swap_state.height),
      Rect2D(0, 0, target_window_->width(), target_window_->height()),
      GL_LINEAR, false);
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

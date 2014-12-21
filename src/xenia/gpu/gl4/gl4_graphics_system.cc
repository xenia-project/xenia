/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2014 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <xenia/gpu/gl4/gl4_graphics_system.h>

namespace xe {
namespace gpu {
namespace gl4 {

GL4GraphicsSystem::GL4GraphicsSystem(Emulator* emulator)
    : GraphicsSystem(emulator) {}

GL4GraphicsSystem::~GL4GraphicsSystem() = default;

X_STATUS GL4GraphicsSystem::Setup() {
  auto loop = emulator_->main_window()->loop();
  loop->Post([this]() {
    control_ = std::make_unique<WGLControl>();
    emulator_->main_window()->AddChild(control_.get());
  });
  return X_STATUS_SUCCESS;
}

void GL4GraphicsSystem::Shutdown() {
  control_.reset();
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

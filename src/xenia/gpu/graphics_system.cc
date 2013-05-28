/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/graphics_system.h>


using namespace xe;
using namespace xe::gpu;


GraphicsSystem::GraphicsSystem(const CreationParams* params) {
  memory_ = xe_memory_retain(params->memory);
}

GraphicsSystem::~GraphicsSystem() {
  xe_memory_release(memory_);
}

xe_memory_ref GraphicsSystem::memory() {
  return xe_memory_retain(memory_);
}

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/nop/nop_graphics_driver.h>

#include <xenia/gpu/gpu-private.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::nop;
using namespace xe::gpu::xenos;


NopGraphicsDriver::NopGraphicsDriver(Memory* memory)
    : GraphicsDriver(memory), resource_cache_(nullptr) {
}

NopGraphicsDriver::~NopGraphicsDriver() {
}

int NopGraphicsDriver::Initialize() {
  return 0;
}

int NopGraphicsDriver::Draw(const DrawCommand& command) {
  return 0;
}

int NopGraphicsDriver::Resolve() {
  return 0;
}

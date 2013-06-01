/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/nop/nop_graphics_system.h>

#include <xenia/gpu/gpu-private.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::nop;


NopGraphicsSystem::NopGraphicsSystem(const CreationParams* params) :
    GraphicsSystem(params) {
}

NopGraphicsSystem::~NopGraphicsSystem() {
}

uint64_t NopGraphicsSystem::ReadRegister(uint32_t r) {
  XELOGGPU("ReadRegister(%.4X)", r);
  return 0;
}

void NopGraphicsSystem::WriteRegister(uint32_t r, uint64_t value) {
  XELOGGPU("WriteRegister(%.4X, %.8X)", r, value);
}

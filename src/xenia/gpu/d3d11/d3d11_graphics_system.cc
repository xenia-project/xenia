/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_graphics_system.h>

#include <xenia/gpu/gpu-private.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;


D3D11GraphicsSystem::D3D11GraphicsSystem(const CreationParams* params) :
    GraphicsSystem(params) {
}

D3D11GraphicsSystem::~D3D11GraphicsSystem() {
}

uint64_t D3D11GraphicsSystem::ReadRegister(uint32_t r) {
  XELOGGPU("ReadRegister(%.4X)", r);
  return 0;
}

void D3D11GraphicsSystem::WriteRegister(uint32_t r, uint64_t value) {
  XELOGGPU("WriteRegister(%.4X, %.8X)", r, value);
}

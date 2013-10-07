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


NopGraphicsDriver::NopGraphicsDriver(xe_memory_ref memory) :
    GraphicsDriver(memory) {
}

NopGraphicsDriver::~NopGraphicsDriver() {
}

void NopGraphicsDriver::Initialize() {
}

void NopGraphicsDriver::InvalidateState(
    uint32_t mask) {
  if (mask == XE_GPU_INVALIDATE_MASK_ALL) {
    XELOGGPU("NOP: (invalidate all)");
  }
  if (mask & XE_GPU_INVALIDATE_MASK_VERTEX_SHADER) {
    XELOGGPU("NOP: invalidate vertex shader");
  }
  if (mask & XE_GPU_INVALIDATE_MASK_PIXEL_SHADER) {
    XELOGGPU("NOP: invalidate pixel shader");
  }
}

void NopGraphicsDriver::SetShader(
    XE_GPU_SHADER_TYPE type,
    uint32_t address,
    uint32_t start,
    uint32_t size_dwords) {
  XELOGGPU("NOP: set shader %d at %0.8X (%db)",
           type, address, size_dwords * 4);
}

void NopGraphicsDriver::DrawIndexed(
    XE_GPU_PRIMITIVE_TYPE prim_type,
    uint32_t index_count) {
 XELOGGPU("NOP: draw indexed %d (%d indicies)",
          prim_type, index_count);
}
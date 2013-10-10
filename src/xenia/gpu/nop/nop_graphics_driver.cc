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
#include <xenia/gpu/ucode/ucode_disassembler.h>
#include <xenia/gpu/ucode/ucode_ops.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::nop;
using namespace xe::gpu::ucode;


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
    uint32_t length) {
  XELOGGPU("NOP: set shader %d at %0.8X (%db)",
           type, address, length);

  uint8_t* p = xe_memory_addr(memory_, address);
  uint32_t dw0 = XEGETUINT32BE(p + 0);

  uint32_t dws[512] = {0};
  for (uint32_t n = 0; n < length; n += 4) {
    dws[n / 4] = XEGETUINT32BE(p + n);
  }

  UcodeDisassembler disasm;
  disasm.Disassemble(dws, length / 4, type == XE_GPU_SHADER_TYPE_PIXEL);
}

void NopGraphicsDriver::DrawIndexed(
    XE_GPU_PRIMITIVE_TYPE prim_type,
    uint32_t index_count) {
 XELOGGPU("NOP: draw indexed %d (%d indicies)",
          prim_type, index_count);
}

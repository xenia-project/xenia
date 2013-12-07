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
#include <xenia/gpu/shader_cache.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::nop;
using namespace xe::gpu::xenos;


NopGraphicsDriver::NopGraphicsDriver(Memory* memory) :
    GraphicsDriver(memory) {
  shader_cache_ = new ShaderCache();
}

NopGraphicsDriver::~NopGraphicsDriver() {
  delete shader_cache_;
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
  // Find or create shader in the cache.
  uint8_t* p = memory_->Translate(address);
  Shader* shader = shader_cache_->FindOrCreate(
      type, p, length);

  // Disassemble.
  const char* source = shader->disasm_src();
  if (!source) {
    source = "<failed to disassemble>";
  }
  XELOGGPU("NOP: set shader %d at %0.8X (%db):\n%s",
           type, address, length, source);
}

void NopGraphicsDriver::DrawIndexBuffer(
    XE_GPU_PRIMITIVE_TYPE prim_type,
    bool index_32bit, uint32_t index_count,
    uint32_t index_base, uint32_t index_size, uint32_t endianness) {
  XELOGGPU("NOP: draw index buffer");
}

void NopGraphicsDriver::DrawIndexAuto(
    XE_GPU_PRIMITIVE_TYPE prim_type,
    uint32_t index_count) {
  XELOGGPU("NOP: draw indexed %d (%d indicies)",
           prim_type, index_count);

  // TODO(benvanik):
  // program control
  // context misc
  // interpolator control
  // shader constants / bools / integers
  // fetch constants
}

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_graphics_driver.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_shader_cache.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


D3D11GraphicsDriver::D3D11GraphicsDriver(
    xe_memory_ref memory, ID3D11Device* device) :
    GraphicsDriver(memory) {
  device_ = device;
  device_->AddRef();

  shader_cache_ = new D3D11ShaderCache(device_);
}

D3D11GraphicsDriver::~D3D11GraphicsDriver() {
  delete shader_cache_;
  device_->Release();
}

void D3D11GraphicsDriver::Initialize() {
}

void D3D11GraphicsDriver::InvalidateState(
    uint32_t mask) {
  if (mask == XE_GPU_INVALIDATE_MASK_ALL) {
    XELOGGPU("D3D11: (invalidate all)");
  }
  if (mask & XE_GPU_INVALIDATE_MASK_VERTEX_SHADER) {
    XELOGGPU("D3D11: invalidate vertex shader");
  }
  if (mask & XE_GPU_INVALIDATE_MASK_PIXEL_SHADER) {
    XELOGGPU("D3D11: invalidate pixel shader");
  }
}

void D3D11GraphicsDriver::SetShader(
    XE_GPU_SHADER_TYPE type,
    uint32_t address,
    uint32_t start,
    uint32_t length) {
  // Find or create shader in the cache.
  uint8_t* p = xe_memory_addr(memory_, address);
  Shader* shader = shader_cache_->FindOrCreate(
      type, p, length);

  // Disassemble.
  char* source = shader->Disassemble();
  if (!source) {
    source = "<failed to disassemble>";
  }
  XELOGGPU("D3D11: set shader %d at %0.8X (%db):\n%s",
           type, address, length, source);
  if (source) {
    xe_free(source);
  }

  // Prepare for use.
  if (shader->Prepare()) {
    XELOGGPU("D3D11: failed to prepare shader");
  }
}

void D3D11GraphicsDriver::DrawIndexed(
    XE_GPU_PRIMITIVE_TYPE prim_type,
    uint32_t index_count) {
  XELOGGPU("D3D11: draw indexed %d (%d indicies)",
           prim_type, index_count);

  // TODO(benvanik):
  // program control
  // context misc
  // interpolator control
  // shader constants / bools / integers
  // fetch constants
}

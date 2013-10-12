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
#include <xenia/gpu/d3d11/d3d11_shader.h>
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
  device_->GetImmediateContext(&context_);
  shader_cache_ = new D3D11ShaderCache(device_);

  xe_zero_struct(&state_, sizeof(state_));

  HRESULT hr;
  D3D11_BUFFER_DESC buffer_desc;
  xe_zero_struct(&buffer_desc, sizeof(buffer_desc));
  buffer_desc.Usage           = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
  buffer_desc.ByteWidth       = (512 * 4) * sizeof(float);
  hr = device_->CreateBuffer(
      &buffer_desc, NULL, &state_.constant_buffers.float_constants);
  buffer_desc.ByteWidth       = (8) * sizeof(int);
  hr = device_->CreateBuffer(
      &buffer_desc, NULL, &state_.constant_buffers.bool_constants);
  buffer_desc.ByteWidth       = (32) * sizeof(int);
  hr = device_->CreateBuffer(
      &buffer_desc, NULL, &state_.constant_buffers.loop_constants);
}

D3D11GraphicsDriver::~D3D11GraphicsDriver() {
  XESAFERELEASE(state_.constant_buffers.float_constants);
  XESAFERELEASE(state_.constant_buffers.bool_constants);
  XESAFERELEASE(state_.constant_buffers.loop_constants);
  delete shader_cache_;
  XESAFERELEASE(context_);
  XESAFERELEASE(device_);
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

  // Stash for later.
  switch (type) {
  case XE_GPU_SHADER_TYPE_VERTEX:
    state_.vertex_shader = (D3D11VertexShader*)shader;
    break;
  case XE_GPU_SHADER_TYPE_PIXEL:
    state_.pixel_shader = (D3D11PixelShader*)shader;
    break;
  }
}

void D3D11GraphicsDriver::DrawAutoIndexed(
    XE_GPU_PRIMITIVE_TYPE prim_type,
    uint32_t index_count) {
  RegisterFile& rf = register_file_;

  XELOGGPU("D3D11: draw indexed %d (%d indicies)",
           prim_type, index_count);

  // Misc state.
  UpdateState();

  // Build constant buffers.
  UpdateConstantBuffers();

  // Bind shaders.
  BindShaders();

  // Switch primitive topology.
  // Some are unsupported on D3D11 and must be emulated.
  D3D11_PRIMITIVE_TOPOLOGY primitive_topology;
  switch (prim_type) {
  default:
  case XE_GPU_PRIMITIVE_TYPE_POINT_LIST:
    primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    break;
  case XE_GPU_PRIMITIVE_TYPE_LINE_LIST:
    primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    break;
  case XE_GPU_PRIMITIVE_TYPE_LINE_STRIP:
    primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    break;
  case XE_GPU_PRIMITIVE_TYPE_TRIANGLE_LIST:
    primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    break;
  case XE_GPU_PRIMITIVE_TYPE_TRIANGLE_STRIP:
    primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    break;
  case XE_GPU_PRIMITIVE_TYPE_TRIANGLE_FAN:
  case XE_GPU_PRIMITIVE_TYPE_UNKNOWN_07:
  case XE_GPU_PRIMITIVE_TYPE_RECTANGLE_LIST:
  case XE_GPU_PRIMITIVE_TYPE_LINE_LOOP:
    XELOGE("D3D11: unsupported primitive type %d", prim_type);
    return;
  }
  context_->IASetPrimitiveTopology(primitive_topology);

  // Setup all fetchers (vertices/textures).
  PrepareFetchers();

  // Setup index buffer.
  PrepareIndexBuffer();

  // Issue draw.
  uint32_t start_index = rf.values[XE_GPU_REG_VGT_INDX_OFFSET].u32;
  uint32_t base_vertex = 0;
  //context_->DrawIndexed(index_count, start_index, base_vertex);
}

void D3D11GraphicsDriver::UpdateState() {
  //context_->OMSetBlendState(blend_state, blend_factor, sample_mask);
  //context_->OMSetDepthStencilState
  //context_->RSSetScissorRects
  //context_->RSSetState
  //context_->RSSetViewports
}

void D3D11GraphicsDriver::UpdateConstantBuffers() {
  RegisterFile& rf = register_file_;

  D3D11_MAPPED_SUBRESOURCE res;
  context_->Map(
      state_.constant_buffers.float_constants, 0,
      D3D11_MAP_WRITE_DISCARD, 0, &res);
  memcpy(res.pData,
         &rf.values[XE_GPU_REG_SHADER_CONSTANT_000_X],
         (512 * 4) * sizeof(float));
  context_->Unmap(state_.constant_buffers.float_constants, 0);

  context_->Map(
      state_.constant_buffers.loop_constants, 0,
      D3D11_MAP_WRITE_DISCARD, 0, &res);
  memcpy(res.pData,
      &rf.values[XE_GPU_REG_SHADER_CONSTANT_LOOP_00],
      (32) * sizeof(int));
  context_->Unmap(state_.constant_buffers.loop_constants, 0);

  context_->Map(
      state_.constant_buffers.bool_constants, 0,
      D3D11_MAP_WRITE_DISCARD, 0, &res);
  memcpy(res.pData,
      &rf.values[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031],
      (8) * sizeof(int));
  context_->Unmap(state_.constant_buffers.bool_constants, 0);
}

void D3D11GraphicsDriver::BindShaders() {
  RegisterFile& rf = register_file_;
  xe_gpu_program_cntl_t program_cntl;
  program_cntl.dword_0 = rf.values[XE_GPU_REG_SQ_PROGRAM_CNTL].u32;

  // Vertex shader setup.
  D3D11VertexShader* vs = state_.vertex_shader;
  if (vs) {
    if (!vs->is_prepared()) {
      // Prepare for use.
      if (vs->Prepare(&program_cntl)) {
        XELOGGPU("D3D11: failed to prepare vertex shader");
        state_.vertex_shader = NULL;
        return;
      }
    }

    // Bind.
    context_->VSSetShader(vs->handle(), NULL, 0);

    // Set constant buffers.
    context_->VSSetConstantBuffers(
        0,
        sizeof(state_.constant_buffers) / sizeof(ID3D11Buffer*),
        (ID3D11Buffer**)&state_.constant_buffers);

    // Setup input layout (as encoded in vertex shader).
    context_->IASetInputLayout(vs->input_layout());

    //context_->VSSetSamplers
    //context_->VSSetShaderResources
  }

  // Pixel shader setup.
  D3D11PixelShader* ps = state_.pixel_shader;
  if (ps) {
    if (!ps->is_prepared()) {
      // Prepare for use.
      if (ps->Prepare(&program_cntl)) {
        XELOGGPU("D3D11: failed to prepare pixel shader");
        state_.pixel_shader = NULL;
        return;
      }
    }

    // Bind.
    context_->PSSetShader(ps->handle(), NULL, 0);

    // Set constant buffers.
    context_->PSSetConstantBuffers(
        0,
        sizeof(state_.constant_buffers) / sizeof(ID3D11Buffer*),
        (ID3D11Buffer**)&state_.constant_buffers);

    //context_->PSSetSamplers
    //context_->PSSetShaderResources
  }
}

void D3D11GraphicsDriver::PrepareFetchers() {
  RegisterFile& rf = register_file_;
  for (int n = 0; n < 32; n++) {
    int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + n * 6;
    xe_gpu_fetch_group_t* group = (xe_gpu_fetch_group_t*)&rf.values[r];
    if (group->type_0 == 0x2) {
      PrepareTextureFetcher(n, &group->texture_fetch);
    } else {
      // TODO(benvanik): verify register numbering.
      if (group->type_0 == 0x3) {
        PrepareVertexFetcher(n * 3 + 0, &group->vertex_fetch_0);
      }
      if (group->type_1 == 0x3) {
        PrepareVertexFetcher(n * 3 + 1, &group->vertex_fetch_1);
      }
      if (group->type_2 == 0x3) {
        PrepareVertexFetcher(n * 3 + 2, &group->vertex_fetch_2);
      }
    }
  }
}

void D3D11GraphicsDriver::PrepareVertexFetcher(
    int slot, xe_gpu_vertex_fetch_t* fetch) {
  uint32_t address = (fetch->address << 2) + address_translation_;
  uint32_t size_dwords = fetch->size;

  ID3D11Buffer* buffer = 0;
  D3D11_BUFFER_DESC buffer_desc;
  xe_zero_struct(&buffer_desc, sizeof(buffer_desc));
  buffer_desc.ByteWidth       = size_dwords * 4;
  buffer_desc.Usage           = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags       = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
  device_->CreateBuffer(&buffer_desc, NULL, &buffer);
  D3D11_MAPPED_SUBRESOURCE res;
  context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  uint32_t* src = (uint32_t*)xe_memory_addr(memory_, address);
  uint32_t* dest = (uint32_t*)res.pData;
  for (uint32_t n = 0; n < size_dwords; n++) {
    union {
      uint32_t i;
      float f;
    } d = {XESWAP32(src[n])};
    XELOGGPU("v%.3d %0.8X %g", n, d.i, d.f);

    dest[n] = XESWAP32(src[n]);
  }
  context_->Unmap(buffer, 0);

  // TODO(benvanik): fetch from VS.
  /*uint32_t stride = 0;
  uint32_t offset = 0;
  context_->IASetVertexBuffers(slot, 1, &buffer, &stride, &offset);*/

  buffer->Release();
}

void D3D11GraphicsDriver::PrepareTextureFetcher(
    int slot, xe_gpu_texture_fetch_t* fetch) {
}

void D3D11GraphicsDriver::PrepareIndexBuffer() {
  RegisterFile& rf = register_file_;

/*
  ID3D11Buffer* buffer = 0;
  D3D11_BUFFER_DESC buffer_desc;
  xe_zero_struct(&buffer_desc, sizeof(buffer_desc));
  buffer_desc.ByteWidth       = size_dwords * 4;
  buffer_desc.Usage           = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags       = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
  device_->CreateBuffer(&buffer_desc, NULL, &buffer);
  D3D11_MAPPED_SUBRESOURCE res;
  context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  uint32_t* src = (uint32_t*)xe_memory_addr(memory_, address);
  uint32_t* dest = (uint32_t*)res.pData;
  for (uint32_t n = 0; n < size_dwords; n++) {
    union {
      uint32_t i;
      float f;
    } d = {XESWAP32(src[n])};
    XELOGGPU("v%.3d %0.8X %g", n, d.i, d.f);

    dest[n] = XESWAP32(src[n]);
  }
  context_->Unmap(buffer, 0);

  DXGI_FORMAT format;
  format = DXGI_FORMAT_R16_UINT;
  format = DXGI_FORMAT_R32_UINT;
  context_->IASetIndexBuffer(buffer, format, 0);

  buffer->Release();*/
}

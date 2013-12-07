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
    Memory* memory, ID3D11Device* device) :
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
  uint8_t* p = memory_->Translate(address);
  Shader* shader = shader_cache_->FindOrCreate(
      type, p, length);

  // Disassemble.
  const char* source = shader->disasm_src();
  if (!source) {
    source = "<failed to disassemble>";
  }
  XELOGGPU("D3D11: set shader %d at %0.8X (%db):\n%s",
           type, address, length, source);

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

int D3D11GraphicsDriver::SetupDraw(XE_GPU_PRIMITIVE_TYPE prim_type) {
  RegisterFile& rf = register_file_;

  // Misc state.
  if (UpdateState()) {
    return 1;
  }

  // Build constant buffers.
  if (UpdateConstantBuffers()) {
    return 1;
  }

  // Bind shaders.
  if (BindShaders()) {
    return 1;
  }

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
    return 1;
  }
  context_->IASetPrimitiveTopology(primitive_topology);

  // Setup all fetchers (vertices/textures).
  if (PrepareFetchers()) {
    return 1;
  }

  // All ready to draw (except index buffer)!

  return 0;
}

void D3D11GraphicsDriver::DrawIndexBuffer(
    XE_GPU_PRIMITIVE_TYPE prim_type,
    bool index_32bit, uint32_t index_count,
    uint32_t index_base, uint32_t index_size, uint32_t endianness) {
  RegisterFile& rf = register_file_;

  XELOGGPU("D3D11: draw indexed %d (%d indicies) from %.8X",
           prim_type, index_count, index_base);

  // Setup shaders/etc.
  if (SetupDraw(prim_type)) {
    return;
  }

  // Setup index buffer.
  if (PrepareIndexBuffer(
      index_32bit, index_count, index_base, index_size, endianness)) {
    return;
  }

  // Issue draw.
  uint32_t start_index = rf.values[XE_GPU_REG_VGT_INDX_OFFSET].u32;
  uint32_t base_vertex = 0;
  context_->DrawIndexed(index_count, start_index, base_vertex);
}

void D3D11GraphicsDriver::DrawIndexAuto(
    XE_GPU_PRIMITIVE_TYPE prim_type,
    uint32_t index_count) {
  RegisterFile& rf = register_file_;

  XELOGGPU("D3D11: draw indexed %d (%d indicies)",
           prim_type, index_count);

  // Setup shaders/etc.
  if (SetupDraw(prim_type)) {
    return;
  }

  // Issue draw.
  uint32_t start_index = rf.values[XE_GPU_REG_VGT_INDX_OFFSET].u32;
  uint32_t base_vertex = 0;
  //context_->DrawIndexed(index_count, start_index, base_vertex);
  context_->Draw(index_count, 0);
}

int D3D11GraphicsDriver::UpdateState() {
  // General rasterizer state.
  ID3D11RasterizerState* rasterizer_state = 0;
  D3D11_RASTERIZER_DESC rasterizer_desc;
  xe_zero_struct(&rasterizer_desc, sizeof(rasterizer_desc));
  rasterizer_desc.FillMode              = D3D11_FILL_SOLID; // D3D11_FILL_WIREFRAME;
  rasterizer_desc.CullMode              = D3D11_CULL_NONE; // D3D11_CULL_FRONT BACK
  rasterizer_desc.FrontCounterClockwise = false;
  rasterizer_desc.DepthBias             = 0;
  rasterizer_desc.DepthBiasClamp        = 0;
  rasterizer_desc.SlopeScaledDepthBias  = 0;
  rasterizer_desc.DepthClipEnable       = true;
  rasterizer_desc.ScissorEnable         = false;
  rasterizer_desc.MultisampleEnable     = false;
  rasterizer_desc.AntialiasedLineEnable = false;
  device_->CreateRasterizerState(&rasterizer_desc, &rasterizer_state);
  context_->RSSetState(rasterizer_state);
  XESAFERELEASE(rasterizer_state);

  // Depth-stencil state.
  //context_->OMSetDepthStencilState

  // Blend state.
  //context_->OMSetBlendState(blend_state, blend_factor, sample_mask);

  // Scissoring.
  // TODO(benvanik): pull from scissor registers.
  context_->RSSetScissorRects(0, NULL);

  // Viewport.
  // If we have resized the window we will want to change this.
  D3D11_VIEWPORT viewport;
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  viewport.TopLeftX = 0;
  viewport.TopLeftY = 0;
  viewport.Width    = 1280;
  viewport.Height   = 720;
  context_->RSSetViewports(1, &viewport);

  return 0;
}

int D3D11GraphicsDriver::UpdateConstantBuffers() {
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

  return 0;
}

int D3D11GraphicsDriver::BindShaders() {
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
        return 1;
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
  } else {
    context_->VSSetShader(NULL, NULL, 0);
    context_->IASetInputLayout(NULL);
    return 1;
  }

  // Pixel shader setup.
  D3D11PixelShader* ps = state_.pixel_shader;
  if (ps) {
    if (!ps->is_prepared()) {
      // Prepare for use.
      if (ps->Prepare(&program_cntl, vs)) {
        XELOGGPU("D3D11: failed to prepare pixel shader");
        state_.pixel_shader = NULL;
        return 1;
      }
    }

    // Bind.
    context_->PSSetShader(ps->handle(), NULL, 0);

    // Set constant buffers.
    context_->PSSetConstantBuffers(
        0,
        sizeof(state_.constant_buffers) / sizeof(ID3D11Buffer*),
        (ID3D11Buffer**)&state_.constant_buffers);

    // TODO(benvanik): set samplers for all inputs.
    D3D11_SAMPLER_DESC sampler_desc;
    xe_zero_struct(&sampler_desc, sizeof(sampler_desc));
    //sampler_desc.Filter = ?
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MipLODBias = 0;
    sampler_desc.MaxAnisotropy = 1;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    //sampler_desc.BorderColor = ...;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = 0;
    ID3D11SamplerState* sampler_state = NULL;
    device_->CreateSamplerState(&sampler_desc, &sampler_state);
    ID3D11SamplerState* sampler_states[] = { sampler_state };
    context_->PSSetSamplers(0, XECOUNT(sampler_states), sampler_states);
    sampler_state->Release();

    //context_->PSSetShaderResources
  } else {
    context_->PSSetShader(NULL, NULL, 0);
    return 1;
  }

  return 0;
}

int D3D11GraphicsDriver::PrepareFetchers() {
  RegisterFile& rf = register_file_;
  for (int n = 0; n < 32; n++) {
    int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + n * 6;
    xe_gpu_fetch_group_t* group = (xe_gpu_fetch_group_t*)&rf.values[r];
    if (group->type_0 == 0x2) {
      if (PrepareTextureFetcher(n, &group->texture_fetch)) {
        return 1;
      }
    } else {
      // TODO(benvanik): verify register numbering.
      if (group->type_0 == 0x3) {
        if (PrepareVertexFetcher(n * 3 + 0, &group->vertex_fetch_0)) {
          return 1;
        }
      }
      if (group->type_1 == 0x3) {
        if (PrepareVertexFetcher(n * 3 + 1, &group->vertex_fetch_1)) {
          return 1;
        }
      }
      if (group->type_2 == 0x3) {
        if (PrepareVertexFetcher(n * 3 + 2, &group->vertex_fetch_2)) {
          return 1;
        }
      }
    }
  }

  return 0;
}

int D3D11GraphicsDriver::PrepareVertexFetcher(
    int fetch_slot, xe_gpu_vertex_fetch_t* fetch) {
  uint32_t address = (fetch->address << 2) + address_translation_;
  uint32_t size_dwords = fetch->size;

  ID3D11Buffer* buffer = 0;
  D3D11_BUFFER_DESC buffer_desc;
  xe_zero_struct(&buffer_desc, sizeof(buffer_desc));
  buffer_desc.ByteWidth       = size_dwords * 4;
  buffer_desc.Usage           = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags       = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
  HRESULT hr = device_->CreateBuffer(&buffer_desc, NULL, &buffer);
  if (FAILED(hr)) {
    XELOGE("D3D11: unable to create vertex fetch buffer");
    return 1;
  }
  D3D11_MAPPED_SUBRESOURCE res;
  hr = context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  if (FAILED(hr)) {
    XELOGE("D3D11: unable to map vertex fetch buffer");
    XESAFERELEASE(buffer);
    return 1;
  }
  uint32_t* src = (uint32_t*)memory_->Translate(address);
  uint32_t* dest = (uint32_t*)res.pData;
  for (uint32_t n = 0; n < size_dwords; n++) {
    // union {
    //   uint32_t i;
    //   float f;
    // } d = {XESWAP32(src[n])};
    // XELOGGPU("v%.3d %0.8X %g", n, d.i, d.f);
    dest[n] = XESWAP32(src[n]);
  }
  context_->Unmap(buffer, 0);

  D3D11VertexShader* vs = state_.vertex_shader;
  if (!vs) {
    return 1;
  }
  const instr_fetch_vtx_t* vtx = vs->GetFetchVtxBySlot(fetch_slot);
  if (!vtx->must_be_one) {
    return 1;
  }
  // TODO(benvanik): always dword aligned?
  uint32_t stride = vtx->stride * 4;
  uint32_t offset = 0;
  int vb_slot = 95 - fetch_slot;
  context_->IASetVertexBuffers(vb_slot, 1, &buffer, &stride, &offset);

  buffer->Release();

  return 0;
}

int D3D11GraphicsDriver::PrepareTextureFetcher(
    int fetch_slot, xe_gpu_texture_fetch_t* fetch) {
  RegisterFile& rf = register_file_;

  // maybe << 2?
  uint32_t address = (fetch->address << 4) + address_translation_;
  return 0;
}

int D3D11GraphicsDriver::PrepareIndexBuffer(
    bool index_32bit, uint32_t index_count,
    uint32_t index_base, uint32_t index_size, uint32_t endianness) {
  RegisterFile& rf = register_file_;

  uint32_t address = index_base + address_translation_;

  // All that's done so far:
  XEASSERT(endianness == 0x2);

  ID3D11Buffer* buffer = 0;
  D3D11_BUFFER_DESC buffer_desc;
  xe_zero_struct(&buffer_desc, sizeof(buffer_desc));
  buffer_desc.ByteWidth       = index_size;
  buffer_desc.Usage           = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags       = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
  device_->CreateBuffer(&buffer_desc, NULL, &buffer);
  D3D11_MAPPED_SUBRESOURCE res;
  context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  if (index_32bit) {
    uint32_t* src = (uint32_t*)memory_->Translate(address);
    uint32_t* dest = (uint32_t*)res.pData;
    for (uint32_t n = 0; n < index_count; n++) {
      uint32_t d = { XESWAP32(src[n]) };
      //XELOGGPU("i%.4d %0.8X", n, d);
      dest[n] = d;
    }
  } else {
    uint16_t* src = (uint16_t*)memory_->Translate(address);
    uint16_t* dest = (uint16_t*)res.pData;
    for (uint32_t n = 0; n < index_count; n++) {
      uint16_t d = XESWAP16(src[n]);
      //XELOGGPU("i%.4d, %.4X", n, d);
      dest[n] = d;
    }
  }
  context_->Unmap(buffer, 0);

  DXGI_FORMAT format;
  format = index_32bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
  context_->IASetIndexBuffer(buffer, format, 0);

  buffer->Release();

  return 0;
}

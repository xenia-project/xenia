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
  case XE_GPU_PRIMITIVE_TYPE_RECTANGLE_LIST:
    XELOGW("D3D11: faking RECTANGLE_LIST as a tri list");
    primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    break;
  case XE_GPU_PRIMITIVE_TYPE_TRIANGLE_FAN:
  case XE_GPU_PRIMITIVE_TYPE_UNKNOWN_07:
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
  // https://chromium.googlesource.com/chromiumos/third_party/mesa/+/6173cc19c45d92ef0b7bc6aa008aa89bb29abbda/src/gallium/drivers/freedreno/freedreno_zsa.c
  // http://cgit.freedesktop.org/mesa/mesa/diff/?id=aac7f06ad843eaa696363e8e9c7781ca30cb4914
  RegisterFile& rf = register_file_;

  // RB_SURFACE_INFO
  // RB_DEPTH_INFO

  // General rasterizer state.
  uint32_t mode_control = rf.values[XE_GPU_REG_RB_MODECONTROL].u32;
  D3D11_RASTERIZER_DESC rasterizer_desc;
  xe_zero_struct(&rasterizer_desc, sizeof(rasterizer_desc));
  rasterizer_desc.FillMode              = D3D11_FILL_SOLID; // D3D11_FILL_WIREFRAME;
  switch (mode_control & 0x3) {
  case 0:
    rasterizer_desc.CullMode            = D3D11_CULL_NONE;
    break;
  case 1:
    rasterizer_desc.CullMode            = D3D11_CULL_FRONT;
    break;
  case 2:
    rasterizer_desc.CullMode            = D3D11_CULL_BACK;
    break;
  }
  rasterizer_desc.FrontCounterClockwise = (mode_control & 0x4) == 0;
  rasterizer_desc.DepthBias             = 0;
  rasterizer_desc.DepthBiasClamp        = 0;
  rasterizer_desc.SlopeScaledDepthBias  = 0;
  rasterizer_desc.DepthClipEnable       = true;
  rasterizer_desc.ScissorEnable         = false;
  rasterizer_desc.MultisampleEnable     = false;
  rasterizer_desc.AntialiasedLineEnable = false;
  ID3D11RasterizerState* rasterizer_state = 0;
  device_->CreateRasterizerState(&rasterizer_desc, &rasterizer_state);
  context_->RSSetState(rasterizer_state);
  XESAFERELEASE(rasterizer_state);

  // Depth-stencil state.
  uint32_t depth_control = rf.values[XE_GPU_REG_RB_DEPTHCONTROL].u32;
  D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
  xe_zero_struct(&depth_stencil_desc, sizeof(depth_stencil_desc));
  // A2XX_RB_DEPTHCONTROL_BACKFACE_ENABLE
  // ?
  // A2XX_RB_DEPTHCONTROL_Z_ENABLE
  depth_stencil_desc.DepthEnable = (depth_control & 0x00000002) != 0;
  // A2XX_RB_DEPTHCONTROL_Z_WRITE_ENABLE
  depth_stencil_desc.DepthWriteMask = (depth_control & 0x00000004) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
  // A2XX_RB_DEPTHCONTROL_EARLY_Z_ENABLE
  // ?
  // A2XX_RB_DEPTHCONTROL_ZFUNC
  // 0 = never, 7 = always -- almost lines up
  depth_stencil_desc.DepthFunc = (D3D11_COMPARISON_FUNC)(((depth_control & 0x00000070) >> 4) + 1);
  // A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE
  depth_stencil_desc.StencilEnable = (depth_control & 0x00000001) != 0;
  depth_stencil_desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
  depth_stencil_desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
  // A2XX_RB_DEPTHCONTROL_STENCILFUNC
  depth_stencil_desc.FrontFace.StencilFunc =
      (D3D11_COMPARISON_FUNC)(((depth_control & 0x00000700) >> 8) + 1);
  // A2XX_RB_DEPTHCONTROL_STENCILFAIL
  // 0 = keep, 7 = decr -- almost lines up
  depth_stencil_desc.FrontFace.StencilFailOp =
      (D3D11_STENCIL_OP)(((depth_control & 0x00003800) >> 11) + 1);
  // A2XX_RB_DEPTHCONTROL_STENCILZPASS
  depth_stencil_desc.FrontFace.StencilPassOp =
      (D3D11_STENCIL_OP)(((depth_control & 0x0001C000) >> 14) + 1);
  // A2XX_RB_DEPTHCONTROL_STENCILZFAIL
  depth_stencil_desc.FrontFace.StencilDepthFailOp =
      (D3D11_STENCIL_OP)(((depth_control & 0x000E0000) >> 17) + 1);
  // A2XX_RB_DEPTHCONTROL_STENCILFUNC_BF
  depth_stencil_desc.BackFace.StencilFunc =
      (D3D11_COMPARISON_FUNC)(((depth_control & 0x00700000) >> 20) + 1);
  // A2XX_RB_DEPTHCONTROL_STENCILFAIL_BF
  depth_stencil_desc.BackFace.StencilFailOp =
      (D3D11_STENCIL_OP)(((depth_control & 0x03800000) >> 23) + 1);
  // A2XX_RB_DEPTHCONTROL_STENCILZPASS_BF
  depth_stencil_desc.BackFace.StencilPassOp =
      (D3D11_STENCIL_OP)(((depth_control & 0x1C000000) >> 26) + 1);
  // A2XX_RB_DEPTHCONTROL_STENCILZFAIL_BF
  depth_stencil_desc.BackFace.StencilDepthFailOp =
      (D3D11_STENCIL_OP)(((depth_control & 0xE0000000) >> 29) + 1);
  ID3D11DepthStencilState* depth_stencil_state = 0;
  device_->CreateDepthStencilState(&depth_stencil_desc, &depth_stencil_state);
  context_->OMSetDepthStencilState(depth_stencil_state, 0 /* stencil ref */);
  XESAFERELEASE(depth_stencil_state);

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
  XEASSERTNOTNULL(state_.vertex_shader);
  auto inputs = state_.vertex_shader->GetVertexBufferInputs();
  for (size_t n = 0; n < inputs->count; n++) {
    auto input = inputs->descs[n];
    if (PrepareVertexBuffer(input)) {
      XELOGE("D3D11: unable to prepare vertex buffer");
      return 1;
    }
  }

  // TODO(benvanik): rewrite by sampler
  RegisterFile& rf = register_file_;
  for (int n = 0; n < 32; n++) {
    int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + n * 6;
    xe_gpu_fetch_group_t* group = (xe_gpu_fetch_group_t*)&rf.values[r];
    if (group->type_0 == 0x2) {
      if (PrepareTextureFetcher(n, &group->texture_fetch)) {
        return 1;
      }
    }
  }

  return 0;
}

int D3D11GraphicsDriver::PrepareVertexBuffer(Shader::vtx_buffer_desc_t& desc) {
  RegisterFile& rf = register_file_;
  int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + (desc.fetch_slot / 3) * 6;
  xe_gpu_fetch_group_t* group = (xe_gpu_fetch_group_t*)&rf.values[r];
  xe_gpu_vertex_fetch_t* fetch = NULL;
  switch (desc.fetch_slot % 3) {
  case 0:
    fetch = &group->vertex_fetch_0;
    break;
  case 1:
    fetch = &group->vertex_fetch_1;
    break;
  case 2:
    fetch = &group->vertex_fetch_2;
    break;
  }
  XEASSERTNOTNULL(fetch);
  // If this assert doesn't hold, maybe we just abort?
  XEASSERT(fetch->type == 0x3);
  XEASSERTNOTZERO(fetch->size);

  ID3D11Buffer* buffer = 0;
  D3D11_BUFFER_DESC buffer_desc;
  xe_zero_struct(&buffer_desc, sizeof(buffer_desc));
  buffer_desc.ByteWidth       = fetch->size * 4;
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
  uint32_t address = (fetch->address << 2) + address_translation_;
  uint8_t* src = (uint8_t*)memory_->Translate(address);
  uint8_t* dest = (uint8_t*)res.pData;
  // TODO(benvanik): rewrite to be faster/special case common/etc
  for (size_t n = 0; n < desc.element_count; n++) {
    auto& el = desc.elements[n];
    uint32_t stride = desc.stride_words;
    uint32_t count = fetch->size / stride;
    uint32_t* src_ptr = (uint32_t*)(src + el.offset_words * 4);
    uint32_t* dest_ptr = (uint32_t*)(dest + el.offset_words * 4);
    uint32_t o = 0;
    for (uint32_t i = 0; i < count; i++) {
      for (uint32_t j = 0; j < el.size_words; j++) {
        dest_ptr[o + j] = XESWAP32(src_ptr[o + j]);
      }
      o += stride;
    }
  }
  context_->Unmap(buffer, 0);

  D3D11VertexShader* vs = state_.vertex_shader;
  if (!vs) {
    return 1;
  }
  // TODO(benvanik): always dword aligned?
  uint32_t stride = desc.stride_words * 4;
  uint32_t offset = 0;
  int vb_slot = desc.input_index;
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

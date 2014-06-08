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
#include <xenia/gpu/buffer_resource.h>
#include <xenia/gpu/shader_resource.h>
#include <xenia/gpu/texture_resource.h>
#include <xenia/gpu/d3d11/d3d11_geometry_shader.h>
#include <xenia/gpu/d3d11/d3d11_shader_resource.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


#define XETRACED3D(fmt, ...) if (FLAGS_trace_ring_buffer) XELOGGPU(fmt, ##__VA_ARGS__)


D3D11GraphicsDriver::D3D11GraphicsDriver(
    Memory* memory, IDXGISwapChain* swap_chain, ID3D11Device* device) :
    GraphicsDriver(memory) {
  swap_chain_ = swap_chain;
  swap_chain_->AddRef();
  device_ = device;
  device_->AddRef();
  device_->GetImmediateContext(&context_);

  resource_cache_ = new D3D11ResourceCache(memory, device_, context_);

  xe_zero_struct(&state_, sizeof(state_));

  xe_zero_struct(&render_targets_, sizeof(render_targets_));

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
  buffer_desc.ByteWidth       = (32) * sizeof(int);
  hr = device_->CreateBuffer(
      &buffer_desc, NULL, &state_.constant_buffers.vs_consts);
  buffer_desc.ByteWidth       = (32) * sizeof(int);
  hr = device_->CreateBuffer(
      &buffer_desc, NULL, &state_.constant_buffers.gs_consts);
}

D3D11GraphicsDriver::~D3D11GraphicsDriver() {
  RebuildRenderTargets(0, 0);
  XESAFERELEASE(state_.constant_buffers.float_constants);
  XESAFERELEASE(state_.constant_buffers.bool_constants);
  XESAFERELEASE(state_.constant_buffers.loop_constants);
  XESAFERELEASE(state_.constant_buffers.vs_consts);
  XESAFERELEASE(state_.constant_buffers.gs_consts);
  for (auto it = rasterizer_state_cache_.begin();
       it != rasterizer_state_cache_.end(); ++it) {
    XESAFERELEASE(it->second);
  }
  for (auto it = blend_state_cache_.begin();
       it != blend_state_cache_.end(); ++it) {
    XESAFERELEASE(it->second);
  }
  for (auto it = depth_stencil_state_cache_.begin();
       it != depth_stencil_state_cache_.end(); ++it) {
    XESAFERELEASE(it->second);
  }
  XESAFERELEASE(invalid_texture_view_);
  XESAFERELEASE(invalid_texture_sampler_state_);
  delete resource_cache_;
  XESAFERELEASE(context_);
  XESAFERELEASE(device_);
  XESAFERELEASE(swap_chain_);
}

int D3D11GraphicsDriver::Initialize() {
  InitializeInvalidTexture();
  return 0;
}

void D3D11GraphicsDriver::InitializeInvalidTexture() {
  // TODO(benvanik): pattern?
  D3D11_TEXTURE2D_DESC texture_desc;
  xe_zero_struct(&texture_desc, sizeof(texture_desc));
  texture_desc.Width          = 4;
  texture_desc.Height         = 4;
  texture_desc.MipLevels      = 1;
  texture_desc.ArraySize      = 1;
  texture_desc.Format         = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count   = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage          = D3D11_USAGE_IMMUTABLE;
  texture_desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
  texture_desc.CPUAccessFlags = 0;
  texture_desc.MiscFlags      = 0; // D3D11_RESOURCE_MISC_GENERATE_MIPS?
  uint32_t texture_data[] = {
    0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00,
    0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00,
    0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00,
    0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00,
  };
  D3D11_SUBRESOURCE_DATA initial_data;
  initial_data.SysMemPitch = 4 * texture_desc.Width;
  initial_data.SysMemSlicePitch = 0;
  initial_data.pSysMem = texture_data;
  ID3D11Texture2D* texture = NULL;
  HRESULT hr = device_->CreateTexture2D(
      &texture_desc, &initial_data, (ID3D11Texture2D**)&texture);
  if (FAILED(hr)) {
    XEFATAL("D3D11: unable to create invalid texture");
    return;
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC texture_view_desc;
  xe_zero_struct(&texture_view_desc, sizeof(texture_view_desc));
  texture_view_desc.Format = texture_desc.Format;
  texture_view_desc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
  texture_view_desc.Texture2D.MipLevels = 1;
  texture_view_desc.Texture2D.MostDetailedMip = 0;
  hr = device_->CreateShaderResourceView(
        texture, &texture_view_desc, &invalid_texture_view_);
  XESAFERELEASE(texture);

  D3D11_SAMPLER_DESC sampler_desc;
  xe_zero_struct(&sampler_desc, sizeof(sampler_desc));
  sampler_desc.Filter;
  sampler_desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MipLODBias;
  sampler_desc.MaxAnisotropy  = 1;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  sampler_desc.BorderColor[0];
  sampler_desc.BorderColor[1];
  sampler_desc.BorderColor[2];
  sampler_desc.BorderColor[3];
  sampler_desc.MinLOD;
  sampler_desc.MaxLOD;
  hr = device_->CreateSamplerState(
      &sampler_desc, &invalid_texture_sampler_state_);
  if (FAILED(hr)) {
    XEFATAL("D3D11: unable to create invalid sampler state");
    return;
  }
}

int D3D11GraphicsDriver::Draw(const DrawCommand& command) {
  SCOPE_profile_cpu_f("gpu");

  // Misc state.
  if (UpdateState(command)) {
    return 1;
  }

  // Build constant buffers.
  if (SetupConstantBuffers(command)) {
    return 1;
  }

  // Bind shaders.
  if (SetupShaders(command)) {
    return 1;
  }

  // Bind vertex buffers/index buffer.
  if (SetupInputAssembly(command)) {
    return 1;
  }

  // Bind texture fetchers.
  if (SetupSamplers(command)) {
    return 1;
  }

  if (command.index_buffer) {
    // Have an actual index buffer.
    XETRACED3D("D3D11: draw indexed %d (indicies [%d,%d] (%d))",
               command.prim_type, command.start_index,
               command.start_index + command.index_count, command.index_count);
    context_->DrawIndexed(command.index_count, command.start_index,
                          command.base_vertex);
  } else {
    // Auto draw.
    XETRACED3D("D3D11: draw indexed auto %d (indicies [%d,%d] (%d))",
               command.prim_type, command.start_index,
               command.start_index + command.index_count, command.index_count);
    context_->Draw(command.index_count, 0);
  }

  return 0;
}

int D3D11GraphicsDriver::UpdateState(const DrawCommand& command) {
  SCOPE_profile_cpu_f("gpu");

  // Most information comes from here:
  // https://chromium.googlesource.com/chromiumos/third_party/mesa/+/6173cc19c45d92ef0b7bc6aa008aa89bb29abbda/src/gallium/drivers/freedreno/freedreno_zsa.c
  // http://cgit.freedesktop.org/mesa/mesa/diff/?id=aac7f06ad843eaa696363e8e9c7781ca30cb4914
  // The only differences so far are extra packets for multiple render targets
  // and a few modes being switched around.

  RegisterFile& rf = register_file_;

  uint32_t window_scissor_tl = register_file_[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL].u32;
  uint32_t window_scissor_br = register_file_[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR].u32;
  //uint32_t window_width =
  //    (window_scissor_br & 0x7FFF) - (window_scissor_tl & 0x7FFF);
  //uint32_t window_height =
  //    ((window_scissor_br >> 16) & 0x7FFF) - ((window_scissor_tl >> 16) & 0x7FFF);
  uint32_t window_width = 1280;
  uint32_t window_height = 720;
  if (RebuildRenderTargets(window_width, window_height)) {
    XELOGE("Unable to rebuild render targets to %d x %d",
            window_width, window_height);
    return 1;
  }

  // RB_SURFACE_INFO ?

  // Enable buffers.
  uint32_t enable_mode = register_file_[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7;
  // 4 = color + depth
  // 6 = copy ?

  // color_info[0-3] has format 8888
  uint32_t color_info[4] = {
    register_file_[XE_GPU_REG_RB_COLOR_INFO].u32,
    register_file_[XE_GPU_REG_RB_COLOR1_INFO].u32,
    register_file_[XE_GPU_REG_RB_COLOR2_INFO].u32,
    register_file_[XE_GPU_REG_RB_COLOR3_INFO].u32,
  };
  ID3D11RenderTargetView* render_target_views[4] = { 0 };
  for (int n = 0; n < XECOUNT(color_info); n++) {
    auto cb = render_targets_.color_buffers[n];
    uint32_t color_format = (color_info[n] >> 16) & 0xF;
    switch (color_format) {
    case 0: // D3DFMT_A8R8G8B8 (or ABGR?)
    case 1:
      render_target_views[n] = cb.color_view_8888;
      break;
    default:
      // Unknown.
      XELOGGPU("Unsupported render target format %d", color_format);
      break;
    }
  }

  // depth_info has format 24_8
  uint32_t depth_info = register_file_[XE_GPU_REG_RB_DEPTH_INFO].u32;
  uint32_t depth_format = (depth_info >> 16) & 0x1;
  ID3D11DepthStencilView* depth_stencil_view = 0;
  switch (depth_format) {
  case 0: // D3DFMT_D24S8
    depth_stencil_view = render_targets_.depth_view_d28s8;
    break;
  default:
  case 1: // D3DFMT_D24FS8
    //depth_stencil_view = render_targets_.depth_view_d28fs8;
    XELOGGPU("Unsupported depth/stencil format %d", depth_format);
    break;
  }
  // TODO(benvanik): when a game switches does it expect to keep the same
  //     depth buffer contents?

  // TODO(benvanik): only enable the number of valid render targets.
  context_->OMSetRenderTargets(4, render_target_views, depth_stencil_view);

  // Viewport.
  // If we have resized the window we will want to change this.
  uint32_t window_offset = register_file_[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32;
  // signed?
  uint32_t window_offset_x = window_offset & 0x7FFF;
  uint32_t window_offset_y = (window_offset >> 16) & 0x7FFF;

  // ?
  // TODO(benvanik): figure out how to emulate viewports in D3D11. Could use
  //     viewport above to scale, though that doesn't support negatives/etc.
  uint32_t vte_control = register_file_[XE_GPU_REG_PA_CL_VTE_CNTL].u32;
  bool vport_xscale_enable = (vte_control & (1 << 0)) > 0;
  float vport_xscale = register_file_[XE_GPU_REG_PA_CL_VPORT_XSCALE].f32; // 640
  bool vport_xoffset_enable = (vte_control & (1 << 1)) > 0;
  float vport_xoffset = register_file_[XE_GPU_REG_PA_CL_VPORT_XOFFSET].f32; // 640
  bool vport_yscale_enable = (vte_control & (1 << 2)) > 0;
  float vport_yscale = register_file_[XE_GPU_REG_PA_CL_VPORT_YSCALE].f32; // -360
  bool vport_yoffset_enable = (vte_control & (1 << 3)) > 0;
  float vport_yoffset = register_file_[XE_GPU_REG_PA_CL_VPORT_YOFFSET].f32; // 360
  bool vport_zscale_enable = (vte_control & (1 << 4)) > 0;
  float vport_zscale = register_file_[XE_GPU_REG_PA_CL_VPORT_ZSCALE].f32; // 1
  bool vport_zoffset_enable = (vte_control & (1 << 5)) > 0;
  float vport_zoffset = register_file_[XE_GPU_REG_PA_CL_VPORT_ZOFFSET].f32; // 0

  // TODO(benvanik): compute viewport values.
  D3D11_VIEWPORT viewport;
  if (vport_xscale_enable) {
    // Viewport enabled.
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width    = 1280;
    viewport.Height   = 720;
  } else {
    // Viewport disabled. Geometry shaders will compensate for this.
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width    = 1280;
    viewport.Height   = 720;
  }
  context_->RSSetViewports(1, &viewport);

  // Viewport constants from D3D11VertexShader.
  //"cbuffer vs_consts {\n"
  //"  float4 window;\n"              // x,y,w,h
  //"  float4 viewport_z_enable;\n"   // min,(max - min),?,enabled
  //"  float4 viewport_size;\n"       // x,y,w,h
  //"};"
  // TODO(benvanik): only when viewport changes.
  D3D11_MAPPED_SUBRESOURCE res;
  context_->Map(
      state_.constant_buffers.vs_consts, 0,
      D3D11_MAP_WRITE_DISCARD, 0, &res);
  float* vsc_buffer = (float*)res.pData;
  vsc_buffer[0] = (float)window_offset_x;
  vsc_buffer[1] = (float)window_offset_y;
  vsc_buffer[2] = (float)window_width;
  vsc_buffer[3] = (float)window_height;
  vsc_buffer[4] = viewport.MinDepth;
  vsc_buffer[5] = viewport.MaxDepth - viewport.MinDepth;
  vsc_buffer[6] = 0; // unused
  vsc_buffer[7] = vport_xscale_enable ? 1.0f : 0.0f;
  vsc_buffer[8] = viewport.TopLeftX;
  vsc_buffer[9] = viewport.TopLeftY;
  vsc_buffer[10] = viewport.Width;
  vsc_buffer[11] = viewport.Height;
  context_->Unmap(state_.constant_buffers.vs_consts, 0);

  // Scissoring.
  // TODO(benvanik): pull from scissor registers.
  // ScissorEnable must be set in raster state above.
  uint32_t screen_scissor_tl = register_file_[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL].u32;
  uint32_t screen_scissor_br = register_file_[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR].u32;
  if (screen_scissor_tl != 0 && screen_scissor_br != 0x20002000) {
    D3D11_RECT scissor_rect;
    scissor_rect.top = (screen_scissor_tl >> 16) & 0x7FFF;
    scissor_rect.left = screen_scissor_tl & 0x7FFF;
    scissor_rect.bottom = (screen_scissor_br >> 16) & 0x7FFF;
    scissor_rect.right = screen_scissor_br & 0x7FFF;
    context_->RSSetScissorRects(1, &scissor_rect);
  } else {
    context_->RSSetScissorRects(0, NULL);
  }

  if (SetupRasterizerState(command)) {
    XELOGE("Unable to setup rasterizer state");
    return 1;
  }
  if (SetupBlendState(command)) {
    XELOGE("Unable to setup blend state");
    return 1;
  }
  if (SetupDepthStencilState(command)) {
    XELOGE("Unable to setup depth/stencil state");
    return 1;
  }

  return 0;
}

int D3D11GraphicsDriver::SetupRasterizerState(const DrawCommand& command) {
  uint32_t mode_control = register_file_[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32;

  // Check cache.
  uint64_t key = hash_combine(mode_control);
  ID3D11RasterizerState* rasterizer_state = nullptr;
  auto it = rasterizer_state_cache_.find(key);
  if (it == rasterizer_state_cache_.end()) {
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
    if (command.prim_type == XE_GPU_PRIMITIVE_TYPE_RECTANGLE_LIST) {
      // Rect lists aren't culled. There may be other things they skip too.
      rasterizer_desc.CullMode            = D3D11_CULL_NONE;
    }
    rasterizer_desc.FrontCounterClockwise = (mode_control & 0x4) == 0;
    rasterizer_desc.DepthBias             = 0;
    rasterizer_desc.DepthBiasClamp        = 0;
    rasterizer_desc.SlopeScaledDepthBias  = 0;
    rasterizer_desc.DepthClipEnable       = false; // ?
    rasterizer_desc.ScissorEnable         = false;
    rasterizer_desc.MultisampleEnable     = false;
    rasterizer_desc.AntialiasedLineEnable = false;
    device_->CreateRasterizerState(&rasterizer_desc, &rasterizer_state);
    rasterizer_state_cache_.insert({ key, rasterizer_state });
  } else {
    rasterizer_state = it->second;
  }

  context_->RSSetState(rasterizer_state);
  return 0;
}

int D3D11GraphicsDriver::SetupBlendState(const DrawCommand& command) {
  static const D3D11_BLEND blend_map[] = {
    /*  0 */ D3D11_BLEND_ZERO,
    /*  1 */ D3D11_BLEND_ONE,
    /*  2 */ D3D11_BLEND_ZERO,              // ?
    /*  3 */ D3D11_BLEND_ZERO,              // ?
    /*  4 */ D3D11_BLEND_SRC_COLOR,
    /*  5 */ D3D11_BLEND_INV_SRC_COLOR,
    /*  6 */ D3D11_BLEND_SRC_ALPHA,
    /*  7 */ D3D11_BLEND_INV_SRC_ALPHA,
    /*  8 */ D3D11_BLEND_DEST_COLOR,
    /*  9 */ D3D11_BLEND_INV_DEST_COLOR,
    /* 10 */ D3D11_BLEND_DEST_ALPHA,
    /* 11 */ D3D11_BLEND_INV_DEST_ALPHA,
    /* 12 */ D3D11_BLEND_BLEND_FACTOR,
    /* 13 */ D3D11_BLEND_INV_BLEND_FACTOR,
    /* 14 */ D3D11_BLEND_SRC1_ALPHA,        // ?
    /* 15 */ D3D11_BLEND_INV_SRC1_ALPHA,    // ?
    /* 16 */ D3D11_BLEND_SRC_ALPHA_SAT,
  };
  static const D3D11_BLEND_OP blend_op_map[] = {
    /*  0 */ D3D11_BLEND_OP_ADD,
    /*  1 */ D3D11_BLEND_OP_SUBTRACT,
    /*  2 */ D3D11_BLEND_OP_MIN,
    /*  3 */ D3D11_BLEND_OP_MAX,
    /*  4 */ D3D11_BLEND_OP_REV_SUBTRACT,
  };

  // alpha testing -- ALPHAREF, ALPHAFUNC, ALPHATESTENABLE
  // Not in D3D11!
  // http://msdn.microsoft.com/en-us/library/windows/desktop/bb205120(v=vs.85).aspx
  uint32_t color_control = register_file_[XE_GPU_REG_RB_COLORCONTROL].u32;

  uint32_t color_mask = register_file_[XE_GPU_REG_RB_COLOR_MASK].u32;
  uint32_t blend_control[4] = {
    register_file_[XE_GPU_REG_RB_BLENDCONTROL_0].u32,
    register_file_[XE_GPU_REG_RB_BLENDCONTROL_1].u32,
    register_file_[XE_GPU_REG_RB_BLENDCONTROL_2].u32,
    register_file_[XE_GPU_REG_RB_BLENDCONTROL_3].u32,
  };

  // Check cache.
  uint64_t key = hash_combine(color_mask,
                              blend_control[0], blend_control[1],
                              blend_control[2], blend_control[3]);
  ID3D11BlendState* blend_state = nullptr;
  auto it = blend_state_cache_.find(key);
  if (it == blend_state_cache_.end()) {
    D3D11_BLEND_DESC blend_desc;
    xe_zero_struct(&blend_desc, sizeof(blend_desc));
    //blend_desc.AlphaToCoverageEnable = false;
    // ?
    blend_desc.IndependentBlendEnable = true;
    for (int n = 0; n < XECOUNT(blend_control); n++) {
      // A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND
      blend_desc.RenderTarget[n].SrcBlend           = blend_map[(blend_control[n] & 0x0000001F) >> 0];
      // A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND
      blend_desc.RenderTarget[n].DestBlend          = blend_map[(blend_control[n] & 0x00001F00) >> 8];
      // A2XX_RB_BLEND_CONTROL_COLOR_COMB_FCN
      blend_desc.RenderTarget[n].BlendOp            = blend_op_map[(blend_control[n] & 0x000000E0) >> 5];
      // A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND
      blend_desc.RenderTarget[n].SrcBlendAlpha      = blend_map[(blend_control[n] & 0x001F0000) >> 16];
      // A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND
      blend_desc.RenderTarget[n].DestBlendAlpha     = blend_map[(blend_control[n] & 0x1F000000) >> 24];
      // A2XX_RB_BLEND_CONTROL_ALPHA_COMB_FCN
      blend_desc.RenderTarget[n].BlendOpAlpha       = blend_op_map[(blend_control[n] & 0x00E00000) >> 21];
      // A2XX_RB_COLOR_MASK_WRITE_*
      blend_desc.RenderTarget[n].RenderTargetWriteMask = (color_mask >> (n * 4)) & 0xF;
      // A2XX_RB_COLORCONTROL_BLEND_DISABLE ?? Can't find this!
      // Just guess based on actions.
      blend_desc.RenderTarget[n].BlendEnable = !(
           (blend_desc.RenderTarget[n].SrcBlend == D3D11_BLEND_ONE) &&
           (blend_desc.RenderTarget[n].DestBlend == D3D11_BLEND_ZERO) &&
           (blend_desc.RenderTarget[n].BlendOp == D3D11_BLEND_OP_ADD) &&
           (blend_desc.RenderTarget[n].SrcBlendAlpha == D3D11_BLEND_ONE) &&
           (blend_desc.RenderTarget[n].DestBlendAlpha == D3D11_BLEND_ZERO) &&
           (blend_desc.RenderTarget[n].BlendOpAlpha == D3D11_BLEND_OP_ADD));
    }
    device_->CreateBlendState(&blend_desc, &blend_state);
    blend_state_cache_.insert({ key, blend_state });
  } else {
    blend_state = it->second;
  }

  float blend_factor[4] = {
    register_file_[XE_GPU_REG_RB_BLEND_RED].f32,
    register_file_[XE_GPU_REG_RB_BLEND_GREEN].f32,
    register_file_[XE_GPU_REG_RB_BLEND_BLUE].f32,
    register_file_[XE_GPU_REG_RB_BLEND_ALPHA].f32,
  };
  uint32_t sample_mask = 0xFFFFFFFF; // ?
  context_->OMSetBlendState(blend_state, blend_factor, sample_mask);
  return 0;
}

int D3D11GraphicsDriver::SetupDepthStencilState(const DrawCommand& command) {
  static const D3D11_COMPARISON_FUNC compare_func_map[] = {
    /*  0 */ D3D11_COMPARISON_NEVER,
    /*  1 */ D3D11_COMPARISON_LESS,
    /*  2 */ D3D11_COMPARISON_EQUAL,
    /*  3 */ D3D11_COMPARISON_LESS_EQUAL,
    /*  4 */ D3D11_COMPARISON_GREATER,
    /*  5 */ D3D11_COMPARISON_NOT_EQUAL,
    /*  6 */ D3D11_COMPARISON_GREATER_EQUAL,
    /*  7 */ D3D11_COMPARISON_ALWAYS,
  };
  static const D3D11_STENCIL_OP stencil_op_map[] = {
    /*  0 */ D3D11_STENCIL_OP_KEEP,
    /*  1 */ D3D11_STENCIL_OP_ZERO,
    /*  2 */ D3D11_STENCIL_OP_REPLACE,
    /*  3 */ D3D11_STENCIL_OP_INCR_SAT,
    /*  4 */ D3D11_STENCIL_OP_DECR_SAT,
    /*  5 */ D3D11_STENCIL_OP_INVERT,
    /*  6 */ D3D11_STENCIL_OP_INCR,
    /*  7 */ D3D11_STENCIL_OP_DECR,
  };

  uint32_t depth_control = register_file_[XE_GPU_REG_RB_DEPTHCONTROL].u32;
  uint32_t stencil_ref_mask = register_file_[XE_GPU_REG_RB_STENCILREFMASK].u32;

  // Check cache.
  uint64_t key = (uint64_t(depth_control) << 32) | stencil_ref_mask;
  ID3D11DepthStencilState* depth_stencil_state = nullptr;
  auto it = depth_stencil_state_cache_.find(key);
  if (it == depth_stencil_state_cache_.end()) {
    D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
    xe_zero_struct(&depth_stencil_desc, sizeof(depth_stencil_desc));
    // A2XX_RB_DEPTHCONTROL_BACKFACE_ENABLE
    // ?
    // A2XX_RB_DEPTHCONTROL_Z_ENABLE
    depth_stencil_desc.DepthEnable                  = (depth_control & 0x00000002) != 0;
    // A2XX_RB_DEPTHCONTROL_Z_WRITE_ENABLE
    depth_stencil_desc.DepthWriteMask               = (depth_control & 0x00000004) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    // A2XX_RB_DEPTHCONTROL_EARLY_Z_ENABLE
    // ?
    // A2XX_RB_DEPTHCONTROL_ZFUNC
    depth_stencil_desc.DepthFunc                    = compare_func_map[(depth_control & 0x00000070) >> 4];
    // A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE
    depth_stencil_desc.StencilEnable                = (depth_control & 0x00000001) != 0;
    // RB_STENCILREFMASK_STENCILMASK
    depth_stencil_desc.StencilReadMask              = (stencil_ref_mask & 0x0000FF00) >> 8;
    // RB_STENCILREFMASK_STENCILWRITEMASK
    depth_stencil_desc.StencilWriteMask             = (stencil_ref_mask & 0x00FF0000) >> 16;
    // A2XX_RB_DEPTHCONTROL_STENCILFUNC
    depth_stencil_desc.FrontFace.StencilFunc        = compare_func_map[(depth_control & 0x00000700) >> 8];
    // A2XX_RB_DEPTHCONTROL_STENCILFAIL
    depth_stencil_desc.FrontFace.StencilFailOp      = stencil_op_map[(depth_control & 0x00003800) >> 11];
    // A2XX_RB_DEPTHCONTROL_STENCILZPASS
    depth_stencil_desc.FrontFace.StencilPassOp      = stencil_op_map[(depth_control & 0x0001C000) >> 14];
    // A2XX_RB_DEPTHCONTROL_STENCILZFAIL
    depth_stencil_desc.FrontFace.StencilDepthFailOp = stencil_op_map[(depth_control & 0x000E0000) >> 17];
    // A2XX_RB_DEPTHCONTROL_STENCILFUNC_BF
    depth_stencil_desc.BackFace.StencilFunc         = compare_func_map[(depth_control & 0x00700000) >> 20];
    // A2XX_RB_DEPTHCONTROL_STENCILFAIL_BF
    depth_stencil_desc.BackFace.StencilFailOp       = stencil_op_map[(depth_control & 0x03800000) >> 23];
    // A2XX_RB_DEPTHCONTROL_STENCILZPASS_BF
    depth_stencil_desc.BackFace.StencilPassOp       = stencil_op_map[(depth_control & 0x1C000000) >> 26];
    // A2XX_RB_DEPTHCONTROL_STENCILZFAIL_BF
    depth_stencil_desc.BackFace.StencilDepthFailOp  = stencil_op_map[(depth_control & 0xE0000000) >> 29];
    device_->CreateDepthStencilState(&depth_stencil_desc, &depth_stencil_state);
    depth_stencil_state_cache_.insert({ key, depth_stencil_state });
  } else {
    depth_stencil_state = it->second;
  }

  // RB_STENCILREFMASK_STENCILREF
  uint32_t stencil_ref = (stencil_ref_mask & 0x000000FF);
  context_->OMSetDepthStencilState(depth_stencil_state, stencil_ref);
  return 0;
}

int D3D11GraphicsDriver::SetupConstantBuffers(const DrawCommand& command) {
  SCOPE_profile_cpu_f("gpu");

  D3D11_MAPPED_SUBRESOURCE res;
  context_->Map(
      state_.constant_buffers.float_constants, 0,
      D3D11_MAP_WRITE_DISCARD, 0, &res);
  memcpy(res.pData,
         command.float4_constants.values,
         command.float4_constants.count * 4 * sizeof(float));
  context_->Unmap(state_.constant_buffers.float_constants, 0);

  context_->Map(
      state_.constant_buffers.loop_constants, 0,
      D3D11_MAP_WRITE_DISCARD, 0, &res);
  memcpy(res.pData,
         command.loop_constants.values,
         command.loop_constants.count * sizeof(int));
  context_->Unmap(state_.constant_buffers.loop_constants, 0);

  context_->Map(
      state_.constant_buffers.bool_constants, 0,
      D3D11_MAP_WRITE_DISCARD, 0, &res);
  memcpy(res.pData,
         command.bool_constants.values,
         command.bool_constants.count * sizeof(int));
  context_->Unmap(state_.constant_buffers.bool_constants, 0);

  return 0;
}

int D3D11GraphicsDriver::SetupShaders(const DrawCommand& command) {
  SCOPE_profile_cpu_f("gpu");

  if (command.vertex_shader) {
    context_->VSSetShader(
        command.vertex_shader->handle_as<ID3D11VertexShader>(), nullptr, 0);

    // Set constant buffers.
    ID3D11Buffer* vs_constant_buffers[] = {
      state_.constant_buffers.float_constants,
      state_.constant_buffers.bool_constants,
      state_.constant_buffers.loop_constants,
      state_.constant_buffers.vs_consts,
    };
    context_->VSSetConstantBuffers(0, XECOUNT(vs_constant_buffers),
                                   vs_constant_buffers);

    // Setup input layout (as encoded in vertex shader).
    auto vs = static_cast<D3D11VertexShaderResource*>(command.vertex_shader);
    context_->IASetInputLayout(vs->input_layout());
  } else {
    context_->VSSetShader(nullptr, nullptr, 0);
    context_->IASetInputLayout(nullptr);
    return 1;
  }

  // Pixel shader setup.
  if (command.pixel_shader) {
    context_->PSSetShader(
        command.pixel_shader->handle_as<ID3D11PixelShader>(), nullptr, 0);

    // Set constant buffers.
    ID3D11Buffer* vs_constant_buffers[] = {
      state_.constant_buffers.float_constants,
      state_.constant_buffers.bool_constants,
      state_.constant_buffers.loop_constants,
    };
    context_->PSSetConstantBuffers(0, XECOUNT(vs_constant_buffers),
                                   vs_constant_buffers);
  } else {
    context_->PSSetShader(nullptr, nullptr, 0);
    return 1;
  }

  return 0;
}

int D3D11GraphicsDriver::SetupInputAssembly(const DrawCommand& command) {
  SCOPE_profile_cpu_f("gpu");

  auto vs = static_cast<D3D11VertexShaderResource*>(command.vertex_shader);
  if (!vs) {
    return 1;
  }

  // Switch primitive topology.
  // Some are unsupported on D3D11 and must be emulated.
  D3D11_PRIMITIVE_TOPOLOGY primitive_topology;
  D3D11GeometryShader* geometry_shader = NULL;
  switch (command.prim_type) {
  case XE_GPU_PRIMITIVE_TYPE_POINT_LIST:
    primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    if (vs->DemandGeometryShader(
        D3D11VertexShaderResource::POINT_SPRITE_SHADER, &geometry_shader)) {
      return 1;
    }
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
    primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    if (vs->DemandGeometryShader(
        D3D11VertexShaderResource::RECT_LIST_SHADER, &geometry_shader)) {
      return 1;
    }
    break;
  case XE_GPU_PRIMITIVE_TYPE_QUAD_LIST:
    primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
    if (vs->DemandGeometryShader(
        D3D11VertexShaderResource::QUAD_LIST_SHADER, &geometry_shader)) {
      return 1;
    }
    break;
  default:
  case XE_GPU_PRIMITIVE_TYPE_TRIANGLE_FAN:
  case XE_GPU_PRIMITIVE_TYPE_UNKNOWN_07:
  case XE_GPU_PRIMITIVE_TYPE_LINE_LOOP:
    primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    XELOGE("D3D11: unsupported primitive type %d", command.prim_type);
    break;
  }
  context_->IASetPrimitiveTopology(primitive_topology);

  // Set the geometry shader, if we are emulating a primitive type.
  if (geometry_shader) {
    context_->GSSetShader(geometry_shader->handle(), NULL, NULL);
    context_->GSSetConstantBuffers(0, 1, &state_.constant_buffers.gs_consts);
  } else {
    context_->GSSetShader(NULL, NULL, NULL);
  }

  // Index buffer, if any. May be auto draw.
  if (command.index_buffer) {
    DXGI_FORMAT format;
    switch (command.index_buffer->info().format) {
    case INDEX_FORMAT_16BIT:
      format = DXGI_FORMAT_R16_UINT;
      break;
    case INDEX_FORMAT_32BIT:
      format = DXGI_FORMAT_R32_UINT;
      break;
    }
    context_->IASetIndexBuffer(
        command.index_buffer->handle_as<ID3D11Buffer>(),
        format, 0);
  } else {
    context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
  }

  // All vertex buffers.
  for (auto i = 0; i < command.vertex_buffer_count; ++i) {
    const auto& vb = command.vertex_buffers[i];
    auto buffer = vb.buffer->handle_as<ID3D11Buffer>();
    auto stride = vb.stride;
    auto offset = vb.offset;
    context_->IASetVertexBuffers(vb.input_index, 1, &buffer,
                                 &stride, &offset);
  }

  return 0;
}

int D3D11GraphicsDriver::SetupSamplers(const DrawCommand& command) {
  SCOPE_profile_cpu_f("gpu");

  for (auto i = 0; i < command.vertex_shader_sampler_count; ++i) {
    const auto& input = command.vertex_shader_samplers[i];
    if (input.texture) {
      auto texture = input.texture->handle_as<ID3D11ShaderResourceView>();
      context_->VSSetShaderResources(input.input_index, 1, &texture);
    } else {
      context_->VSSetShaderResources(input.input_index, 1, &invalid_texture_view_);
    }
    if (input.sampler_state) {
      auto sampler_state = input.sampler_state->handle_as<ID3D11SamplerState>();
      context_->VSSetSamplers(input.input_index, 1, &sampler_state);
    } else {
      context_->VSSetSamplers(input.input_index, 1, &invalid_texture_sampler_state_);
    }
  }

  for (auto i = 0; i < command.pixel_shader_sampler_count; ++i) {
    const auto& input = command.pixel_shader_samplers[i];
    if (input.texture) {
      auto texture = input.texture->handle_as<ID3D11ShaderResourceView>();
      context_->PSSetShaderResources(input.input_index, 1, &texture);
    } else {
      context_->PSSetShaderResources(input.input_index, 1, &invalid_texture_view_);
    }
    if (input.sampler_state) {
      auto sampler_state = input.sampler_state->handle_as<ID3D11SamplerState>();
      context_->PSSetSamplers(input.input_index, 1, &sampler_state);
    } else {
      context_->PSSetSamplers(input.input_index, 1, &invalid_texture_sampler_state_);
    }
  }

  return 0;
}

int D3D11GraphicsDriver::RebuildRenderTargets(uint32_t width,
                                              uint32_t height) {
  if (width == render_targets_.width &&
      height == render_targets_.height) {
    // Cached copies are good.
    return 0;
  }

  SCOPE_profile_cpu_f("gpu");

  // Remove old versions.
  for (int n = 0; n < XECOUNT(render_targets_.color_buffers); n++) {
    auto& cb = render_targets_.color_buffers[n];
    XESAFERELEASE(cb.buffer);
    XESAFERELEASE(cb.color_view_8888);
  }
  XESAFERELEASE(render_targets_.depth_buffer);
  XESAFERELEASE(render_targets_.depth_view_d28s8);
  XESAFERELEASE(render_targets_.depth_view_d28fs8);

  render_targets_.width   = width;
  render_targets_.height  = height;

  if (!width || !height) {
    // This should only happen when cleaning up.
    return 0;
  }

  for (int n = 0; n < XECOUNT(render_targets_.color_buffers); n++) {
    auto& cb = render_targets_.color_buffers[n];
    D3D11_TEXTURE2D_DESC color_buffer_desc;
    xe_zero_struct(&color_buffer_desc, sizeof(color_buffer_desc));
    color_buffer_desc.Width           = width;
    color_buffer_desc.Height          = height;
    color_buffer_desc.MipLevels       = 1;
    color_buffer_desc.ArraySize       = 1;
    color_buffer_desc.Format          = DXGI_FORMAT_R8G8B8A8_UNORM;
    color_buffer_desc.SampleDesc.Count    = 1;
    color_buffer_desc.SampleDesc.Quality  = 0;
    color_buffer_desc.Usage           = D3D11_USAGE_DEFAULT;
    color_buffer_desc.BindFlags       =
        D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_RENDER_TARGET;
    color_buffer_desc.CPUAccessFlags  = 0;
    color_buffer_desc.MiscFlags       = 0;
    device_->CreateTexture2D(
        &color_buffer_desc, NULL, &cb.buffer);

    D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc;
    xe_zero_struct(&render_target_view_desc, sizeof(render_target_view_desc));
    render_target_view_desc.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
    render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    // render_target_view_desc.Buffer ?
    device_->CreateRenderTargetView(
        cb.buffer,
        &render_target_view_desc,
        &cb.color_view_8888);
  }

  D3D11_TEXTURE2D_DESC depth_stencil_desc;
  xe_zero_struct(&depth_stencil_desc, sizeof(depth_stencil_desc));
  depth_stencil_desc.Width          = width;
  depth_stencil_desc.Height         = height;
  depth_stencil_desc.MipLevels      = 1;
  depth_stencil_desc.ArraySize      = 1;
  depth_stencil_desc.Format         = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_stencil_desc.SampleDesc.Count   = 1;
  depth_stencil_desc.SampleDesc.Quality = 0;
  depth_stencil_desc.Usage          = D3D11_USAGE_DEFAULT;
  depth_stencil_desc.BindFlags      = D3D11_BIND_DEPTH_STENCIL;
  depth_stencil_desc.CPUAccessFlags = 0;
  depth_stencil_desc.MiscFlags      = 0;
  device_->CreateTexture2D(
      &depth_stencil_desc, NULL, &render_targets_.depth_buffer);

  D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_view_desc;
  xe_zero_struct(&depth_stencil_view_desc, sizeof(depth_stencil_view_desc));
  depth_stencil_view_desc.Format        = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_stencil_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  depth_stencil_view_desc.Flags         = 0;
  device_->CreateDepthStencilView(
      render_targets_.depth_buffer,
      &depth_stencil_view_desc,
      &render_targets_.depth_view_d28s8);

  return 0;
}

int D3D11GraphicsDriver::Resolve() {
  SCOPE_profile_cpu_f("gpu");

  // No clue how this is supposed to work yet.
  ID3D11Texture2D* back_buffer = 0;
  swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                         (LPVOID*)&back_buffer);
  D3D11_TEXTURE2D_DESC desc;
  back_buffer->GetDesc(&desc);
  if (desc.Width == render_targets_.width &&
      desc.Height == render_targets_.height) {
    // Same size/format, so copy quickly.
    context_->CopyResource(back_buffer, render_targets_.color_buffers[0].buffer);
  } else {
    // TODO(benvanik): scale size using a quad draw.
  }
  XESAFERELEASE(back_buffer);

  // TODO(benvanik): remove!
  float color[4] = { 0.5f, 0.5f, 0.0f, 1.0f };
  context_->ClearRenderTargetView(
      render_targets_.color_buffers[0].color_view_8888, color);
  // TODO(benvanik): take clear values from register
  context_->ClearDepthStencilView(
      render_targets_.depth_view_d28s8, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);

  return 0;
}

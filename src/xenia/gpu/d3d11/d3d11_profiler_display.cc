/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_profiler_display.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_window.h>

#include <d3dcompiler.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;


namespace {
const uint8_t profiler_font[] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x10,0x78,0x38,0x78,0x7c,0x7c,0x3c,0x44,0x38,0x04,0x44,0x40,0x44,0x44,0x38,0x78,
  0x38,0x78,0x38,0x7c,0x44,0x44,0x44,0x44,0x44,0x7c,0x00,0x00,0x40,0x00,0x04,0x00,
  0x18,0x00,0x40,0x10,0x08,0x40,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x38,0x10,0x38,0x7c,0x08,0x7c,0x1c,0x7c,0x38,0x38,
  0x10,0x28,0x28,0x10,0x00,0x20,0x10,0x08,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x04,0x00,0x20,0x38,0x38,0x70,0x00,0x1c,0x10,0x00,0x1c,0x10,0x70,0x30,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x28,0x44,0x44,0x44,0x40,0x40,0x40,0x44,0x10,0x04,0x48,0x40,0x6c,0x44,0x44,0x44,
  0x44,0x44,0x44,0x10,0x44,0x44,0x44,0x44,0x44,0x04,0x00,0x00,0x40,0x00,0x04,0x00,
  0x24,0x00,0x40,0x00,0x00,0x40,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x44,0x30,0x44,0x04,0x18,0x40,0x20,0x04,0x44,0x44,
  0x10,0x28,0x28,0x3c,0x44,0x50,0x10,0x10,0x08,0x54,0x10,0x00,0x00,0x00,0x04,0x00,
  0x00,0x08,0x00,0x10,0x44,0x44,0x40,0x40,0x04,0x28,0x00,0x30,0x10,0x18,0x58,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x44,0x44,0x40,0x44,0x40,0x40,0x40,0x44,0x10,0x04,0x50,0x40,0x54,0x64,0x44,0x44,
  0x44,0x44,0x40,0x10,0x44,0x44,0x44,0x28,0x28,0x08,0x00,0x38,0x78,0x3c,0x3c,0x38,
  0x20,0x38,0x78,0x30,0x18,0x44,0x10,0x6c,0x78,0x38,0x78,0x3c,0x5c,0x3c,0x3c,0x44,
  0x44,0x44,0x44,0x44,0x7c,0x00,0x4c,0x10,0x04,0x08,0x28,0x78,0x40,0x08,0x44,0x44,
  0x10,0x00,0x7c,0x50,0x08,0x50,0x00,0x20,0x04,0x38,0x10,0x00,0x00,0x00,0x08,0x10,
  0x10,0x10,0x7c,0x08,0x08,0x54,0x40,0x20,0x04,0x44,0x00,0x30,0x10,0x18,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x44,0x78,0x40,0x44,0x78,0x78,0x40,0x7c,0x10,0x04,0x60,0x40,0x54,0x54,0x44,0x78,
  0x44,0x78,0x38,0x10,0x44,0x44,0x54,0x10,0x10,0x10,0x00,0x04,0x44,0x40,0x44,0x44,
  0x78,0x44,0x44,0x10,0x08,0x48,0x10,0x54,0x44,0x44,0x44,0x44,0x60,0x40,0x10,0x44,
  0x44,0x44,0x28,0x44,0x08,0x00,0x54,0x10,0x18,0x18,0x48,0x04,0x78,0x10,0x38,0x3c,
  0x10,0x00,0x28,0x38,0x10,0x20,0x00,0x20,0x04,0x10,0x7c,0x00,0x7c,0x00,0x10,0x00,
  0x00,0x20,0x00,0x04,0x10,0x5c,0x40,0x10,0x04,0x00,0x00,0x60,0x10,0x0c,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x7c,0x44,0x40,0x44,0x40,0x40,0x4c,0x44,0x10,0x04,0x50,0x40,0x44,0x4c,0x44,0x40,
  0x54,0x50,0x04,0x10,0x44,0x44,0x54,0x28,0x10,0x20,0x00,0x3c,0x44,0x40,0x44,0x7c,
  0x20,0x44,0x44,0x10,0x08,0x70,0x10,0x54,0x44,0x44,0x44,0x44,0x40,0x38,0x10,0x44,
  0x44,0x54,0x10,0x44,0x10,0x00,0x64,0x10,0x20,0x04,0x7c,0x04,0x44,0x20,0x44,0x04,
  0x10,0x00,0x7c,0x14,0x20,0x54,0x00,0x20,0x04,0x38,0x10,0x10,0x00,0x00,0x20,0x10,
  0x10,0x10,0x7c,0x08,0x10,0x58,0x40,0x08,0x04,0x00,0x00,0x30,0x10,0x18,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x44,0x44,0x44,0x44,0x40,0x40,0x44,0x44,0x10,0x44,0x48,0x40,0x44,0x44,0x44,0x40,
  0x48,0x48,0x44,0x10,0x44,0x28,0x6c,0x44,0x10,0x40,0x00,0x44,0x44,0x40,0x44,0x40,
  0x20,0x3c,0x44,0x10,0x08,0x48,0x10,0x54,0x44,0x44,0x44,0x44,0x40,0x04,0x12,0x4c,
  0x28,0x54,0x28,0x3c,0x20,0x00,0x44,0x10,0x40,0x44,0x08,0x44,0x44,0x20,0x44,0x08,
  0x00,0x00,0x28,0x78,0x44,0x48,0x00,0x10,0x08,0x54,0x10,0x10,0x00,0x00,0x40,0x00,
  0x10,0x08,0x00,0x10,0x00,0x40,0x40,0x04,0x04,0x00,0x00,0x30,0x10,0x18,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x44,0x78,0x38,0x78,0x7c,0x40,0x3c,0x44,0x38,0x38,0x44,0x7c,0x44,0x44,0x38,0x40,
  0x34,0x44,0x38,0x10,0x38,0x10,0x44,0x44,0x10,0x7c,0x00,0x3c,0x78,0x3c,0x3c,0x3c,
  0x20,0x04,0x44,0x38,0x48,0x44,0x38,0x44,0x44,0x38,0x78,0x3c,0x40,0x78,0x0c,0x34,
  0x10,0x6c,0x44,0x04,0x7c,0x00,0x38,0x38,0x7c,0x38,0x08,0x38,0x38,0x20,0x38,0x70,
  0x10,0x00,0x28,0x10,0x00,0x34,0x00,0x08,0x10,0x10,0x00,0x20,0x00,0x10,0x00,0x00,
  0x20,0x04,0x00,0x20,0x10,0x3c,0x70,0x00,0x1c,0x00,0x7c,0x1c,0x10,0x70,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x38,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x40,0x04,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

const char* shader_code = " \
cbuffer MatrixBuffer {\n \
  float4x4 projection_matrix;\n \
};\n \
Texture2D texture0;\n \
SamplerState sampler0;\n \
struct Vertex {\n \
  float2 position : POSITION0;\n \
  float2 tex : TEXCOORD0;\n \
  float4 color : COLOR0;\n \
};\n \
struct Pixel {\n \
  float4 position : SV_POSITION;\n \
  float2 tex : TEXCOORD0;\n \
  float4 color : COLOR0;\n \
};\n \
Pixel vs(Vertex v) {\n \
  Pixel p;\n \
  p.position = float4(mul(float4(v.position, 0.0f, 1.0f), projection_matrix).xy - float2(1.0f, -1.0f), 0.0f, 1.0f);\n \
  p.tex = v.tex;\n \
  p.color = v.color;\n \
  return p;\n \
}\n \
float4 ps(Pixel p) : SV_TARGET {\n \
  if (p.tex.x > 1.0f) {\n \
      return float4(p.color.rgb, 0.5f);\n \
  } else {\n \
    float4 sample = texture0.Sample(sampler0, p.tex);\n \
    if(sample.w < 0.5f) {\n \
      discard;\n \
    }\n \
    return p.color * sample;\n \
  }\n \
}\n";

}  // namespace


D3D11ProfilerDisplay::D3D11ProfilerDisplay(D3D11Window* window) : window_(window) {
  draw_state_ = { 0 };
  if (!SetupState() ||
      !SetupShaders() ||
      !SetupFont()) {
    // Hrm.
    assert_always();
  }

  // Pass through mouse events.
  window->mouse_down.AddListener([](xe::ui::MouseEvent& e) {
    Profiler::OnMouseDown(
        e.button() == xe::ui::MouseEvent::MOUSE_BUTTON_LEFT,
        e.button() == xe::ui::MouseEvent::MOUSE_BUTTON_RIGHT);
  });
  window->mouse_up.AddListener([](xe::ui::MouseEvent& e) {
    Profiler::OnMouseUp();
  });
  window->mouse_move.AddListener([](xe::ui::MouseEvent& e) {
    Profiler::OnMouseMove(e.x(), e.y());
  });
  window->mouse_wheel.AddListener([](xe::ui::MouseEvent& e) {
    Profiler::OnMouseWheel(e.x(), e.y(), -e.dy());
  });

  // Watch for toggle/mode keys and such.
  window->key_down.AddListener([](xe::ui::KeyEvent& e) {
    Profiler::OnKeyDown(e.key_code());
  });
  window->key_up.AddListener([](xe::ui::KeyEvent& e) {
    Profiler::OnKeyUp(e.key_code());
  });
}

bool D3D11ProfilerDisplay::SetupState() {
  HRESULT hr;
  auto device = window_->device();

  D3D11_BLEND_DESC blend_desc;
  xe_zero_struct(&blend_desc, sizeof(blend_desc));
  blend_desc.RenderTarget[0].BlendEnable = true;
  blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
  blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  blend_desc.RenderTarget[0].RenderTargetWriteMask = 0x0F;
  hr = device->CreateBlendState(&blend_desc, &blend_state_);
  assert_true(SUCCEEDED(hr));

  D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
  xe_zero_struct(&depth_stencil_desc, sizeof(depth_stencil_desc));
  depth_stencil_desc.DepthEnable = false;
  depth_stencil_desc.StencilEnable = false;
  depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  hr = device->CreateDepthStencilState(&depth_stencil_desc, &depth_stencil_state_);
  assert_true(SUCCEEDED(hr));

  return true;
}

bool D3D11ProfilerDisplay::SetupShaders() {
  HRESULT hr;
  auto device = window_->device();

  ID3DBlob* vs_code_blob = nullptr;
  ID3DBlob* vs_errors = nullptr;
  hr = D3DCompile(
      shader_code, xestrlena(shader_code),
      "D3D11ProfilerDisplay.vs",
      nullptr,
      nullptr,
      "vs",
      "vs_5_0",
      D3DCOMPILE_ENABLE_STRICTNESS,
      0,
      &vs_code_blob,
      &vs_errors);
  if (FAILED(hr)) {
    XELOGE("Failed to compile profiler vs: %s",
           reinterpret_cast<const char*>(vs_errors->GetBufferPointer()));
    return false;
  }
  hr = device->CreateVertexShader(vs_code_blob->GetBufferPointer(),
                                  vs_code_blob->GetBufferSize(),
                                  nullptr,
                                  &vertex_shader_);
  if (FAILED(hr)) {
    XELOGE("Failed to create profiler vs");
    return false;
  }
  ID3DBlob* ps_code_blob = nullptr;
  ID3DBlob* ps_errors = nullptr;
  hr = D3DCompile(
      shader_code, xestrlena(shader_code),
      "D3D11ProfilerDisplay.ps",
      nullptr,
      nullptr,
      "ps",
      "ps_5_0",
      D3DCOMPILE_ENABLE_STRICTNESS,
      0,
      &ps_code_blob,
      &ps_errors);
  if (FAILED(hr)) {
    XELOGE("Failed to compile profiler ps: %s",
           reinterpret_cast<const char*>(ps_errors->GetBufferPointer()));
    return false;
  }
  hr = device->CreatePixelShader(ps_code_blob->GetBufferPointer(),
                                 ps_code_blob->GetBufferSize(),
                                 nullptr,
                                 &pixel_shader_);
  if (FAILED(hr)) {
    XELOGE("Failed to create profiler ps");
    return false;
  }

  D3D11_BUFFER_DESC buffer_desc = { 0 };
  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  buffer_desc.ByteWidth = sizeof(float) * 16;
  hr = device->CreateBuffer(&buffer_desc, nullptr, &shader_constants_);
  if (FAILED(hr)) {
    XELOGE("Failed to create profiler constant buffer");
    return false;
  }

  D3D11_INPUT_ELEMENT_DESC element_descs[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0, },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0, },
    { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0, },
  };
  hr = device->CreateInputLayout(element_descs, (UINT)XECOUNT(element_descs),
                                 vs_code_blob->GetBufferPointer(),
                                 vs_code_blob->GetBufferSize(),
                                 &shader_layout_);
  if (FAILED(hr)) {
    XELOGE("Failed to create profiler input layout");
    return false;
  }

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  buffer_desc.ByteWidth = sizeof(draw_state_.vertex_buffer);
  hr = device->CreateBuffer(&buffer_desc, nullptr, &vertex_buffer_);
  if (FAILED(hr)) {
    XELOGE("Failed to create profiler vertex buffer");
    return false;
  }

  return true;
}

bool D3D11ProfilerDisplay::SetupFont() {
  HRESULT hr;
  auto device = window_->device();

  // Setup font lookup table.
  for (uint32_t i = 0; i < XECOUNT(font_description_.char_offsets); ++i) {
    font_description_.char_offsets[i] = 206;
  }
  for (uint32_t i = 'A'; i <= 'Z'; ++i) {
    font_description_.char_offsets[i] = (i-'A')*8+1;
  }
  for (uint32_t i = 'a'; i <= 'z'; ++i) {
    font_description_.char_offsets[i] = (i-'a')*8+217;
  }
  for (uint32_t i = '0'; i <= '9'; ++i) {
    font_description_.char_offsets[i] = (i-'0')*8+433;
  }
  for (uint32_t i = '!'; i <= '/'; ++i) {
    font_description_.char_offsets[i] = (i-'!')*8+513;
  }
  for (uint32_t i = ':'; i <= '@'; ++i) {
    font_description_.char_offsets[i] = (i-':')*8+625+8;
  }
  for (uint32_t i = '['; i <= '_'; ++i) {
    font_description_.char_offsets[i] = (i-'[')*8+681+8;
  }
  for (uint32_t i = '{'; i <= '~'; ++i) {
    font_description_.char_offsets[i] = (i-'{')*8+721+8;
  }

  // Unpack font bitmap into an RGBA texture.
  const int FONT_TEX_X = 1024;
  const int FONT_TEX_Y = 9;
  const int UNPACKED_SIZE = FONT_TEX_X * FONT_TEX_Y * 4;
  uint32_t unpacked[UNPACKED_SIZE];
  int idx = 0;
  int end = FONT_TEX_X * FONT_TEX_Y / 8;
  for (int i = 0; i < end; i++) {
    uint8_t b = profiler_font[i];
    for (int j = 0; j < 8; ++j) {
      unpacked[idx++] = b & 0x80 ? 0xFFFFFFFFu : 0;
      b <<= 1;
    }
  }

  D3D11_TEXTURE2D_DESC texture_desc = { 0 };
  texture_desc.Width = FONT_TEX_X;
  texture_desc.Height = FONT_TEX_Y;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
  texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  texture_desc.CPUAccessFlags = 0;
  texture_desc.MiscFlags = 0;
  D3D11_SUBRESOURCE_DATA initial_data = { 0 };
  initial_data.pSysMem = unpacked;
  initial_data.SysMemPitch = FONT_TEX_X * 4;
  initial_data.SysMemSlicePitch = 0;
  ID3D11Texture2D* font_texture = nullptr;
  hr = device->CreateTexture2D(&texture_desc, &initial_data, &font_texture);
  if (FAILED(hr)) {
    XELOGE("Unable to create profiler font texture");
    return false;
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC texture_view_desc;
  xe_zero_struct(&texture_view_desc, sizeof(texture_view_desc));
  texture_view_desc.Format = texture_desc.Format;
  texture_view_desc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
  texture_view_desc.Texture2D.MipLevels = 1;
  texture_view_desc.Texture2D.MostDetailedMip = 0;
  hr = device->CreateShaderResourceView(
        font_texture, &texture_view_desc, &font_texture_view_);
  XESAFERELEASE(font_texture);
  if (FAILED(hr)) {
    XELOGE("Unable to create profiler font texture view");
    return false;
  }

  D3D11_SAMPLER_DESC sampler_desc;
  xe_zero_struct(&sampler_desc, sizeof(sampler_desc));
  sampler_desc.Filter         = D3D11_ENCODE_BASIC_FILTER(
      D3D11_FILTER_TYPE_POINT, D3D11_FILTER_TYPE_POINT,
      D3D11_FILTER_TYPE_POINT, false);
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
  hr = device->CreateSamplerState(
      &sampler_desc, &font_sampler_state_);
  if (FAILED(hr)) {
    XEFATAL("D3D11: unable to create invalid sampler state");
    return false;
  }

  return true;
}

D3D11ProfilerDisplay::~D3D11ProfilerDisplay() {
  XESAFERELEASE(blend_state_);
  XESAFERELEASE(depth_stencil_state_);
  XESAFERELEASE(vertex_shader_);
  XESAFERELEASE(pixel_shader_);
  XESAFERELEASE(shader_constants_);
  XESAFERELEASE(shader_layout_);
  XESAFERELEASE(font_texture_view_);
  XESAFERELEASE(font_sampler_state_);
  XESAFERELEASE(vertex_buffer_);
}

uint32_t D3D11ProfilerDisplay::width() const {
  return window_->width();
}

uint32_t D3D11ProfilerDisplay::height() const {
  return window_->height();
}

void D3D11ProfilerDisplay::Begin() {
  auto context = window_->context();

  D3D11_VIEWPORT viewport;
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(width());
  viewport.Height = static_cast<float>(height());
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context->RSSetViewports(1, &viewport);

  // Setup projection matrix.
  float left = viewport.TopLeftX;
  float right = viewport.TopLeftX + viewport.Width;
  float bottom = viewport.TopLeftY + viewport.Height;
  float top = viewport.TopLeftY;
  float z_near = viewport.MinDepth;
  float z_far = viewport.MaxDepth;
  float projection[16] = { 0 };
  projection[0] = 2.0f / (right - left);
  projection[5] = 2.0f / (top - bottom);
  projection[10] = -2.0f / (z_far - z_near);
  projection[12] = -(right + left) / (right - left);
  projection[13] = -(top + bottom) / (top - bottom);
  projection[14] = -(z_far + z_near) / (z_far - z_near);
  projection[15] = 1.0f;
  D3D11_MAPPED_SUBRESOURCE res;
  context->Map(shader_constants_, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  memcpy(res.pData, projection, sizeof(projection));
  context->Unmap(shader_constants_, 0);

  // Setup state.
  context->OMSetBlendState(blend_state_, { 0 }, 0xFFFFFFFF);
  context->OMSetDepthStencilState(depth_stencil_state_, 0);

  // Bind shaders.
  context->GSSetShader(nullptr, nullptr, 0);
  context->VSSetShader(vertex_shader_, nullptr, 0);
  context->VSSetConstantBuffers(0, 1, &shader_constants_);
  context->PSSetShader(pixel_shader_, nullptr, 0);
  context->PSSetConstantBuffers(0, 1, &shader_constants_);
  ID3D11SamplerState* ps_samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {
    font_sampler_state_,
    nullptr,
  };
  context->PSSetSamplers(0, XECOUNT(ps_samplers), ps_samplers);
  ID3D11ShaderResourceView* ps_resources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {
    font_texture_view_,
    nullptr,
  };
  context->PSSetShaderResources(0, XECOUNT(ps_resources), ps_resources);
  context->IASetInputLayout(shader_layout_);
}

void D3D11ProfilerDisplay::End() {
  Flush();
}

D3D11ProfilerDisplay::Vertex* D3D11ProfilerDisplay::AllocateVertices(
    D3D_PRIMITIVE_TOPOLOGY primitive, size_t count) {
  if (draw_state_.vertex_index + count > XECOUNT(draw_state_.vertex_buffer)) {
    Flush();
  }
  assert_true(draw_state_.vertex_index + count <= XECOUNT(draw_state_.vertex_buffer));

  size_t head = draw_state_.vertex_index;
  draw_state_.vertex_index += count;

  if (draw_state_.command_index &&
      draw_state_.commands[draw_state_.command_index - 1].primitive == primitive) {
    draw_state_.commands[draw_state_.command_index - 1].vertex_count += count;
  } else {
    assert_true(draw_state_.command_index < XECOUNT(draw_state_.commands));
    draw_state_.commands[draw_state_.command_index].primitive = primitive;
    draw_state_.commands[draw_state_.command_index].vertex_count = count;
    ++draw_state_.command_index;
  }
  return &draw_state_.vertex_buffer[head];
}

void D3D11ProfilerDisplay::Flush() {
  auto context = window_->context();
  if (!draw_state_.vertex_index) {
    return;
  }

  D3D11_MAPPED_SUBRESOURCE res;
  context->Map(vertex_buffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  memcpy(res.pData, draw_state_.vertex_buffer, sizeof(Vertex) * draw_state_.vertex_index);
  context->Unmap(vertex_buffer_, 0);

  uint32_t stride = 20;
  uint32_t offset = 0;
  context->IASetVertexBuffers(0, 1, &vertex_buffer_, &stride, &offset);

  size_t vertex_index = 0;
  for (int i = 0; i < draw_state_.command_index; ++i) {
    size_t count = draw_state_.commands[i].vertex_count;
    context->IASetPrimitiveTopology(draw_state_.commands[i].primitive);
    context->Draw((UINT)count, (UINT)vertex_index);
    vertex_index += count;
  }

  draw_state_.vertex_index = 0;
  draw_state_.command_index = 0;
}

#define Q0(d, member, v) d[0].member = v
#define Q1(d, member, v) d[1].member = v; d[3].member = v
#define Q2(d, member, v) d[4].member = v
#define Q3(d, member, v) d[2].member = v; d[5].member = v

void D3D11ProfilerDisplay::DrawBox(
    int x, int y, int x1, int y1, uint32_t color, BoxType type) {
  Vertex* v = AllocateVertices(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 6);
  uint32_t color0;
  uint32_t color1;
  if (type == BOX_TYPE_FLAT) {
    color0 = 0xFF000000 |
             ((color & 0xFF) << 16) |
             (color & 0xFF00FF00) |
             ((color >> 16) & 0xFF);
    color1 = color0;
  } else {
    uint32_t r = 0xFF & (color >> 16);
    uint32_t g = 0xFF & (color >> 8);
    uint32_t b = 0xFF & color;
    uint32_t max_c = MAX(MAX(MAX(r, g), b), 30u);
    uint32_t min_c = MIN(MIN(MIN(r, g), b), 180u);
    uint32_t r0 = 0xFF & ((r + max_c)/2);
    uint32_t g0 = 0xFF & ((g + max_c)/2);
    uint32_t b0 = 0xFF & ((b + max_c)/2);
    uint32_t r1 = 0xFF & ((r + min_c) / 2);
    uint32_t g1 = 0xFF & ((g + min_c) / 2);
    uint32_t b1 = 0xFF & ((b + min_c) / 2);
    color0 = r0 | (g0 << 8) | (b0 << 16) | (0xFF000000 & color);
    color1 = r1 | (g1 << 8) | (b1 << 16) | (0xFF000000 & color);
  }
  Q0(v, x, (float)x);
  Q0(v, y, (float)y);
  Q0(v, color, color0);
  Q0(v, u, 2.0f);
  Q0(v, v, 2.0f);
  Q1(v, x, (float)x1);
  Q1(v, y, (float)y);
  Q1(v, color, color0);
  Q1(v, u, 3.0f);
  Q1(v, v, 2.0f);
  Q2(v, x, (float)x1);
  Q2(v, y, (float)y1);
  Q2(v, color, color1);
  Q2(v, u, 3.0f);
  Q2(v, v, 3.0f);
  Q3(v, x, (float)x);
  Q3(v, y, (float)y1);
  Q3(v, color, color1);
  Q3(v, u, 2.0f);
  Q3(v, v, 3.0f);
}

void D3D11ProfilerDisplay::DrawLine2D(
    uint32_t count, float* vertices, uint32_t color) {
  if (!count || !vertices) {
    return;
  }
  color = 0xFF000000 |
          ((color & 0xFF) << 16) |
          (color & 0xFF00FF00) |
          ((color >> 16) & 0xFF);
  Vertex* v = AllocateVertices(D3D11_PRIMITIVE_TOPOLOGY_LINELIST, 2 * (count - 1));
  for (uint32_t i = 0; i < count - 1; ++i) {
    v[0].x = vertices[i * 2];
    v[0].y = vertices[i * 2 + 1];
    v[0].color = color;
    v[0].u = 2.0f;
    v[0].v = 2.0f;
    v[1].x = vertices[(i + 1) * 2];
    v[1].y = vertices[(i + 1) * 2 + 1] ;
    v[1].color = color;
    v[1].u = 2.0f;
    v[1].v = 2.0f;
    v += 2;
  }
}

void D3D11ProfilerDisplay::DrawText(
    int x, int y, uint32_t color, const char* text, size_t text_length) {
  const float offset_u = 5.0f / 1024.0f;
  float fx = (float)x;
  float fy = (float)y;
  float fy2 = fy + (MICROPROFILE_TEXT_HEIGHT + 1);
  color = 0xFF000000 |
          ((color & 0xFF) << 16) |
          (color & 0xFF00FF00) |
          ((color >> 16) & 0xFF);
  Vertex* v = AllocateVertices(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 6 * text_length);
  const char* s = text;
  for (uint32_t j = 0; j < text_length; ++j) {
    int16_t nOffset = font_description_.char_offsets[(int)*s++];
    float fOffset = nOffset / 1024.f;
    Q0(v, x, fx);
    Q0(v, y, fy);
    Q0(v, color, color);
    Q0(v, u, fOffset);
    Q0(v, v, 0.0f);
    Q1(v, x, fx + MICROPROFILE_TEXT_WIDTH);
    Q1(v, y, fy);
    Q1(v, color, color);
    Q1(v, u, fOffset + offset_u);
    Q1(v, v, 0.0f);
    Q2(v, x, fx + MICROPROFILE_TEXT_WIDTH);
    Q2(v, y, fy2);
    Q2(v, color, color);
    Q2(v, u, fOffset + offset_u);
    Q2(v, v, 1.0f);
    Q3(v, x, fx);
    Q3(v, y, fy2);
    Q3(v, color, color);
    Q3(v, u, fOffset);
    Q3(v, v, 1.0f);
    fx += MICROPROFILE_TEXT_WIDTH + 1;
    v += 6;
  }
}

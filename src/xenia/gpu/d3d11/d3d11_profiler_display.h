/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_PROFILER_DISPLAY_H_
#define XENIA_GPU_D3D11_D3D11_PROFILER_DISPLAY_H_

#include <xenia/core.h>
#include <xenia/gpu/d3d11/d3d11_gpu-private.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11Window;


class D3D11ProfilerDisplay : public ProfilerDisplay {
public:
  D3D11ProfilerDisplay(D3D11Window* window);
  virtual ~D3D11ProfilerDisplay();

  uint32_t width() const override;
  uint32_t height() const override;

  // TODO(benvanik): GPU timestamping.

  void Begin() override;
  void End() override;
  void DrawBox(int x, int y, int x1, int y1, uint32_t color, BoxType type) override;
  void DrawLine2D(uint32_t count, float* vertices, uint32_t color) override;
  void DrawText(int x, int y, uint32_t color, const char* text, size_t text_length) override;

private:
  bool SetupState();
  bool SetupShaders();
  bool SetupFont();

  struct Vertex {
    float x, y;
    float u, v;
    uint32_t color;
  };
  struct {
    size_t vertex_index;
    Vertex vertex_buffer[16 << 10];
    struct {
      D3D11_PRIMITIVE_TOPOLOGY primitive;
      size_t vertex_count;
    } commands[32];
    size_t command_index;
  } draw_state_;
  Vertex* AllocateVertices(D3D_PRIMITIVE_TOPOLOGY primitive, size_t count);
  void Flush();

  D3D11Window* window_;
  ID3D11BlendState* blend_state_;
  ID3D11DepthStencilState* depth_stencil_state_;
  ID3D11VertexShader* vertex_shader_;
  ID3D11PixelShader* pixel_shader_;
  ID3D11Buffer* shader_constants_;
  ID3D11InputLayout* shader_layout_;
  ID3D11ShaderResourceView* font_texture_view_;
  ID3D11SamplerState* font_sampler_state_;
  ID3D11Buffer* vertex_buffer_;

  struct {
    uint16_t char_offsets[256];
  } font_description_;
};



}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_PROFILER_DISPLAY_H_

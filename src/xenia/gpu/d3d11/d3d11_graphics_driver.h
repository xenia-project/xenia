/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_GRAPHICS_DRIVER_H_
#define XENIA_GPU_D3D11_D3D11_GRAPHICS_DRIVER_H_

#include <xenia/core.h>

#include <xenia/gpu/graphics_driver.h>
#include <xenia/gpu/shader.h>
#include <xenia/gpu/d3d11/d3d11_gpu-private.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11BufferCache;
class D3D11PixelShader;
class D3D11ShaderCache;
class D3D11TextureCache;
struct D3D11TextureView;
class D3D11VertexShader;


class D3D11GraphicsDriver : public GraphicsDriver {
public:
  D3D11GraphicsDriver(
      Memory* memory, IDXGISwapChain* swap_chain, ID3D11Device* device);
  virtual ~D3D11GraphicsDriver();

  virtual void Initialize();

  virtual void InvalidateState(
      uint32_t mask);
  virtual void SetShader(
      xenos::XE_GPU_SHADER_TYPE type,
      uint32_t address,
      uint32_t start,
      uint32_t length);
  virtual void DrawIndexBuffer(
      xenos::XE_GPU_PRIMITIVE_TYPE prim_type,
      bool index_32bit, uint32_t index_count,
      uint32_t index_base, uint32_t index_size, uint32_t endianness);
  virtual void DrawIndexAuto(
      xenos::XE_GPU_PRIMITIVE_TYPE prim_type,
      uint32_t index_count);

  // TODO(benvanik): figure this out.
  virtual int Resolve();

private:
  int SetupDraw(xenos::XE_GPU_PRIMITIVE_TYPE prim_type);
  int RebuildRenderTargets(uint32_t width, uint32_t height);
  int UpdateState(uint32_t state_overrides = 0);
  int UpdateConstantBuffers();
  int BindShaders();
  int PrepareFetchers();
  int PrepareVertexBuffer(Shader::vtx_buffer_desc_t& desc);
  int PrepareTextureFetchers();
  int PrepareTextureSampler(xenos::XE_GPU_SHADER_TYPE shader_type,
                            Shader::tex_buffer_desc_t& desc);
  int PrepareIndexBuffer(
      bool index_32bit, uint32_t index_count,
      uint32_t index_base, uint32_t index_size, uint32_t endianness);

private:
  IDXGISwapChain*       swap_chain_;
  ID3D11Device*         device_;
  ID3D11DeviceContext*  context_;
  D3D11BufferCache*     buffer_cache_;
  D3D11ShaderCache*     shader_cache_;
  D3D11TextureCache*    texture_cache_;

  ID3D11ShaderResourceView* invalid_texture_view_;
  ID3D11SamplerState*       invalid_texture_sampler_state_;

  struct {
    uint32_t width;
    uint32_t height;
    struct {
      ID3D11Texture2D*        buffer;
      ID3D11RenderTargetView* color_view_8888;
    } color_buffers[4];
    ID3D11Texture2D*        depth_buffer;
    ID3D11DepthStencilView* depth_view_d28s8;
    ID3D11DepthStencilView* depth_view_d28fs8;
  } render_targets_;

  struct {
    D3D11VertexShader*  vertex_shader;
    D3D11PixelShader*   pixel_shader;

    struct {
      ID3D11Buffer*     float_constants;
      ID3D11Buffer*     bool_constants;
      ID3D11Buffer*     loop_constants;
      ID3D11Buffer*     vs_consts;
      ID3D11Buffer*     gs_consts;
    } constant_buffers;

    struct {
      bool        enabled;
      xenos::xe_gpu_texture_fetch_t fetch;
      D3D11TextureView* view;
    } texture_fetchers[32];
  } state_;

  enum StateOverrides {
    STATE_OVERRIDE_DISABLE_CULLING  = (1 << 0),
  };
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_GRAPHICS_DRIVER_H_

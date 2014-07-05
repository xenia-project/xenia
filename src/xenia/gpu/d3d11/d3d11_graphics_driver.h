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
#include <xenia/gpu/d3d11/d3d11_gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_resource_cache.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {


class D3D11GraphicsDriver : public GraphicsDriver {
public:
  D3D11GraphicsDriver(
      Memory* memory, IDXGISwapChain* swap_chain, ID3D11Device* device);
  virtual ~D3D11GraphicsDriver();

  ResourceCache* resource_cache() const override { return resource_cache_; }

  int Initialize() override;

  int Draw(const DrawCommand& command) override;

  // TODO(benvanik): figure this out.
  int Resolve() override;

private:
  void InitializeInvalidTexture();

  int UpdateState(const DrawCommand& command);
  int SetupRasterizerState(const DrawCommand& command);
  int SetupBlendState(const DrawCommand& command);
  int SetupDepthStencilState(const DrawCommand& command);
  int SetupConstantBuffers(const DrawCommand& command);
  int SetupShaders(const DrawCommand& command);
  int SetupInputAssembly(const DrawCommand& command);
  int SetupSamplers(const DrawCommand& command);

  int RebuildRenderTargets(uint32_t width, uint32_t height);

private:
  IDXGISwapChain*       swap_chain_;
  ID3D11Device*         device_;
  ID3D11DeviceContext*  context_;

  D3D11ResourceCache*   resource_cache_;

  ID3D11ShaderResourceView* invalid_texture_view_;
  ID3D11SamplerState*       invalid_texture_sampler_state_;

  std::unordered_map<uint64_t, ID3D11RasterizerState*> rasterizer_state_cache_;
  std::unordered_map<uint64_t, ID3D11BlendState*> blend_state_cache_;
  std::unordered_map<uint64_t, ID3D11DepthStencilState*> depth_stencil_state_cache_;

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
    struct {
      ID3D11Buffer*     float_constants;
      ID3D11Buffer*     bool_constants;
      ID3D11Buffer*     loop_constants;
      ID3D11Buffer*     vs_consts;
      ID3D11Buffer*     gs_consts;
    } constant_buffers;
  } state_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_GRAPHICS_DRIVER_H_

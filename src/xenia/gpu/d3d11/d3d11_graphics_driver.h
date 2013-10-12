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
#include <xenia/gpu/d3d11/d3d11-private.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11PixelShader;
class D3D11ShaderCache;
class D3D11VertexShader;


class D3D11GraphicsDriver : public GraphicsDriver {
public:
  D3D11GraphicsDriver(xe_memory_ref memory, ID3D11Device* device);
  virtual ~D3D11GraphicsDriver();

  virtual void Initialize();

  virtual void InvalidateState(
      uint32_t mask);
  virtual void SetShader(
      xenos::XE_GPU_SHADER_TYPE type,
      uint32_t address,
      uint32_t start,
      uint32_t length);
  virtual void DrawAutoIndexed(
      xenos::XE_GPU_PRIMITIVE_TYPE prim_type,
      uint32_t index_count);

private:
  void UpdateState();
  void UpdateConstantBuffers();
  void BindShaders();
  void PrepareFetchers();
  void PrepareVertexFetcher(
      int slot, xenos::xe_gpu_vertex_fetch_t* fetch);
  void PrepareTextureFetcher(
      int slot, xenos::xe_gpu_texture_fetch_t* fetch);
  void PrepareIndexBuffer();

private:
  ID3D11Device*         device_;
  ID3D11DeviceContext*  context_;
  D3D11ShaderCache*     shader_cache_;

  struct {
    D3D11VertexShader*  vertex_shader;
    D3D11PixelShader*   pixel_shader;

    struct {
      ID3D11Buffer*     float_constants;
      ID3D11Buffer*     bool_constants;
      ID3D11Buffer*     loop_constants;
    } constant_buffers;
  } state_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_GRAPHICS_DRIVER_H_

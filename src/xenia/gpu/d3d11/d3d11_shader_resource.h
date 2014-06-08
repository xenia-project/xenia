/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_SHADER_RESOURCE_H_
#define XENIA_GPU_D3D11_D3D11_SHADER_RESOURCE_H_

#include <xenia/gpu/shader_resource.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11GeometryShader;
class D3D11ResourceCache;

struct Output;
typedef struct {
  Output*       output;
  xenos::XE_GPU_SHADER_TYPE type;
  uint32_t      tex_fetch_index;
} xe_gpu_translate_ctx_t;

class D3D11VertexShaderResource : public VertexShaderResource {
public:
  D3D11VertexShaderResource(D3D11ResourceCache* resource_cache,
                            const MemoryRange& memory_range,
                            const Info& info);
  ~D3D11VertexShaderResource() override;

  void* handle() const override { return handle_; }
  ID3D11InputLayout* input_layout() const { return input_layout_; }
  const char* translated_src() const { return translated_src_; }

  int Prepare(const xenos::xe_gpu_program_cntl_t& program_cntl) override;

  enum GeometryShaderType {
    POINT_SPRITE_SHADER,
    RECT_LIST_SHADER,
    QUAD_LIST_SHADER,
    MAX_GEOMETRY_SHADER_TYPE,  // keep at the end
  };
  int DemandGeometryShader(GeometryShaderType type,
                           D3D11GeometryShader** out_shader);

private:
  int CreateInputLayout(const void* byte_code, size_t byte_code_length);

  D3D11ResourceCache* resource_cache_;
  ID3D11VertexShader* handle_;
  ID3D11InputLayout* input_layout_;
  D3D11GeometryShader* geometry_shaders_[MAX_GEOMETRY_SHADER_TYPE];
  char* translated_src_;
};


class D3D11PixelShaderResource : public PixelShaderResource {
public:
  D3D11PixelShaderResource(D3D11ResourceCache* resource_cache,
                           const MemoryRange& memory_range,
                           const Info& info);
  ~D3D11PixelShaderResource() override;

  void* handle() const override { return handle_; }
  const char* translated_src() const { return translated_src_; }

  int Prepare(const xenos::xe_gpu_program_cntl_t& program_cntl,
              VertexShaderResource* vertex_shader) override;

private:
  D3D11ResourceCache* resource_cache_;
  ID3D11PixelShader* handle_;
  char* translated_src_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_SHADER_RESOURCE_H_

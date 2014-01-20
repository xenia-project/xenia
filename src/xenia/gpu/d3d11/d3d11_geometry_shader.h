/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_GEOMETRY_SHADER_H_
#define XENIA_GPU_D3D11_D3D11_GEOMETRY_SHADER_H_

#include <xenia/core.h>

#include <alloy/string_buffer.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11VertexShader;


class D3D11GeometryShader {
public:
  virtual ~D3D11GeometryShader();

  ID3D11GeometryShader* handle() const { return handle_; }

  int Prepare(D3D11VertexShader* vertex_shader);

protected:
  D3D11GeometryShader(ID3D11Device* device, uint64_t hash);

  ID3D10Blob* Compile(const char* shader_source);

  virtual int Generate(D3D11VertexShader* vertex_shader,
                       alloy::StringBuffer* output);

protected:
  ID3D11Device* device_;
  uint64_t      hash_;
  ID3D11GeometryShader* handle_;
};


class D3D11PointSpriteGeometryShader : public D3D11GeometryShader {
public:
  D3D11PointSpriteGeometryShader(ID3D11Device* device, uint64_t hash);
  virtual ~D3D11PointSpriteGeometryShader();

protected:
  virtual int Generate(D3D11VertexShader* vertex_shader,
                       alloy::StringBuffer* output);
};


class D3D11RectListGeometryShader : public D3D11GeometryShader {
public:
  D3D11RectListGeometryShader(ID3D11Device* device, uint64_t hash);
  virtual ~D3D11RectListGeometryShader();

protected:
  virtual int Generate(D3D11VertexShader* vertex_shader,
                       alloy::StringBuffer* output);
};


class D3D11QuadListGeometryShader : public D3D11GeometryShader {
public:
  D3D11QuadListGeometryShader(ID3D11Device* device, uint64_t hash);
  virtual ~D3D11QuadListGeometryShader();

protected:
  virtual int Generate(D3D11VertexShader* vertex_shader,
                       alloy::StringBuffer* output);
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_SHADER_H_

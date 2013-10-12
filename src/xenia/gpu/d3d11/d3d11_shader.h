/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_SHADER_H_
#define XENIA_GPU_D3D11_D3D11_SHADER_H_

#include <xenia/core.h>

#include <xenia/gpu/shader.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {


class D3D11Shader : public Shader {
public:
  virtual ~D3D11Shader();

protected:
  D3D11Shader(
      ID3D11Device* device,
      xenos::XE_GPU_SHADER_TYPE type,
      const uint8_t* src_ptr, size_t length,
      uint64_t hash);

protected:
  ID3D11Device* device_;
};


class D3D11VertexShader : public D3D11Shader {
public:
  D3D11VertexShader(
      ID3D11Device* device,
      const uint8_t* src_ptr, size_t length,
      uint64_t hash);
  virtual ~D3D11VertexShader();

  virtual int Prepare();

private:
  ID3D11VertexShader* handle_;
};


class D3D11PixelShader : public D3D11Shader {
public:
  D3D11PixelShader(
      ID3D11Device* device,
      const uint8_t* src_ptr, size_t length,
      uint64_t hash);
  virtual ~D3D11PixelShader();

  virtual int Prepare();

private:
  ID3D11PixelShader*  handle_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_SHADER_H_

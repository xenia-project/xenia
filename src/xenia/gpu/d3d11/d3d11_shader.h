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
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {

struct Output;

typedef struct {
  Output*       output;
  xenos::XE_GPU_SHADER_TYPE type;
  uint32_t      tex_fetch_index;
} xe_gpu_translate_ctx_t;

class D3D11GeometryShader;


class D3D11Shader : public Shader {
public:
  virtual ~D3D11Shader();

  const static uint32_t MAX_INTERPOLATORS = 16;

protected:
  D3D11Shader(
      ID3D11Device* device,
      xenos::XE_GPU_SHADER_TYPE type,
      const uint8_t* src_ptr, size_t length,
      uint64_t hash);

  const char* translated_src() const { return translated_src_; }
  void set_translated_src(char* value);

  void AppendTextureHeader(Output* output);
  int TranslateExec(
      xe_gpu_translate_ctx_t& ctx, const xenos::instr_cf_exec_t& cf);

  ID3D10Blob* Compile(const char* shader_source);

protected:
  ID3D11Device* device_;

  char*   translated_src_;
};


class D3D11VertexShader : public D3D11Shader {
public:
  D3D11VertexShader(
      ID3D11Device* device,
      const uint8_t* src_ptr, size_t length,
      uint64_t hash);
  virtual ~D3D11VertexShader();

  ID3D11VertexShader* handle() const { return handle_; }
  ID3D11InputLayout* input_layout() const { return input_layout_; }

  int Prepare(xenos::xe_gpu_program_cntl_t* program_cntl);

  enum GeometryShaderType {
    POINT_SPRITE_SHADER,
    RECT_LIST_SHADER,
    QUAD_LIST_SHADER,

    MAX_GEOMETRY_SHADER_TYPE,
  };
  int DemandGeometryShader(GeometryShaderType type,
                           D3D11GeometryShader** out_shader);

private:
  const char* Translate(xenos::xe_gpu_program_cntl_t* program_cntl);

private:
  ID3D11VertexShader*   handle_;
  ID3D11InputLayout*    input_layout_;
  D3D11GeometryShader*  geometry_shaders_[MAX_GEOMETRY_SHADER_TYPE];
};


class D3D11PixelShader : public D3D11Shader {
public:
  D3D11PixelShader(
      ID3D11Device* device,
      const uint8_t* src_ptr, size_t length,
      uint64_t hash);
  virtual ~D3D11PixelShader();

  ID3D11PixelShader* handle() const { return handle_; }

  int Prepare(xenos::xe_gpu_program_cntl_t* program_cntl,
              D3D11VertexShader* input_shader);

private:
  const char* Translate(xenos::xe_gpu_program_cntl_t* program_cntl,
                        D3D11VertexShader* input_shader);

private:
  ID3D11PixelShader*  handle_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_SHADER_H_

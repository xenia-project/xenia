/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_SHADER_TRANSLATOR_H_
#define XENIA_GPU_D3D11_D3D11_SHADER_TRANSLATOR_H_

#include <xenia/gpu/shader_resource.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {


class D3D11ShaderTranslator {
public:
  const static uint32_t kMaxInterpolators = 16;

  D3D11ShaderTranslator();

  int TranslateVertexShader(VertexShaderResource* vertex_shader,
                            const xenos::xe_gpu_program_cntl_t& program_cntl);
  int TranslatePixelShader(
      PixelShaderResource* pixel_shader,
      const xenos::xe_gpu_program_cntl_t& program_cntl,
      const VertexShaderResource::AllocCounts& alloc_counts);

  const char* translated_src() const { return buffer_; }

private:
  xenos::XE_GPU_SHADER_TYPE type_;
  uint32_t tex_fetch_index_;
  const uint32_t* dwords_;

  static const int kCapacity = 64 * 1024;
  char buffer_[kCapacity];
  size_t capacity_;
  size_t offset_;
  void append(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int len = xevsnprintfa(buffer_ + offset_, capacity_ - offset_,
                           format, args);
    va_end(args);
    offset_ += len;
    buffer_[offset_] = 0;
  }

  void AppendTextureHeader(
      const ShaderResource::SamplerInputs& sampler_inputs);

  void AppendSrcReg(uint32_t num, uint32_t type, uint32_t swiz, uint32_t negate,
                    uint32_t abs);
  void AppendDestRegName(uint32_t num, uint32_t dst_exp);
  void AppendDestReg(uint32_t num, uint32_t mask, uint32_t dst_exp);
  void AppendDestRegPost(uint32_t num, uint32_t mask, uint32_t dst_exp);
  void PrintSrcReg(uint32_t num, uint32_t type, uint32_t swiz, uint32_t negate,
                   uint32_t abs);
  void PrintDstReg(uint32_t num, uint32_t mask, uint32_t dst_exp);
  void PrintExportComment(uint32_t num);

  int TranslateALU(const xenos::instr_alu_t* alu, int sync);
  int TranslateALU_ADDv(const xenos::instr_alu_t& alu);
  int TranslateALU_MULv(const xenos::instr_alu_t& alu);
  int TranslateALU_MAXv(const xenos::instr_alu_t& alu);
  int TranslateALU_MINv(const xenos::instr_alu_t& alu);
  int TranslateALU_SETXXv(const xenos::instr_alu_t& alu, const char* op);
  int TranslateALU_SETEv(const xenos::instr_alu_t& alu);
  int TranslateALU_SETGTv(const xenos::instr_alu_t& alu);
  int TranslateALU_SETGTEv(const xenos::instr_alu_t& alu);
  int TranslateALU_SETNEv(const xenos::instr_alu_t& alu);
  int TranslateALU_FRACv(const xenos::instr_alu_t& alu);
  int TranslateALU_TRUNCv(const xenos::instr_alu_t& alu);
  int TranslateALU_FLOORv(const xenos::instr_alu_t& alu);
  int TranslateALU_MULADDv(const xenos::instr_alu_t& alu);
  int TranslateALU_CNDXXv(const xenos::instr_alu_t& alu, const char* op);
  int TranslateALU_CNDEv(const xenos::instr_alu_t& alu);
  int TranslateALU_CNDGTEv(const xenos::instr_alu_t& alu);
  int TranslateALU_CNDGTv(const xenos::instr_alu_t& alu);
  int TranslateALU_DOT4v(const xenos::instr_alu_t& alu);
  int TranslateALU_DOT3v(const xenos::instr_alu_t& alu);
  int TranslateALU_DOT2ADDv(const xenos::instr_alu_t& alu);
  // CUBEv
  int TranslateALU_MAX4v(const xenos::instr_alu_t& alu);
  // ...
  int TranslateALU_MAXs(const xenos::instr_alu_t& alu);
  int TranslateALU_MINs(const xenos::instr_alu_t& alu);
  int TranslateALU_SETXXs(const xenos::instr_alu_t& alu, const char* op);
  int TranslateALU_SETEs(const xenos::instr_alu_t& alu);
  int TranslateALU_SETGTs(const xenos::instr_alu_t& alu);
  int TranslateALU_SETGTEs(const xenos::instr_alu_t& alu);
  int TranslateALU_SETNEs(const xenos::instr_alu_t& alu);
  int TranslateALU_RECIP_IEEE(const xenos::instr_alu_t& alu);
  int TranslateALU_MUL_CONST_0(const xenos::instr_alu_t& alu);
  int TranslateALU_MUL_CONST_1(const xenos::instr_alu_t& alu);
  int TranslateALU_ADD_CONST_0(const xenos::instr_alu_t& alu);
  int TranslateALU_ADD_CONST_1(const xenos::instr_alu_t& alu);
  int TranslateALU_SUB_CONST_0(const xenos::instr_alu_t& alu);
  int TranslateALU_SUB_CONST_1(const xenos::instr_alu_t& alu);

  void PrintDestFecth(uint32_t dst_reg, uint32_t dst_swiz);
  void AppendFetchDest(uint32_t dst_reg, uint32_t dst_swiz);
  int GetFormatComponentCount(uint32_t format);

  int TranslateExec(const xenos::instr_cf_exec_t& cf);
  int TranslateVertexFetch(const xenos::instr_fetch_vtx_t* vtx, int sync);
  int TranslateTextureFetch(const xenos::instr_fetch_tex_t* tex, int sync);
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_SHADER_TRANSLATOR_H_

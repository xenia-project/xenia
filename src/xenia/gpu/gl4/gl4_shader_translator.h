/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_GL4_SHADER_TRANSLATOR_H_
#define XENIA_GPU_GL4_GL4_SHADER_TRANSLATOR_H_

#include <string>

#include "xenia/base/string_buffer.h"
#include "xenia/gpu/gl4/gl4_shader.h"
#include "xenia/gpu/ucode.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/gl/gl_context.h"

namespace xe {
namespace gpu {
namespace gl4 {

class GL4ShaderTranslator {
 public:
  static const uint32_t kMaxInterpolators = 16;

  GL4ShaderTranslator();
  ~GL4ShaderTranslator();

  std::string TranslateVertexShader(
      GL4Shader* vertex_shader,
      const xenos::xe_gpu_program_cntl_t& program_cntl);
  std::string TranslatePixelShader(
      GL4Shader* pixel_shader,
      const xenos::xe_gpu_program_cntl_t& program_cntl);

 protected:
  ShaderType shader_type_;
  const uint32_t* dwords_ = nullptr;

  static const int kOutputCapacity = 64 * 1024;
  StringBuffer output_;

  bool is_vertex_shader() const { return shader_type_ == ShaderType::kVertex; }
  bool is_pixel_shader() const { return shader_type_ == ShaderType::kPixel; }

  void Reset(GL4Shader* shader);

  void AppendSrcReg(const ucode::instr_alu_t& op, int i);
  void AppendSrcReg(const ucode::instr_alu_t& op, uint32_t num, uint32_t type,
                    uint32_t swiz, uint32_t negate, int const_slot);
  void PrintSrcReg(uint32_t num, uint32_t type, uint32_t swiz, uint32_t negate,
                   uint32_t abs);
  void PrintVectorDstReg(const ucode::instr_alu_t& alu);
  void PrintScalarDstReg(const ucode::instr_alu_t& alu);
  void PrintExportComment(uint32_t num);

  bool TranslateALU(const ucode::instr_alu_t* alu, int sync);
  bool TranslateALU_ADDv(const ucode::instr_alu_t& alu);
  bool TranslateALU_MULv(const ucode::instr_alu_t& alu);
  bool TranslateALU_MAXv(const ucode::instr_alu_t& alu);
  bool TranslateALU_MINv(const ucode::instr_alu_t& alu);
  bool TranslateALU_SETXXv(const ucode::instr_alu_t& alu, const char* op);
  bool TranslateALU_SETEv(const ucode::instr_alu_t& alu);
  bool TranslateALU_SETNEv(const ucode::instr_alu_t& alu);
  bool TranslateALU_SETGTv(const ucode::instr_alu_t& alu);
  bool TranslateALU_SETGTEv(const ucode::instr_alu_t& alu);
  bool TranslateALU_FRACv(const ucode::instr_alu_t& alu);
  bool TranslateALU_TRUNCv(const ucode::instr_alu_t& alu);
  bool TranslateALU_FLOORv(const ucode::instr_alu_t& alu);
  bool TranslateALU_MULADDv(const ucode::instr_alu_t& alu);
  bool TranslateALU_CNDXXv(const ucode::instr_alu_t& alu, const char* op);
  bool TranslateALU_CNDEv(const ucode::instr_alu_t& alu);
  bool TranslateALU_CNDGTEv(const ucode::instr_alu_t& alu);
  bool TranslateALU_CNDGTv(const ucode::instr_alu_t& alu);
  bool TranslateALU_DOT4v(const ucode::instr_alu_t& alu);
  bool TranslateALU_DOT3v(const ucode::instr_alu_t& alu);
  bool TranslateALU_DOT2ADDv(const ucode::instr_alu_t& alu);
  bool TranslateALU_CUBEv(const ucode::instr_alu_t& alu);
  bool TranslateALU_MAX4v(const ucode::instr_alu_t& alu);
  bool TranslateALU_PRED_SETXX_PUSHv(const ucode::instr_alu_t& alu,
                                     const char* op);
  bool TranslateALU_PRED_SETE_PUSHv(const ucode::instr_alu_t& alu);
  bool TranslateALU_PRED_SETNE_PUSHv(const ucode::instr_alu_t& alu);
  bool TranslateALU_PRED_SETGT_PUSHv(const ucode::instr_alu_t& alu);
  bool TranslateALU_PRED_SETGTE_PUSHv(const ucode::instr_alu_t& alu);
  bool TranslateALU_DSTv(const ucode::instr_alu_t& alu);
  bool TranslateALU_MOVAv(const ucode::instr_alu_t& alu);
  bool TranslateALU_ADDs(const ucode::instr_alu_t& alu);
  bool TranslateALU_ADD_PREVs(const ucode::instr_alu_t& alu);
  bool TranslateALU_MULs(const ucode::instr_alu_t& alu);
  bool TranslateALU_MUL_PREVs(const ucode::instr_alu_t& alu);
  // ...
  bool TranslateALU_MAXs(const ucode::instr_alu_t& alu);
  bool TranslateALU_MINs(const ucode::instr_alu_t& alu);
  bool TranslateALU_SETXXs(const ucode::instr_alu_t& alu, const char* op);
  bool TranslateALU_SETEs(const ucode::instr_alu_t& alu);
  bool TranslateALU_SETGTs(const ucode::instr_alu_t& alu);
  bool TranslateALU_SETGTEs(const ucode::instr_alu_t& alu);
  bool TranslateALU_SETNEs(const ucode::instr_alu_t& alu);
  bool TranslateALU_FRACs(const ucode::instr_alu_t& alu);
  bool TranslateALU_TRUNCs(const ucode::instr_alu_t& alu);
  bool TranslateALU_FLOORs(const ucode::instr_alu_t& alu);
  bool TranslateALU_EXP_IEEE(const ucode::instr_alu_t& alu);
  bool TranslateALU_LOG_CLAMP(const ucode::instr_alu_t& alu);
  bool TranslateALU_LOG_IEEE(const ucode::instr_alu_t& alu);
  bool TranslateALU_RECIP_CLAMP(const ucode::instr_alu_t& alu);
  bool TranslateALU_RECIP_FF(const ucode::instr_alu_t& alu);
  bool TranslateALU_RECIP_IEEE(const ucode::instr_alu_t& alu);
  bool TranslateALU_RECIPSQ_CLAMP(const ucode::instr_alu_t& alu);
  bool TranslateALU_RECIPSQ_FF(const ucode::instr_alu_t& alu);
  bool TranslateALU_RECIPSQ_IEEE(const ucode::instr_alu_t& alu);
  bool TranslateALU_MOVAs(const ucode::instr_alu_t& alu);
  bool TranslateALU_MOVA_FLOORs(const ucode::instr_alu_t& alu);
  bool TranslateALU_SUBs(const ucode::instr_alu_t& alu);
  bool TranslateALU_SUB_PREVs(const ucode::instr_alu_t& alu);
  bool TranslateALU_PRED_SETXXs(const ucode::instr_alu_t& alu, const char* op);
  bool TranslateALU_PRED_SETEs(const ucode::instr_alu_t& alu);
  bool TranslateALU_PRED_SETNEs(const ucode::instr_alu_t& alu);
  bool TranslateALU_PRED_SETGTs(const ucode::instr_alu_t& alu);
  bool TranslateALU_PRED_SETGTEs(const ucode::instr_alu_t& alu);
  bool TranslateALU_PRED_SET_INVs(const ucode::instr_alu_t& alu);
  bool TranslateALU_PRED_SET_POPs(const ucode::instr_alu_t& alu);
  bool TranslateALU_SQRT_IEEE(const ucode::instr_alu_t& alu);
  bool TranslateALU_MUL_CONST_0(const ucode::instr_alu_t& alu);
  bool TranslateALU_MUL_CONST_1(const ucode::instr_alu_t& alu);
  bool TranslateALU_ADD_CONST_0(const ucode::instr_alu_t& alu);
  bool TranslateALU_ADD_CONST_1(const ucode::instr_alu_t& alu);
  bool TranslateALU_SUB_CONST_0(const ucode::instr_alu_t& alu);
  bool TranslateALU_SUB_CONST_1(const ucode::instr_alu_t& alu);
  bool TranslateALU_SIN(const ucode::instr_alu_t& alu);
  bool TranslateALU_COS(const ucode::instr_alu_t& alu);
  bool TranslateALU_RETAIN_PREV(const ucode::instr_alu_t& alu);

  struct AppendFlag {};
  void BeginAppendVectorOp(const ucode::instr_alu_t& op);
  void AppendVectorOpSrcReg(const ucode::instr_alu_t& op, int i);
  void EndAppendVectorOp(const ucode::instr_alu_t& op,
                         uint32_t append_flags = 0);
  void BeginAppendScalarOp(const ucode::instr_alu_t& op);
  void AppendScalarOpSrcReg(const ucode::instr_alu_t& op, int i);
  void EndAppendScalarOp(const ucode::instr_alu_t& op,
                         uint32_t append_flags = 0);
  void AppendOpDestRegName(const ucode::instr_alu_t& op, uint32_t dest_num);

  void PrintDestFetch(uint32_t dst_reg, uint32_t dst_swiz);
  void AppendFetchDest(uint32_t dst_reg, uint32_t dst_swiz);

  void AppendPredPre(bool is_cond_cf, uint32_t cf_condition,
                     uint32_t pred_select, uint32_t condition);
  void AppendPredPost(bool is_cond_cf, uint32_t cf_condition,
                      uint32_t pred_select, uint32_t condition);

  bool TranslateBlocks(GL4Shader* shader);
  bool TranslateExec(const ucode::instr_cf_exec_t& cf);
  bool TranslateJmp(const ucode::instr_cf_jmp_call_t& cf);
  bool TranslateLoopStart(const ucode::instr_cf_loop_t& cf);
  bool TranslateLoopEnd(const ucode::instr_cf_loop_t& cf);
  bool TranslateVertexFetch(const ucode::instr_fetch_vtx_t* vtx, int sync);
  bool TranslateTextureFetch(const ucode::instr_fetch_tex_t* tex, int sync);
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_GL4_SHADER_TRANSLATOR_H_
